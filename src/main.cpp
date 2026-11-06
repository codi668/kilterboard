// Kilterboard -- LED-Kletterwand mit Touch-Griffen, Weboberflaeche und OTA.
//
// Ablauf beim Start: LEDs initialisieren, WLAN im Hintergrund anstossen,
// Countdown 3-2-1 anzeigen, Touch-Sensor einmessen, Netzwerk fertig
// aufbauen (eigener Access Point, falls kein bekanntes WLAN da ist),
// dann Webserver und OTA starten.
//
// Danach laeuft alles ohne blockierende Wartezeiten in loop().
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "secrets.h"

#define HOSTNAME   "kilterboard"
#define AP_SSID    "Kletterwand"

Preferences prefs;
static DNSServer dns;
static bool apMode = false;
static bool dirty = false;
static uint32_t dirtyAt = 0;

void slotsLoad();   // in web.cpp

// --------------------------------------------------------- Einstellungen
static void settingsLoad() {
  prefs.begin("kilter", false);
  st.brightness  = prefs.getUChar("bright", 160);
  st.speed       = prefs.getUChar("speed", 50);
  st.ledOrder    = prefs.getUChar("order", 0);
  st.captive     = prefs.getBool("captive", false);

  // Bis Version 1.0.0 stand hier GRB (aus dem urspruenglichen Sketch).
  // An der Wand zeigte sich: Rot und Gruen sind damit vertauscht, der
  // Streifen will RGB. Einmalig auf den neuen Wert ziehen, sonst bliebe
  // ein frueher gespeichertes GRB stehen.
  if (prefs.getUChar("cfgver", 0) < 1) {
    st.ledOrder = 0;
    prefs.putUChar("order", 0);
    prefs.putUChar("cfgver", 1);
  }
  st.gameBestMs  = prefs.getULong("best", 0);
  st.gameTotal   = prefs.getUChar("gtotal", 10);
  tc.threshold   = prefs.getUChar("thr", 20);
  tc.sensitivity = prefs.getUChar("sens", 2);
  tc.useDelta    = prefs.getBool("delta", false);
}

// Wird oft gerufen (z. B. bei jedem Tippen im Routen-Editor), schreibt
// aber erst, wenn drei Sekunden lang Ruhe war -- schont den Flash.
void settingsSave() {
  dirty = true;
  dirtyAt = millis();
}

static void settingsFlush() {
  prefs.putUChar("bright", st.brightness);
  prefs.putUChar("speed", st.speed);
  prefs.putUChar("order", st.ledOrder);
  prefs.putBool("captive", st.captive);
  prefs.putULong("best", st.gameBestMs);
  prefs.putUChar("gtotal", st.gameTotal);
  prefs.putUChar("thr", tc.threshold);
  prefs.putUChar("sens", tc.sensitivity);
  prefs.putBool("delta", tc.useDelta);
  dirty = false;
}

// ---------------------------------------------------------------- Netzwerk
String netInfo() {
  String j = "{\"mode\":\"";
  j += apMode ? "AP" : "WLAN";
  j += "\",\"ssid\":\"";
  j += apMode ? AP_SSID : WiFi.SSID();
  j += "\",\"ip\":\"";
  j += (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString();
  j += "\",\"host\":\"" HOSTNAME ".local\"";
  j += ",\"rssi\":" + String(apMode ? 0 : WiFi.RSSI());
  j += ",\"clients\":" + String(apMode ? WiFi.softAPgetStationNum() : 0);
  j += ",\"captive\":" + String(st.captive ? "true" : "false");
  j += "}";
  return j;
}

static bool wifiConfigured() {
  const char *s = WIFI_SSID;
  return strlen(s) > 0 && strcmp(s, "-") != 0;
}

// Captive-Portal: Ein eigener DNS-Server beantwortet jede Namensanfrage
// mit der eigenen Adresse. Das Handy merkt daran, dass es in einem Netz
// mit Anmeldeseite haengt, und oeffnet die Seite von selbst in einem
// Fenster. Praktisch, aber aufdringlich -- deshalb standardmaessig aus.
void setCaptive(bool an) {
  st.captive = an;
  dns.stop();
  if (an && apMode) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    Serial.println("[wifi] Captive-Portal an");
  } else {
    Serial.println("[wifi] Captive-Portal aus");
  }
}

static void startAccessPoint() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(100);
  setCaptive(st.captive);
  Serial.printf("[wifi] Access Point \"%s\" -> http://%s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void startOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    Serial.println("[ota] Start");
    boardShowProgress(0, 0x000000);
  });
  ArduinoOTA.onProgress([](unsigned p, unsigned t) {
    if (t) boardShowProgress((uint8_t)(100ULL * p / t), 0x0040FF);
  });
  ArduinoOTA.onEnd([] { boardShowProgress(100, 0x00FF00); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] Fehler %u\n", e);
    boardShowProgress(100, 0xFF0000);
  });
  ArduinoOTA.begin();
  Serial.println("[ota] bereit (Port 3232)");
}

// -------------------------------------------------------------------- Setup
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n== Kletterwand " FW_VERSION " ==");

  randomSeed(esp_random());
  settingsLoad();
  boardBegin();

  // WLAN im Hintergrund anstossen, dann laeuft es waehrend des Countdowns
  if (wifiConfigured()) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[wifi] verbinde mit \"%s\" ...\n", WIFI_SSID);
  }

  boardShowCountdown();

  // Touch-Sensor einmessen. Dabei sollte niemand die Griffe anfassen.
  touchBegin();

  if (wifiConfigured()) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      boardShowProgress((uint8_t)((millis() - start) / 80), 0x001830);
      delay(50);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    Serial.printf("[wifi] verbunden, IP %s\n", WiFi.localIP().toString().c_str());
    boardShowProgress(100, 0x003000);
  } else {
    startAccessPoint();
    boardShowProgress(100, 0x000030);
  }

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mdns] http://" HOSTNAME ".local");
  }

  startOta();
  webBegin();
  serialBegin();

  delay(600);
  boardSetMode(MODE_RAINBOW);
}

// --------------------------------------------------------------------- Loop
void loop() {
  ArduinoOTA.handle();
  webTick();
  serialTick();
  if (apMode && st.captive) dns.processNextRequest();
  touchTick();
  boardTick();

  if (dirty && millis() - dirtyAt > 3000) settingsFlush();

  // Wenn das bekannte WLAN wegbricht, spannt die Wand ihr eigenes auf,
  // damit die Oberflaeche erreichbar bleibt.
  static uint32_t lastCheck = 0;
  if (!apMode && millis() - lastCheck > 10000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] Verbindung verloren -> Access Point");
      startAccessPoint();
    }
  }
}

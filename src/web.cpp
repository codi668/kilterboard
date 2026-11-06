// Webserver: liefert die Oberflaeche aus, nimmt Befehle an und erlaubt
// das Firmware-Update ueber den Browser.
#include "config.h"
#include "web_page.h"
#include "secrets.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <Preferences.h>

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "kletterwand"
#endif

static WebServer server(80);
extern Preferences prefs;

Score scores[NUM_SCORES];

// Namen kommen aus Eingabefeldern. Alles, was JSON oder die Anzeige
// zerlegen koennte, fliegt hier raus -- sonst bricht die Statusantwort
// und die Oberflaeche bleibt stehen.
static void sanitize(const String &in, char *out, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < in.length() && n < len - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || c == '<' || c == '>' || (uint8_t)c < 0x20) continue;
    out[n++] = c;
  }
  out[n] = 0;
}

void scoresLoad() {
  if (!prefs.isKey("top") ||
      prefs.getBytes("top", scores, sizeof(scores)) != sizeof(scores)) {
    memset(scores, 0, sizeof(scores));
    return;
  }
  for (int i = 0; i < NUM_SCORES; i++) {
    scores[i].name[sizeof(scores[i].name) - 1] = 0;
    char sauber[sizeof(scores[i].name)];
    sanitize(String(scores[i].name), sauber, sizeof(sauber));
    strcpy(scores[i].name, sauber);
  }
}

void scoresClear() {
  memset(scores, 0, sizeof(scores));
  prefs.putBytes("top", scores, sizeof(scores));
}

// Ergebnis einsortieren. Verglichen wird die Zeit je Griff, damit kurze
// und lange Runden fair nebeneinander stehen.
void scoreAdd(uint32_t ms, uint8_t grips) {
  if (!grips) return;
  uint32_t proGriff = ms / grips;
  int platz = -1;
  for (int i = 0; i < NUM_SCORES; i++) {
    uint32_t anderer = scores[i].grips ? scores[i].ms / scores[i].grips : 0xFFFFFFFF;
    if (proGriff < anderer) { platz = i; break; }
  }
  if (platz < 0) return;
  for (int i = NUM_SCORES - 1; i > platz; i--) scores[i] = scores[i - 1];
  sanitize(strlen(st.playerName) ? st.playerName : "Gast",
           scores[platz].name, sizeof(scores[platz].name));
  scores[platz].ms    = ms;
  scores[platz].grips = grips;
  prefs.putBytes("top", scores, sizeof(scores));
}

// -------------------------------------------------------------- Statusdaten
static String statusJson() {
  String j;
  j.reserve(2200);
  j = "{\"ver\":\"" FW_VERSION "\",\"up\":" + String(millis());
  j += ",\"heap\":" + String((uint32_t)ESP.getFreeHeap());
  j += ",\"mode\":" + String((int)st.mode);
  j += ",\"bright\":" + String(st.brightness);
  j += ",\"speed\":" + String(st.speed);
  j += ",\"ledOrder\":" + String(st.ledOrder);
  j += ",\"ledOrders\":[";
  for (int i = 0; i < LED_ORDER_COUNT; i++)
    j += String(i ? ",\"" : "\"") + ledOrderNames[i] + "\"";
  j += "]";

  j += ",\"grips\":[";
  for (int i = 0; i < NUM_GRIPS; i++) j += (i ? "," : "") + String(st.gripState[i]);
  j += "]";

  // Farben aller Griffpositionen als ein Hex-String (6 Zeichen je Position)
  j += ",\"cells\":\"";
  char hex[8];
  for (int i = 0; i < NUM_CELLS; i++) {
    snprintf(hex, sizeof(hex), "%06X", (unsigned)(boardCellColor(i) & 0xFFFFFF));
    j += hex;
  }
  j += "\"";

  uint32_t ms = (st.gameState == GAME_RUNNING) ? millis() - st.gameStartMs : st.gameTimeMs;
  j += ",\"game\":{\"state\":" + String((int)st.gameState);
  j += ",\"hits\":" + String(st.gameHits) + ",\"miss\":" + String(st.gameMiss);
  j += ",\"total\":" + String(st.gameTotal) + ",\"ms\":" + String(ms);
  j += ",\"best\":" + String(st.gameBestMs) + ",\"target\":" + String(st.gameTarget);
  j += ",\"split\":" + String(st.gameSplitMs) + ",\"bestSplit\":" + String(st.gameBestSplit);
  j += ",\"player\":\"" + String(st.playerName) + "\"}";

  j += ",\"top\":[";
  for (int i = 0; i < NUM_SCORES; i++) {
    if (i) j += ",";
    j += "{\"n\":\"" + String(scores[i].name) + "\",\"ms\":" + String(scores[i].ms);
    j += ",\"g\":" + String(scores[i].grips) + "}";
  }
  j += "]";

  j += ",\"touch\":{\"ok\":" + String(tc.present ? "true" : "false");
  j += ",\"pid\":" + String(tc.productId) + ",\"status\":" + String(tc.status);
  j += ",\"thr\":" + String(tc.threshold) + ",\"sens\":" + String(tc.sensitivity);
  j += ",\"useDelta\":" + String(tc.useDelta ? "true" : "false");
  j += ",\"delta\":[";
  for (int i = 0; i < 8; i++) j += (i ? "," : "") + String(tc.delta[i]);
  j += "],\"down\":[";
  for (int i = 0; i < 8; i++) j += String(i ? "," : "") + (tc.down[i] ? "true" : "false");
  j += "]}";

  j += ",\"net\":" + netInfo();
  j += "}";
  return j;
}

// ---------------------------------------------------------------- Handler
static int argInt(const char *name, int fallback = -1) {
  return server.hasArg(name) ? server.arg(name).toInt() : fallback;
}

static void sendOk() { server.send(200, "text/plain", "ok"); }

static bool requireAuth() {
  if (server.authenticate("admin", OTA_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void webBegin() {
  scoresLoad();

  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/mode", HTTP_GET, [] {
    int m = argInt("m", 1);
    if (m >= MODE_RAINBOW && m <= MODE_TEST) boardSetMode((Mode)m);
    sendOk();
  });
  server.on("/api/bright", HTTP_GET, [] {
    boardSetBrightness((uint8_t)constrain(argInt("v", 160), 5, 255));
    settingsSave();
    sendOk();
  });
  server.on("/api/speed", HTTP_GET, [] {
    st.speed = (uint8_t)constrain(argInt("v", 50), 0, 100);
    settingsSave();
    sendOk();
  });
  server.on("/api/grip", HTTP_GET, [] {
    int i = argInt("i", -1);
    if (i >= 0 && i < NUM_GRIPS) {
      if (server.hasArg("c")) boardSetGrip(i, (uint8_t)argInt("c", 0));
      else                    boardCycleGrip(i);
      if (st.mode != MODE_TOUCH) boardSetMode(MODE_TOUCH);
    }
    sendOk();
  });


  server.on("/api/game/start", HTTP_GET, [] {
    if (server.hasArg("name")) {
      String n = server.arg("name");
      n.trim();
      sanitize(n, st.playerName, sizeof(st.playerName));
    }
    st.gameTotal = (uint8_t)constrain(argInt("n", 10), 3, 50);
    settingsSave();
    boardStartGame(st.gameTotal);
    sendOk();
  });
  server.on("/api/game/top/clear", HTTP_GET, [] { scoresClear(); sendOk(); });
  server.on("/api/game/stop",  HTTP_GET, [] { boardStopGame(); sendOk(); });
  server.on("/api/game/reset", HTTP_GET, [] { st.gameBestMs = 0; settingsSave(); sendOk(); });

  server.on("/api/ledorder", HTTP_GET, [] {
    boardSetLedOrder((uint8_t)argInt("o", 2));
    if (st.mode != MODE_TEST) boardSetMode(MODE_TEST);
    settingsSave();
    sendOk();
  });
  server.on("/api/touch", HTTP_GET, [] {
    if (server.hasArg("thr"))  tc.threshold = (uint8_t)constrain(argInt("thr", 20), 2, 120);
    if (server.hasArg("sens")) touchSetSensitivity((uint8_t)constrain(argInt("sens", 2), 0, 7));
    if (server.hasArg("src"))  tc.useDelta = server.arg("src") == "delta";
    settingsSave();
    sendOk();
  });
  server.on("/api/touch/calib", HTTP_GET, [] { touchCalibrate(); sendOk(); });

  server.on("/api/reboot", HTTP_GET, [] {
    if (!requireAuth()) return;
    server.send(200, "text/plain", "Neustart");
    delay(200);
    ESP.restart();
  });

  // ---- Firmware-Update ueber den Browser (passwortgeschuetzt) ----
  server.on("/update", HTTP_GET, [] {
    if (!requireAuth()) return;
    server.send_P(200, "text/html; charset=utf-8", UPDATE_HTML);
  });
  server.on("/update", HTTP_POST,
    [] {   // nach dem Upload
      if (!server.authenticate("admin", OTA_PASSWORD)) { server.requestAuthentication(); return; }
      if (Update.hasError()) {
        server.send(500, "text/plain", "Update fehlgeschlagen");
      } else {
        server.send(200, "text/plain", "ok");
        delay(400);
        ESP.restart();
      }
    },
    [] {   // Datenstrom
      static bool erlaubt = false;
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        erlaubt = server.authenticate("admin", OTA_PASSWORD);
        if (!erlaubt) { Serial.println("[ota] Upload ohne Passwort abgewiesen"); return; }
        Serial.printf("[ota] Update: %s\n", up.filename.c_str());
        boardSetMode(MODE_OFF);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!erlaubt) return;
        if (Update.write(up.buf, up.currentSize) != up.currentSize)
          Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_END) {
        if (!erlaubt) return;
        if (Update.end(true)) Serial.printf("[ota] %u Bytes geschrieben\n", up.totalSize);
        else                  Update.printError(Serial);
      }
    });

  server.onNotFound([] {
    // Handys pruefen nach dem Verbinden eine Testadresse; wir schicken sie
    // auf die Startseite, damit die Oberflaeche von selbst aufgeht.
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("[web] Server auf Port 80 gestartet");
}

void webTick() { server.handleClient(); }

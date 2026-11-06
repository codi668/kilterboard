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


// ===================== Schnittstelle fuer eigene Programme ===============
// Farbe aus einem Parameter lesen: "RRGGBB", "#RRGGBB", "off" oder "aus".
static bool parseColor(const String &in, uint32_t &out) {
  String t = in;
  t.trim();
  t.toLowerCase();
  if (t == "off" || t == "aus" || t == "0") { out = 0; return true; }
  if (t.startsWith("#")) t = t.substring(1);
  if (t.length() != 6) return false;
  for (unsigned i = 0; i < t.length(); i++)
    if (!isHexadecimalDigit(t[i])) return false;
  out = (uint32_t)strtoul(t.c_str(), nullptr, 16);
  return true;
}

static String hex6(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%06X", (unsigned)(c & 0xFFFFFF));
  return String(buf);
}

// Welche Griffe leuchten, in welcher Farbe, und was melden die Sensoren
static String ledsJson() {
  String j;
  j.reserve(2600);
  j = "{\"mode\":" + String((int)st.mode);
  j += ",\"remote\":" + String(st.mode == MODE_API ? "true" : "false");
  j += ",\"brightness\":" + String(st.brightness);
  j += ",\"gripCount\":" + String(NUM_GRIPS) + ",\"cellCount\":" + String(NUM_CELLS);

  j += ",\"grips\":[";
  for (int g = 0; g < NUM_GRIPS; g++) {
    uint8_t c0 = boardCellOfGrip(g, 0), c1 = boardCellOfGrip(g, 1);
    uint32_t col = boardCellColor(c0);
    if (g) j += ",";
    j += "{\"id\":" + String(g + 1);
    j += ",\"on\":" + String(col ? "true" : "false");
    j += ",\"color\":\"" + hex6(col) + "\"";
    j += ",\"touched\":" + String(tc.down[g + 2] ? "true" : "false");
    j += ",\"touches\":" + String(tc.touches[g + 2]);
    j += ",\"cells\":[" + String(c0) + "," + String(c1) + "]";
    j += ",\"leds\":[" + String(boardCellLed(c0)) + "," + String(boardCellLed(c1)) + "]}";
  }
  j += "]";

  j += ",\"cells\":[";
  for (int i = 0; i < NUM_CELLS; i++) {
    uint32_t col = boardCellColor(i);
    if (i) j += ",";
    j += "{\"i\":" + String(i);
    j += ",\"on\":" + String(col ? "true" : "false");
    j += ",\"color\":\"" + hex6(col) + "\"";
    j += ",\"led\":" + String(boardCellLed(i));
    j += ",\"sensor\":" + String(boardIsGripCell(i) ? "true" : "false") + "}";
  }
  j += "]}";
  return j;
}

static String sensorsJson() {
  static const char *namen[8] = {"T1", "T2", "Griff 1", "Griff 2",
                                 "Griff 3", "Griff 4", "Griff 5", "Griff 6"};
  String j;
  j.reserve(900);
  j = "{\"ok\":" + String(tc.present ? "true" : "false");
  j += ",\"chip\":\"CAP1298\",\"productId\":" + String(tc.productId);
  j += ",\"source\":\"" + String(tc.useDelta ? "delta" : "status") + "\"";
  j += ",\"threshold\":" + String(tc.threshold);
  j += ",\"sensitivity\":" + String(tc.sensitivity);
  j += ",\"statusRegister\":" + String(tc.status);
  j += ",\"channels\":[";
  for (int i = 0; i < 8; i++) {
    if (i) j += ",";
    j += "{\"ch\":" + String(i + 1) + ",\"name\":\"" + namen[i] + "\"";
    j += ",\"grip\":" + (i >= 2 ? String(i - 1) : String("null"));
    j += ",\"touched\":" + String(tc.down[i] ? "true" : "false");
    j += ",\"touches\":" + String(tc.touches[i]);
    j += ",\"raw\":" + String(tc.delta[i]) + "}";
  }
  j += "]}";
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
    if (m >= MODE_RAINBOW && m <= MODE_API) boardSetMode((Mode)m);
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


  // ---- Schnittstelle fuer eigene Programme ----
  server.on("/api/leds", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", ledsJson());
  });
  server.on("/api/sensors", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", sensorsJson());
  });
  server.on("/api/leds/off", HTTP_GET, [] {
    boardApiSetAll(0);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/leds/set", HTTP_GET, [] {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    uint32_t farbe = 0;
    bool hatFarbe = server.hasArg("color") && parseColor(server.arg("color"), farbe);
    int gesetzt = 0;

    // Alle Positionen auf einmal: 34 mal sechs Hex-Zeichen
    if (server.hasArg("cells")) {
      String raw = server.arg("cells");
      if (raw.length() != (unsigned)(NUM_CELLS * 6)) {
        server.send(400, "application/json",
          "{\"ok\":false,\"error\":\"cells braucht " + String(NUM_CELLS * 6) +
          " Hex-Zeichen, bekommen: " + String(raw.length()) + "\"}");
        return;
      }
      for (int i = 0; i < NUM_CELLS; i++) {
        uint32_t c;
        if (!parseColor(raw.substring(i * 6, i * 6 + 6), c)) {
          server.send(400, "application/json",
            "{\"ok\":false,\"error\":\"ungueltige Farbe an Position " + String(i) + "\"}");
          return;
        }
        boardApiSetCell(i, c);
        gesetzt++;
      }
    } else if (server.hasArg("all")) {
      uint32_t c;
      if (!parseColor(server.arg("all"), c)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"ungueltige Farbe\"}");
        return;
      }
      boardApiSetAll(c);
      gesetzt = NUM_CELLS;
    } else if (server.hasArg("grip") || server.hasArg("cell")) {
      if (!hatFarbe) {
        server.send(400, "application/json",
          "{\"ok\":false,\"error\":\"color fehlt oder ist ungueltig (RRGGBB oder off)\"}");
        return;
      }
      if (server.hasArg("grip")) {
        int g = server.arg("grip").toInt();      // 1..6, menschlich gezaehlt
        if (g < 1 || g > NUM_GRIPS) {
          server.send(400, "application/json",
            "{\"ok\":false,\"error\":\"grip muss 1 bis " + String(NUM_GRIPS) + " sein\"}");
          return;
        }
        boardApiSetGrip(g - 1, farbe);
        gesetzt = 1;
      }
      if (server.hasArg("cell")) {
        int c = server.arg("cell").toInt();      // 0..33
        if (c < 0 || c >= NUM_CELLS) {
          server.send(400, "application/json",
            "{\"ok\":false,\"error\":\"cell muss 0 bis " + String(NUM_CELLS - 1) + " sein\"}");
          return;
        }
        boardApiSetCell(c, farbe);
        gesetzt++;
      }
    } else {
      server.send(400, "application/json",
        "{\"ok\":false,\"error\":\"grip, cell, all oder cells angeben\"}");
      return;
    }
    server.send(200, "application/json",
      "{\"ok\":true,\"set\":" + String(gesetzt) + ",\"mode\":" + String((int)st.mode) + "}");
  });

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

// Die Schnittstelle: eine Stelle, an der jeder Befehl verarbeitet wird --
// egal ob er ueber WLAN (web.cpp) oder ueber USB (serial.cpp) kommt.
// Dadurch koennen beide Wege nicht auseinanderlaufen.
#include "config.h"
#include <WiFi.h>
#include <Preferences.h>

extern Preferences prefs;

Score scores[NUM_SCORES];

static void sanitize(const String &in, char *out, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < in.length() && n < len - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || c == '<' || c == '>' || (uint8_t)c < 0x20) continue;
    out[n++] = c;
  }
  out[n] = 0;
}

void apiScoresLoad() {
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

String apiStatusJson() {
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

String apiLedsJson() {
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

String apiSensorsJson() {
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

// ======================= Der gemeinsame Einstieg ========================
// Verarbeitet einen Aufruf und liefert die Antwort als JSON. Der Pfad ist
// derselbe wie bei HTTP, die Parameter kommen ueber ApiArgs -- aus dem
// Webserver oder aus einer Zeile, die ueber USB hereinkommt.

static String ok(const String &extra = "") {
  return extra.length() ? "{\"ok\":true," + extra + "}" : "{\"ok\":true}";
}

static String fehler(const String &text, int &code) {
  code = 400;
  String t = text;
  t.replace("\"", "'");
  return "{\"ok\":false,\"error\":\"" + t + "\"}";
}

String apiDispatch(const String &pfad, const ApiArgs &a, int &code) {
  code = 200;

  // ---- Zustand abfragen ----
  if (pfad == "/api/status")  return apiStatusJson();
  if (pfad == "/api/leds")    return apiLedsJson();
  if (pfad == "/api/sensors") return apiSensorsJson();

  // ---- Betriebsart und Grundeinstellungen ----
  if (pfad == "/api/mode") {
    int m = a.getInt("m", -1);
    if (m < MODE_RAINBOW || m > MODE_API)
      return fehler("m muss 1 bis " + String((int)MODE_API) + " sein", code);
    boardSetMode((Mode)m);
    return ok("\"mode\":" + String((int)st.mode));
  }
  if (pfad == "/api/bright") {
    int v = a.getInt("v", -1);
    if (v < 5 || v > 255) return fehler("v muss 5 bis 255 sein", code);
    boardSetBrightness((uint8_t)v);
    settingsSave();
    return ok("\"brightness\":" + String(st.brightness));
  }
  if (pfad == "/api/speed") {
    int v = a.getInt("v", -1);
    if (v < 0 || v > 100) return fehler("v muss 0 bis 100 sein", code);
    st.speed = (uint8_t)v;
    settingsSave();
    return ok("\"speed\":" + String(st.speed));
  }
  if (pfad == "/api/grip") {
    int i = a.getInt("i", -1);
    if (i < 0 || i >= NUM_GRIPS)
      return fehler("i muss 0 bis " + String(NUM_GRIPS - 1) + " sein", code);
    if (a.has("c")) boardSetGrip(i, (uint8_t)a.getInt("c", 0));
    else            boardCycleGrip(i);
    if (st.mode != MODE_TOUCH) boardSetMode(MODE_TOUCH);
    return ok("\"grip\":" + String(i) + ",\"state\":" + String(st.gripState[i]));
  }

  // ---- Fernsteuerung der LEDs ----
  if (pfad == "/api/leds/off") {
    boardApiSetAll(0);
    return ok("\"set\":" + String(NUM_CELLS));
  }
  if (pfad == "/api/leds/set") {
    uint32_t farbe = 0;
    bool hatFarbe = a.has("color") && parseColor(a.get("color"), farbe);

    if (a.has("cells")) {
      String raw = a.get("cells");
      if (raw.length() != (unsigned)(NUM_CELLS * 6))
        return fehler("cells braucht " + String(NUM_CELLS * 6) +
                      " Hex-Zeichen, bekommen: " + String(raw.length()), code);
      for (int i = 0; i < NUM_CELLS; i++) {
        uint32_t c;
        if (!parseColor(raw.substring(i * 6, i * 6 + 6), c))
          return fehler("ungueltige Farbe an Position " + String(i), code);
        boardApiSetCell(i, c);
      }
      return ok("\"set\":" + String(NUM_CELLS) + ",\"mode\":" + String((int)st.mode));
    }
    if (a.has("all")) {
      uint32_t c;
      if (!parseColor(a.get("all"), c)) return fehler("ungueltige Farbe", code);
      boardApiSetAll(c);
      return ok("\"set\":" + String(NUM_CELLS) + ",\"mode\":" + String((int)st.mode));
    }
    if (a.has("grip") || a.has("cell")) {
      if (!hatFarbe) return fehler("color fehlt oder ist ungueltig (RRGGBB oder off)", code);
      int gesetzt = 0;
      if (a.has("grip")) {
        int g = a.getInt("grip", 0);          // 1..6, menschlich gezaehlt
        if (g < 1 || g > NUM_GRIPS)
          return fehler("grip muss 1 bis " + String(NUM_GRIPS) + " sein", code);
        boardApiSetGrip(g - 1, farbe);
        gesetzt++;
      }
      if (a.has("cell")) {
        int c = a.getInt("cell", -1);         // 0..33
        if (c < 0 || c >= NUM_CELLS)
          return fehler("cell muss 0 bis " + String(NUM_CELLS - 1) + " sein", code);
        boardApiSetCell(c, farbe);
        gesetzt++;
      }
      return ok("\"set\":" + String(gesetzt) + ",\"mode\":" + String((int)st.mode));
    }
    return fehler("grip, cell, all oder cells angeben", code);
  }

  // ---- Spiel ----
  if (pfad == "/api/game/start") {
    if (a.has("name")) {
      String n = a.get("name");
      n.trim();
      sanitize(n, st.playerName, sizeof(st.playerName));
    }
    int n = a.getInt("n", 10);
    if (n < 3 || n > 50) return fehler("n muss 3 bis 50 sein", code);
    st.gameTotal = (uint8_t)n;
    settingsSave();
    boardStartGame(st.gameTotal);
    return ok("\"total\":" + String(st.gameTotal));
  }
  if (pfad == "/api/game/stop")      { boardStopGame(); return ok(); }
  if (pfad == "/api/game/reset")     { st.gameBestMs = 0; settingsSave(); return ok(); }
  if (pfad == "/api/game/top/clear") { scoresClear(); return ok(); }

  // ---- Sensor ----
  if (pfad == "/api/touch") {
    if (a.has("thr")) {
      int v = a.getInt("thr", 20);
      if (v < 2 || v > 120) return fehler("thr muss 2 bis 120 sein", code);
      tc.threshold = (uint8_t)v;
    }
    if (a.has("sens")) {
      int v = a.getInt("sens", 2);
      if (v < 0 || v > 7) return fehler("sens muss 0 bis 7 sein", code);
      touchSetSensitivity((uint8_t)v);
    }
    if (a.has("src")) {
      String s = a.get("src");
      if (s != "delta" && s != "status") return fehler("src muss status oder delta sein", code);
      tc.useDelta = (s == "delta");
    }
    settingsSave();
    return ok("\"threshold\":" + String(tc.threshold) +
              ",\"sensitivity\":" + String(tc.sensitivity) +
              ",\"source\":\"" + String(tc.useDelta ? "delta" : "status") + "\"");
  }
  if (pfad == "/api/touch/calib") { touchCalibrate(); return ok(); }

  // ---- Sonstiges ----
  if (pfad == "/api/ledorder") {
    int o = a.getInt("o", -1);
    if (o < 0 || o >= LED_ORDER_COUNT)
      return fehler("o muss 0 bis " + String(LED_ORDER_COUNT - 1) + " sein", code);
    boardSetLedOrder((uint8_t)o);
    if (st.mode != MODE_TEST) boardSetMode(MODE_TEST);
    settingsSave();
    return ok("\"ledOrder\":\"" + String(ledOrderNames[st.ledOrder]) + "\"");
  }
  if (pfad == "/api/captive") {
    if (a.has("on")) {
      String v = a.get("on");
      setCaptive(v == "1" || v == "true" || v == "an");
      settingsSave();
    }
    return ok("\"captive\":" + String(st.captive ? "true" : "false"));
  }
  if (pfad == "/api/reboot") {
    // Der Neustart selbst passiert beim Aufrufer, damit die Antwort noch rausgeht.
    return ok("\"reboot\":true");
  }

  code = 404;
  return "{\"ok\":false,\"error\":\"unbekannter Befehl: " + pfad + "\"}";
}

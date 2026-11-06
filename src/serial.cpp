// Dieselbe Schnittstelle wie ueber WLAN, nur ueber das USB-Kabel.
//
// Jede Zeile ist ein Befehl, jede Antwort eine Zeile JSON. Es gibt zwei
// Schreibweisen, beide landen in derselben Funktion apiDispatch():
//
//   leds                        kurz, zum Tippen
//   set grip 3 00FF00
//   /api/leds/set?grip=3&color=00FF00    wie bei HTTP, zum Kopieren
//
// Dazu zwei Dinge, die es nur hier gibt:
//   log off      die Meldungen wie "[touch] Griff 1 beruehrt" abschalten,
//                damit ein Programm nur noch Antworten liest
//   events off   die Beruehrungsmeldungen abschalten (sie sind an)
#include "config.h"

bool logEnabled    = true;
bool eventsEnabled = true;

static const size_t MAX_LINE = 320;
static char   zeile[MAX_LINE];
static size_t fuell = 0;

// Parameter aus einer Befehlszeile: Paare name=wert
struct LineArgs : ApiArgs {
  static const int MAX = 8;
  String name[MAX], wert[MAX];
  int anzahl = 0;

  void add(const String &n, const String &w) {
    if (anzahl < MAX) { name[anzahl] = n; wert[anzahl] = w; anzahl++; }
  }
  bool has(const char *n) const override {
    for (int i = 0; i < anzahl; i++) if (name[i] == n) return true;
    return false;
  }
  String get(const char *n) const override {
    for (int i = 0; i < anzahl; i++) if (name[i] == n) return wert[i];
    return String("");
  }
};

// %20 und + in Leerzeichen zurueckverwandeln, wie bei einer URL
static String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (unsigned i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') out += ' ';
    else if (c == '%' && i + 2 < in.length()) {
      char hex[3] = {in[i + 1], in[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else out += c;
  }
  return out;
}

static void antwort(const String &json) {
  Serial.println(json);
}

static void hilfe() {
  Serial.println(F("# Befehle (Antwort ist immer eine Zeile JSON):"));
  Serial.println(F("#   leds                  was leuchtet gerade, mit Sensorzustand"));
  Serial.println(F("#   sensors               alle acht Kanaele einzeln"));
  Serial.println(F("#   status                alles auf einmal"));
  Serial.println(F("#   set grip <1-6> <RRGGBB|off>"));
  Serial.println(F("#   set cell <0-33> <RRGGBB|off>"));
  Serial.println(F("#   set all <RRGGBB|off>"));
  Serial.println(F("#   set cells <204 Hex-Zeichen>"));
  Serial.println(F("#   off                   alle LEDs aus"));
  Serial.println(F("#   mode <1-6>            1 Regenbogen 2 Griffe 3 Spiel"));
  Serial.println(F("#                         4 Aus 5 Testbild 6 Fernsteuerung"));
  Serial.println(F("#   bright <5-255>        Helligkeit"));
  Serial.println(F("#   calib                 Sensor neu einmessen"));
  Serial.println(F("#   log on|off            diese Meldungen ein-/ausschalten"));
  Serial.println(F("#   events on|off         Beruehrungen von selbst melden (an)"));
  Serial.println(F("#   help                  diese Liste"));
  Serial.println(F("# Auch die HTTP-Schreibweise geht:"));
  Serial.println(F("#   /api/leds/set?grip=3&color=00FF00"));
}

// Kurzform in Pfad und Parameter uebersetzen
static bool kurzform(const String &in, String &pfad, LineArgs &args) {
  String rest = in;
  int leer = rest.indexOf(' ');
  String befehl = (leer < 0) ? rest : rest.substring(0, leer);
  String arg    = (leer < 0) ? String("") : rest.substring(leer + 1);
  arg.trim();
  befehl.toLowerCase();

  if (befehl == "leds")    { pfad = "/api/leds";    return true; }
  if (befehl == "sensors") { pfad = "/api/sensors"; return true; }
  if (befehl == "status")  { pfad = "/api/status";  return true; }
  if (befehl == "off")     { pfad = "/api/leds/off"; return true; }
  if (befehl == "calib")   { pfad = "/api/touch/calib"; return true; }
  if (befehl == "mode")    { pfad = "/api/mode";   args.add("m", arg); return true; }
  if (befehl == "bright")  { pfad = "/api/bright"; args.add("v", arg); return true; }

  if (befehl == "set") {
    int l2 = arg.indexOf(' ');
    String was  = (l2 < 0) ? arg : arg.substring(0, l2);
    String rest2 = (l2 < 0) ? String("") : arg.substring(l2 + 1);
    rest2.trim();
    was.toLowerCase();
    pfad = "/api/leds/set";
    if (was == "all")   { args.add("all", rest2);     return true; }
    if (was == "cells") { args.add("cells", rest2);   return true; }
    if (was == "grip" || was == "cell") {
      int l3 = rest2.indexOf(' ');
      if (l3 < 0) return false;
      args.add(was, rest2.substring(0, l3));
      String farbe = rest2.substring(l3 + 1);
      farbe.trim();
      args.add("color", farbe);
      return true;
    }
    return false;
  }
  return false;
}

static void verarbeite(String in) {
  in.trim();
  if (!in.length() || in.startsWith("#")) return;

  // nur hier, nicht in apiDispatch: Ausgaben steuern
  String klein = in;
  klein.toLowerCase();
  if (klein == "help" || klein == "?") { hilfe(); return; }
  if (klein.startsWith("log ")) {
    logEnabled = klein.endsWith("on") || klein.endsWith("an");
    antwort(String("{\"ok\":true,\"log\":") + (logEnabled ? "true" : "false") + "}");
    return;
  }
  if (klein.startsWith("events ")) {
    eventsEnabled = klein.endsWith("on") || klein.endsWith("an");
    antwort(String("{\"ok\":true,\"events\":") + (eventsEnabled ? "true" : "false") + "}");
    return;
  }

  String pfad;
  LineArgs args;

  if (in.startsWith("/")) {
    // HTTP-Schreibweise: /api/leds/set?grip=3&color=00FF00
    int frage = in.indexOf('?');
    pfad = (frage < 0) ? in : in.substring(0, frage);
    if (frage >= 0) {
      String q = in.substring(frage + 1);
      while (q.length()) {
        int und = q.indexOf('&');
        String paar = (und < 0) ? q : q.substring(0, und);
        q = (und < 0) ? String("") : q.substring(und + 1);
        int gleich = paar.indexOf('=');
        if (gleich > 0)
          args.add(paar.substring(0, gleich), urlDecode(paar.substring(gleich + 1)));
        else if (paar.length())
          args.add(paar, "1");
      }
    }
  } else if (!kurzform(in, pfad, args)) {
    antwort("{\"ok\":false,\"error\":\"unbekannter Befehl, 'help' zeigt die Liste\"}");
    return;
  }

  int code = 200;
  String aus = apiDispatch(pfad, args, code);
  antwort(aus);

  if (pfad == "/api/reboot" && code == 200) {
    Serial.flush();
    delay(200);
    ESP.restart();
  }
}

void serialBegin() {
  Serial.println(F("# Serielle Schnittstelle bereit -- 'help' zeigt die Befehle"));
  Serial.println(F("# Beruehrungen werden von selbst gemeldet: "
                   "{\"event\":\"touch\",\"n\":1..6}  ('events off' schaltet das ab)"));
}

void serialTick() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      zeile[fuell] = 0;
      String eingabe(zeile);
      fuell = 0;
      verarbeite(eingabe);
    } else if (fuell < MAX_LINE - 1) {
      zeile[fuell++] = c;
    } else {
      // Zeile zu lang: verwerfen, bis der naechste Umbruch kommt
      fuell = 0;
      Serial.println(F("{\"ok\":false,\"error\":\"Zeile zu lang\"}"));
    }
  }
}

// Von selbst melden, wenn jemand einen Griff anfasst (events on)
void serialEvent(const char *art, int nummer) {
  if (!eventsEnabled) return;
  Serial.printf("{\"event\":\"%s\",\"n\":%d,\"ms\":%lu}\n",
                art, nummer, (unsigned long)millis());
}

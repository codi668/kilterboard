// Webserver: liefert die Oberflaeche aus und reicht alle Befehle an
// apiDispatch() in api.cpp weiter -- dieselbe Stelle, die auch die
// serielle Schnittstelle benutzt. Hier stehen nur noch die Dinge, die es
// ausschliesslich ueber HTTP gibt: die Seite selbst und das
// Firmware-Update ueber den Browser.
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

// Parameter aus der HTTP-Anfrage
struct HttpArgs : ApiArgs {
  bool   has(const char *name) const override { return server.hasArg(name); }
  String get(const char *name) const override { return server.arg(name); }
};

static bool requireAuth() {
  if (server.authenticate("admin", OTA_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

// Ein Handler fuer alle Befehle: Pfad und Parameter an api.cpp geben.
static void handleApi() {
  HttpArgs args;
  int code = 200;
  String antwort = apiDispatch(server.uri(), args, code);

  bool neustart = (server.uri() == "/api/reboot" && code == 200);
  if (neustart && !requireAuth()) return;

  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", antwort);

  if (neustart) { delay(300); ESP.restart(); }
}

void webBegin() {
  apiScoresLoad();

  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  // Alle Befehle laufen ueber denselben Handler
  static const char *pfade[] = {
    "/api/status", "/api/leds", "/api/leds/set", "/api/leds/off", "/api/sensors",
    "/api/mode", "/api/bright", "/api/speed", "/api/grip",
    "/api/game/start", "/api/game/stop", "/api/game/reset", "/api/game/top/clear",
    "/api/touch", "/api/touch/calib", "/api/ledorder", "/api/captive", "/api/reboot"
  };
  for (const char *p : pfade) server.on(p, HTTP_GET, handleApi);

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
    // Handys rufen nach dem Verbinden eine Testadresse ab. Antwortet man
    // darauf mit einer Umleitung, oeffnet das Geraet die Seite von selbst
    // in einem Fenster. Das passiert nur, wenn das Captive-Portal
    // eingeschaltet ist -- sonst gibt es hier eine schlichte Absage.
    if (st.captive) {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain",
                  "Nicht gefunden. Die Steuerung liegt unter http://" +
                  WiFi.softAPIP().toString() + "/");
    }
  });

  server.begin();
  Serial.println("[web] Server auf Port 80 gestartet");
}

void webTick() { server.handleClient(); }

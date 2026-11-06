// Vorlage: nach src/secrets.h kopieren und ausfuellen.
// secrets.h ist in .gitignore und bleibt lokal.
#pragma once

// Bekanntes WLAN. Leer lassen oder "-" eintragen, dann spannt die Wand
// immer ihr eigenes WLAN auf (das ist am Tag der offenen Tuer gewollt).
#define WIFI_SSID      "-"
#define WIFI_PASSWORD  ""

// WLAN, das die Wand selbst aufspannt (mindestens 8 Zeichen).
#define AP_PASSWORD    "kletterwand"

// Passwort fuer OTA-Updates und die Seite /update (Benutzer: admin).
#define OTA_PASSWORD   "BITTE-AENDERN"

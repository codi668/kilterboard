# Die Firmware erklärt

Für den, der den ursprünglichen Aufbau gebaut hat.

Dein `Kilterboard_test` ist die Grundlage von allem hier: die Verdrahtung,
das LED-Mapping, der CAP1298 an 0x28, die Idee mit Regenbogen und
Griffe-Durchschalten. Was dazugekommen ist, sind im Kern drei Dinge —
eine Weboberfläche, ein Spiel und Updates über WLAN. Damit das
zusammenpasst, musste der Aufbau ringsum geändert werden. Dieser Text
erklärt, warum, und zwar von deinem Code aus.

Reihenfolge zum Lesen: Abschnitt 1 bis 3 sind das Fundament, danach kannst
du springen.

## Inhalt

1. [Die eine Regel: loop() darf nicht warten](#1-die-eine-regel-loop-darf-nicht-warten)
2. [Der gemeinsame Zustand](#2-der-gemeinsame-zustand)
3. [Wie ein Bild entsteht](#3-wie-ein-bild-entsteht)
4. [Weg 1: vom Handy auf die Wand](#4-weg-1-vom-handy-auf-die-wand)
5. [Weg 2: vom Griff in den Browser](#5-weg-2-vom-griff-in-den-browser)
6. [Der CAP1298 — warum dein Delta-Trick nötig war](#6-der-cap1298--warum-dein-delta-trick-nötig-war)
7. [Der Webserver](#7-der-webserver)
8. [Das Spiel](#8-das-spiel)
9. [Updates über WLAN](#9-updates-über-wlan)
10. [Einstellungen, die den Neustart überleben](#10-einstellungen-die-den-neustart-überleben)
11. [Selbst ausprobieren](#11-selbst-ausprobieren)

---

## 1. Die eine Regel: loop() darf nicht warten

Das ist die Änderung, aus der alle anderen folgen.

Dein Original hatte am Ende jedes Durchlaufs ein `delay()`:

```cpp
// vorher
void loop() {
  byte touchStatus = readCAP1298Status(0x03);
  ...
  if (currentMode == 1) {
      // Regenbogen zeichnen
      pixels.show();
      firstPixelHue += 256;
      delay(20);          // <-- hier steht alles still
  }
}
```

Für sich genommen ist das völlig in Ordnung: 20 ms merkt niemand, und die
Taster reagieren schnell genug. Sobald aber ein Webserver dazukommt, ist
es das Ende. Ein Handy, das die Seite abruft, will bedient werden,
*während* die Animation läuft. Steht der Prozessor 20 ms in `delay()`,
wartet die Anfrage. Kommen mehrere Handys dazu, wird die Seite zäh oder
bricht ab.

Deshalb wartet jetzt nichts mehr. Jede Teilaufgabe schaut selbst auf die
Uhr und entscheidet, ob sie dran ist:

```cpp
// jetzt (board.cpp)
void boardTick() {
  uint32_t now = millis();
  uint16_t frame = (st.mode == MODE_RAINBOW) ? 20 : 30;
  if (now - lastFrameMs < frame) return;   // noch nicht dran -> sofort zurück
  lastFrameMs = now;

  switch (st.mode) {
    case MODE_RAINBOW: tickRainbow(); break;
    ...
  }
  render();
}
```

`millis()` zählt die Millisekunden seit dem Start. Steht seit dem letzten
Bild noch keine volle Bildzeit an, kehrt die Funktion sofort zurück, und
`loop()` kommt zur nächsten Aufgabe. Die Hauptschleife sieht dann so aus:

```cpp
// main.cpp
void loop() {
  ArduinoOTA.handle();   // läuft ein Update über WLAN?
  webTick();             // wartet ein Browser auf Antwort?
  touchTick();           // alle 20 ms den Sensor lesen
  boardTick();           // alle 20–30 ms ein neues Bild
  ...
}
```

Diese Schleife läuft viele tausend Mal pro Sekunde durch, und jeder
Teilnehmer meldet sich, wenn er an der Reihe ist. Das ist das ganze
Geheimnis — es braucht keine Threads, keine Tasks und kein FreeRTOS.

> **Eine Falle dabei:** Schreib Zeitvergleiche immer als *Differenz*
> (`now - start > dauer`) und nie als direkten Vergleich
> (`now > start + dauer`). `millis()` läuft nach 49 Tagen über und fängt
> wieder bei null an. Die Differenz stimmt auch dann noch, der direkte
> Vergleich nicht.

Ein `delay()` gibt es nur noch an zwei Stellen, und beide sind harmlos:
im Startvorgang (da läuft noch nichts, was warten müsste) und beim
Countdown 3-2-1, der absichtlich blockiert.

## 2. Der gemeinsame Zustand

In deinem Original lagen die Variablen für den Zustand einzeln herum:
`currentMode`, `gripState[6]`, `firstPixelHue`. Das ist bei einer Datei
völlig ausreichend. Jetzt greifen aber mehrere Module darauf zu — der
Touch-Treiber, die Animation und der Webserver —, deshalb liegt alles
gebündelt in einer Struktur in `config.h`:

```cpp
struct State {
  Mode     mode        = MODE_RAINBOW;
  uint8_t  brightness  = 160;
  uint8_t  speed       = 50;
  uint8_t  gripState[NUM_GRIPS] = {0};
  // Spiel
  GameState gameState  = GAME_IDLE;
  uint8_t  gameTarget  = 0;
  uint8_t  gameHits    = 0;
  ...
};
extern State st;            // eine einzige Ausgabe davon, in board.cpp
```

Dieses `st` ist die Mitte des Programms. Die Regel ist einfach:

- **Wer etwas ändern will** (Webserver, Touch-Treiber), schreibt nach `st`.
- **Wer etwas anzeigt** (die Animation), liest aus `st`.

Niemand ruft quer durchs Programm Funktionen in anderen Modulen auf, um
etwas umzuschalten. Dadurch ist es egal, ob eine Änderung vom Handy oder
von einem Griff an der Wand kommt — beides landet an derselben Stelle,
und die Animation merkt den Unterschied gar nicht.

## 3. Wie ein Bild entsteht

Dein Code hat direkt mit LED-Nummern gearbeitet:

```cpp
// vorher
pixels.setPixelColor(led1, color);
pixels.setPixelColor(led2, color);
pixels.show();
```

Das funktioniert, hat aber zwei Haken: Man muss überall die Sperrliste
`ausgeschalteteLEDs` mitdenken, und niemand außer dem Streifen weiß
hinterher, welche Farbe wo liegt — die Weboberfläche soll das aber
anzeigen.

Deshalb gibt es jetzt eine Zwischenstufe. Der Code rechnet in
**Griffpositionen** 0…33 statt in LED-Nummern:

```cpp
static int      cellLed[NUM_CELLS];    // Position -> LED-Nummer im Streifen
static uint32_t cellColor[NUM_CELLS];  // Farbe dieser Position
```

`cellLed[]` wird beim Start einmal aus deinen Tabellen `mainMatrix` und
`miniMatrix` gefüllt. Danach läuft jedes Bild in zwei Schritten:

```cpp
// Schritt 1: die Betriebsart malt in cellColor[]
static void tickTouch() {
  fillAll(0);
  for (int g = 0; g < NUM_GRIPS; g++)
    setGripColor(g, touchColors[st.gripState[g] & 3]);
}

// Schritt 2: render() bringt es auf den Streifen
static void render() {
  pixels.clear();
  for (int i = 0; i < NUM_CELLS; i++) {
    uint32_t c = cellColor[i];
    if (istErlaubt(cellLed[i]) && c) pixels.setPixelColor(cellLed[i], c);
  }
  ... // kurze Rückmeldungen (Treffer, Fehlgriff) darüberlegen
  pixels.show();
}
```

Was das bringt:

- Die 16 LEDs ohne Griff bleiben zuverlässig dunkel, weil jedes Bild mit
  `pixels.clear()` anfängt und nur bekannte Positionen gesetzt werden.
  Die Sperrliste braucht man nur noch an einer Stelle.
- Die Weboberfläche kann `cellColor[]` auslesen und dieselbe Wand auf dem
  Handy zeichnen.
- Die Helligkeit steckt *nicht* in `cellColor[]`, sondern wird erst von
  der Bibliothek beim Ausgeben verrechnet. Dadurch zeigt das Handy satte
  Farben, auch wenn die Wand auf 20 % gedimmt ist.

## 4. Weg 1: vom Handy auf die Wand

Jetzt der ganze Weg an einem Beispiel. Jemand tippt am Handy auf
„Regenbogen“. Was passiert?

**Schritt 1 — der Browser schickt eine Anfrage.** Im JavaScript der Seite
hängt an jedem Knopf nur eine Zeile:

```js
document.querySelectorAll('[data-mode]').forEach(b =>
  b.onclick = () => go('/api/mode?m=' + b.dataset.mode));
```

`go()` ist ein `fetch()`, also ein ganz normaler HTTP-Aufruf. Beim Knopf
„Regenbogen“ wird daraus `GET /api/mode?m=1`.

**Schritt 2 — der Server nimmt sie an.** In `webBegin()` ist für jede
Adresse eine Funktion hinterlegt:

```cpp
server.on("/api/mode", HTTP_GET, [] {
  int m = argInt("m", 1);
  if (m >= MODE_RAINBOW && m <= MODE_TEST) boardSetMode((Mode)m);
  sendOk();
});
```

Das `[] { ... }` ist eine Lambda-Funktion — eine Funktion ohne Namen, die
direkt an Ort und Stelle steht. Sie wird nicht sofort ausgeführt, sondern
gespeichert und später aufgerufen, wenn eine passende Anfrage kommt. Man
könnte genauso gut normale Funktionen schreiben und deren Namen übergeben;
die Lambdas halten nur alles beieinander.

Wichtig ist die Prüfung: `argInt("m", 1)` holt die Zahl aus der Adresse,
und die `if`-Zeile lässt nur gültige Betriebsarten durch. Alles, was von
außen kommt, wird geprüft, bevor es irgendwo landet.

**Schritt 3 — der Zustand ändert sich.** `boardSetMode()` schreibt nach
`st.mode` und räumt auf:

```cpp
void boardSetMode(Mode m) {
  st.mode = m;
  fillAll(0);
  for (int g = 0; g < NUM_GRIPS; g++) flashUntil[g] = 0;
  ...
  render();
}
```

**Schritt 4 — das nächste Bild sieht anders aus.** Beim nächsten
Durchlauf von `boardTick()` greift der `switch` auf `st.mode` und ruft
`tickRainbow()` statt vorher etwas anderem. Fertig.

Der Aufruf vom Handy hat also nichts gezeichnet und nichts an die LEDs
geschickt — er hat nur eine Variable gesetzt. Das Zeichnen passiert
weiterhin an genau einer Stelle. Genau deshalb kommen sich Handy und Wand
nie in die Quere.

## 5. Weg 2: vom Griff in den Browser

Jetzt andersherum: Jemand fasst an der Wand einen Griff an, und das Handy
soll es anzeigen.

**Schritt 1 — der Treiber erkennt die Flanke.** `touchTick()` liest alle
20 ms das Statusregister und vergleicht mit dem letzten Stand:

```cpp
if (raw && !tc.down[i] && now - lastEdge[i] > DEBOUNCE_MS) {
  tc.down[i]  = true;
  lastEdge[i] = now;
  ...
  uint8_t grip = i - 2;              // CS3..CS8 sind die Griffe 1..6
  boardOnGripTouched(grip);
}
```

Gemeldet wird nur der *Übergang* von „nicht berührt“ nach „berührt“, und
nur, wenn seit der letzten Meldung 150 ms vergangen sind. Das ist dieselbe
Flankenerkennung wie in deinem Original mit `gripPressed[]`, nur mit
Entprellzeit dazu.

**Schritt 2 — die Berührung wird je nach Betriebsart verarbeitet.**

```cpp
void boardOnGripTouched(uint8_t grip) {
  if (st.mode == MODE_TOUCH) { boardCycleGrip(grip); return; }   // Farbe weiter
  if (st.mode == MODE_GAME)  { ... Treffer oder Fehlgriff ... }
  flashGrip(grip, 0xFFFFFF, 120);   // sonst nur kurz aufblitzen
}
```

**Schritt 3 — das Handy fragt nach.** Die Seite ruft viermal pro Sekunde
`/api/status` ab. Der Server baut daraus eine JSON-Antwort mit allem, was
die Oberfläche braucht — unter anderem die Farben aller 34 Positionen:

```cpp
j += ",\"cells\":\"";
char hex[8];
for (int i = 0; i < NUM_CELLS; i++) {
  snprintf(hex, sizeof(hex), "%06X", (unsigned)(boardCellColor(i) & 0xFFFFFF));
  j += hex;
}
j += "\"";
```

Das ergibt eine einzige Zeichenkette aus 34 × 6 Hex-Zeichen, also
`"FF000000FF00..."`. Als Feld aus 34 Einzelwerten wäre dieselbe
Information etwa dreimal so groß — bei vier Abfragen pro Sekunde und
mehreren Handys lohnt sich das.

**Warum Abfragen statt Melden?** Der ESP32 könnte über WebSockets von sich
aus Bescheid geben. Das wäre sparsamer, braucht aber eine dauerhafte
Verbindung pro Handy und mehr Code auf beiden Seiten. Viermal pro Sekunde
eine kleine Antwort ist einfacher, robuster, und wenn ein Handy im
Funkloch war, ist es beim nächsten Abruf von selbst wieder aktuell.

## 6. Der CAP1298 — warum dein Delta-Trick nötig war

In deinem Code steht etwas, das auf den ersten Blick inkonsequent
aussieht: Die Taster T1/T2 liest du über das Statusregister `0x03`, die
sechs Griffe aber über die Delta-Register `0x12`…`0x17` mit eigener
Schwelle. Dahinter steckt ein echtes Problem des Bausteins.

**Das Statusregister ist gelatcht.** Setzt der Chip dort ein Bit, bleibt
es gesetzt — auch wenn der Finger längst weg ist. Gelöscht wird es erst,
wenn man das INT-Bit im Main-Control-Register `0x00` zurücksetzt. Ohne
dieses Quittieren sieht das Programm nach der ersten Berührung dauerhaft
„gedrückt“, und die Flankenerkennung feuert nie wieder. Für die Griffe
musstest du deshalb auf die Delta-Werte ausweichen — die werden nicht
gelatcht.

Jetzt wird nach jedem Lesen quittiert, und damit funktioniert das
Statusregister für alle acht Kanäle:

```cpp
static void clearInterrupt() {
  bool ok = false;
  uint8_t main = regRead(REG_MAIN_CONTROL, &ok);
  if (ok) regWrite(REG_MAIN_CONTROL, main & ~0x01);   // INT-Bit löschen
}
```

Zwei weitere Einstellungen, die beim Start gesetzt werden:

```cpp
regWrite(REG_MULTI_TOUCH, 0x00);   // mehrere Griffe gleichzeitig erlauben
regWrite(REG_REPEAT_RATE, 0x00);   // keine Wiederholung, nur Flanken
```

Das erste ist wichtiger, als es aussieht: Ab Werk lässt der CAP1298 nur
**einen** Kanal gleichzeitig zu (Multiple Touch Blocking). An einer
Kletterwand, an der zwei Hände hängen, wird damit die zweite Hand
verschluckt.

Und eine Eigenheit, die beim Testen aufgefallen ist: Nach dem Einmessen
(`0x26`) meldet der Chip etwa eine Sekunde lang Berührungen, obwohl
niemand die Griffe anfasst. Ohne Gegenmaßnahme springt die Wand direkt
nach dem Einschalten von selbst in eine andere Betriebsart. Während dieser
Zeit werden die Register deshalb nur gelesen und quittiert, aber keine
Flanken ausgewertet.

Noch eine Messung aus der Praxis: Wenn das Statusregister eine Berührung
meldet, stehen die Delta-Werte trotzdem oft nahe null — der Chip führt
seine Basislinie sehr schnell nach. Die Delta-Werte taugen also nicht, um
eine Berührung gegenzuprüfen. Als Anzeige in der Oberfläche sind sie
trotzdem nützlich, um die Empfindlichkeit einzustellen.

Beide Auswertungen stecken weiterhin im Code und lassen sich im Betrieb
umschalten (Info → Sensor & System). Falls die Status-Auswertung an einem
Aufbau zickt, kommst du mit einem Fingertipp zurück auf deine
Delta-Variante, ohne neu zu flashen.

## 7. Der Webserver

Der Server ist die Standardbibliothek `WebServer` aus dem ESP32-Paket —
der einfache, synchrone. Er arbeitet eine Anfrage nach der anderen ab, und
genau dafür ist `webTick()` da:

```cpp
void webTick() { server.handleClient(); }
```

Liegt nichts an, kehrt der Aufruf sofort zurück. Liegt etwas an, wird die
passende Lambda-Funktion aufgerufen, die Antwort geschrieben, fertig.
Alles läuft im selben Ablauf wie die Animation — deshalb die Regel aus
Abschnitt 1.

**Die Seite selbst liegt im Flash**, als eine große Zeichenkette in
`web_page.h`:

```cpp
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
...
)HTML";
```

`R"HTML( ... )HTML"` ist ein Raw-String: Alles dazwischen gilt wörtlich,
auch Anführungszeichen und Zeilenumbrüche. `PROGMEM` sorgt dafür, dass der
Text im Programmspeicher bleibt und nicht beim Start ins RAM kopiert wird,
und `server.send_P()` kann direkt von dort senden.

Warum nicht als Datei in ein Dateisystem? Weil es dann zwei Dinge zu
flashen gäbe, die zusammenpassen müssen. So erneuert ein einziges
Firmware-Update Programm und Oberfläche gemeinsam.

Und warum keine fertigen Bausteine wie Bootstrap? Weil die Wand am Tag der
offenen Tür ihr eigenes WLAN aufspannt und **kein Internet** hat. Alles,
was nachgeladen werden müsste, wäre tot — deshalb stecken HTML, CSS und
JavaScript ohne eine einzige externe Abhängigkeit in dieser Datei.

**Eine Sache, die man leicht übersieht:** Alles, was von außen kommt, kann
Unsinn enthalten. Gibt ein Kind als Spielernamen `Max"` ein, würde das
Anführungszeichen die JSON-Antwort zerlegen, und die Oberfläche könnte sie
nicht mehr lesen — sie bliebe einfach stehen. Deshalb läuft jeder Name
durch einen Filter:

```cpp
static void sanitize(const String &in, char *out, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < in.length() && n < len - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || c == '<' || c == '>' || (uint8_t)c < 0x20) continue;
    out[n++] = c;
  }
  out[n] = 0;
}
```

Die Bedingung `n < len - 1` ist dabei genauso wichtig wie das Aussortieren
der Zeichen: Sie sorgt dafür, dass ein langer Name nicht über das Ende des
Zielpuffers hinausschreibt.

## 8. Das Spiel

„Griff-Jagd“ ist eine Zustandsmaschine mit vier Zuständen. Das klingt
größer, als es ist — es bedeutet nur: Es gibt eine Variable
`st.gameState`, und was passiert, hängt davon ab.

```
┌──────┐  Start     ┌───────┐  3-2-1  ┌─────────┐  alle   ┌────────┐
│ IDLE │───────────▶│ READY │────────▶│ RUNNING │────────▶│  DONE  │
└──────┘            └───────┘         └─────────┘ getroffen└────┬───┘
   ▲                                                            │
   └────────────────────────────────────────────────────────────┘
                        nach 10 Sekunden
```

- **IDLE**: Die sechs Touch-Griffe atmen blau. Eine Berührung startet eine
  Runde — ganz ohne Handy, was am Tag der offenen Tür der Normalfall ist.
- **READY**: Countdown auf der Wand, danach läuft die Uhr.
- **RUNNING**: Ein Griff leuchtet türkis. Treffer blitzt grün, Fehlgriff
  rot-orange.
- **DONE**: Grüne Welle, dann zurück zu IDLE.

Der Zustandswechsel passiert an zwei Stellen: in `boardOnGripTouched()`
(wenn jemand greift) und in `tickGame()` (wenn Zeit vergangen ist). Der
Countdown zum Beispiel ist nur eine Rechnung mit `millis()`:

```cpp
uint8_t step = (now - phaseMs) / 700;     // 0, 1, 2 -> drei, zwei, eins
if (step > 2) {
  st.gameState   = GAME_RUNNING;
  st.gameStartMs = now;
  st.gameTarget  = random(NUM_GRIPS);
  break;
}
```

Zwei Kleinigkeiten, die sich erst im Betrieb gezeigt haben:

```cpp
// Das nächste Ziel darf nie das aktuelle sein -- sonst wirkt es,
// als hätte das Spiel den Treffer nicht gemerkt.
static void gameNextTarget() {
  uint8_t next = st.gameTarget;
  while (next == st.gameTarget) next = random(NUM_GRIPS);
  st.gameTarget = next;
}
```

Und eine Sperre von zwei Sekunden nach Rundenende, bevor eine Berührung
die nächste Runde starten darf. Ohne sie löst derselbe Griff, auf dem die
Hand beim letzten Treffer noch liegt, sofort die nächste Runde aus — der
Sensor kalibriert bei liegender Hand nach und erzeugt dabei eine neue
Flanke.

## 9. Updates über WLAN

Nach dem ersten Flashen über USB geht alles Weitere drahtlos. Dahinter
stecken zwei unabhängige Wege:

**ArduinoOTA** (Port 3232) für PlatformIO. In `setup()` angemeldet, in
`loop()` bedient:

```cpp
ArduinoOTA.setHostname(HOSTNAME);
ArduinoOTA.setPassword(OTA_PASSWORD);
ArduinoOTA.onProgress([](unsigned p, unsigned t) {
  if (t) boardShowProgress((uint8_t)(100ULL * p / t), 0x0040FF);
});
ArduinoOTA.begin();
```

Der Fortschritt läuft als blauer Balken über die Wand — praktisch, weil
man dann sieht, ob überhaupt etwas ankommt.

**Browser-Upload** auf `/update` für alle, die kein PlatformIO haben. Die
Datei kommt in Blöcken an, und jeder Block wird direkt in den freien
Programmbereich geschrieben:

```cpp
HTTPUpload &up = server.upload();
if (up.status == UPLOAD_FILE_START) {
  erlaubt = server.authenticate("admin", OTA_PASSWORD);
  if (!erlaubt) return;
  Update.begin(UPDATE_SIZE_UNKNOWN);
} else if (up.status == UPLOAD_FILE_WRITE) {
  if (!erlaubt) return;
  Update.write(up.buf, up.currentSize);
} else if (up.status == UPLOAD_FILE_END) {
  if (!erlaubt) return;
  Update.end(true);
}
```

Das `erlaubt` muss sein: Das Passwort lässt sich nur beim ersten Block
prüfen, danach schickt der Browser keinen Anmeldekopf mehr mit. Ohne den
Merker wäre nur das erste Paket geschützt und alles Weitere offen.

**Warum das überhaupt geht:** Der Flash ist so aufgeteilt, dass zwei
vollständige Programme nebeneinander passen (`min_spiffs.csv`, zwei
Bereiche à 1,9 MB). Das Update wird in den gerade *nicht* laufenden
Bereich geschrieben; erst wenn alles da und die Prüfsumme in Ordnung ist,
schaltet der Bootloader um. Bricht die Übertragung ab, läuft die alte
Fassung unverändert weiter — man kann sich also nicht aussperren.

## 10. Einstellungen, die den Neustart überleben

Helligkeit, Tempo, Bestenliste und die Sensoreinstellungen liegen im NVS,
einem kleinen Schlüssel-Wert-Speicher im Flash:

```cpp
prefs.begin("kilter", false);
st.brightness = prefs.getUChar("bright", 160);   // 160 = Vorgabe, falls leer
```

Geschrieben wird **nicht sofort**. Flash verträgt nur eine begrenzte Zahl
Schreibzyklen, und ein Schieberegler löst beim Ziehen Dutzende Änderungen
aus. Deshalb setzt `settingsSave()` nur einen Merker, und erst wenn drei
Sekunden Ruhe war, wird wirklich geschrieben:

```cpp
void settingsSave() { dirty = true; dirtyAt = millis(); }

// in loop():
if (dirty && millis() - dirtyAt > 3000) settingsFlush();
```

Ein Schlüssel ist besonders: `cfgver`. Damit lassen sich beim Update
gespeicherte Werte gezielt korrigieren. Gebraucht wurde das schon einmal —
die Farbreihenfolge des LED-Streifens stand auf `GRB` (aus deinem
Original), richtig ist an dieser Wand aber `RGB`. Ohne Migration wäre der
alte Wert aus dem Flash nach dem Update wieder geladen worden und die
Korrektur hätte nichts bewirkt.

## 11. Selbst ausprobieren

Zum Reinkommen eignen sich kleine Änderungen, die man sofort sieht. Nach
jeder Änderung einmal `pio run -e kilterboard -t upload` (oder
`tools/ota.sh`, wenn die Wand im Netz hängt).

**Eine Farbe ändern.** In `board.cpp` steht die Farbfolge deiner
Griffe-Betriebsart:

```cpp
static const uint32_t touchColors[4] = {0x000000, 0x0000FF, 0x00FF00, 0xFF0000};
```

Setz statt Blau mal Orange (`0xFF8000`) ein. Das ist die Farbe in der
Form `0xRRGGBB`.

**Eine eigene Betriebsart bauen.** Vier Schritte:

1. In `config.h` bei `enum Mode` eine Nummer ergänzen, z. B.
   `MODE_LAUFLICHT = 7`.
2. In `board.cpp` eine Funktion schreiben, die `cellColor[]` füllt:
   ```cpp
   static void tickLauflicht() {
     fillAll(0);
     int pos = (millis() / 150) % NUM_CELLS;
     cellColor[pos] = 0xFFFFFF;
   }
   ```
3. Im `switch` in `boardTick()` eintragen:
   `case MODE_LAUFLICHT: tickLauflicht(); break;`
4. In `web_page.h` bei den Modusknöpfen eine Zeile ergänzen:
   `<button data-mode="7">Lauflicht</button>`

Mehr braucht es nicht — den Rest erledigt `/api/mode` von allein.

**Das Spiel schwerer machen.** In `tickGame()` im Zustand `GAME_RUNNING`
steht die Notbremse nach 120 Sekunden. Bau daraus ein Zeitlimit pro Griff:
Wer nicht innerhalb von zwei Sekunden trifft, verliert.

**Die Schnittstelle für eigene Programme** ist ein eigenes Thema und in
[API.md](API.md) beschrieben: Dort steht, wie man von außen abfragt, was
leuchtet, Griffe schaltet und die Sensoren ausliest. Im Code sind das die
Endpunkte `/api/leds`, `/api/leds/set` und `/api/sensors` in `web.cpp`
sowie die Betriebsart `MODE_API`, in der `boardTick()` absichtlich nichts
zeichnet — sonst würde die Animation jede von außen gesetzte Farbe im
nächsten Bild überschreiben.

**Beim Suchen hilft:** Jede Datei hat oben einen Kommentar, was sie macht.
Die Übersicht über alle Dateien und den Datenfluss steht in
[DOKUMENTATION.md](DOKUMENTATION.md), Verkabelung und Bedienung in
[README.md](README.md).

# Dokumentation des Quelltexts

Diese Datei beschreibt, wie die Firmware aufgebaut ist und warum sie so
aufgebaut ist. Für Verkabelung, Bedienung und Flashen siehe
[README.md](README.md).

## Inhalt

- [Überblick](#überblick)
- [Wie ein Bild auf die Wand kommt](#wie-ein-bild-auf-die-wand-kommt)
- [Die Module im Einzelnen](#die-module-im-einzelnen)
- [Der Touch-Sensor und seine Fallstricke](#der-touch-sensor-und-seine-fallstricke)
- [Das Spiel als Zustandsmaschine](#das-spiel-als-zustandsmaschine)
- [Netzwerk und Update](#netzwerk-und-update)
- [Was dauerhaft gespeichert wird](#was-dauerhaft-gespeichert-wird)
- [Die Weboberfläche](#die-weboberfläche)
- [Eine neue Betriebsart einbauen](#eine-neue-betriebsart-einbauen)
- [Stolpersteine](#stolpersteine)

## Überblick

Ein Seeed XIAO ESP32-C3 treibt 50 WS2812B-LEDs und liest einen
kapazitiven 8-Kanal-Sensor CAP1298 aus. Gleichzeitig liefert er eine
Weboberfläche aus, über die sich alles vom Handy steuern lässt, und nimmt
Firmware-Updates über WLAN an.

```
                    ┌──────────────────────────────────┐
   Browser  ───────▶│  web.cpp      Webserver + API    │
   (Handy)  ◀────── │  web_page.h   Oberfläche im Flash│
                    └───────────┬──────────────────────┘
                                │ ändert Zustand
                                ▼
   CAP1298 ──────▶ ┌────────────────────┐      ┌────────────────────┐
   (I2C)           │ touch.cpp          │─────▶│ config.h: State st │
                   │ Flanken erkennen   │      │ gemeinsamer Zustand│
                   └────────────────────┘      └─────────┬──────────┘
                                                         │ liest
                                                         ▼
                                              ┌──────────────────────┐
                                              │ board.cpp            │
   WS2812B ◀────────────────────────────────  │ Animation + Spiel    │
   (50 LEDs an D10)                           └──────────────────────┘
                                                         ▲
                    ┌────────────────────────────────────┘
                    │ main.cpp: Start, WLAN, OTA, Hauptschleife
                    └────────────────────────────────────────────
```

Es gibt genau einen Ausführungsstrang. `loop()` ruft nacheinander
`ArduinoOTA.handle()`, `webTick()`, `touchTick()` und `boardTick()` auf.
Keine dieser Funktionen wartet; jede prüft selbst, ob sie an der Reihe ist.
Dadurch braucht es keine Tasks, keine Mutexe und keine Sorge um
Nebenläufigkeit — der einzige `delay()` steht im Startvorgang.

```cpp
void loop() {
  ArduinoOTA.handle();
  webTick();                              // HTTP-Anfragen abarbeiten
  if (apMode) dns.processNextRequest();   // Captive-Portal
  touchTick();                            // alle 20 ms
  boardTick();                            // alle 20–30 ms ein Bild
  if (dirty && millis() - dirtyAt > 3000) settingsFlush();
  ...
}
```

## Wie ein Bild auf die Wand kommt

Die Wand hat 50 LEDs, aber nur 34 davon sitzen hinter Griffen. Statt
überall mit LED-Nummern zu rechnen, kennt der Code **Griffpositionen**
0…33 und bildet sie einmalig auf LED-Indizes ab:

```cpp
static int cellLed[NUM_CELLS];      // Position -> LED-Index im Streifen
static uint32_t cellColor[NUM_CELLS];  // logische Farbe dieser Position
```

Gerendert wird in zwei Schritten:

1. Die aktive Betriebsart schreibt Farben nach `cellColor[]`.
2. `render()` überträgt sie auf den Streifen und legt kurze
   Rückmeldungen (Treffer, Fehlgriff) darüber.

Der Umweg lohnt sich aus drei Gründen:

- **Die Weboberfläche kann mitlesen.** `boardCellColor()` liefert die
  logische Farbe, unabhängig von der eingestellten Helligkeit. Die Wand
  auf dem Handy zeigt deshalb satte Farben, auch wenn die echte Wand
  gedimmt ist.
- **Kein Farbverlust.** Würde man die Helligkeit direkt in die
  Pixelwerte rechnen, verlöre man bei jedem Wechsel Genauigkeit. Hier
  geht jedes Bild frisch aus `cellColor[]` hervor.
- **Die 16 unbenutzten LEDs bleiben zuverlässig dunkel**, weil `render()`
  immer mit `pixels.clear()` beginnt und nur bekannte Positionen setzt.

Die Zuordnung Position → LED stammt aus der Verdrahtung und steht in
`mainMatrix` (5×6, Reihe 0 oben) und `miniMatrix` (2×2). Die sechs Griffe
mit Touch-Sensor belegen je zwei Positionen und liegen in den oberen zwei
Reihen; `gripCells[][]` wird beim Start aus der LED-Tabelle berechnet,
damit die Zuordnung nicht doppelt gepflegt werden muss.

## Die Module im Einzelnen

### `config.h`

Enthält keine Logik, sondern das, was alle brauchen: Pins, Maße,
Aufzählungen und vor allem den gemeinsamen Zustand `struct State st` mit
Betriebsart, Helligkeit, Griff-Farben, Spielstand und Spielername. Alles,
was die Weboberfläche anzeigt oder ändert, steht hier.

### `board.cpp`

LED-Mapping, Animationen und Spiellogik. Pro Betriebsart gibt es eine
`tick…()`-Funktion, die ein Bild in `cellColor[]` schreibt:

| Funktion | Betriebsart |
|---|---|
| `tickRainbow()` | Farbverlauf, pro Reihe um `ROW_OFFSET` versetzt |
| `tickTouch()` | Farbe je Griff aus `st.gripState[]` |
| `tickGame()` | Spielanzeige je nach Zustand |
| `tickTest()` | Testbild für die Farbreihenfolge |

`boardTick()` begrenzt die Bildrate (20 ms im Regenbogen, sonst 30 ms)
und ruft die passende Funktion auf. `flashGrip()` legt eine kurze
Rückmeldung über das Bild, ohne die Betriebsart zu stören.

### `touch.cpp`

Liest den CAP1298 alle 20 ms, erkennt Flanken und meldet sie weiter:
Kanal 1 und 2 sind die Taster T1/T2 und schalten die Betriebsart, Kanal
3…8 sind die Griffe und gehen an `boardOnGripTouched()`. Details unten.

### `web.cpp`

Der Webserver (`WebServer`, synchron) mit allen Endpunkten, der
JSON-Statusantwort, der Bestenliste und dem Firmware-Upload. Die
Statusantwort wird von Hand zusammengebaut statt mit einer JSON-Bibliothek
— sie ist rund 1,5 kB groß und hat eine feste Form, eine Abhängigkeit
lohnt dafür nicht.

Alles, was von außen kommt, läuft durch `sanitize()`:

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

Ohne das würde ein Spielername mit Anführungszeichen die Statusantwort
zerlegen; die Oberfläche könnte sie nicht mehr lesen und bliebe stehen.

### `web_page.h`

Die komplette Oberfläche als `PROGMEM`-Zeichenkette: HTML, CSS und
JavaScript in einem Stück, ohne externe Schriften oder Bibliotheken. Am
Tag der offenen Tür hängt die Wand in ihrem eigenen WLAN ohne Internet —
alles, was nachgeladen werden müsste, wäre tot. Ein Dateisystem braucht es
dadurch auch nicht, und ein einziges OTA-Update erneuert Firmware und
Oberfläche gemeinsam.

### `main.cpp`

Startvorgang, WLAN, mDNS, OTA und die Hauptschleife. Außerdem das
verzögerte Speichern der Einstellungen.

## Der Touch-Sensor und seine Fallstricke

Der CAP1298 ist der Teil, an dem die meiste Zeit hängen geblieben ist.
Drei Eigenheiten muss man kennen:

**1. Das Statusregister ist gelatcht.** Register `0x03` behält ein
gesetztes Bit, bis das INT-Bit im Main-Control-Register `0x00` gelöscht
wird. Ohne dieses Quittieren bleibt eine Berührung „ewig gedrückt“, und es
kommt genau eine Flanke an — danach nie wieder:

```cpp
static void clearInterrupt() {
  bool ok = false;
  uint8_t main = regRead(REG_MAIN_CONTROL, &ok);
  if (ok) regWrite(REG_MAIN_CONTROL, main & ~0x01);
}
```

**2. Mehrere Griffe gleichzeitig sind ab Werk gesperrt.** Das Register
`0x2A` (Multiple Touch Configuration) lässt in der Grundeinstellung nur
einen Kanal zu. Für eine Kletterwand, an der zwei Hände hängen, ist das
unbrauchbar, deshalb wird es abgeschaltet.

**3. Nach dem Einmessen schwingt der Chip ein.** Ein `0xFF` auf Register
`0x26` startet die Kalibrierung; danach meldet der Sensor etwa eine
Sekunde lang Berührungen, obwohl niemand die Griffe anfasst. Ohne
Gegenmaßnahme springt die Wand direkt nach dem Start von selbst in eine
andere Betriebsart. Während der Einschwingzeit werden die Register
deshalb nur gelesen und quittiert, aber keine Flanken ausgewertet.

Zwei Auswertungswege stehen zur Wahl, umschaltbar im laufenden Betrieb:

- **Statusregister** (Standard): Der Chip entscheidet selbst, was eine
  Berührung ist. Zuverlässig, solange das INT-Bit quittiert wird.
- **Delta-Werte** mit eigener Schwelle und Hysterese: nützlich, wenn die
  eingebaute Erkennung nicht passt.

Gemessen an der echten Wand: Das Statusregister meldet zuverlässig,
während die Delta-Register dabei trotzdem nahe null stehen — die
Basislinie wird intern schnell nachgeführt. Die Delta-Werte taugen hier
also nicht als Plausibilitätsprüfung, wohl aber als Anzeige in der
Oberfläche, um die Empfindlichkeit einzustellen.

Fällt der Sensor aus (Wackelkontakt an der I2C-Leitung), merkt der
Treiber das am fehlgeschlagenen Lesen, setzt `tc.present = false` und
versucht alle fünf Sekunden einen Neuaufbau, statt den Touch bis zum
Neustart aufzugeben.

## Das Spiel als Zustandsmaschine

„Griff-Jagd“ läuft über vier Zustände in `st.gameState`:

```
   ┌────────────────────────────────────────────────────┐
   │                                                    │
   ▼                                                    │
┌──────┐  Start      ┌───────┐  3-2-1   ┌─────────┐    │ nach 10 s
│ IDLE │────────────▶│ READY │─────────▶│ RUNNING │    │
└──────┘  (Website   └───────┘ (2,1 s)  └────┬────┘    │
   ▲      oder Griff                         │          │
   │      an der Wand)          alle Griffe  │          │
   │                            getroffen    ▼          │
   │                                    ┌────────┐      │
   └────────────────────────────────────│  DONE  │──────┘
            Abbruch, 120-s-Notbremse    └────────┘
```

- **IDLE**: Die sechs Touch-Griffe atmen blau als Einladung. Eine
  Berührung startet eine Runde — ohne Handy, was am Tag der offenen Tür
  der häufigste Fall ist.
- **READY**: Countdown 3-2-1 auf der Hauptmatrix, dann startet die Uhr.
- **RUNNING**: Ein zufälliger Griff leuchtet türkis. Treffer blitzt grün
  und setzt das nächste Ziel, ein Fehlgriff blitzt rot-orange und zählt
  einen Fehler. Die unterste Reihe zeigt den Fortschritt.
- **DONE**: Grüne Welle als Belohnung, nach zehn Sekunden zurück zu IDLE.

Zwei Details, die in der Praxis nötig waren:

```cpp
// Das nächste Ziel ist nie dasselbe wie das aktuelle
static void gameNextTarget() {
  uint8_t next = st.gameTarget;
  while (next == st.gameTarget) next = random(NUM_GRIPS);
  st.gameTarget = next;
}
```

Und eine Sperre von zwei Sekunden nach Rundenende: Sonst löst derselbe
Griff, auf dem die Hand beim letzten Treffer noch liegt, sofort die
nächste Runde aus — der Sensor kann bei liegender Hand nachkalibrieren und
dabei eine neue Flanke erzeugen.

Gewertet wird nach **Zeit je Griff**, nicht nach Gesamtzeit, damit Runden
mit 5, 10 und 20 Griffen in derselben Bestenliste vergleichbar sind.

## Netzwerk und Update

Beim Start versucht die Wand das in `src/secrets.h` hinterlegte WLAN. Der
Verbindungsversuch läuft im Hintergrund, während der Countdown 3-2-1
angezeigt wird; erst danach wird bis zu acht Sekunden gewartet. Klappt es
nicht, spannt sie ihr eigenes WLAN auf und startet einen DNS-Server, der
alle Namen auf die eigene Adresse beantwortet — zusammen mit einer
Umleitung auf `/` sorgt das dafür, dass sich die Seite auf den meisten
Handys von selbst öffnet.

Bricht ein verbundenes WLAN später weg, prüft `loop()` das alle zehn
Sekunden und schaltet auf den eigenen Zugang um, damit die Oberfläche
erreichbar bleibt.

Für Updates gibt es zwei Wege:

- **ArduinoOTA** auf Port 3232, passwortgeschützt, für
  `pio run -e kilterboard_ota -t upload`. Der Fortschritt läuft als Balken
  über die Wand, damit man sieht, dass etwas passiert.
- **Browser-Upload** auf `/update` mit HTTP-Basisauthentifizierung. Die
  Berechtigung wird beim ersten Datenblock geprüft und in einem Merker
  festgehalten, der für jeden weiteren Block und den Abschluss erneut
  abgefragt wird — sonst griffe der Schutz nur für das erste Paket.

Die Partitionierung `min_spiffs.csv` hält zwei App-Bereiche à 1,9 MB
bereit, von denen die Firmware knapp die Hälfte belegt. Ein
fehlgeschlagenes Update lässt die alte Fassung unberührt.

## Was dauerhaft gespeichert wird

Alles liegt im NVS unter dem Namensraum `kilter`:

| Schlüssel | Inhalt |
|---|---|
| `bright`, `speed` | Helligkeit und Animationstempo |
| `order` | Farbreihenfolge des LED-Streifens |
| `cfgver` | Fassung der Einstellungen, für Umstellungen beim Update |
| `best`, `gtotal` | Bestzeit und zuletzt gewählte Rundenlänge |
| `top` | Bestenliste, fünf Einträge als Block |
| `thr`, `sens`, `delta` | Einstellungen des Touch-Sensors |

Geschrieben wird **verzögert**: `settingsSave()` setzt nur einen Merker,
und erst wenn drei Sekunden lang nichts mehr passiert ist, landen die
Werte wirklich im Flash. Ohne das würde jede Bewegung eines Schiebereglers
einen Schreibvorgang auslösen.

`cfgver` ist der Haken für Umstellungen: Beim Sprung auf eine neue
Fassung lassen sich gespeicherte Werte gezielt korrigieren, ohne dass der
Anwender etwas tun muss. Benutzt wurde das bereits, um die Farbreihenfolge
von `GRB` auf `RGB` zu ziehen.

## Die Weboberfläche

Ein einziges HTML-Dokument mit drei Reitern (Steuern, Spiel, Info). Sie
fragt viermal pro Sekunde `/api/status` ab und zeichnet daraus alles neu:
Wandansicht, Knöpfe, Spielstand, Bestenliste, Sensorwerte. Befehle gehen
als einfache GET-Aufrufe zurück.

Die Farben der 34 Positionen kommen als **eine** Zeichenkette aus 34 mal
sechs Hex-Zeichen statt als Feld aus 34 Einträgen — das spart in jeder
Antwort einige hundert Byte und lässt sich mit `substr` genauso leicht
auslesen.

Beim Zeichnen gilt durchgehend: Ein Bedienelement, das gerade den Fokus
hat oder in das jemand getippt hat, wird nicht überschrieben. Sonst
springt ein Schieberegler zurück, während man ihn zieht.

Der **Farbtest** verdient eine eigene Erwähnung. Welche Reihenfolge der
Farbkanäle ein LED-Streifen erwartet, steht ihm nicht an. Statt sechs
Möglichkeiten durchzuprobieren, fragt die Seite nur, welche Farbe die
oberste und die zweite Reihe des Testbilds *wirklich* zeigen, und rechnet
daraus die richtige Einstellung aus:

```js
const akt = state.ledOrders[state.ledOrder];
const p = [];
p[akt.indexOf('R')] = gesehen0;   // Reihe 1 ist als Rot programmiert
p[akt.indexOf('G')] = gesehen1;   // Reihe 2 als Grün
const fehlt = 'RGB'.split('').find(c => !p.includes(c));
for (let i = 0; i < 3; i++) if (!p[i]) p[i] = fehlt;
```

Dahinter steckt: Die Bibliothek legt einen Farbwert an die Stelle im
Datenstrom, an der die eingestellte Reihenfolge diesen Kanal führt; der
Streifen liest dieselbe Stelle als seinen eigenen Kanal. Aus zwei
Beobachtungen ergibt sich die Zuordnung eindeutig, die dritte folgt. Die
Rechnung wurde für alle 36 Kombinationen aus tatsächlicher und
eingestellter Reihenfolge geprüft.

## Eine neue Betriebsart einbauen

1. In `config.h` einen Wert in `enum Mode` ergänzen.
2. In `board.cpp` eine `tick…()`-Funktion schreiben, die `cellColor[]`
   füllt, und sie in `boardTick()` eintragen.
3. Wenn Berührungen anders wirken sollen: in `boardOnGripTouched()`
   behandeln.
4. In `web_page.h` einen Knopf mit `data-mode="<Nummer>"` ergänzen — mehr
   braucht die Oberfläche nicht, der Rest passiert über `/api/mode`.

Soll etwas dauerhaft gespeichert werden, kommt es in `settingsLoad()` und
`settingsFlush()` in `main.cpp` dazu.

## Stolpersteine

- **Flashen über USB bricht ab** („No serial data received“), weil der
  native USB-Serial/JTAG des ESP32-C3 den esptool-Stub nicht zuverlässig
  mitmacht. Mit `--no-stub` geht es; der vollständige Befehl steht im
  README.
- **`millis()` läuft nach 49 Tagen über.** Alle Zeitvergleiche im Code
  arbeiten deshalb mit Differenzen (`now - start > dauer`) statt mit
  direkten Vergleichen.
- **`float` verliert Genauigkeit**, wenn man `millis()` direkt
  hineinrechnet. Nach wenigen Stunden Laufzeit bliebe eine
  Pulsanimation stehen, deshalb wird vorher auf einen kleinen Bereich
  zurückgerechnet (`millis() % 100000`).
- **Beim Start nicht an die Griffe fassen** — dann misst sich der Sensor
  auf einen falschen Ruhewert ein. Nachhelfen geht über „Sensor
  kalibrieren“ in der Oberfläche.

# Kilterboard — LED-Kletterwand mit Touch-Griffen

Kletterwand im Kleinformat nach dem Vorbild eines Kilterboards: hinter den
Griffen sitzen einzeln ansteuerbare LEDs, sechs Griffe erkennen per
kapazitivem Sensor, dass eine Hand sie berührt. Bedient wird alles über
eine Weboberfläche, die der Mikrocontroller selbst ausliefert — ohne
Internet, ohne App, ohne Server.

Ursprung ist der Testaufbau `Kilterboard_test` eines Schulkollegen (ein
`main.cpp`, blockierend, nur Regenbogen und Griffe-Durchschalten) — von ihm
stammen der Aufbau, die Verdrahtung und das LED-Mapping. Diese Fassung ist
der Ausbau für den Tag der offenen Tür: Weboberfläche, Reaktionsspiel und
Updates über WLAN.

Wie die Firmware innen aufgebaut ist, steht in
[DOKUMENTATION.md](DOKUMENTATION.md).

## Hardware

| Teil | Anschluss | Bemerkung |
|---|---|---|
| Seeed XIAO ESP32-C3 | USB-C | WLAN, programmiert über den nativen USB |
| 50x WS2812B | D10 | 34 davon sitzen hinter Griffen, 16 bleiben dunkel |
| CAP1298 | D4 = SDA (GPIO6), D5 = SCL (GPIO7) | I2C-Adresse 0x28, 3V3/GND |

Kanalbelegung des CAP1298: **CS1** = Taster T1, **CS2** = Taster T2,
**CS3…CS8** = Griffe 1…6. Jeder Griff hat zwei LEDs.

Positionen: 5x6 Hauptmatrix (Position 0…29, zeilenweise, Reihe 0 oben) plus
2x2 Zusatz (30…33). Die sechs Touch-Griffe sind die Positionen 0…11, also
die beiden oberen Reihen, paarweise.

## Betriebsarten

| Nr. | Name | Was passiert |
|---|---|---|
| 1 | Regenbogen | fließender Farbverlauf über alle Positionen |
| 2 | Griffe | jede Berührung schaltet die Farbe weiter: aus → blau → grün → rot |
| 3 | Spiel | Reaktionsspiel „Griff-Jagd“ |
| 4 | Aus | alles dunkel |
| 5 | Testbild | Reihe 1 rot, 2 grün, 3 blau, 4 gelb, 5 weiß — zum Einstellen der Farbreihenfolge |

**Griff-Jagd:** Ein Griff leuchtet türkis, wer ihn berührt, bekommt den
nächsten. Gemessen werden Gesamtzeit und Reaktionszeit pro Griff; die
Bestenliste (Top 5, nach Zeit je Griff) überlebt den Neustart. Gestartet
wird über die Website oder direkt an der Wand durch Antippen eines Griffs —
ohne Handy, was am Tag der offenen Tür praktisch ist.

An der Wand selbst: **T1** schaltet auf Regenbogen, **T2** wechselt
zwischen Griffe und Spiel.

## Farbreihenfolge des LED-Streifens

Der ursprüngliche Sketch nahm `NEO_GRB` an. An dieser Wand stimmt das nicht:
Was als grün angefordert wird, leuchtet damit rot — Rot und Grün sind
vertauscht, Blau passt. Die Vorgabe steht deshalb auf **`RGB`**.

Ein zweiter Weg, dasselbe zu prüfen, ohne das Testbild: In der Betriebsart
**Griffe** schaltet jede Berührung aus → blau → grün → rot. Was davon
ankommt, verrät die Reihenfolge (ausgehend von der Einstellung `RGB`):

| gesehene Abfolge | richtige Einstellung |
|---|---|
| blau → grün → rot | `RGB` (stimmt bereits) |
| grün → blau → rot | `RBG` |
| blau → rot → grün | `GRB` |
| rot → blau → grün | `GBR` |
| grün → rot → blau | `BRG` |
| rot → grün → blau | `BGR` |

Sollte es an einem anderen Aufbau wieder anders sein: **Info → Farben
richtigstellen**. Dort Testbild einschalten und angeben, welche Farbe die
oberste und die zweite Reihe *wirklich* zeigen; die Seite rechnet die
passende Reihenfolge aus und stellt sie ein. Die Auswahl liegt im NVS und
übersteht den Neustart, ein Flashen ist dafür nicht nötig.

## Netzwerk

Beim Start versucht die Wand das in `src/secrets.h` eingetragene WLAN. Klappt
das nicht (oder steht dort `-`), spannt sie ihr eigenes auf:

- WLAN **Kletterwand**, Passwort `kletterwand`
- Adresse **http://192.168.4.1** — ein Captive-DNS leitet alle Anfragen
  dorthin, auf den meisten Handys geht die Seite von selbst auf
- im Heimnetz zusätzlich **http://kilterboard.local**

Bricht ein verbundenes WLAN weg, schaltet die Wand nach spätestens zehn
Sekunden selbst auf den eigenen Access Point um.

## Bauen und flashen

```bash
cp src/secrets.example.h src/secrets.h   # WLAN + Passwörter eintragen
cp secrets.example.ini secrets.ini       # OTA-Ziel + gleiches Passwort
pio run -e kilterboard -t upload         # erstes Mal über USB
pio device monitor                       # Logs ansehen
```

Falls `pio ... -t upload` mit *„No serial data received“* abbricht: Der
native USB-Serial/JTAG des C3 verträgt den Stub nicht zuverlässig. Dann
ohne Stub flashen:

```bash
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32c3 --port /dev/ttyACM0 --no-stub --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 .pio/build/kilterboard/bootloader.bin \
  0x8000 .pio/build/kilterboard/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/kilterboard/firmware.bin
```

## Update über WLAN (OTA)

Ab dem ersten USB-Flash geht alles Weitere drahtlos — zwei Wege:

```bash
tools/ota.sh                 # an kilterboard.local
tools/ota.sh 192.168.4.1     # an die Wand im eigenen WLAN
```

Oder im Browser: **Info → Sensor & System → Firmware-Update**, dort
`.pio/build/kilterboard/firmware.bin` hochladen. Benutzer `admin`,
Passwort wie `OTA_PASSWORD` in `src/secrets.h`.

Die Partitionierung (`min_spiffs.csv`) hält zwei App-Bereiche à 1,9 MB
bereit; die Firmware belegt davon knapp die Hälfte. Schlägt ein Update
fehl, läuft die alte Fassung weiter.

## Aufbau des Quelltexts

| Datei | Inhalt |
|---|---|
| `src/config.h` | Konstanten, Betriebsarten, gemeinsamer Zustand |
| `src/board.cpp` | LED-Mapping, Animationen, Spiellogik |
| `src/touch.cpp` | CAP1298-Treiber, Flankenerkennung |
| `src/web.cpp` | Webserver, API, Browser-Update |
| `src/web_page.h` | die Weboberfläche (HTML/CSS/JS im Flash) |
| `src/main.cpp` | Start, WLAN, OTA, Hauptschleife |

Gerendert wird zweistufig: Die Betriebsart schreibt Farben in `cellColor[]`
(eine je Griffposition), `render()` schiebt sie auf den Strip. Dadurch kennt
die Weboberfläche jederzeit die logische Farbe jeder Position, unabhängig
von der Helligkeit. In `loop()` wartet nichts — `delay()` gibt es nur noch
im Startvorgang.

## Was am CAP1298 wichtig ist

- Das Statusregister `0x03` ist **gelatcht**. Es löscht sich erst, wenn das
  INT-Bit im Main-Control-Register `0x00` zurückgesetzt wird. Ohne dieses
  Quittieren bleibt eine Berührung „ewig gedrückt“ und es kommt genau eine
  Flanke an. Der Treiber quittiert nach jedem Lesen.
- **Multiple-Touch-Blocking** (`0x2A`) ist ab Werk aktiv und lässt nur einen
  Kanal gleichzeitig zu — für eine Kletterwand unbrauchbar, daher
  abgeschaltet.
- Nach dem Einmessen (`0x26`) meldet der Chip rund eine Sekunde lang
  Berührungen, obwohl niemand die Griffe anfasst. Der Treiber wertet in
  dieser Zeit keine Flanken aus, sonst springt die Wand beim Start von
  selbst in eine andere Betriebsart.
- Beim Start sollte niemand die Griffe anfassen — dann wird die Basislinie
  falsch eingemessen. Nachhelfen geht über **Sensor kalibrieren** in der
  Weboberfläche.
- Arbeitet ein Griff nicht zuverlässig, zeigt die Oberfläche unter
  **Info → Sensor & System** die Rohwerte aller acht Kanäle. Dort lassen
  sich Empfindlichkeit und Schwelle live verstellen und zwischen
  Statusregister und Delta-Auswertung umschalten, ohne neu zu flashen.

## Schnittstelle

Alles über GET, Antwort `ok`; Status als JSON unter `/api/status`.

```
/api/mode?m=1..5              Betriebsart
/api/ledorder?o=0..5          Farbreihenfolge (RGB, RBG, GRB, GBR, BRG, BGR)
/api/bright?v=5..255          Helligkeit
/api/speed?v=0..100           Animationstempo
/api/grip?i=0..5[&c=0..3]     Griff weiterschalten oder Farbe setzen
/api/game/start?n=3..50&name= Runde starten
/api/game/stop                abbrechen
/api/game/reset               Bestzeit löschen
/api/game/top/clear           Bestenliste löschen
/api/touch?thr=&sens=&src=    Sensor einstellen (src = status|delta)
/api/touch/calib              neu einmessen
/api/reboot                   Neustart (Passwort nötig)
```

## Geheimnisse

`src/secrets.h` (WLAN, AP-Passwort, OTA-Passwort) und `secrets.ini`
(OTA-Ziel fürs Hochladen) liegen nicht im Repo. Das OTA-Passwort muss in
beiden Dateien gleich sein. Vorlagen: `src/secrets.example.h` und
`secrets.example.ini`.

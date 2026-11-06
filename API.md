# Schnittstelle über WLAN

Mit dieser Schnittstelle lässt sich die Wand aus eigenen Programmen
steuern: abfragen, welche Griffe leuchten und in welcher Farbe, Griffe
ein- und ausschalten und die Sensoren auslesen.

Alles läuft über einfache HTTP-Aufrufe ohne besondere Kopfzeilen. Jeder
Browser, `curl`, Python, Node oder ein ESP32 kommt damit zurecht.
`Access-Control-Allow-Origin: *` ist gesetzt, eine Webseite von einem
anderen Rechner darf die Wand also auch direkt ansprechen.

## Adresse

| Lage | Adresse |
|---|---|
| Wand spannt ihr eigenes WLAN auf (`Kletterwand`) | `http://192.168.4.1` |
| Wand hängt in einem bekannten WLAN | `http://kilterboard.local` |

Im Folgenden steht `$WAND` für eine der beiden Adressen:

```bash
WAND=http://192.168.4.1
curl $WAND/api/leds
```

## Begriffe

- **Griff (1…6)** — ein Griff mit Touch-Sensor. Jeder belegt zwei LEDs,
  die immer gemeinsam leuchten.
- **Position / cell (0…33)** — eine der 34 Griffstellen. 0…29 sind die
  Hauptmatrix (zeilenweise, Reihe 0 oben, 6 je Reihe), 30…33 die vier
  Zusatzstellen. Die Positionen 0…11 gehören zu den sechs Touch-Griffen.
- **Farbe** — `RRGGBB` als Hex, mit oder ohne `#`, oder `off` zum
  Ausschalten. Die eingestellte Helligkeit wird erst beim Ausgeben
  verrechnet; abgefragt wird immer die gesetzte Farbe.

---

## Abfragen: was leuchtet gerade?

```
GET /api/leds
```

```json
{
  "mode": 6,
  "remote": true,
  "brightness": 160,
  "gripCount": 6,
  "cellCount": 34,
  "grips": [
    { "id": 1, "on": true, "color": "FF0000", "touched": true,
      "touches": 17, "cells": [0, 1], "leds": [4, 3] },
    { "id": 2, "on": false, "color": "000000", "touched": false,
      "touches": 3, "cells": [2, 3], "leds": [1, 0] }
  ],
  "cells": [
    { "i": 0, "on": true, "color": "FF0000", "led": 4, "sensor": true }
  ]
}
```

| Feld | Bedeutung |
|---|---|
| `mode` | aktive Betriebsart (6 = Fernsteuerung) |
| `remote` | `true`, wenn gerade nur gilt, was über die Schnittstelle gesetzt wurde |
| `grips[].on` / `.color` | leuchtet dieser Griff, und in welcher Farbe |
| `grips[].touched` | wird er in diesem Moment berührt |
| `grips[].touches` | Zähler aller Berührungen seit dem Einschalten |
| `grips[].cells` / `.leds` | zugehörige Positionen und LED-Nummern im Streifen |
| `cells[].sensor` | sitzt an dieser Position ein Touch-Sensor |

Der Zähler `touches` ist dafür da, dass ein Programm keine Berührung
verpasst: Wer nur jede halbe Sekunde abfragt, sieht ein kurzes Antippen in
`touched` nicht, am gestiegenen Zähler aber schon.

## Schalten: Griffe leuchten lassen

```
GET /api/leds/set?grip=<1..6>&color=<RRGGBB|off>
GET /api/leds/set?cell=<0..33>&color=<RRGGBB|off>
GET /api/leds/set?all=<RRGGBB|off>
GET /api/leds/set?cells=<34 × 6 Hex-Zeichen>
GET /api/leds/off
```

```bash
curl "$WAND/api/leds/set?grip=3&color=00FF00"   # Griff 3 grün
curl "$WAND/api/leds/set?grip=3&color=off"      # Griff 3 aus
curl "$WAND/api/leds/set?cell=17&color=FF8000"  # einzelne Position orange
curl "$WAND/api/leds/set?all=101010"            # alles schwach weiß
curl "$WAND/api/leds/off"                       # alles aus
```

Antwort:

```json
{ "ok": true, "set": 1, "mode": 6 }
```

Für ganze Bilder gibt es `cells`: 34 Farben hintereinander, je sechs
Hex-Zeichen, insgesamt 204 Zeichen. Das ist ein einziger Aufruf statt 34
und damit der richtige Weg für eigene Animationen.

```bash
# alle 34 Positionen auf einmal: die ersten zwei rot, der Rest aus
curl "$WAND/api/leds/set?cells=FF0000FF0000$(printf '000000%.0s' {1..32})"
```

Bei falschen Angaben kommt `400` mit Begründung:

```json
{ "ok": false, "error": "grip muss 1 bis 6 sein" }
```

> **Wichtig:** Der erste Schaltbefehl stellt die Wand auf Fernsteuerung
> (`mode` 6). Sonst würde die laufende Animation die gesetzte Farbe im
> nächsten Bild sofort überschreiben. Gesetzte Farben bleiben dann stehen,
> bis sie geändert werden. Zurück geht es mit `GET /api/mode?m=1`
> (Regenbogen) oder auf der Weboberfläche — dort erscheint in diesem
> Zustand ein Hinweis samt Knopf, damit niemand vor einer scheinbar toten
> Wand steht.

## Sensoren abfragen

```
GET /api/sensors
```

```json
{
  "ok": true,
  "chip": "CAP1298",
  "productId": 113,
  "source": "status",
  "threshold": 20,
  "sensitivity": 2,
  "statusRegister": 4,
  "channels": [
    { "ch": 1, "name": "T1", "grip": null, "touched": false, "touches": 0, "raw": 0 },
    { "ch": 3, "name": "Griff 1", "grip": 1, "touched": true, "touches": 17, "raw": 45 }
  ]
}
```

Acht Kanäle: `ch` 1 und 2 sind die beiden Taster T1/T2 (`grip` ist `null`),
`ch` 3…8 sind die Griffe 1…6.

| Feld | Bedeutung |
|---|---|
| `ok` | antwortet der Sensor überhaupt |
| `touched` | wird der Kanal in diesem Moment berührt |
| `touches` | Zähler seit dem Einschalten |
| `raw` | Delta-Rohwert (vorzeichenbehaftet, −128…127) |
| `source` | `status` = Erkennung durch den Chip, `delta` = eigene Schwelle |

Zum `raw`-Wert: Bei der Einstellung `status` entscheidet der CAP1298
selbst, was eine Berührung ist, und führt seine Basislinie dabei schnell
nach. Der Rohwert steht deshalb oft nahe null, **auch während einer
echten Berührung** — er taugt zum Einstellen der Empfindlichkeit, nicht
als Prüfgröße. Verlass dich auf `touched` und `touches`.

Einstellen lässt sich der Sensor über:

```
GET /api/touch?thr=<2..120>&sens=<0..7>&src=<status|delta>
GET /api/touch/calib      # neu einmessen, dabei die Griffe nicht anfassen
```

## Nebenbei nützlich

```
GET /api/status           alles auf einmal (das, was die Weboberfläche nutzt)
GET /api/mode?m=<1..6>    1 Regenbogen, 2 Griffe, 3 Spiel, 4 Aus,
                          5 Testbild, 6 Fernsteuerung
GET /api/bright?v=<5..255>  Helligkeit
```

## Beispiele

### Lauflicht über alle sechs Griffe

```bash
WAND=http://192.168.4.1
while true; do
  for g in 1 2 3 4 5 6; do
    curl -s "$WAND/api/leds/off" > /dev/null
    curl -s "$WAND/api/leds/set?grip=$g&color=00AAFF" > /dev/null
    sleep 0.2
  done
done
```

### Berührte Griffe aufleuchten lassen (Python)

```python
import requests, time

WAND = "http://192.168.4.1"
vorher = {}

while True:
    s = requests.get(f"{WAND}/api/sensors", timeout=2).json()
    for ch in s["channels"]:
        if ch["grip"] is None:
            continue
        # Zähler gestiegen? Dann wurde der Griff angefasst.
        if ch["touches"] != vorher.get(ch["ch"], ch["touches"]):
            print(f"{ch['name']} berührt")
            requests.get(f"{WAND}/api/leds/set",
                         params={"grip": ch["grip"], "color": "FF00FF"})
        vorher[ch["ch"]] = ch["touches"]
    time.sleep(0.1)
```

### Ganzes Bild in einem Aufruf (Python)

```python
import requests

WAND = "http://192.168.4.1"
farben = ["000000"] * 34
farben[0] = farben[1] = "FF0000"     # Griff 1 rot
farben[4] = farben[5] = "00FF00"     # Griff 3 grün

requests.get(f"{WAND}/api/leds/set", params={"cells": "".join(farben)})
```

### Reaktionsspiel selbst bauen (Python)

```python
import requests, random, time

WAND = "http://192.168.4.1"
get = lambda pfad, **p: requests.get(f"{WAND}{pfad}", params=p, timeout=2).json()

get("/api/leds/off")
punkte, start = 0, time.time()

while punkte < 10:
    ziel = random.randint(1, 6)
    get("/api/leds/set", grip=ziel, color="00FFFF")
    stand = get("/api/sensors")["channels"][ziel + 1]["touches"]

    while get("/api/sensors")["channels"][ziel + 1]["touches"] == stand:
        time.sleep(0.05)

    get("/api/leds/set", grip=ziel, color="00FF00")
    time.sleep(0.15)
    get("/api/leds/set", grip=ziel, color="off")
    punkte += 1

print(f"10 Griffe in {time.time() - start:.1f} s")
```

## Grenzen

- Der Webserver bearbeitet **eine Anfrage nach der anderen**. Mehrere
  Programme gleichzeitig gehen, werden aber langsamer. Für flüssige
  Animationen reichen etwa 10 bis 20 Aufrufe pro Sekunde; darüber lohnt
  `cells`, weil ein Aufruf das ganze Bild setzt.
- Es gibt **keine Anmeldung**. Wer im selben WLAN ist, darf schalten. Das
  ist für ein Gerät gedacht, das sein eigenes WLAN aufspannt. Geschützt
  sind nur `/update` und `/api/reboot`.
- Die Wand **meldet sich nicht von selbst**, wenn jemand einen Griff
  anfasst — es gibt keine WebSockets und keine Rückrufe. Dafür ist der
  Zähler `touches` da: abfragen, vergleichen, fertig.
- Gesetzte Farben stehen **nur im Arbeitsspeicher**. Nach einem Neustart
  ist die Wand wieder im Regenbogen.

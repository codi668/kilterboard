// Treiber fuer den kapazitiven 8-Kanal-Sensor CAP1298.
//
// Wichtig gegenueber der ersten Fassung: das Statusregister 0x03 ist
// gelatcht. Es loescht sich erst, wenn im Main-Control-Register 0x00 das
// INT-Bit zurueckgesetzt wird. Ohne dieses Quittieren bleibt eine
// Beruehrung "ewig gedrueckt" und es kommt nur genau eine Flanke an.
// Deshalb wird hier nach jedem Lesen quittiert.
//
// Kanalbelegung: CS1 = Taster T1, CS2 = Taster T2, CS3..CS8 = Griffe 1..6.
#include "config.h"
#include <Wire.h>

TouchInfo tc;

// Register
#define REG_MAIN_CONTROL  0x00
#define REG_STATUS        0x03
#define REG_DELTA_BASE    0x10
#define REG_SENSITIVITY   0x1F
#define REG_INPUT_ENABLE  0x21
#define REG_CALIB_ACTIVE  0x26
#define REG_INT_ENABLE    0x27
#define REG_REPEAT_RATE   0x28
#define REG_MULTI_TOUCH   0x2A
#define REG_THRESH_BASE   0x30
#define REG_PRODUCT_ID    0xFD

static const uint32_t DEBOUNCE_MS = 150;
// Nach dem Einmessen meldet der Chip eine Weile Beruehrungen, obwohl
// niemand die Griffe anfasst (die Basislinie schwingt sich erst ein).
// So lange werden die Register nur gelesen und quittiert, aber keine
// Flanken ausgewertet -- sonst springt die Wand beim Start von selbst
// in eine andere Betriebsart.
static const uint32_t SETTLE_MS = 1500;
static uint32_t settleUntil = 0;
static uint32_t lastEdge[8] = {0};
static uint32_t lastPollMs  = 0;

static bool regWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(CAP1298_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static uint8_t regRead(uint8_t reg, bool *ok = nullptr) {
  Wire.beginTransmission(CAP1298_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) { if (ok) *ok = false; return 0; }
  if (Wire.requestFrom((uint8_t)CAP1298_ADDR, (uint8_t)1) != 1) {
    if (ok) *ok = false; return 0;
  }
  if (ok) *ok = true;
  return Wire.read();
}

void touchCalibrate() {
  regWrite(REG_CALIB_ACTIVE, 0xFF);   // alle 8 Kanaele neu einmessen
  settleUntil = millis() + SETTLE_MS;
}

void touchSetSensitivity(uint8_t s) {
  tc.sensitivity = s & 0x07;
  // DELTA_SENSE liegt in Bit 6:4, die unteren Bits bleiben auf dem
  // Vorgabewert 0x0F (BASE_SHIFT).
  regWrite(REG_SENSITIVITY, (uint8_t)((tc.sensitivity << 4) | 0x0F));
}

void touchBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  bool ok = false;
  tc.productId = regRead(REG_PRODUCT_ID, &ok);
  tc.present   = ok;
  if (!ok) {
    Serial.println("[touch] CAP1298 antwortet nicht (Verkabelung/Adresse 0x28 pruefen)");
    return;
  }
  Serial.printf("[touch] CAP1298 gefunden, Product-ID 0x%02X\n", tc.productId);

  regWrite(REG_INPUT_ENABLE, 0xFF);   // alle 8 Kanaele aktiv
  regWrite(REG_INT_ENABLE,   0xFF);   // jeder Kanal setzt das INT-Bit
  regWrite(REG_REPEAT_RATE,  0x00);   // keine Wiederholung, nur Flanken
  regWrite(REG_MULTI_TOUCH,  0x00);   // mehrere Griffe gleichzeitig erlauben
  touchSetSensitivity(tc.sensitivity);
  for (int i = 0; i < 8; i++) regWrite(REG_THRESH_BASE + i, 0x40);
  touchCalibrate();
}

// Quittiert das INT-Bit, damit 0x03 beim naechsten Mal frische Werte hat.
static void clearInterrupt() {
  bool ok = false;
  uint8_t main = regRead(REG_MAIN_CONTROL, &ok);
  if (ok) regWrite(REG_MAIN_CONTROL, main & ~0x01);
}

void touchTick() {
  uint32_t now = millis();
  if (now - lastPollMs < 20) return;
  lastPollMs = now;

  // Sensor nicht gefunden (oder Kabel gewackelt): alle 5 s neu versuchen,
  // statt den Touch bis zum Neustart aufzugeben.
  if (!tc.present) {
    static uint32_t lastRetry = 0;
    if (now - lastRetry > 5000) {
      lastRetry = now;
      bool ok = false;
      uint8_t id = regRead(REG_PRODUCT_ID, &ok);
      if (ok) {
        tc.productId = id;
        tc.present = true;
        Serial.println("[touch] CAP1298 wieder da, wird neu eingerichtet");
        regWrite(REG_INPUT_ENABLE, 0xFF);
        regWrite(REG_INT_ENABLE, 0xFF);
        regWrite(REG_REPEAT_RATE, 0x00);
        regWrite(REG_MULTI_TOUCH, 0x00);
        touchSetSensitivity(tc.sensitivity);
        touchCalibrate();
      }
    }
    return;
  }

  bool ok = false;
  tc.status = regRead(REG_STATUS, &ok);
  if (!ok) {
    tc.present = false;
    Serial.println("[touch] CAP1298 antwortet nicht mehr");
    return;
  }
  for (int i = 0; i < 8; i++) tc.delta[i] = (int8_t)regRead(REG_DELTA_BASE + i);
  clearInterrupt();

  // Einschwingphase: Zustand uebernehmen, aber nichts ausloesen
  // (Differenzvergleich, damit der millis-Ueberlauf nichts kaputt macht)
  if ((int32_t)(now - settleUntil) < 0) {
    for (int i = 0; i < 8; i++) {
      tc.down[i] = tc.useDelta ? (tc.delta[i] > tc.threshold) : (tc.status & (1 << i));
      lastEdge[i] = now;
    }
    return;
  }

  for (int i = 0; i < 8; i++) {
    bool raw;
    if (tc.useDelta) {
      // Hysterese: Einschalten bei Schwelle, Ausschalten bei 60 % davon
      int schwelle = tc.down[i] ? (tc.threshold * 3) / 5 : tc.threshold;
      raw = tc.delta[i] > schwelle;
    } else {
      raw = tc.status & (1 << i);
    }

    if (raw && !tc.down[i] && now - lastEdge[i] > DEBOUNCE_MS) {
      tc.down[i]  = true;
      tc.touches[i]++;      // Zaehler fuer Abfragen ueber die Schnittstelle
      lastEdge[i] = now;

      if (i == 0) {                       // T1 -> Regenbogen
        boardSetMode(MODE_RAINBOW);
        Serial.println("[touch] T1: Regenbogen");
      } else if (i == 1) {                // T2 -> Griffe / Spiel
        Mode next = (st.mode == MODE_TOUCH) ? MODE_GAME : MODE_TOUCH;
        boardSetMode(next);
        Serial.printf("[touch] T2: Betriebsart %d\n", (int)next);
      } else {                            // CS3..CS8 -> Griffe 1..6
        uint8_t grip = i - 2;
        Serial.printf("[touch] Griff %d beruehrt (Delta %d)\n", grip + 1, tc.delta[i]);
        boardOnGripTouched(grip);
      }
    } else if (!raw && tc.down[i]) {
      tc.down[i] = false;
    }
  }
}

// LED-Ansteuerung, Betriebsarten und Spiellogik der Kletterwand.
//
// Gerendert wird immer in zwei Schritten: die Betriebsart schreibt
// Farben in cellColor[] (eine Farbe je Griffposition), render() schiebt
// sie auf den LED-Strip. Dadurch kennt die Weboberflaeche jederzeit die
// logische Farbe jeder Position, unabhaengig von der Helligkeit.
#include "config.h"
#include <Adafruit_NeoPixel.h>

State st;

static Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_RGB + NEO_KHZ800);

// Reihenfolge der Farbkanaele. Steht die falsch, zeigt die Wand andere
// Farben als die Weboberflaeche -- umschaltbar unter Info > Farbtest.
static const neoPixelType ledOrders[LED_ORDER_COUNT] = {
  NEO_RGB, NEO_RBG, NEO_GRB, NEO_GBR, NEO_BRG, NEO_BGR
};
const char *ledOrderNames[LED_ORDER_COUNT] = {
  "RGB", "RBG", "GRB", "GBR", "BRG", "BGR"
};

// LED-Indizes, hinter denen kein Griff sitzt -- bleiben immer dunkel.
static const int ausgeschalteteLEDs[] = {2,  5,  8, 11, 17, 20, 26, 32,
                                         35, 39, 42, 45, 46, 47, 48, 49};

static const int mainMatrix[MAIN_ROWS][MAIN_COLS] = {
  { 4,  3,  1,  0, 44, 43},
  { 6,  7,  9, 10, 40, 41},
  {16, 15, 13, 12, 37, 38},
  {18, 19, 31, 30, 33, 34},
  {21, 22, 24, 25, 28, 27}
};
static const int miniMatrix[MINI_ROWS][MINI_COLS] = {{14, 36}, {23, 29}};

// Die 6 Touch-Griffe (CS3..CS8) mit ihren je zwei LEDs.
static const int gripLEDs[NUM_GRIPS][2] = {
  { 4,  3},   // Griff 1 (CS3)
  { 1,  0},   // Griff 2 (CS4)
  {44, 43},   // Griff 3 (CS5)
  { 6,  7},   // Griff 4 (CS6)
  { 9, 10},   // Griff 5 (CS7)
  {40, 41}    // Griff 6 (CS8)
};

static int      cellLed[NUM_CELLS];            // Position -> LED-Index
static uint8_t  gripCells[NUM_GRIPS][2];       // Griff    -> zwei Positionen
static uint32_t cellColor[NUM_CELLS];          // aktuelle logische Farbe

// Farben der Griffe in MODE_TOUCH (wie im Original: aus/blau/gruen/rot)
static const uint32_t touchColors[4] = {0x000000, 0x0000FF, 0x00FF00, 0xFF0000};

static const byte num3[MAIN_ROWS][MAIN_COLS] = {{1,1,1,1,1,1},{0,0,0,0,1,1},{1,1,1,1,1,1},{0,0,0,0,1,1},{1,1,1,1,1,1}};
static const byte num2[MAIN_ROWS][MAIN_COLS] = {{1,1,1,1,1,1},{0,0,0,0,1,1},{1,1,1,1,1,1},{1,1,0,0,0,0},{1,1,1,1,1,1}};
static const byte num1[MAIN_ROWS][MAIN_COLS] = {{0,0,0,0,1,1},{0,0,0,0,1,1},{0,0,0,0,1,1},{0,0,0,0,1,1},{0,0,0,0,1,1}};

static long     rainbowHue   = 0;
static uint32_t lastFrameMs  = 0;
static uint32_t phaseMs      = 0;      // Zeitmarke fuer Spielphasen
static uint32_t flashUntil[NUM_GRIPS] = {0};
static uint32_t flashColor[NUM_GRIPS] = {0};
static const int ROW_OFFSET = 5500;

// ---------------------------------------------------------------- Helfer
static bool istErlaubt(int ledIndex) {
  if (ledIndex < 0 || ledIndex >= NUMPIXELS) return false;
  for (unsigned j = 0; j < sizeof(ausgeschalteteLEDs) / sizeof(int); j++)
    if (ledIndex == ausgeschalteteLEDs[j]) return false;
  return true;
}

int boardCellLed(uint8_t cell) { return cell < NUM_CELLS ? cellLed[cell] : -1; }
uint32_t boardCellColor(uint8_t cell) { return cell < NUM_CELLS ? cellColor[cell] : 0; }
uint8_t boardGripCell(uint8_t grip) { return grip < NUM_GRIPS ? gripCells[grip][0] : 0; }

static void fillAll(uint32_t c) {
  for (int i = 0; i < NUM_CELLS; i++) cellColor[i] = c;
}

static void setGripColor(uint8_t grip, uint32_t c) {
  cellColor[gripCells[grip][0]] = c;
  cellColor[gripCells[grip][1]] = c;
}

// cellColor[] auf den Strip schieben
static void render() {
  pixels.clear();
  for (int i = 0; i < NUM_CELLS; i++) {
    uint32_t c = cellColor[i];
    if (istErlaubt(cellLed[i]) && c) pixels.setPixelColor(cellLed[i], c);
  }
  // kurze Rueckmeldungen (Treffer/Fehlgriff) ueberschreiben alles
  uint32_t now = millis();
  for (int g = 0; g < NUM_GRIPS; g++) {
    if ((int32_t)(flashUntil[g] - now) > 0) {
      for (int k = 0; k < 2; k++) {
        int led = cellLed[gripCells[g][k]];
        if (istErlaubt(led)) pixels.setPixelColor(led, flashColor[g]);
      }
    }
  }
  pixels.show();
}

static void flashGrip(uint8_t grip, uint32_t color, uint16_t ms) {
  flashColor[grip] = color;
  flashUntil[grip] = millis() + ms;
}

// -------------------------------------------------------------- Lebenszyklus
void boardBegin() {
  // Positionstabelle aufbauen: zuerst die Hauptmatrix (zeilenweise),
  // dann die Zusatzmatrix.
  int n = 0;
  for (int r = 0; r < MAIN_ROWS; r++)
    for (int c = 0; c < MAIN_COLS; c++) cellLed[n++] = mainMatrix[r][c];
  for (int r = 0; r < MINI_ROWS; r++)
    for (int c = 0; c < MINI_COLS; c++) cellLed[n++] = miniMatrix[r][c];

  // Griffe auf Positionen abbilden
  for (int g = 0; g < NUM_GRIPS; g++) {
    for (int k = 0; k < 2; k++) {
      gripCells[g][k] = 0;
      for (int i = 0; i < NUM_CELLS; i++)
        if (cellLed[i] == gripLEDs[g][k]) { gripCells[g][k] = i; break; }
    }
  }

  pixels.begin();
  pixels.updateType(ledOrders[st.ledOrder % LED_ORDER_COUNT] + NEO_KHZ800);
  pixels.setBrightness(st.brightness);
  fillAll(0);
  render();
}

void boardShowCountdown() {
  const byte (*zahlen[3])[MAIN_COLS] = {num3, num2, num1};
  for (int z = 0; z < 3; z++) {
    fillAll(0);
    for (int r = 0; r < MAIN_ROWS; r++)
      for (int c = 0; c < MAIN_COLS; c++)
        if (zahlen[z][r][c]) cellColor[r * MAIN_COLS + c] = 0xFFFFFF;
    render();
    delay(700);
  }
  fillAll(0);
  render();
}

void boardSetLedOrder(uint8_t o) {
  if (o >= LED_ORDER_COUNT) return;
  st.ledOrder = o;
  pixels.updateType(ledOrders[o] + NEO_KHZ800);
  render();
}

void boardSetBrightness(uint8_t b) {
  st.brightness = constrain(b, 5, 255);
  pixels.setBrightness(st.brightness);
  render();
}

void boardSetMode(Mode m) {
  st.mode = m;
  fillAll(0);
  for (int g = 0; g < NUM_GRIPS; g++) flashUntil[g] = 0;
  if (m == MODE_TOUCH)
    for (int g = 0; g < NUM_GRIPS; g++) st.gripState[g] = 0;
  if (m == MODE_GAME) { st.gameState = GAME_IDLE; phaseMs = millis(); }
  else                { st.gameState = GAME_IDLE; }
  render();
}

// ------------------------------------------------------------- Bedienung
void boardSetGrip(uint8_t grip, uint8_t colorIdx) {
  if (grip >= NUM_GRIPS) return;
  st.gripState[grip] = colorIdx % 4;
}

void boardCycleGrip(uint8_t grip) {
  if (grip >= NUM_GRIPS) return;
  st.gripState[grip] = (st.gripState[grip] + 1) % 4;
}

// ----------------------------------------------------------------- Spiel
static void gameNextTarget() {
  uint8_t next = st.gameTarget;
  while (next == st.gameTarget) next = random(NUM_GRIPS);
  st.gameTarget = next;
}

void boardStartGame(uint8_t targets) {
  st.mode      = MODE_GAME;
  st.gameTotal = constrain(targets, 3, 50);
  st.gameHits  = 0;
  st.gameMiss  = 0;
  st.gameTimeMs = 0;
  st.gameSplitMs = 0;
  st.gameBestSplit = 0;
  st.gameState = GAME_READY;
  phaseMs = millis();
  for (int g = 0; g < NUM_GRIPS; g++) flashUntil[g] = 0;
}

void boardStopGame() {
  st.gameState = GAME_IDLE;
  phaseMs = millis();
}

// Wird vom Touch-Treiber bei jeder neuen Beruehrung eines Griffs gerufen.
void boardOnGripTouched(uint8_t grip) {
  if (grip >= NUM_GRIPS) return;

  if (st.mode == MODE_TOUCH) {
    boardCycleGrip(grip);
    return;
  }

  if (st.mode == MODE_GAME) {
    if (st.gameState == GAME_IDLE ||
        (st.gameState == GAME_DONE && millis() - phaseMs > 2000)) {
      boardStartGame(st.gameTotal);     // Beruehrung startet den Durchgang
    } else if (st.gameState == GAME_RUNNING) {
      if (grip == st.gameTarget) {
        uint32_t jetzt = millis();
        // Reaktionszeit fuer diesen einen Griff
        st.gameSplitMs = (uint16_t)min<uint32_t>(jetzt - st.gameLastHitMs, 65535);
        if (!st.gameBestSplit || st.gameSplitMs < st.gameBestSplit)
          st.gameBestSplit = st.gameSplitMs;
        st.gameLastHitMs = jetzt;
        st.gameHits++;
        flashGrip(grip, 0x00FF00, 180);
        if (st.gameHits >= st.gameTotal) {
          st.gameTimeMs = jetzt - st.gameStartMs;
          scoreAdd(st.gameTimeMs, st.gameTotal);
          if (!st.gameBestMs || st.gameTimeMs < st.gameBestMs) {
            st.gameBestMs = st.gameTimeMs;
            settingsSave();
          }
          st.gameState = GAME_DONE;
          phaseMs = millis();
        } else {
          gameNextTarget();
        }
      } else {
        st.gameMiss++;
        flashGrip(grip, 0xFF2000, 150);
      }
    }
    return;
  }

  // In allen anderen Betriebsarten gibt der Griff nur eine kurze
  // Rueckmeldung, damit man merkt, dass der Sensor arbeitet.
  flashGrip(grip, 0xFFFFFF, 120);
}

// ------------------------------------------------------------- Animation
static void tickRainbow() {
  for (int r = 0; r < MAIN_ROWS; r++) {
    uint32_t farbe = pixels.gamma32(pixels.ColorHSV(rainbowHue + r * ROW_OFFSET));
    for (int c = 0; c < MAIN_COLS; c++) cellColor[r * MAIN_COLS + c] = farbe;
  }
  for (int r = 0; r < MINI_ROWS; r++) {
    uint32_t farbe = pixels.gamma32(
        pixels.ColorHSV(rainbowHue + (r + 2) * ROW_OFFSET + ROW_OFFSET / 2));
    for (int c = 0; c < MINI_COLS; c++)
      cellColor[MAIN_CELLS + r * MINI_COLS + c] = farbe;
  }
  rainbowHue += 64 + st.speed * 8;
  if (rainbowHue >= 65536) rainbowHue -= 65536;
}

static void tickTouch() {
  fillAll(0);
  for (int g = 0; g < NUM_GRIPS; g++)
    setGripColor(g, touchColors[st.gripState[g] & 3]);
}

static void tickGame() {
  uint32_t now = millis();
  fillAll(0);

  switch (st.gameState) {
    case GAME_IDLE: {
      // Einladung: die sechs Touch-Griffe atmen blau
      float puls = 0.3f + 0.7f * (0.5f + 0.5f * sinf((now % 100000) / 600.0f));
      uint32_t c = pixels.Color(0, (uint8_t)(90 * puls), (uint8_t)(255 * puls));
      for (int g = 0; g < NUM_GRIPS; g++) setGripColor(g, c);
      break;
    }
    case GAME_READY: {
      // 3-2-1 auf der Hauptmatrix, dann losgehen
      const byte (*zahlen[3])[MAIN_COLS] = {num3, num2, num1};
      uint8_t step = (now - phaseMs) / 700;
      if (step > 2) {
        st.gameState  = GAME_RUNNING;
        st.gameStartMs = now;
        st.gameLastHitMs = now;
        st.gameTarget = random(NUM_GRIPS);
        break;
      }
      for (int r = 0; r < MAIN_ROWS; r++)
        for (int c = 0; c < MAIN_COLS; c++)
          if (zahlen[step][r][c]) cellColor[r * MAIN_COLS + c] = 0xFFC000;
      break;
    }
    case GAME_RUNNING: {
      setGripColor(st.gameTarget, 0x00FFFF);
      // Fortschritt in der untersten Reihe
      int leuchten = (int)((float)st.gameHits / st.gameTotal * MAIN_COLS + 0.5f);
      for (int c = 0; c < leuchten && c < MAIN_COLS; c++)
        cellColor[(MAIN_ROWS - 1) * MAIN_COLS + c] = 0x102000;
      if (now - st.gameStartMs > 120000) boardStopGame();   // Notbremse
      break;
    }
    case GAME_DONE: {
      // gruene Welle als Belohnung, danach zurueck zur Einladung
      uint8_t welle = ((now - phaseMs) / 90) % (MAIN_ROWS + 2);
      for (int r = 0; r < MAIN_ROWS; r++) {
        uint8_t hell = (r == welle) ? 255 : (r + 1 == welle ? 60 : 0);
        for (int c = 0; c < MAIN_COLS; c++)
          cellColor[r * MAIN_COLS + c] = pixels.Color(0, hell, hell / 4);
      }
      if (now - phaseMs > 10000) boardStopGame();
      break;
    }
  }
}

// Testbild: Reihe 1 rot, 2 gruen, 3 blau, 4 gelb, 5 weiss. Stimmen die
// Farben an der Wand nicht mit der Beschriftung ueberein, ist die
// Reihenfolge der Farbkanaele eine andere.
static void tickTest() {
  static const uint32_t reihenFarben[MAIN_ROWS] = {
    0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFFFFFF
  };
  for (int r = 0; r < MAIN_ROWS; r++)
    for (int c = 0; c < MAIN_COLS; c++)
      cellColor[r * MAIN_COLS + c] = reihenFarben[r];
  for (int i = MAIN_CELLS; i < NUM_CELLS; i++) cellColor[i] = 0x00FFFF;
}

void boardTick() {
  uint32_t now = millis();
  uint16_t frame = (st.mode == MODE_RAINBOW) ? 20 : 30;
  if (now - lastFrameMs < frame) return;
  lastFrameMs = now;

  switch (st.mode) {
    case MODE_RAINBOW: tickRainbow(); break;
    case MODE_TOUCH:   tickTouch();   break;
    case MODE_GAME:    tickGame();    break;
    case MODE_OFF:     fillAll(0);    break;
    case MODE_TEST:    tickTest();    break;
  }
  render();
}

void boardShowProgress(uint8_t prozent, uint32_t color) {
  int n = (int)((long)prozent * MAIN_CELLS / 100);
  fillAll(0);
  for (int i = 0; i < n && i < MAIN_CELLS; i++) cellColor[i] = color;
  render();
}

// Gemeinsame Typen, Konstanten und Zustand der Kletterwand.
#pragma once
#include <Arduino.h>

#define FW_VERSION "1.0.0"

// ---- Hardware ----------------------------------------------------------
#define LED_PIN      D10
#define NUMPIXELS    50
#define I2C_SDA      6      // D4
#define I2C_SCL      7      // D5
#define CAP1298_ADDR 0x28

// ---- Board-Geometrie ---------------------------------------------------
// Die Wand hat 5x6 Hauptpositionen plus 2x2 Zusatzpositionen. Insgesamt
// sitzen hinter 34 der 50 LEDs Griffe, die restlichen 16 bleiben immer
// dunkel (siehe ausgeschalteteLEDs in board.cpp).
#define MAIN_ROWS 5
#define MAIN_COLS 6
#define MINI_ROWS 2
#define MINI_COLS 2
#define MAIN_CELLS (MAIN_ROWS * MAIN_COLS)          // 30
#define NUM_CELLS  (MAIN_CELLS + MINI_ROWS * MINI_COLS)  // 34
#define NUM_GRIPS  6        // Griffe mit Touch-Sensor (CS3..CS8)

// ---- Betriebsarten -----------------------------------------------------
enum Mode : uint8_t {
  MODE_RAINBOW = 1,   // Regenbogen-Animation
  MODE_TOUCH   = 2,   // Griffe per Beruehrung durchschalten
  MODE_GAME    = 3,   // Reaktionsspiel "Griff-Jagd"
  MODE_OFF     = 4,   // Alles dunkel
  MODE_TEST    = 5,   // Testbild zum Einstellen der Farbreihenfolge
  MODE_API     = 6    // Fernsteuerung: keine Animation, nur was die API setzt
};

// Reihenfolge der Farbkanaele im LED-Streifen. Welche stimmt, haengt vom
// verbauten Streifen ab und laesst sich in der Weboberflaeche umschalten.
#define LED_ORDER_COUNT 6
extern const char *ledOrderNames[LED_ORDER_COUNT];

enum GameState : uint8_t {
  GAME_IDLE = 0, GAME_READY = 1, GAME_RUNNING = 2, GAME_DONE = 3
};

// ---- Zustand (in board.cpp definiert) ----------------------------------
struct State {
  Mode     mode        = MODE_RAINBOW;
  uint8_t  brightness  = 160;
  uint8_t  speed       = 50;        // 0..100, Tempo der Animation
  uint8_t  ledOrder    = 0;         // Index in ledOrderNames (0 = RGB)
  uint8_t  gripState[NUM_GRIPS] = {0};   // Farbindex je Griff in MODE_TOUCH
  // Spiel
  GameState gameState  = GAME_IDLE;
  uint8_t  gameTarget  = 0;         // aktuell leuchtender Griff
  uint8_t  gameHits    = 0;
  uint8_t  gameMiss    = 0;
  uint8_t  gameTotal   = 10;        // Treffer bis zum Ziel
  uint32_t gameStartMs = 0;
  uint32_t gameTimeMs  = 0;         // Ergebnis des letzten Durchgangs
  uint32_t gameBestMs  = 0;         // Bestzeit (im NVS gespeichert)
  uint32_t gameLastHitMs = 0;       // Zeitpunkt des letzten Treffers
  uint16_t gameSplitMs = 0;         // Reaktionszeit beim letzten Griff
  uint16_t gameBestSplit = 0;       // schnellster Griff dieser Runde
  char     playerName[16] = "";     // wer gerade spielt
};
extern State st;

// Bestenliste (in web.cpp gepflegt, im NVS gespeichert)
#define NUM_SCORES 5
struct Score { char name[16]; uint32_t ms; uint8_t grips; };
extern Score scores[NUM_SCORES];
void scoreAdd(uint32_t ms, uint8_t grips);
void scoresLoad();
void scoresClear();

// ---- board.cpp ---------------------------------------------------------
void boardBegin();
void boardTick();                       // ruft Animation/Spiel, nicht blockierend
void boardSetMode(Mode m);
void boardSetBrightness(uint8_t b);
void boardSetLedOrder(uint8_t o);
void boardCycleGrip(uint8_t grip);      // Farbe eines Griffs weiterschalten
void boardSetGrip(uint8_t grip, uint8_t colorIdx);

// Fernsteuerung: Farben direkt setzen (schaltet nach MODE_API)
void boardApiSetCell(uint8_t cell, uint32_t color);
void boardApiSetGrip(uint8_t grip, uint32_t color);
void boardApiSetAll(uint32_t color);
bool boardIsGripCell(uint8_t cell);
uint8_t boardCellOfGrip(uint8_t grip, uint8_t nr);   // nr = 0 oder 1
void boardStartGame(uint8_t targets);
void boardStopGame();
void boardOnGripTouched(uint8_t grip);  // vom Touch-Treiber gerufen
void boardShowCountdown();              // Boot-Animation 3-2-1
int  boardCellLed(uint8_t cell);        // LED-Index einer Position
uint32_t boardCellColor(uint8_t cell);  // aktuelle Farbe (fuer die Webansicht)
uint8_t  boardGripCell(uint8_t grip);   // Position eines Touch-Griffs

// ---- touch.cpp ---------------------------------------------------------
struct TouchInfo {
  bool    present   = false;      // CAP1298 antwortet
  uint8_t productId = 0;
  uint8_t status    = 0;          // Register 0x03
  int8_t  delta[8]  = {0};        // Register 0x10..0x17
  bool     down[8]   = {false};   // entprellter Zustand
  uint16_t touches[8] = {0};      // Zaehler je Kanal, damit ein Programm
                                  // keine Beruehrung verpasst
  uint8_t threshold = 20;         // Delta-Schwelle (nur im Delta-Modus)
  uint8_t sensitivity = 2;        // 0..7 -> DELTA_SENSE
  bool    useDelta  = false;      // false = Status-Register (Standard)
};
extern TouchInfo tc;

void touchBegin();
void touchTick();                 // alle ~20 ms
void touchCalibrate();
void touchSetSensitivity(uint8_t s);

// ---- web.cpp -----------------------------------------------------------
void webBegin();
void webTick();

// ---- main.cpp ----------------------------------------------------------
void settingsSave();              // Helligkeit, Tempo, Bestzeit, Route
String netInfo();                 // JSON-Fragment zum Netzwerkstatus

// Fortschritts-/Statusbalken auf der Hauptmatrix (Boot, OTA)
void boardShowProgress(uint8_t prozent, uint32_t color);

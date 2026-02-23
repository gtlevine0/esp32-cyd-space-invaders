// Dot-matrix style scrolling hello-world for ESP32 CYD.
// Lines appear one per second at the bottom and push older lines upward.
// Older lines fade to darker green, newest line is brightest.
// Relies on User_Setup.h already configured for CYD (pins 12/13/14/15/2/21).

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// ---- Layout ----
static const uint8_t  TEXT_SIZE     = 2;    // 12×16 px per char
static const int16_t  LINE_H        = 16;   // 8 * TEXT_SIZE
static const uint8_t  MAX_LINES     = 15;   // 240 / 16
static const uint32_t LINE_INTERVAL = 1000; // ms between new lines

// ---- Line buffer (circular) ----
uint32_t lineBuf[MAX_LINES];
uint8_t  lineHead = 0;   // index of the oldest visible line
uint8_t  lineFill = 0;   // how many lines are currently on screen
uint32_t nextNum  = 0;   // next hello_world number to print

uint32_t lastAddMs = 0;

// ---- Redraw all lines ----
void redraw() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(TEXT_SIZE);

  for (uint8_t i = 0; i < lineFill; i++) {
    uint8_t  idx = (lineHead + i) % MAX_LINES;
    int16_t  y   = (MAX_LINES - lineFill + i) * LINE_H;

    // Oldest line is dim green, newest is bright green
    uint8_t bright = (lineFill <= 1)
                   ? 200
                   : (uint8_t)map(i, 0, lineFill - 1, 40, 220);

    tft.setTextColor(tft.color565(0, bright, bright / 6), TFT_BLACK);

    char buf[32];
    snprintf(buf, sizeof(buf), "hello_world %lu", lineBuf[idx]);
    tft.setCursor(4, y);
    tft.print(buf);
  }
}

// ---- Add a new line, scrolling old ones up ----
void addLine() {
  if (lineFill < MAX_LINES) {
    lineBuf[(lineHead + lineFill) % MAX_LINES] = nextNum++;
    lineFill++;
  } else {
    // Screen full — overwrite the oldest slot and advance head
    lineBuf[lineHead] = nextNum++;
    lineHead = (lineHead + 1) % MAX_LINES;
  }
  redraw();
}

void setup() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);   // backlight on

  tft.init();
  tft.setRotation(1);       // landscape 320×240
  tft.fillScreen(TFT_BLACK);

  // Print the very first line immediately
  addLine();
  lastAddMs = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastAddMs >= LINE_INTERVAL) {
    lastAddMs += LINE_INTERVAL;   // += avoids drift over time
    addLine();
  }
}

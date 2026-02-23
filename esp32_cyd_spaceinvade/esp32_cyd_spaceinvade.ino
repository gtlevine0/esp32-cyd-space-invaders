// Simple Space Invaders – ESP32 CYD
// Display : TFT_eSPI   (HSPI – MOSI=13 MISO=12 CLK=14 CS=15 DC=2)
// Touch   : XPT2046_Touchscreen on separate VSPI bus (MOSI=32 MISO=39 CLK=25 CS=33 IRQ=36)

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ---- Touch hardware (separate SPI bus from display) ----
#define T_CLK  25
#define T_MISO 39
#define T_MOSI 32
#define T_CS   33
#define T_IRQ  36

SPIClass           touchSPI(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);

TFT_eSPI tft = TFT_eSPI();

// ---- Raw touch → screen mapping ----
// Y: if HUD tap doesn't pause, swap T_RAW_Y_MIN / T_RAW_Y_MAX values.
#define T_RAW_MIN    300
#define T_RAW_MAX   3800
#define T_RAW_Y_MIN  200
#define T_RAW_Y_MAX 3900

// ============================================================
//  Layout
// ============================================================
static const int16_t W        = 320;
static const int16_t H        = 240;
static const int16_t HUD_H    =  12;
static const int16_t BTN_H    =  28;
static const int16_t GAME_TOP = HUD_H;
static const int16_t GAME_BOT = H - BTN_H - 1;
static const int16_t BTN_Y    = H - BTN_H;
static const int16_t BTN_L    = 107;
static const int16_t BTN_R    = 213;

// ============================================================
//  Game constants
// ============================================================
static const uint8_t  INV_COLS  =  7;
static const uint8_t  INV_ROWS  =  4;
static const int16_t  INV_XGAP  = 36;
static const int16_t  INV_YGAP  = 22;
static const int16_t  INV_X0    = 34;   // re-centred for 7 cols: (320-6*36)/2 ≈ 52, nudged
static const int16_t  INV_Y0    = 36;   // moved down from 28 — clears UFO lane
static const int16_t  INV_STEP  =  8;
static const uint8_t  INV_RATE  = 34;   // was 28 — slightly slower march
static const int16_t  INV_CHW   =  7;
static const int16_t  INV_CHH   =  5;
static const int16_t  INV_EHW   = 12;
static const int16_t  INV_EHH   =  9;

static const int16_t  PLR_Y     = 196;
static const int16_t  PLR_HW    =  12;
static const int16_t  PLR_H     =  10;
static const int16_t  PLR_NOS   =   7;
static const int16_t  PLR_SPD   =   4;
static const int16_t  PLR_EY    = PLR_Y - PLR_NOS;
static const int16_t  PLR_EH    = PLR_NOS + PLR_H;

static const int8_t   PBLT_SPD  =  -8;
static const int8_t   EBLT_SPD  =   4;
static const uint8_t  SHOOT_CD  =  25;
static const uint8_t  ENEMY_CD  =  55;
static const uint32_t FRAME_MS  =  33;

static const uint8_t  PTS[INV_ROWS]     = { 30, 20, 20, 10 };
static const uint16_t INV_COL[INV_ROWS] = { TFT_RED, TFT_MAGENTA, TFT_YELLOW, TFT_CYAN };

// ---- Shields ----
static const uint8_t  NUM_SHIELDS = 4;
static const uint8_t  SH_COLS     = 5;
static const uint8_t  SH_ROWS     = 4;
static const uint8_t  SH_BLOCK    = 4;
static const uint8_t  SH_MAX_HP   = 3;
static const int16_t  SH_Y        = 165;
static const int16_t  SH_CX[]     = {58, 126, 194, 262};

// ---- UFO ----
static const int16_t  UFO_Y       = 20;    // Y centre — flies in lane between HUD and aliens
static const int16_t  UFO_SPD     =  2;    // px per frame
static const int16_t  UFO_CHW     =  9;    // collision half-width
static const uint16_t UFO_PTS[]   = {50, 100, 150, 300};
static const uint16_t UFO_SPAWN_0 = 250;   // frames after first drop before first UFO
static const uint16_t UFO_SPAWN_R = 350;   // frames between subsequent UFOs (+random 200)

// ============================================================
//  State
// ============================================================
enum State : uint8_t { PLAYING, WIN, LOSE };

struct Invader { int16_t x, y; bool alive; };
struct Bullet  { int16_t x, y; bool active; };
struct UFO     { int16_t x; int8_t dir; bool active; };

Invader inv[INV_ROWS][INV_COLS];
uint8_t shieldHP[NUM_SHIELDS][SH_ROWS][SH_COLS];
UFO     ufo;
int16_t  plrX;
uint8_t  plrLives, shootCD, plrInvTimer;
Bullet   pblt, eblt;
int8_t   swarmDir;
uint8_t  swarmTimer, enemyTimer;
uint32_t score, hi;
uint16_t shotsFired, shotsHit;
State    state;
bool     paused;
bool     swarmDropped;  // true once aliens have dropped at least one row
uint16_t ufoTimer;      // countdown frames until next UFO spawn (0xFFFF = locked)
uint8_t  ufoHitTimer;   // frames to display score popup (0 = none)
int16_t  ufoHitX;       // X centre of score popup
uint16_t ufoHitPts;     // points to display

bool     tLeft, tFire, tRight, prevTouch;
uint32_t lastFrame;

// Dirty-render tracking
int16_t  drPlrX;    bool drPlrVis;
bool     drPblt;    int16_t drPbltX, drPbltY;
bool     drEblt;    int16_t drEbltX, drEbltY;
bool     invDirty;
uint32_t drScore;   uint8_t drLives;
bool     drTL, drTF, drTR;
bool     drPaused;
bool     drUfoVis;  int16_t drUfoX;

// ============================================================
//  Touch helpers
// ============================================================
int16_t rawToScreenX(int16_t raw) {
  return (int16_t)constrain(map(raw, T_RAW_MIN, T_RAW_MAX, 0, W), 0, W - 1);
}
int16_t rawToScreenY(int16_t raw) {
  return (int16_t)constrain(map(raw, T_RAW_Y_MIN, T_RAW_Y_MAX, 0, H), 0, H - 1);
}

// ============================================================
//  Alien drawing
// ============================================================
inline void ir(int16_t cx, int16_t cy, int16_t dx, int16_t dy,
               int16_t w, int16_t h, uint16_t col) {
  tft.fillRect(cx + dx, cy + dy, w, h, col);
}
inline void ip(int16_t cx, int16_t cy, int16_t dx, int16_t dy, int16_t w, int16_t h) {
  tft.fillRect(cx + dx, cy + dy, w, h, TFT_BLACK);
}
void drawBug(int16_t cx, int16_t cy, uint16_t col) {
  ir(cx,cy, -4,-7,  2, 3, col);  ir(cx,cy,  2,-7,  2, 3, col);
  ir(cx,cy, -5,-5, 10, 3, col);
  ir(cx,cy, -6,-3, 12, 5, col);
  ir(cx,cy, -9,-2,  4, 3, col);  ir(cx,cy,  5,-2,  4, 3, col);
  ir(cx,cy, -6, 4,  3, 3, col);  ir(cx,cy, -1, 4,  2, 3, col);
  ir(cx,cy,  3, 4,  3, 3, col);
  ip(cx,cy, -3,-2,  2, 2);       ip(cx,cy,  1,-2,  2, 2);
}
void drawCrab(int16_t cx, int16_t cy, uint16_t col) {
  ir(cx,cy, -5,-8,  2, 4, col);  ir(cx,cy,  3,-8,  2, 4, col);
  ir(cx,cy, -4,-5,  8, 3, col);
  ir(cx,cy, -7,-3, 14, 7, col);
  ir(cx,cy,-11,-2,  5, 4, col);  ir(cx,cy,  6,-2,  5, 4, col);
  ir(cx,cy, -5, 4,  3, 3, col);  ir(cx,cy,  2, 4,  3, 3, col);
  ip(cx,cy, -3,-2,  2, 2);       ip(cx,cy,  1,-2,  2, 2);
}
void drawSquid(int16_t cx, int16_t cy, uint16_t col) {
  ir(cx,cy, -3,-8,  6, 3, col);
  ir(cx,cy, -5,-6, 10, 4, col);
  ir(cx,cy, -6,-3, 12, 5, col);
  ir(cx,cy, -9,-5,  4, 5, col);  ir(cx,cy,  5,-5,  4, 5, col);
  ir(cx,cy, -5, 2,  2, 5, col);  ir(cx,cy, -1, 2,  2, 5, col);
  ir(cx,cy,  3, 2,  2, 5, col);
  ip(cx,cy, -3,-4,  2, 2);       ip(cx,cy,  1,-4,  2, 2);
}
void drawInvaderAt(int16_t cx, int16_t cy, uint8_t row) {
  if      (row < 2)  drawBug  (cx, cy, INV_COL[row]);
  else if (row == 2) drawCrab (cx, cy, INV_COL[row]);
  else               drawSquid(cx, cy, INV_COL[row]);
}

// ============================================================
//  UFO drawing
// ============================================================
// Shape (relative to centre cx, cy = UFO_Y):
//   dome:  cx-5..cx+4, cy-4..cy-2   (10×3)
//   body:  cx-9..cx+8, cy-1..cy+2   (18×4)
//   windows punched out at body interior
void drawUFO(int16_t cx) {
  int16_t cy = UFO_Y;
  tft.fillRect(cx - 5, cy - 4, 10, 3, TFT_RED);   // dome
  tft.fillRect(cx - 9, cy - 1, 18, 4, TFT_RED);   // body
  tft.fillRect(cx - 6, cy,      3, 2, TFT_BLACK);  // window 1
  tft.fillRect(cx - 1, cy,      3, 2, TFT_BLACK);  // window 2
  tft.fillRect(cx + 4, cy,      3, 2, TFT_BLACK);  // window 3
}
void eraseUFO(int16_t cx) {
  tft.fillRect(cx - 10, UFO_Y - 5, 21, 8, TFT_BLACK);
}

// ============================================================
//  Shield drawing & collision
// ============================================================
void drawShieldBlock(uint8_t s, uint8_t r, uint8_t c) {
  int16_t x = SH_CX[s] - (SH_COLS * SH_BLOCK / 2) + c * SH_BLOCK;
  int16_t y = SH_Y + r * SH_BLOCK;
  uint8_t hp = shieldHP[s][r][c];
  uint16_t col;
  if      (hp == 0)         col = TFT_BLACK;
  else if (hp >= SH_MAX_HP) col = TFT_GREEN;
  else if (hp == 2)         col = tft.color565(0, 150, 0);
  else                      col = tft.color565(0, 70,  0);
  tft.fillRect(x, y, SH_BLOCK, SH_BLOCK, col);
}
void drawAllShields() {
  for (uint8_t s = 0; s < NUM_SHIELDS; s++)
    for (uint8_t r = 0; r < SH_ROWS; r++)
      for (uint8_t c = 0; c < SH_COLS; c++)
        drawShieldBlock(s, r, c);
}
void initShields() {
  for (uint8_t s = 0; s < NUM_SHIELDS; s++)
    for (uint8_t r = 0; r < SH_ROWS; r++)
      for (uint8_t c = 0; c < SH_COLS; c++) {
        bool notch = (r == SH_ROWS - 1 && c >= 1 && c <= 3);
        shieldHP[s][r][c] = notch ? 0 : SH_MAX_HP;
      }
}
bool checkBulletShield(int16_t bx, int16_t by, int16_t bh) {
  for (uint8_t s = 0; s < NUM_SHIELDS; s++) {
    int16_t sx = SH_CX[s] - (SH_COLS * SH_BLOCK / 2);
    if (bx < sx || bx >= sx + SH_COLS * SH_BLOCK) continue;
    uint8_t c = (uint8_t)((bx - sx) / SH_BLOCK);
    for (uint8_t r = 0; r < SH_ROWS; r++) {
      int16_t sy = SH_Y + r * SH_BLOCK;
      if (by < sy + SH_BLOCK && by + bh > sy && shieldHP[s][r][c] > 0) {
        // Top and bottom rows erode faster — 2 HP per hit, middle rows 1 HP
        uint8_t dmg = (r == 0 || r == SH_ROWS - 1) ? 2 : 1;
        shieldHP[s][r][c] = (shieldHP[s][r][c] > dmg) ? shieldHP[s][r][c] - dmg : 0;
        drawShieldBlock(s, r, c);
        return true;
      }
    }
  }
  return false;
}
void crushShieldUnder(int16_t ix, int16_t iy) {
  int16_t it = iy - INV_EHH, ib = iy + INV_EHH;
  if (ib < SH_Y || it > SH_Y + SH_ROWS * SH_BLOCK) return;
  for (uint8_t s = 0; s < NUM_SHIELDS; s++) {
    int16_t sx = SH_CX[s] - (SH_COLS * SH_BLOCK / 2);
    int16_t se = sx + SH_COLS * SH_BLOCK;
    if (ix + INV_EHW < sx || ix - INV_EHW >= se) continue;
    for (uint8_t r = 0; r < SH_ROWS; r++) {
      int16_t sy = SH_Y + r * SH_BLOCK;
      if (it > sy + SH_BLOCK || ib < sy) continue;
      for (uint8_t c = 0; c < SH_COLS; c++) {
        if (shieldHP[s][r][c] == 0) continue;
        int16_t sxc = sx + c * SH_BLOCK;
        if (ix - INV_EHW < sxc + SH_BLOCK && ix + INV_EHW >= sxc) {
          shieldHP[s][r][c] = 0;
          drawShieldBlock(s, r, c);
        }
      }
    }
  }
}

// ============================================================
//  Draw helpers
// ============================================================
inline void eraseInvader(uint8_t r, uint8_t c) {
  tft.fillRect(inv[r][c].x - INV_EHW, inv[r][c].y - INV_EHH,
               INV_EHW * 2, INV_EHH * 2, TFT_BLACK);
}
inline void erasePlayer(int16_t x) {
  tft.fillRect(x - PLR_HW, PLR_EY, PLR_HW * 2, PLR_EH, TFT_BLACK);
}
void drawPlayerAt(int16_t x) {
  tft.fillRect(x - PLR_HW, PLR_Y, PLR_HW * 2, PLR_H, TFT_GREEN);
  tft.fillTriangle(x, PLR_Y - PLR_NOS, x - 6, PLR_Y, x + 6, PLR_Y, TFT_GREEN);
}
void drawAllInvaders() {
  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive) drawInvaderAt(inv[r][c].x, inv[r][c].y, r);
}
void drawHUD() {
  tft.fillRect(0, 0, W, HUD_H - 1, TFT_BLACK);
  tft.setTextSize(1);
  char buf[36];
  if (paused) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "SC:%05lu  HI:%05lu    || PAUSED ||", score, hi);
    tft.setCursor(2, 2);  tft.print(buf);
  } else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "SC:%05lu  HI:%05lu", score, hi);
    tft.setCursor(2, 2);  tft.print(buf);
    // Mini ship icons for remaining lives (up to 5), right-aligned
    for (uint8_t i = 0; i < plrLives && i < 5; i++) {
      int16_t cx = (W - 6) - (int16_t)(plrLives - 1 - i) * 8;
      tft.fillTriangle(cx, 2, cx - 2, 5, cx + 2, 5, TFT_GREEN);  // nose
      tft.fillRect(cx - 2, 5, 5, 3, TFT_GREEN);                   // body
    }
  }
  tft.drawFastHLine(0, HUD_H - 1, W, paused ? TFT_CYAN : TFT_GREEN);
  drScore = score;  drLives = plrLives;  drPaused = paused;
}
void drawButtons() {
  tft.fillRect(0, BTN_Y, W, BTN_H, TFT_BLACK);
  tft.drawFastHLine(0, BTN_Y, W, TFT_GREEN);
  tft.drawFastVLine(BTN_L, BTN_Y, BTN_H, TFT_DARKGREY);
  tft.drawFastVLine(BTN_R, BTN_Y, BTN_H, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(tLeft  ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(12,  BTN_Y + 10); tft.print("< LEFT");
  tft.setTextColor(tFire  ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(136, BTN_Y + 10); tft.print("FIRE");
  tft.setTextColor(tRight ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(228, BTN_Y + 10); tft.print("RIGHT >");
  drTL = tLeft;  drTF = tFire;  drTR = tRight;
}
void drawPauseOverlay() {
  tft.fillRect(112, 106, 96, 28, TFT_BLACK);
  tft.drawRect(112, 106, 96, 28, TFT_CYAN);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(124, 113);
  tft.print("PAUSED");
}
void drawOverlay(const char* title, uint16_t col) {
  // Box: x=35 y=65 w=250 h=112
  tft.fillRect(35, 65, 250, 112, TFT_BLACK);
  tft.drawRect(35, 65, 250, 112, col);

  // Title
  tft.setTextSize(3);  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(35 + (250 - (int16_t)strlen(title) * 18) / 2, 74);
  tft.print(title);

  // Accuracy block (Galaga style)
  tft.setTextSize(1);  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[28];
  snprintf(buf, sizeof(buf), "SHOTS FIRED     %4u", shotsFired);
  tft.setCursor(55, 106);  tft.print(buf);
  snprintf(buf, sizeof(buf), "NUMBER OF HITS  %4u", shotsHit);
  tft.setCursor(55, 117);  tft.print(buf);
  uint16_t pct10 = shotsFired
                   ? (uint16_t)((uint32_t)shotsHit * 1000 / shotsFired)
                   : 0;
  snprintf(buf, sizeof(buf), "HIT-MISS RATIO  %u.%u%%", pct10 / 10, pct10 % 10);
  tft.setCursor(55, 128);  tft.print(buf);

  // Separator + prompt
  tft.drawFastHLine(45, 141, 230, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(83, 149);
  tft.print("Tap screen to play again");
}

// ============================================================
//  Init
// ============================================================
int countAlive() {
  int n = 0;
  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive) n++;
  return n;
}
void initGame() {
  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      inv[r][c] = { (int16_t)(INV_X0 + c * INV_XGAP),
                    (int16_t)(INV_Y0 + r * INV_YGAP), true };
  plrX = W / 2;  plrLives = 5;  shootCD = 0;  plrInvTimer = 0;
  pblt  = {0,0,false};  eblt = {0,0,false};
  ufo   = {0, 1, false};
  swarmDir = 1;  swarmTimer = 0;  enemyTimer = ENEMY_CD;
  score = 0;  state = PLAYING;  paused = false;
  shotsFired = 0;  shotsHit = 0;
  swarmDropped = false;  ufoTimer = 0xFFFF;
  ufoHitTimer = 0;

  tft.fillScreen(TFT_BLACK);
  drawHUD();  drawButtons();
  initShields();  drawAllShields();
  invDirty  = true;
  drPlrVis  = false;  drPlrX  = -999;
  drPblt    = false;  drEblt  = false;
  drUfoVis  = false;  drUfoX  = -999;
  drScore   = 0xFFFFFFFF;  drLives = 0xFF;  drPaused = false;
  drTL = drTF = drTR = false;
}

// ============================================================
//  Dirty render
// ============================================================
void render() {
  // Player
  bool plrVis = !(plrInvTimer > 0 && (plrInvTimer / 5) % 2 == 0);
  if (drPlrVis && (drPlrX != plrX || !plrVis)) erasePlayer(drPlrX);
  if (plrVis  && (!drPlrVis || drPlrX != plrX)) drawPlayerAt(plrX);
  drPlrX = plrX;  drPlrVis = plrVis;

  // Player bullet
  if (drPblt) tft.fillRect(drPbltX - 1, drPbltY, 2, 7, TFT_BLACK);
  if (pblt.active) tft.fillRect(pblt.x - 1, pblt.y, 2, 7, TFT_YELLOW);
  drPblt = pblt.active;  drPbltX = pblt.x;  drPbltY = pblt.y;

  // Enemy bullet
  if (drEblt) tft.fillRect(drEbltX - 1, drEbltY, 2, 6, TFT_BLACK);
  if (eblt.active) tft.fillRect(eblt.x - 1, eblt.y, 2, 6, TFT_RED);
  drEblt = eblt.active;  drEbltX = eblt.x;  drEbltY = eblt.y;

  // UFO
  if (drUfoVis) eraseUFO(drUfoX);
  if (ufo.active) drawUFO(ufo.x);
  drUfoVis = ufo.active;  drUfoX = ufo.x;

  // Score popup (shown for ufoHitTimer frames after UFO kill)
  if (ufoHitTimer > 0) {
    ufoHitTimer--;
    if (ufoHitTimer == 0)
      tft.fillRect(ufoHitX - 13, UFO_Y - 5, 27, 8, TFT_BLACK);
  }

  if (invDirty) { drawAllInvaders(); invDirty = false; }
  if (score != drScore || plrLives != drLives || paused != drPaused) drawHUD();
  if (tLeft != drTL || tFire != drTF || tRight != drTR) drawButtons();
}

// ============================================================
//  Update
// ============================================================
void handleTouch() {
  bool touched = ts.touched();
  int16_t sx = 0, sy_scr = H / 2;

  if (touched) {
    TS_Point p = ts.getPoint();
    sx     = rawToScreenX(p.x);
    sy_scr = rawToScreenY(p.y);
  }

  if (state != PLAYING) {
    if (touched && !prevTouch) initGame();
    prevTouch = touched;
    tLeft = tFire = tRight = false;
    return;
  }

  // Pause toggle: rising-edge tap anywhere in top third of screen
  if (touched && !prevTouch && sy_scr < H / 3) {
    paused = !paused;
    if (paused) {
      drawHUD();
      drawPauseOverlay();
    } else {
      tft.fillScreen(TFT_BLACK);
      drawHUD();  drawButtons();
      drawAllShields();
      if (ufo.active) drawUFO(ufo.x);
      invDirty  = true;
      drPlrVis  = false;  drPlrX = -999;
      drPblt    = false;  drEblt = false;
      drUfoVis  = false;
      drScore   = 0xFFFFFFFF;  drLives = 0xFF;
      drTL = drTF = drTR = false;
    }
    prevTouch = touched;
    tLeft = tFire = tRight = false;
    return;
  }

  if (paused) {
    prevTouch = touched;
    tLeft = tFire = tRight = false;
    return;
  }

  tLeft  = touched && sx < BTN_L;
  tFire  = touched && sx >= BTN_L && sx <= BTN_R;
  tRight = touched && sx > BTN_R;
  prevTouch = touched;
}

void updatePlayer() {
  if (tLeft  && plrX - PLR_HW > 0)  plrX -= PLR_SPD;
  if (tRight && plrX + PLR_HW < W)  plrX += PLR_SPD;
  if (shootCD > 0) shootCD--;
  if (tFire && shootCD == 0 && !pblt.active) {
    pblt = { plrX, (int16_t)(PLR_Y - PLR_NOS - 1), true };
    shootCD = SHOOT_CD;
    shotsFired++;
  }
  if (plrInvTimer > 0) plrInvTimer--;
  if (pblt.active) {
    pblt.y += PBLT_SPD;
    if (pblt.y < GAME_TOP) pblt.active = false;
  }
}

void updateSwarm() {
  int alive = countAlive();
  if (alive == 0) { state = WIN; return; }

  uint8_t killed   = (uint8_t)(INV_ROWS * INV_COLS) - (uint8_t)alive;
  uint8_t interval = (uint8_t)max(4, (int)INV_RATE - (int)killed * 4 / 3);
  if (++swarmTimer < interval) return;
  swarmTimer = 0;

  int16_t minX = 30000, maxX = -30000;
  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive) {
        minX = min(minX, inv[r][c].x);
        maxX = max(maxX, inv[r][c].x);
      }

  bool bounce = (swarmDir ==  1 && maxX + INV_EHW + INV_STEP >= W)
             || (swarmDir == -1 && minX - INV_EHW - INV_STEP <= 0);

  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive) eraseInvader(r, c);

  if (bounce) {
    swarmDir = -swarmDir;
    for (uint8_t r = 0; r < INV_ROWS; r++)
      for (uint8_t c = 0; c < INV_COLS; c++)
        if (inv[r][c].alive) inv[r][c].y += INV_STEP;

    // Unlock UFO on the very first drop
    if (!swarmDropped) {
      swarmDropped = true;
      ufoTimer = UFO_SPAWN_0 + random(100);
    }
  } else {
    for (uint8_t r = 0; r < INV_ROWS; r++)
      for (uint8_t c = 0; c < INV_COLS; c++)
        if (inv[r][c].alive) inv[r][c].x += swarmDir * INV_STEP;
  }

  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive) crushShieldUnder(inv[r][c].x, inv[r][c].y);

  invDirty = true;

  for (uint8_t r = 0; r < INV_ROWS; r++)
    for (uint8_t c = 0; c < INV_COLS; c++)
      if (inv[r][c].alive && inv[r][c].y + INV_EHH >= PLR_Y - 4)
        { state = LOSE; return; }
}

void updateUFO() {
  if (!swarmDropped) return;

  if (ufo.active) {
    ufo.x += ufo.dir * UFO_SPD;
    // Exits screen — schedule next appearance
    if (ufo.x < -20 || ufo.x > W + 20) {
      ufo.active = false;
      ufoTimer   = UFO_SPAWN_R + random(200);
    }
  } else {
    if (ufoTimer > 0) {
      ufoTimer--;
    } else {
      // Spawn from a random edge
      ufo.dir = (random(2) == 0) ? 1 : -1;
      ufo.x   = (ufo.dir == 1) ? -15 : W + 15;
      ufo.active = true;
    }
  }
}

void updateEnemyShoot() {
  if (eblt.active) {
    eblt.y += EBLT_SPD;
    if (eblt.y > GAME_BOT) eblt.active = false;
  }
  if (!eblt.active && --enemyTimer == 0) {
    enemyTimer = ENEMY_CD / 2 + random(ENEMY_CD / 2 + 1);
    int alive = countAlive();
    if (alive > 0) {
      int pick = random(alive), n = 0;
      for (uint8_t r = 0; r < INV_ROWS; r++)
        for (uint8_t c = 0; c < INV_COLS; c++)
          if (inv[r][c].alive && n++ == pick)
            eblt = { inv[r][c].x, (int16_t)(inv[r][c].y + INV_EHH), true };
    }
  }
}

void updateCollisions() {
  if (pblt.active) {
    if (checkBulletShield(pblt.x, pblt.y, 7)) {
      pblt.active = false;
    } else if (ufo.active &&
               abs(pblt.x - ufo.x) <= UFO_CHW &&
               pblt.y + 7 >= UFO_Y - 4 && pblt.y <= UFO_Y + 2) {
      // UFO hit — pick random bonus, show popup
      int16_t  hitX = ufo.x;
      uint16_t pts  = UFO_PTS[random(4)];
      eraseUFO(hitX);
      ufo.active  = false;
      pblt.active = false;
      ufoTimer    = UFO_SPAWN_R + random(200);
      score += pts;
      if (score > hi) hi = score;
      shotsHit++;
      // Draw score popup; render() will erase it when ufoHitTimer reaches 0
      char buf[6];  snprintf(buf, sizeof(buf), "+%d", pts);
      tft.setTextSize(1);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setCursor(hitX - 12, UFO_Y - 4);
      tft.print(buf);
      ufoHitX     = hitX;
      ufoHitPts   = pts;
      ufoHitTimer = 45;   // ~1.5 s at 33 ms/frame
    } else {
      for (uint8_t r = 0; r < INV_ROWS && pblt.active; r++)
        for (uint8_t c = 0; c < INV_COLS && pblt.active; c++) {
          if (!inv[r][c].alive) continue;
          if (abs(pblt.x - inv[r][c].x) <= INV_CHW &&
              abs(pblt.y - inv[r][c].y) <= INV_CHH + 4) {
            eraseInvader(r, c);
            inv[r][c].alive = false;
            pblt.active = false;
            score += PTS[r];
            if (score > hi) hi = score;
            shotsHit++;
          }
        }
    }
  }
  if (eblt.active) {
    if (checkBulletShield(eblt.x, eblt.y, 6)) {
      eblt.active = false;
    } else if (plrInvTimer == 0) {
      if (abs(eblt.x - plrX) < PLR_HW &&
          eblt.y >= PLR_EY && eblt.y <= PLR_Y + PLR_H) {
        eblt.active = false;
        if (--plrLives == 0) { state = LOSE; return; }
        plrInvTimer = 90;
      }
    }
  }
}

// ============================================================
//  Arduino entry points
// ============================================================
void setup() {
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tft.init();
  tft.setRotation(1);

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  randomSeed(analogRead(34));

  hi = 0;  prevTouch = false;
  initGame();
  lastFrame = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  // Capture state before any updates so we can detect the transition frame
  bool wasPlaying = (state == PLAYING);

  handleTouch();

  if (state == PLAYING) {
    if (!paused) {
      updatePlayer();
      updateSwarm();
      updateUFO();
      updateEnemyShoot();
      updateCollisions();
      render();
    }
  }

  // Draw end-game overlay on the exact frame state first leaves PLAYING.
  // Using wasPlaying avoids the prevState timing bug where prevState was
  // already updated to LOSE before the else-branch ever had a chance to run.
  if (state != PLAYING && wasPlaying) {
    tft.fillScreen(TFT_BLACK);
    drawHUD();
    drawOverlay(state == WIN ? "YOUR HITS" : "GAME OVR",
                state == WIN ? TFT_GREEN : TFT_RED);
  }
}

// ============================================================
//  Space Invaders – ESP32 CYD (2.8" ILI9341 320×240 + XPT2046)
//
//  Pin config is injected inline so no User_Setup.h modification
//  is needed.  Install TFT_eSPI via the Arduino Library Manager,
//  then flash this single file.
// ============================================================

// --- CYD pin definitions (must appear before #include <TFT_eSPI.h>) ---
#define USER_SETUP_LOADED
#define ILI9341_DRIVER

#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // tied to EN line on CYD
#define TFT_BL    21
#define TFT_BACKLIGHT_ON HIGH

#define TOUCH_CS  33

#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000

#include <TFT_eSPI.h>

// ============================================================
//  Constants
// ============================================================

static const int16_t SCREEN_W = 320;
static const int16_t SCREEN_H = 240;

// Touch zone: bottom strip (y >= TOUCH_ZONE_Y)
static const int16_t TOUCH_ZONE_Y  = 168;
static const int16_t TOUCH_LEFT_X  = 107;
static const int16_t TOUCH_RIGHT_X = 213;

// Player
static const int16_t PLAYER_SPEED = 3;
static const int16_t PLAYER_Y     = 144;  // top of ship in game coords
static const int16_t PLAYER_HW    = 10;   // half-width for collision
static const int16_t PLAYER_HT    = 22;   // height for collision

// Bullets
static const int8_t  BULLET_SPEED       =  6;
static const int8_t  ENEMY_BULLET_SPEED =  3;
static const uint8_t MAX_PLAYER_BULLETS =  3;
static const uint8_t MAX_ENEMY_BULLETS  =  8;
static const uint8_t SHOOT_COOLDOWN     = 30;

// Invader grid
static const uint8_t  INVADER_COLS    =  8;
static const uint8_t  INVADER_ROWS    =  4;
static const int16_t  INVADER_H_GAP   = 36;
static const int16_t  INVADER_V_GAP   = 26;
static const int16_t  INVADER_START_X = 22;
static const int16_t  INVADER_START_Y = 20;
static const int16_t  INVADER_STEP    =  6;   // px per swarm step
static const uint8_t  INVADER_HW      =  9;   // half-width for collision
static const uint8_t  INVADER_HH      =  8;   // half-height for collision
static const uint8_t  ENEMY_SHOOT_RATE = 60;

// Shields  (4 × 5 cols × 4 rows, 4 px blocks)
static const uint8_t  NUM_SHIELDS  = 4;
static const uint8_t  SHIELD_COLS  = 5;
static const uint8_t  SHIELD_ROWS  = 4;
static const uint8_t  SHIELD_BLOCK = 4;
static const int16_t  SHIELD_Y     = 120;
// Shield centres: 4 shields × 20 px each in 320 px → 48 px gaps
static const int16_t  SHIELD_CX[4] = { 58, 126, 194, 262 };

// Point values per invader row (row 0 = top)
static const uint8_t  INV_PTS[4] = { 30, 20, 20, 10 };

// Frame timing
static const uint32_t FRAME_MS = 33;   // ~30 fps

// UFO Y (fixed near top of game area, above HUD separator)
static const int16_t  UFO_Y = 16;

// ============================================================
//  Data structures
// ============================================================

struct Bullet {
  int16_t  x, y;
  int8_t   speed;
  uint16_t color;
  bool     active;
};

struct Invader {
  int16_t x, y;
  uint8_t row;
  bool    alive;
};

struct UFO {
  int16_t  x;
  bool     active;
  int8_t   dir;
  uint16_t points;
  uint32_t spawnTimer;
};

enum GameState : uint8_t { STATE_PLAYING, STATE_WIN, STATE_LOSE };

// ============================================================
//  Globals
// ============================================================

TFT_eSPI tft = TFT_eSPI();

// Game objects
Bullet   playerBullets[MAX_PLAYER_BULLETS];
Bullet   enemyBullets[MAX_ENEMY_BULLETS];
Invader  invaders[INVADER_COLS * INVADER_ROWS];
UFO      ufo;
uint8_t  shieldHP[NUM_SHIELDS][SHIELD_COLS][SHIELD_ROWS];  // [shield][col][row]

// Player state
int16_t  playerX;
uint8_t  shootCooldown;
uint8_t  playerLives;
uint8_t  playerInvincible;   // countdown frames
uint8_t  playerShieldHP;     // emergency last-life shield charges
bool     shieldGranted;

// Swarm state
int8_t   swarmDir;
uint8_t  swarmTimer;
uint8_t  swarmInterval;
uint8_t  enemyShootTimer;
uint8_t  animFrame;

// Score
uint32_t  score;
uint32_t  highScore;

// Game state
GameState gameState;
uint32_t  lastFrameMs;

// Touch
bool touchLeft, touchRight, touchFire;
bool prevTouched;

// ============================================================
//  Forward declarations
// ============================================================

void     initGame();
void     handleTouch();
void     updatePlayer();
void     updateInvaders();
void     updateBullets();
void     updateUFO();
void     drawPlayer();
void     drawInvader(int16_t x, int16_t y, uint8_t row, uint8_t anim);
void     drawUFO();
void     drawShields();
void     drawHUD();
void     drawTouchButtons();
void     drawOverlay(const char* title, uint16_t color, const char* sub);
int      countAlive();

// ============================================================
//  Drawing helpers
// ============================================================

void drawPlayer() {
  if (playerInvincible > 0 && (playerInvincible / 6) % 2 == 0) return;  // blink

  int16_t cx = playerX;
  int16_t cy = PLAYER_Y;

  // Emergency shield ring
  if (playerShieldHP > 0) {
    uint16_t sc = tft.color565(0, 200, 220);
    tft.drawEllipse(cx, cy + 11, 16, 13, sc);
  }

  // Nose triangle
  tft.fillTriangle(cx, cy, cx - 7, cy + 10, cx + 7, cy + 10, TFT_GREEN);
  // Body
  tft.fillRect(cx - 10, cy + 10, 20, 8, TFT_GREEN);
  // Left wing
  tft.fillTriangle(cx - 10, cy + 10, cx - 18, cy + 18, cx - 10, cy + 18, TFT_GREEN);
  // Right wing
  tft.fillTriangle(cx + 10, cy + 10, cx + 18, cy + 18, cx + 10, cy + 18, TFT_GREEN);
  // Engine nub
  tft.fillRect(cx - 4, cy + 18, 8, 4, TFT_GREEN);

  // Player bullets
  for (uint8_t i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (playerBullets[i].active)
      tft.fillRect(playerBullets[i].x - 1, playerBullets[i].y - 5, 2, 6, TFT_YELLOW);
  }
}

void drawInvader(int16_t cx, int16_t cy, uint8_t row, uint8_t anim) {
  uint16_t color;
  if      (row < 2) color = TFT_RED;
  else if (row < 3) color = TFT_MAGENTA;
  else              color = TFT_CYAN;

  if (row < 2) {
    // Rows 0–1: octopus body
    tft.fillRect(cx - 8, cy - 5, 16, 10, color);
    tft.fillRect(cx - 6, cy - 8,  12,  4, color);
    tft.fillCircle(cx - 3, cy - 2, 2, TFT_BLACK);
    tft.fillCircle(cx + 3, cy - 2, 2, TFT_BLACK);
    if (anim == 0) {
      tft.drawLine(cx - 7, cy + 5, cx - 10, cy + 9, color);
      tft.drawLine(cx + 7, cy + 5, cx + 10, cy + 9, color);
    } else {
      tft.drawLine(cx - 7, cy + 5, cx - 4, cy + 9, color);
      tft.drawLine(cx + 7, cy + 5, cx + 4, cy + 9, color);
    }
  } else if (row == 2) {
    // Row 2: crab body
    tft.fillRect(cx - 8, cy - 3, 16, 10, color);
    tft.fillRect(cx - 5, cy - 7, 10,  5, color);
    tft.fillCircle(cx - 3, cy - 1, 2, TFT_BLACK);
    tft.fillCircle(cx + 3, cy - 1, 2, TFT_BLACK);
    if (anim == 0) {
      tft.drawLine(cx - 8, cy - 1, cx - 12, cy - 5, color);
      tft.drawLine(cx + 8, cy - 1, cx + 12, cy - 5, color);
      tft.drawLine(cx - 8, cy + 6, cx - 12, cy + 10, color);
      tft.drawLine(cx + 8, cy + 6, cx + 12, cy + 10, color);
    } else {
      tft.drawLine(cx - 8, cy - 1, cx - 12, cy + 3, color);
      tft.drawLine(cx + 8, cy - 1, cx + 12, cy + 3, color);
      tft.drawLine(cx - 8, cy + 6, cx - 12, cy + 2, color);
      tft.drawLine(cx + 8, cy + 6, cx + 12, cy + 2, color);
    }
  } else {
    // Row 3: squid
    tft.fillEllipse(cx, cy, 8, 6, color);
    tft.fillCircle(cx - 3, cy - 1, 1, TFT_BLACK);
    tft.fillCircle(cx + 3, cy - 1, 1, TFT_BLACK);
    if (anim == 0) {
      tft.drawLine(cx - 6, cy + 6, cx - 8,  cy + 10, color);
      tft.drawLine(cx,     cy + 6, cx,      cy + 10, color);
      tft.drawLine(cx + 6, cy + 6, cx + 8,  cy + 10, color);
    } else {
      tft.drawLine(cx - 6, cy + 6, cx - 4,  cy + 10, color);
      tft.drawLine(cx,     cy + 6, cx,      cy + 10, color);
      tft.drawLine(cx + 6, cy + 6, cx + 4,  cy + 10, color);
    }
  }
}

void drawUFO() {
  if (!ufo.active) return;
  int16_t x = ufo.x;
  tft.fillEllipse(x, UFO_Y,     16, 5, TFT_RED);
  tft.fillEllipse(x, UFO_Y - 4,  8, 4, TFT_RED);
  for (int8_t ox = -10; ox <= 10; ox += 5)
    tft.fillCircle(x + ox, UFO_Y + 2, 1, TFT_YELLOW);
}

void drawShields() {
  for (uint8_t s = 0; s < NUM_SHIELDS; s++) {
    int16_t left = SHIELD_CX[s] - (SHIELD_COLS * SHIELD_BLOCK) / 2;
    for (uint8_t r = 0; r < SHIELD_ROWS; r++) {
      for (uint8_t c = 0; c < SHIELD_COLS; c++) {
        uint8_t hp = shieldHP[s][c][r];
        if (hp == 0) continue;
        uint16_t col = (hp >= 3) ? tft.color565(0, 200, 0)
                     : (hp == 2) ? tft.color565(0, 130, 0)
                                 : tft.color565(0, 60,  0);
        tft.fillRect(left + c * SHIELD_BLOCK, SHIELD_Y + r * SHIELD_BLOCK,
                     SHIELD_BLOCK, SHIELD_BLOCK, col);
      }
    }
  }
}

void drawHUD() {
  char buf[24];
  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "SC:%05lu", score);
  tft.setCursor(0, 1);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "HI:%05lu", highScore);
  tft.setCursor(90, 1);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "LV:%d", playerLives);
  tft.setCursor(252, 1);
  tft.print(buf);

  if (playerShieldHP > 0) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "SH:%d", playerShieldHP);
    tft.setCursor(288, 1);
    tft.print(buf);
  }

  tft.drawFastHLine(0, 10, SCREEN_W, TFT_GREEN);
}

void drawTouchButtons() {
  tft.drawFastHLine(0, TOUCH_ZONE_Y, SCREEN_W, TFT_GREEN);
  tft.drawFastVLine(TOUCH_LEFT_X,  TOUCH_ZONE_Y, SCREEN_H - TOUCH_ZONE_Y, TFT_DARKGREY);
  tft.drawFastVLine(TOUCH_RIGHT_X, TOUCH_ZONE_Y, SCREEN_H - TOUCH_ZONE_Y, TFT_DARKGREY);

  int16_t ty = TOUCH_ZONE_Y + 10;
  tft.setTextSize(1);

  tft.setTextColor(touchLeft  ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(12, ty);
  tft.print("< LEFT");

  tft.setTextColor(touchFire  ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(134, ty);
  tft.print("FIRE");

  tft.setTextColor(touchRight ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  tft.setCursor(226, ty);
  tft.print("RIGHT >");
}

void drawOverlay(const char* title, uint16_t color, const char* sub) {
  tft.fillRect(30, 72, 260, 84, tft.color565(0, 0, 64));
  tft.drawRect(30, 72, 260, 84, color);

  tft.setTextSize(3);
  tft.setTextColor(color);
  int16_t tw = strlen(title) * 18;
  tft.setCursor((SCREEN_W - tw) / 2, 86);
  tft.print(title);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  int16_t sw = strlen(sub) * 6;
  tft.setCursor((SCREEN_W - sw) / 2, 136);
  tft.print(sub);
}

// ============================================================
//  Game logic
// ============================================================

int countAlive() {
  int n = 0;
  for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++)
    if (invaders[i].alive) n++;
  return n;
}

void initGame() {
  playerX         = SCREEN_W / 2;
  shootCooldown   = 0;
  playerLives     = 3;
  playerInvincible = 0;
  playerShieldHP  = 0;
  shieldGranted   = false;

  for (uint8_t i = 0; i < MAX_PLAYER_BULLETS; i++) playerBullets[i].active = false;
  for (uint8_t i = 0; i < MAX_ENEMY_BULLETS;  i++) enemyBullets[i].active  = false;

  for (uint8_t r = 0; r < INVADER_ROWS; r++) {
    for (uint8_t c = 0; c < INVADER_COLS; c++) {
      uint8_t idx = r * INVADER_COLS + c;
      invaders[idx].x     = INVADER_START_X + c * INVADER_H_GAP;
      invaders[idx].y     = INVADER_START_Y + r * INVADER_V_GAP;
      invaders[idx].row   = r;
      invaders[idx].alive = true;
    }
  }

  for (uint8_t s = 0; s < NUM_SHIELDS; s++)
    for (uint8_t c = 0; c < SHIELD_COLS; c++)
      for (uint8_t r = 0; r < SHIELD_ROWS; r++)
        shieldHP[s][c][r] = 3;

  ufo.active     = false;
  ufo.dir        = 1;
  ufo.spawnTimer = 400 + random(500);
  ufo.points     = (random(4) + 1) * 50;

  swarmDir      = 1;
  swarmTimer    = 0;
  swarmInterval = 30;
  enemyShootTimer = ENEMY_SHOOT_RATE;
  animFrame       = 0;

  score     = 0;
  gameState = STATE_PLAYING;
}

void handleTouch() {
  uint16_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty);

  if (gameState != STATE_PLAYING) {
    if (touched && !prevTouched) initGame();
    prevTouched = touched;
    touchLeft = touchRight = touchFire = false;
    return;
  }

  touchLeft  = false;
  touchRight = false;
  touchFire  = false;

  if (touched && ty >= (uint16_t)TOUCH_ZONE_Y) {
    if      (tx < (uint16_t)TOUCH_LEFT_X)  touchLeft  = true;
    else if (tx > (uint16_t)TOUCH_RIGHT_X) touchRight = true;
    else                                    touchFire  = true;
  }

  prevTouched = touched;
}

void updatePlayer() {
  if (touchLeft  && playerX - PLAYER_HW > 0)            playerX -= PLAYER_SPEED;
  if (touchRight && playerX + PLAYER_HW < SCREEN_W)     playerX += PLAYER_SPEED;

  if (shootCooldown > 0) shootCooldown--;

  if (touchFire && shootCooldown == 0) {
    for (uint8_t i = 0; i < MAX_PLAYER_BULLETS; i++) {
      if (!playerBullets[i].active) {
        playerBullets[i] = { playerX, (int16_t)(PLAYER_Y - 2),
                             -BULLET_SPEED, TFT_YELLOW, true };
        shootCooldown = SHOOT_COOLDOWN;
        break;
      }
    }
  }

  if (playerInvincible > 0) playerInvincible--;

  // Grant emergency shield the moment lives drop to 1
  if (playerLives == 1 && !shieldGranted) {
    playerShieldHP = 3;
    shieldGranted  = true;
  }

  for (uint8_t i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) continue;
    playerBullets[i].y += playerBullets[i].speed;
    if (playerBullets[i].y < 0) playerBullets[i].active = false;
  }
}

void updateInvaders() {
  int alive = countAlive();
  if (alive == 0) { gameState = STATE_WIN; return; }

  // Speed scales with kills
  uint8_t interval = max(3, (int)swarmInterval - (INVADER_COLS * INVADER_ROWS - alive));

  swarmTimer++;
  if (swarmTimer >= interval) {
    swarmTimer = 0;
    animFrame ^= 1;

    // Bounding box of alive swarm
    int16_t minX = 32767, maxX = -32768;
    for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++) {
      if (!invaders[i].alive) continue;
      if (invaders[i].x < minX) minX = invaders[i].x;
      if (invaders[i].x > maxX) maxX = invaders[i].x;
    }

    bool bounce = (swarmDir ==  1 && maxX + INVADER_HW + INVADER_STEP >= SCREEN_W)
               || (swarmDir == -1 && minX - INVADER_HW - INVADER_STEP <= 0);

    if (bounce) {
      swarmDir = -swarmDir;
      for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++)
        if (invaders[i].alive) invaders[i].y += INVADER_STEP;
    } else {
      for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++)
        if (invaders[i].alive) invaders[i].x += swarmDir * INVADER_STEP;
    }

    // Check reach-player and shield collision after move
    for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++) {
      if (!invaders[i].alive) continue;
      if (invaders[i].y + INVADER_HH >= PLAYER_Y - 4) {
        gameState = STATE_LOSE;
        return;
      }
      // Crush shield blocks invader overlaps
      for (uint8_t s = 0; s < NUM_SHIELDS; s++) {
        int16_t sl = SHIELD_CX[s] - (SHIELD_COLS * SHIELD_BLOCK) / 2;
        int16_t sr = sl + SHIELD_COLS * SHIELD_BLOCK;
        int16_t sb = SHIELD_Y + SHIELD_ROWS * SHIELD_BLOCK;
        if (invaders[i].x + INVADER_HW < sl || invaders[i].x - INVADER_HW > sr) continue;
        if (invaders[i].y + INVADER_HH < SHIELD_Y || invaders[i].y - INVADER_HH > sb) continue;
        for (uint8_t c2 = 0; c2 < SHIELD_COLS; c2++) {
          for (uint8_t r2 = 0; r2 < SHIELD_ROWS; r2++) {
            if (shieldHP[s][c2][r2] == 0) continue;
            int16_t bx = sl + c2 * SHIELD_BLOCK + SHIELD_BLOCK / 2;
            int16_t by = SHIELD_Y + r2 * SHIELD_BLOCK + SHIELD_BLOCK / 2;
            if (abs(bx - invaders[i].x) <= INVADER_HW &&
                abs(by - invaders[i].y) <= INVADER_HH)
              shieldHP[s][c2][r2] = 0;
          }
        }
      }
    }
  }

  // Enemy shooting
  enemyShootTimer--;
  if (enemyShootTimer == 0) {
    enemyShootTimer = ENEMY_SHOOT_RATE / 2 + random(ENEMY_SHOOT_RATE / 2 + 1);
    int pick = random(INVADER_COLS * INVADER_ROWS);
    for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++) {
      int idx = (pick + i) % (INVADER_COLS * INVADER_ROWS);
      if (!invaders[idx].alive) continue;
      for (uint8_t b = 0; b < MAX_ENEMY_BULLETS; b++) {
        if (!enemyBullets[b].active) {
          enemyBullets[b] = { invaders[idx].x, invaders[idx].y,
                              ENEMY_BULLET_SPEED, TFT_RED, true };
          break;
        }
      }
      break;
    }
  }
}

void updateBullets() {
  // --- Enemy bullets ---
  for (uint8_t i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!enemyBullets[i].active) continue;
    enemyBullets[i].y += enemyBullets[i].speed;

    if (enemyBullets[i].y >= TOUCH_ZONE_Y) { enemyBullets[i].active = false; continue; }

    bool consumed = false;

    // Hit shield?
    for (uint8_t s = 0; s < NUM_SHIELDS && !consumed; s++) {
      int16_t sl = SHIELD_CX[s] - (SHIELD_COLS * SHIELD_BLOCK) / 2;
      if (enemyBullets[i].x < sl || enemyBullets[i].x >= sl + SHIELD_COLS * SHIELD_BLOCK) continue;
      if (enemyBullets[i].y < SHIELD_Y || enemyBullets[i].y >= SHIELD_Y + SHIELD_ROWS * SHIELD_BLOCK) continue;
      uint8_t c = (enemyBullets[i].x - sl) / SHIELD_BLOCK;
      uint8_t r = (enemyBullets[i].y - SHIELD_Y) / SHIELD_BLOCK;
      if (c < SHIELD_COLS && r < SHIELD_ROWS && shieldHP[s][c][r] > 0) {
        shieldHP[s][c][r]--;
        enemyBullets[i].active = false;
        consumed = true;
      }
    }
    if (consumed) continue;

    // Hit player?
    if (playerInvincible == 0 &&
        enemyBullets[i].x >= playerX - PLAYER_HW &&
        enemyBullets[i].x <= playerX + PLAYER_HW &&
        enemyBullets[i].y >= PLAYER_Y &&
        enemyBullets[i].y <= PLAYER_Y + PLAYER_HT) {
      enemyBullets[i].active = false;
      if (playerShieldHP > 0) {
        playerShieldHP--;
        playerInvincible = 60;
      } else {
        playerLives--;
        playerInvincible = 120;
        if (playerLives <= 0) { gameState = STATE_LOSE; return; }
      }
    }
  }

  // --- Player bullets ---
  for (uint8_t i = 0; i < MAX_PLAYER_BULLETS; i++) {
    if (!playerBullets[i].active) continue;
    bool consumed = false;

    // vs invaders
    for (int j = 0; j < INVADER_COLS * INVADER_ROWS && !consumed; j++) {
      if (!invaders[j].alive) continue;
      if (abs(playerBullets[i].x - invaders[j].x) < INVADER_HW &&
          abs(playerBullets[i].y - invaders[j].y) < INVADER_HH) {
        invaders[j].alive = false;
        playerBullets[i].active = false;
        score += INV_PTS[invaders[j].row];
        if (score > highScore) highScore = score;
        consumed = true;
      }
    }
    if (consumed) continue;

    // vs UFO
    if (ufo.active &&
        abs(playerBullets[i].x - ufo.x) < 16 &&
        abs(playerBullets[i].y - UFO_Y)  <  8) {
      score += ufo.points;
      if (score > highScore) highScore = score;
      ufo.active     = false;
      ufo.spawnTimer = 300 + random(400);
      ufo.points     = (random(4) + 1) * 50;
      playerBullets[i].active = false;
      continue;
    }

    // vs shields
    for (uint8_t s = 0; s < NUM_SHIELDS && !consumed; s++) {
      int16_t sl = SHIELD_CX[s] - (SHIELD_COLS * SHIELD_BLOCK) / 2;
      if (playerBullets[i].x < sl || playerBullets[i].x >= sl + SHIELD_COLS * SHIELD_BLOCK) continue;
      if (playerBullets[i].y < SHIELD_Y || playerBullets[i].y >= SHIELD_Y + SHIELD_ROWS * SHIELD_BLOCK) continue;
      uint8_t c = (playerBullets[i].x - sl) / SHIELD_BLOCK;
      uint8_t r = (playerBullets[i].y - SHIELD_Y) / SHIELD_BLOCK;
      if (c < SHIELD_COLS && r < SHIELD_ROWS && shieldHP[s][c][r] > 0) {
        shieldHP[s][c][r]--;
        playerBullets[i].active = false;
        consumed = true;
      }
    }
  }
}

void updateUFO() {
  if (!ufo.active) {
    if (ufo.spawnTimer > 0) { ufo.spawnTimer--; return; }
    ufo.active = true;
    ufo.dir    = random(2) ? 1 : -1;
    ufo.x      = (ufo.dir == 1) ? -20 : SCREEN_W + 20;
    return;
  }
  ufo.x += ufo.dir * 2;
  if (ufo.x < -40 || ufo.x > SCREEN_W + 40) {
    ufo.active     = false;
    ufo.spawnTimer = 300 + random(400);
    ufo.points     = (random(4) + 1) * 50;
  }
}

// ============================================================
//  Arduino entry points
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Boot");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);   // landscape

  // Default CYD calibration for landscape.
  // To calibrate your unit: uncomment tft.calibrateTouch() below,
  // flash once, note serial output, paste values here, re-flash.
  uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };
  tft.setTouch(calData);

  randomSeed(analogRead(34));  // floating ADC for entropy

  highScore   = 0;
  prevTouched = false;
  initGame();
  lastFrameMs = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;

  handleTouch();

  if (gameState == STATE_PLAYING) {
    updatePlayer();
    updateInvaders();
    updateBullets();
    updateUFO();
  }

  // --- Render ---
  tft.fillScreen(TFT_BLACK);

  drawHUD();
  drawShields();
  drawUFO();

  for (int i = 0; i < INVADER_COLS * INVADER_ROWS; i++)
    if (invaders[i].alive)
      drawInvader(invaders[i].x, invaders[i].y, invaders[i].row, animFrame);

  drawPlayer();

  for (uint8_t i = 0; i < MAX_ENEMY_BULLETS; i++)
    if (enemyBullets[i].active)
      tft.fillRect(enemyBullets[i].x - 1, enemyBullets[i].y, 2, 5, TFT_RED);

  drawTouchButtons();

  if (gameState == STATE_WIN)
    drawOverlay("YOU WIN!", TFT_GREEN, "Tap to play again");
  else if (gameState == STATE_LOSE)
    drawOverlay("GAME OVER", TFT_RED, "Tap to play again");
}

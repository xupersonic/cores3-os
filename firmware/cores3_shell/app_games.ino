// 应用 4：游戏（v0.7.0 取代系统信息）—— 俄罗斯方块 + 2048
//
// 刷新策略（守项目约定，见 shell.h 里 uiTick 上方那段）：
//   · 菜单：纯静态，画一次就再也不画。
//   · 2048：纯事件驱动，按键 / 点击才重画，一帧都不空转。
//   · 俄罗斯方块：唯一「自己会变」的内容是自动下落，但也不整屏重画 ——
//     只擦掉方块占的 4 格、在新位置重画 4 格；落定或消行才整盘重画。
//     右侧面板只在分数 / 消行 / 等级 / 下一个变化时重画。
//
// 操作：Keyboard3 的方向键要按 Fn+K/M/N/$（183/192/191/193），打游戏太别扭，
//       所以两个游戏都另外认 WASD —— 左手四个键正好是方向。
//
// v0.7.1：加了静音开关。两个游戏共用一颗（写在 NVS 里，掉电不丢），
//       菜单 / 方块 / 2048 三处都能切 —— 点按钮或按 M 键。
//       按钮上直接写状态（声音开 = 绿色实心，已静音 = 红色描边），不用猜。
//       静音只管游戏里的提示音，别的应用（控制器按键反馈等）不受影响。

#include "shell.h"
#include <Preferences.h>

enum { GM_MENU = 0, GM_TETRIS = 1, GM_2048 = 2 };
static uint8_t g_mode = GM_MENU;

// 静音开关的三处位置（都在屏幕右下角，握着机器拇指正好够得到）
static const int MMX = 8,   MMY = 212, MMW = 104, MMH = 24;   // 菜单：底部左边
static const int TMX = 122, TMY = 216, TMW = 188, TMH = 20;   // 方块：面板最下面
static const int BMX = 192, BMY = 220, BMW = 120, BMH = 18;   // 2048：面板下面

// ─── 小工具 ──────────────────────────────────────────────────────────────
// 方块高光：把颜色往白色推一点，做出像素块的立体感
static uint32_t gmBright(uint32_t c, int amt) {
  int r = (int)((c >> 16) & 0xFF) + amt; if (r > 255) r = 255;
  int g = (int)((c >> 8) & 0xFF) + amt;  if (g > 255) g = 255;
  int b = (int)(c & 0xFF) + amt;         if (b > 255) b = 255;
  return (uint32_t)((r << 16) | (g << 8) | b);
}

// 面板里的统计行：左边暗色标签，右边霓虹数值
static void gmStat(int lx, int rx, int y, const char* label, const char* val, uint32_t ac) {
  setAutoFont(label);
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString(label, lx, y);
  setPxFont();
  M5.Display.setTextColor(ac);
  M5.Display.drawRightString(val, rx, y + 4);
}

// ─── 静音开关 ────────────────────────────────────────────────────────────
static bool g_mute = false;
static void muteToggle();                  // 定义在后面（要重画当前界面）

// 游戏里所有提示音都走这里，静音时一声不出
static void gmBeep(uint16_t freq, uint16_t ms) { if (!g_mute) beep(freq, ms); }

// 有声 = 绿色实心，静音 = 红色描边（一眼能看出当前是哪档）
static void drawMuteBox(int x, int y, int w, int h) {
  drawKeyBox(x, y, w, h, g_mute ? S(S_GAME_MUTED) : S(S_GAME_SNDON),
             g_mute ? C_RED : C_GREEN, !g_mute);
}

static void muteLoad() {
  Preferences p;
  p.begin("cores3", true);
  g_mute = p.getBool("gmute", false);
  p.end();
}
static void muteSave() {
  Preferences p;
  p.begin("cores3", false);
  p.putBool("gmute", g_mute);
  p.end();
}

// ═════════════════════════════════════════════════════════════════════════
//  俄罗斯方块
// ═════════════════════════════════════════════════════════════════════════
static const int TBX = 8,  TBY = 42,  TBW = 104, TBH = 184;   // 棋盘外框
static const int TCX = 10, TCY = 44,  TCS = 10;               // 格子起点 / 边长
// 面板比棋盘高 12px：多出来的那一条留给静音开关（v0.7.1）
static const int TPX = 118, TPY = 42, TPW = 194, TPH = 196;   // 右侧面板
static const int TW = 10,  TH = 18;                           // 10 列 × 18 行

static const uint32_t PIECE_COL[7] = {
  C_CYAN, 0x3B82F6u, C_AMBER, 0xFFE100u, C_GREEN, C_VIOLET, C_RED
};

// 7 种方块在 4x4 框里的初始 4 格；4 个旋转态由 (x,y) → (y, 3-x) 推出来
static const uint8_t PIECE_BASE[7][4][2] = {
  { {0,1},{1,1},{2,1},{3,1} },   // I
  { {0,0},{0,1},{1,1},{2,1} },   // J
  { {2,0},{0,1},{1,1},{2,1} },   // L
  { {1,0},{2,0},{1,1},{2,1} },   // O
  { {1,0},{2,0},{0,1},{1,1} },   // S
  { {1,0},{0,1},{1,1},{2,1} },   // T
  { {0,0},{1,0},{1,1},{2,1} },   // Z
};
static uint8_t PIECE_ROT[7][4][4][2];      // [类型][旋转][格][x|y]
static bool    PIECE_BUILT = false;

static uint8_t t_grid[TH][TW];
static int8_t  t_px = 3, t_py = 0;
static uint8_t t_type = 0, t_rot = 0, t_next = 0;
static uint32_t t_score = 0, t_lines = 0, t_level = 1;
static bool    t_over = false, t_pause = false;
static uint32_t t_lastFall = 0;
// 面板上已经画出来的值（用来判断要不要重画，避免跟着下落节拍一起闪）
static uint32_t d_score = 0xFFFFFFFF, d_lines = 0xFFFFFFFF, d_level = 0, d_next = 255;
static bool     d_mute = false;

static void tetrisBuildRot() {
  for (int t = 0; t < 7; t++)
    for (int c = 0; c < 4; c++) {
      int x = PIECE_BASE[t][c][0], y = PIECE_BASE[t][c][1];
      for (int r = 0; r < 4; r++) {
        int cx = x, cy = y;
        for (int k = 0; k < r; k++) { int nx = cy, ny = 3 - cx; cx = nx; cy = ny; }
        PIECE_ROT[t][r][c][0] = (uint8_t)cx;
        PIECE_ROT[t][r][c][1] = (uint8_t)cy;
      }
    }
  PIECE_BUILT = true;
}

static uint32_t tetrisInterval() {                 // 等级越高落得越快
  uint32_t v = 800 - (t_level - 1) * 70;
  return v < 120 ? 120 : v;
}

static bool tetrisFit(int type, int rot, int px, int py) {
  for (int c = 0; c < 4; c++) {
    int x = px + PIECE_ROT[type][rot][c][0];
    int y = py + PIECE_ROT[type][rot][c][1];
    if (x < 0 || x >= TW) return false;
    if (y >= TH) return false;
    if (y < 0) continue;                            // 还在盘面之上，允许
    if (t_grid[y][x]) return false;
  }
  return true;
}

// 画一格：v=0 是空格（铺面板底色），否则是第 v-1 种方块
static void tetrisCell(int gx, int gy, uint8_t v) {
  int x = TCX + gx * TCS, y = TCY + gy * TCS;
  if (v == 0) { M5.Display.fillRect(x, y, TCS, TCS, C_PANEL); return; }
  uint32_t c = PIECE_COL[v - 1];
  M5.Display.fillRect(x + 1, y + 1, 8, 8, c);
  M5.Display.fillRect(x + 2, y + 2, 4, 4, gmBright(c, 70));
}

static void tetrisPiecePaint(uint8_t v) {          // v=0 擦掉，v=类型+1 画上
  for (int c = 0; c < 4; c++) {
    int x = t_px + PIECE_ROT[t_type][t_rot][c][0];
    int y = t_py + PIECE_ROT[t_type][t_rot][c][1];
    if (y >= 0) tetrisCell(x, y, v);
  }
}

static void tetrisDrawBoard() {
  M5.Display.fillRect(TCX, TCY, TW * TCS, TH * TCS, C_PANEL);
  for (int y = 0; y < TH; y++)
    for (int x = 0; x < TW; x++)
      if (t_grid[y][x]) tetrisCell(x, y, t_grid[y][x]);
  if (!t_over) tetrisPiecePaint(t_type + 1);
}

// 试着走一步：先擦掉当前位置，能落下就更新坐标再画，不行就原地画回去
static bool tetrisTry(int dx, int dy, int dr) {
  int nr = (int)((t_rot + dr + 4) & 3);
  tetrisPiecePaint(0);
  if (!tetrisFit(t_type, nr, t_px + dx, t_py + dy)) {
    tetrisPiecePaint(t_type + 1);
    return false;
  }
  t_px = (int8_t)(t_px + dx);
  t_py = (int8_t)(t_py + dy);
  t_rot = (uint8_t)nr;
  tetrisPiecePaint(t_type + 1);
  return true;
}

// 旋转（带一点贴墙补偿：转不动就先试着左右挪一格再转，不然靠墙根本转不了）
static void tetrisRotate() {
  if (tetrisTry(0, 0, 1)) return;
  if (tetrisTry(-1, 0, 1)) return;
  tetrisTry(1, 0, 1);
}

static void tetrisSpawn() {
  t_type = t_next;
  t_next = (uint8_t)random(7);
  t_rot = 0;
  t_px = 3;
  t_py = 0;
  if (!tetrisFit(t_type, t_rot, t_px, t_py)) t_over = true;
}

static void tetrisLock() {
  for (int c = 0; c < 4; c++) {
    int x = t_px + PIECE_ROT[t_type][t_rot][c][0];
    int y = t_py + PIECE_ROT[t_type][t_rot][c][1];
    if (y < 0) { t_over = true; return; }
    t_grid[y][x] = (uint8_t)(t_type + 1);
  }
  static const int LINE_SCORE[5] = { 0, 100, 300, 500, 800 };
  int cleared = 0;
  for (int y = TH - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < TW; x++) if (!t_grid[y][x]) { full = false; break; }
    if (!full) continue;
    cleared++;
    for (int yy = y; yy > 0; yy--) memcpy(t_grid[yy], t_grid[yy - 1], TW);
    memset(t_grid[0], 0, TW);
    y++;                                            // 上面整体下移了一行，这一行要重查
  }
  if (cleared) {
    t_score += (uint32_t)LINE_SCORE[cleared] * t_level;
    t_lines += (uint32_t)cleared;
    t_level  = (uint32_t)(t_lines / 10) + 1;
    gmBeep(660 + cleared * 110, 90);
  }
  if (!t_over) tetrisSpawn();
}

static void tetrisReset() {
  memset(t_grid, 0, sizeof(t_grid));
  t_score = t_lines = 0;
  t_level = 1;
  t_over = t_pause = false;
  t_next = (uint8_t)random(7);
  tetrisSpawn();
  t_lastFall = millis();
  d_score = 0xFFFFFFFF; d_lines = 0xFFFFFFFF; d_level = 0; d_next = 255; d_mute = !g_mute;
}

static void tetrisDrawNext() {
  M5.Display.fillRect(214, 66, 92, 46, C_PANEL);
  int minx = 9, maxx = -1, miny = 9, maxy = -1;
  for (int c = 0; c < 4; c++) {
    int x = PIECE_ROT[t_next][0][c][0], y = PIECE_ROT[t_next][0][c][1];
    if (x < minx) minx = x; if (x > maxx) maxx = x;
    if (y < miny) miny = y; if (y > maxy) maxy = y;
  }
  int w = (maxx - minx + 1) * 10, h = (maxy - miny + 1) * 10;
  int ox = 214 + (92 - w) / 2 - minx * 10;
  int oy = 66  + (46 - h) / 2 - miny * 10;
  for (int c = 0; c < 4; c++) {
    int x = PIECE_ROT[t_next][0][c][0], y = PIECE_ROT[t_next][0][c][1];
    uint32_t col = PIECE_COL[t_next];
    M5.Display.fillRect(ox + x * 10 + 1, oy + y * 10 + 1, 8, 8, col);
    M5.Display.fillRect(ox + x * 10 + 2, oy + y * 10 + 2, 4, 4, gmBright(col, 70));
  }
}

static void tetrisDrawPanel() {
  M5.Display.fillRect(TPX, TPY, TPW, TPH, C_PANEL);
  drawFrame(TPX, TPY, TPW, TPH, C_MAGENTA);

  char v[24];
  snprintf(v, sizeof(v), "%lu", (unsigned long)t_score);
  gmStat(124, 206, 48, S(S_GAME_SCORE), v, C_CYAN);
  snprintf(v, sizeof(v), "%lu", (unsigned long)t_lines);
  gmStat(124, 206, 70, S(S_GAME_LINES), v, C_CYAN);
  snprintf(v, sizeof(v), "%lu", (unsigned long)t_level);
  gmStat(124, 206, 92, S(S_GAME_LEVEL), v, C_CYAN);

  drawFrame(212, 46, 96, 70, C_MAGENTA);
  setAutoFont(S(S_GAME_NEXT));                 // 中文标签要走自动字体，8x8 点阵没汉字
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString(S(S_GAME_NEXT), 218, 48);
  tetrisDrawNext();

  drawKeyBox(122, 120, 92, 44, S(S_GAME_LEFT),  C_CYAN, false);
  drawKeyBox(218, 120, 92, 44, S(S_GAME_RIGHT), C_CYAN, false);
  drawKeyBox(122, 168, 92, 44, S(S_GAME_ROT),   C_CYAN, false);
  drawKeyBox(218, 168, 92, 44, S(S_GAME_DROP),  C_CYAN, false);

  drawMuteBox(TMX, TMY, TMW, TMH);

  d_score = t_score; d_lines = t_lines; d_level = t_level; d_next = t_next; d_mute = g_mute;
}

static void tetrisDrawOverlay() {
  if (!t_over && !t_pause) return;
  M5.Display.fillRect(14, 88, 92, 56, C_BG);
  drawFrame(14, 88, 92, 56, t_over ? C_RED : C_AMBER);
  drawCenterText(60, 96, 16, S(t_over ? S_GAME_OVER : S_GAME_PAUSE),
                 t_over ? C_RED : C_AMBER);
  drawCenterText(60, 120, 16, S(S_GAME_TAP), C_DIM);
}

static void tetrisDraw(bool full) {
  if (full) {
    clearCanvas();
    M5.Display.fillRect(TBX, TBY, TBW, TBH, C_PANEL);
    drawFrame(TBX, TBY, TBW, TBH, C_MAGENTA);
    tetrisDrawBoard();
    tetrisDrawPanel();
  } else {
    tetrisDrawBoard();
    if (t_score != d_score || t_lines != d_lines || t_level != d_level ||
        t_next != d_next || g_mute != d_mute) tetrisDrawPanel();
  }
  tetrisDrawOverlay();
}

static void tetrisTick() {
  if (t_over || t_pause) return;
  uint32_t now = millis();
  if (now - t_lastFall < tetrisInterval()) return;
  t_lastFall = now;
  wakeScreen();                       // 手上没动作也别让屏幕熄掉
  if (!tetrisTry(0, 1, 0)) tetrisLock();
  tetrisDraw(false);
  if (t_over) gmBeep(220, 400);
}

static void tetrisHardDrop() {
  while (tetrisTry(0, 1, 0)) t_score += 2;
  tetrisLock();
  tetrisDraw(false);
  if (t_over) gmBeep(220, 400);
}

static void tetrisKey(uint8_t raw) {
  if (raw == 'p' || raw == 'P') { t_pause = !t_pause; tetrisDraw(true); return; }
  if (raw == 'n' || raw == 'N') { tetrisReset(); tetrisDraw(true); return; }
  if (t_over) { tetrisReset(); tetrisDraw(true); return; }
  if (t_pause) return;
  if (raw == 191 || raw == 'a' || raw == 'A') { tetrisTry(-1, 0, 0); tetrisDraw(false); return; }
  if (raw == 193 || raw == 'd' || raw == 'D') { tetrisTry( 1, 0, 0); tetrisDraw(false); return; }
  if (raw == 183 || raw == 'w' || raw == 'W') { tetrisRotate(); tetrisDraw(false); return; }
  if (raw == 192 || raw == 's' || raw == 'S') {
    if (tetrisTry(0, 1, 0)) t_score += 1;
    else { tetrisLock(); if (t_over) gmBeep(220, 400); }
    tetrisDraw(false);
    return;
  }
  if (raw == ' ') { tetrisHardDrop(); return; }
}

static void tetrisTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, TMX, TMY, TMW, TMH)) { muteToggle(); return; }
  if (t_over) { tetrisReset(); tetrisDraw(true); return; }
  if (t_pause) { t_pause = false; tetrisDraw(true); return; }
  if (hitBox(x, y, 122, 120, 92, 44)) { tetrisTry(-1, 0, 0); tetrisDraw(false); return; }
  if (hitBox(x, y, 218, 120, 92, 44)) { tetrisTry( 1, 0, 0); tetrisDraw(false); return; }
  if (hitBox(x, y, 122, 168, 92, 44)) { tetrisRotate(); tetrisDraw(false); return; }
  if (hitBox(x, y, 218, 168, 92, 44)) { tetrisHardDrop(); return; }
  // 点棋盘也能左右挪：左半边往左、右半边往右（单手握着时比够按钮顺手）
  if (hitBox(x, y, TBX, TBY, TBW, TBH)) {
    tetrisTry(x < TBX + TBW / 2 ? -1 : 1, 0, 0);
    tetrisDraw(false);
  }
}

// ═════════════════════════════════════════════════════════════════════════
//  2048
// ═════════════════════════════════════════════════════════════════════════
static const int BBX = 8,  BBY = 42,  BBW = 176, BBH = 176;   // 棋盘外框
static const int BCX = 12, BCY = 46,  BCS = 40,  BSTP = 44;   // 格子起点 / 边长 / 步进
static const int BPX = 192, BPY = 42, BPW = 120, BPH = 176;   // 右侧面板

static uint16_t b_grid[4][4];
static uint32_t b_score = 0, b_best = 0;
static bool     b_over = false, b_win = false;

static uint32_t tileColor(uint16_t v) {
  switch (v) {
    case 2:    return 0x223044u;
    case 4:    return 0x2E4055u;
    case 8:    return 0xC77A00u;
    case 16:   return C_AMBER;
    case 32:   return 0xFF6B2Cu;
    case 64:   return C_RED;
    case 128:  return C_MAGENTA;
    case 256:  return C_VIOLET;
    case 512:  return 0x7C3AEDu;
    case 1024: return C_LIME;
    default:   return C_CYAN;      // 2048 及以上
  }
}
static uint32_t tileText(uint16_t v) { return (v <= 4) ? C_TEXT : C_BG; }

static void bBestLoad() {
  Preferences p;
  p.begin("cores3", true);
  b_best = p.getUInt("g2048best", 0);
  p.end();
}
static void bBestSave() {
  Preferences p;
  p.begin("cores3", false);
  p.putUInt("g2048best", b_best);
  p.end();
}

static void bSpawn() {
  int ex[16], ey[16], n = 0;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) if (!b_grid[y][x]) { ex[n] = x; ey[n] = y; n++; }
  if (!n) return;
  int k = (int)random(n);
  b_grid[ey[k]][ex[k]] = (random(10) == 0) ? 4 : 2;
}

static bool bCanMove() {
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) {
      if (!b_grid[y][x]) return true;
      if (x + 1 < 4 && b_grid[y][x] == b_grid[y][x + 1]) return true;
      if (y + 1 < 4 && b_grid[y][x] == b_grid[y + 1][x]) return true;
    }
  return false;
}

// dir: 0=上 1=下 2=左 3=右。把每条线抽出来压到一端合并，再写回去
static bool bMove(int dir) {
  bool moved = false;
  for (int i = 0; i < 4; i++) {
    uint16_t line[4], out[4] = {0,0,0,0}, mg[4] = {0,0,0,0};
    // 按「移动方向优先」的顺序读进来，压到 j=0 一端
    for (int j = 0; j < 4; j++) {
      int x, y;
      if      (dir == 2) { x = j;     y = i; }
      else if (dir == 3) { x = 3 - j; y = i; }
      else if (dir == 0) { x = i;     y = j; }
      else               { x = i;     y = 3 - j; }
      line[j] = b_grid[y][x];
    }
    int n = 0;
    for (int j = 0; j < 4; j++) if (line[j]) out[n++] = line[j];
    int m = 0;
    for (int j = 0; j < n; j++) {
      if (j + 1 < n && out[j] == out[j + 1]) {
        mg[m++] = (uint16_t)(out[j] * 2);
        b_score += (uint32_t)(out[j] * 2);
        if (out[j] * 2 >= 2048) b_win = true;
        j++;
      } else mg[m++] = out[j];
    }
    for (int j = 0; j < 4; j++) {
      int x, y;
      if      (dir == 2) { x = j;     y = i; }
      else if (dir == 3) { x = 3 - j; y = i; }
      else if (dir == 0) { x = i;     y = j; }
      else               { x = i;     y = 3 - j; }
      if (b_grid[y][x] != mg[j]) moved = true;
      b_grid[y][x] = mg[j];
    }
  }
  return moved;
}

static void bReset() {
  memset(b_grid, 0, sizeof(b_grid));
  b_score = 0;
  b_over = b_win = false;
  bSpawn();
  bSpawn();
}

static void bDrawBoard() {
  M5.Display.fillRect(BBX, BBY, BBW, BBH, C_PANEL);
  drawFrame(BBX, BBY, BBW, BBH, C_MAGENTA);
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      int cx = BCX + x * BSTP, cy = BCY + y * BSTP;
      uint16_t v = b_grid[y][x];
      M5.Display.fillRect(cx, cy, BCS, BCS, v ? tileColor(v) : 0x16202Cu);
      if (!v) continue;
      char s[8];
      snprintf(s, sizeof(s), "%u", (unsigned)v);
      setPxFont();
      if (strlen(s) <= 2) M5.Display.setTextSize(2);
      M5.Display.setTextColor(tileText(v));
      M5.Display.drawCenterString(s, cx + BCS / 2,
                                  cy + (BCS - (strlen(s) <= 2 ? 16 : 8)) / 2);
      M5.Display.setTextSize(1);
    }
  }
}

static void bDrawPanel() {
  M5.Display.fillRect(BPX, BPY, BPW, BPH, C_PANEL);
  drawFrame(BPX, BPY, BPW, BPH, C_MAGENTA);
  char v[24];
  snprintf(v, sizeof(v), "%lu", (unsigned long)b_score);
  gmStat(196, 308, 48, S(S_GAME_SCORE), v, C_AMBER);
  snprintf(v, sizeof(v), "%lu", (unsigned long)(b_score > b_best ? b_score : b_best));
  gmStat(196, 308, 74, S(S_GAME_BEST), v, C_AMBER);

  drawKeyBox(196, 92,  112, 24, S(S_GAME_NEW), C_AMBER, false);
  drawKeyBox(196, 122, 54, 44, S(S_GAME_UP),    C_AMBER, false);
  drawKeyBox(254, 122, 54, 44, S(S_GAME_DOWN),  C_AMBER, false);
  drawKeyBox(196, 170, 54, 44, S(S_GAME_LEFT),  C_AMBER, false);
  drawKeyBox(254, 170, 54, 44, S(S_GAME_RIGHT), C_AMBER, false);
  // 254+54 = 308，面板右缘 312，正好留 4px
}

static void bDraw(bool full) {
  if (full) clearCanvas();
  M5.Display.fillRect(BPX, BPY, BPW, BPH, C_PANEL);
  bDrawBoard();
  bDrawPanel();
  // 只清棋盘下面那一条（218 起，别啃掉最后一行格子和外框）
  clearArea(0, 218, 320, 22);
  if (b_win) drawCenterText(96, 220, 18, S(S_GAME_WIN), C_CYAN);
  else       drawCenterText(96, 220, 18, S(S_GAME_FOOT2), C_DIM);
  drawMuteBox(BMX, BMY, BMW, BMH);
  if (b_over) {
    M5.Display.fillRect(28, 88, 140, 60, C_BG);
    drawFrame(28, 88, 140, 60, C_RED);
    drawCenterText(98, 96, 16, S(S_GAME_OVER), C_RED);
    drawCenterText(98, 120, 16, S(S_GAME_TAP), C_DIM);
  }
}

static void bStep(int dir) {
  if (b_over) { bReset(); bDraw(true); return; }
  if (!bMove(dir)) return;
  bSpawn();
  if (b_score > b_best) b_best = b_score;   // 只在 RAM 里更新，退出应用时才写 NVS
  if (!bCanMove()) {                        // 终局时才落盘，别每次合并都写 flash
    b_over = true;
    bBestSave();
    gmBeep(220, 400);
  } else gmBeep(880, 40);
  bDraw(false);
}

static void bKey(uint8_t raw) {
  if (raw == 'n' || raw == 'N') { bReset(); bDraw(true); return; }
  if (raw == 183 || raw == 'w' || raw == 'W') { bStep(0); return; }
  if (raw == 192 || raw == 's' || raw == 'S') { bStep(1); return; }
  if (raw == 191 || raw == 'a' || raw == 'A') { bStep(2); return; }
  if (raw == 193 || raw == 'd' || raw == 'D') { bStep(3); return; }
}

static void bTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, BMX, BMY, BMW, BMH))  { muteToggle(); return; }
  if (hitBox(x, y, 196, 92, 112, 24))  { bReset(); bDraw(true); return; }
  if (hitBox(x, y, 196, 122, 54, 44))  { bStep(0); return; }
  if (hitBox(x, y, 254, 122, 54, 44))  { bStep(1); return; }
  if (hitBox(x, y, 196, 170, 54, 44))  { bStep(2); return; }
  if (hitBox(x, y, 254, 170, 54, 44))  { bStep(3); return; }
  if (b_over && hitBox(x, y, BBX, BBY, BBW, BBH)) { bReset(); bDraw(true); }
}

// ═════════════════════════════════════════════════════════════════════════
//  菜单
// ═════════════════════════════════════════════════════════════════════════
static const uint8_t ICON_TETRIS[8] = { 0x00,0x78,0x78,0x00,0x3C,0x3C,0x7E,0x7E };
static const uint8_t ICON_2048[8]   = { 0xFF,0x99,0x99,0xFF,0x99,0x99,0x99,0xFF };

static const int MCW = 148, MCH = 152, MCY = 58;
static int mcX(int i) { return 8 + i * 156; }

static void menuDraw() {
  clearCanvas();
  drawTitle(38, S(S_GAME_PICK), C_MAGENTA);
  for (int i = 0; i < 2; i++) {
    int x = mcX(i);
    uint32_t ac = (i == 0) ? C_CYAN : C_AMBER;
    M5.Display.fillRect(x, MCY, MCW, MCH, C_PANEL);
    drawFrame(x, MCY, MCW, MCH, ac);
    drawPxIcon(x + MCW / 2, MCY + 52, i == 0 ? ICON_TETRIS : ICON_2048, 5, ac);
    drawCenterText(x + MCW / 2, MCY + 92, 16,
                   i == 0 ? S(S_GAME_TETRIS) : S(S_GAME_2048), ac);
    drawCenterText(x + MCW / 2, MCY + 118, 16, S(S_GAME_TAP), C_DIM);
  }
  drawMuteBox(MMX, MMY, MMW, MMH);
  drawCenterText(216, 212, 24, S(S_GAME_FOOTM), C_DIM);   // 提示挪到右边，给开关让位
}

// ═════════════════════════════════════════════════════════════════════════
//  对外生命周期
// ═════════════════════════════════════════════════════════════════════════
static void gameEnter(uint8_t m) {
  g_mode = m;
  if (m == GM_MENU)        menuDraw();
  else if (m == GM_TETRIS) { tetrisReset(); tetrisDraw(true); }
  else                     { bReset();      bDraw(true); }
}

// 切完立刻重画当前那一屏（三个界面都是事件驱动，不重画按钮不会变）
static void muteToggle() {
  g_mute = !g_mute;
  muteSave();
  if (!g_mute) beep(880, 60);      // 打开声音时给一声确认；关掉时本来就不该出声
  if (g_mode == GM_MENU)        menuDraw();
  else if (g_mode == GM_TETRIS) tetrisDraw(true);
  else                          bDraw(true);
}

void appGamesStart() {
  if (!PIECE_BUILT) tetrisBuildRot();
  randomSeed(micros());      // 用微秒级时间播种：方块序列别每次开机都一样
  bBestLoad();
  muteLoad();
  gameEnter(GM_MENU);
}

void appGamesLoop() {
  if (g_mode == GM_TETRIS) tetrisTick();
  // 菜单和 2048 都是纯事件驱动，这里不做任何事 —— 一帧都不重画
}

void appGamesTouch(int16_t x, int16_t y) {
  if (g_mode == GM_MENU) {
    if (hitBox(x, y, MMX, MMY, MMW, MMH)) { muteToggle(); return; }
    for (int i = 0; i < 2; i++)
      if (hitBox(x, y, mcX(i), MCY, MCW, MCH)) { gameEnter(i + 1); return; }
    return;
  }
  if (g_mode == GM_TETRIS) tetrisTouch(x, y);
  else                     bTouch(x, y);
}

void appGamesKey(uint8_t raw) {
  if (raw == 'm' || raw == 'M') { muteToggle(); return; }
  if (g_mode == GM_MENU) {
    if (raw == '1' || raw == '2') gameEnter(raw - '0');
    return;
  }
  if (g_mode == GM_TETRIS) tetrisKey(raw);
  else                     bKey(raw);
}

// 左滑 / 导航条返回：游戏里先回菜单，再滑一次才回桌面
bool appGamesBack() {
  if (g_mode == GM_MENU) return false;
  gameEnter(GM_MENU);
  return true;
}

void appGamesStop() {
  if (b_score > b_best) { b_best = b_score; bBestSave(); }
  g_mode = GM_MENU;
}

// 串口 `game` 命令：打印状态；`game <0|1|2>` 切到菜单 / 方块 / 2048（自测用）
void gameGoto(uint8_t m) { if (m <= GM_2048) gameEnter(m); }

// 串口 `game mute`：切换静音（只在游戏应用里调，会顺带重画当前界面）
void gameToggleMute() { muteToggle(); }

void gamePrintStatus() {
  Serial.printf("[game] mode=%s mute=%d\n",
                g_mode == GM_MENU ? "menu" : (g_mode == GM_TETRIS ? "tetris" : "2048"),
                (int)g_mute);
  Serial.printf("[game] tetris score=%lu lines=%lu level=%lu over=%d pause=%d next=%d\n",
                (unsigned long)t_score, (unsigned long)t_lines, (unsigned long)t_level,
                (int)t_over, (int)t_pause, (int)t_next);
  if (g_mode == GM_TETRIS) {
    Serial.printf("[game] 当前块 type=%d rot=%d px=%d py=%d 间隔=%lums\n",
                  (int)t_type, (int)t_rot, (int)t_px, (int)t_py,
                  (unsigned long)tetrisInterval());
    for (int y = 0; y < TH; y++) {                 // 盘面转储：'#'=已固定
      char row[TW + 1];
      for (int x = 0; x < TW; x++) row[x] = t_grid[y][x] ? '#' : '.';
      row[TW] = 0;
      Serial.printf("[game] %02d %s\n", y, row);
    }
  }
  Serial.printf("[game] 2048 score=%lu best=%lu over=%d win=%d\n",
                (unsigned long)b_score, (unsigned long)b_best, (int)b_over, (int)b_win);
  Serial.printf("[game] 盘面: %u %u %u %u / %u %u %u %u / %u %u %u %u / %u %u %u %u\n",
                b_grid[0][0], b_grid[0][1], b_grid[0][2], b_grid[0][3],
                b_grid[1][0], b_grid[1][1], b_grid[1][2], b_grid[1][3],
                b_grid[2][0], b_grid[2][1], b_grid[2][2], b_grid[2][3],
                b_grid[3][0], b_grid[3][1], b_grid[3][2], b_grid[3][3]);
}

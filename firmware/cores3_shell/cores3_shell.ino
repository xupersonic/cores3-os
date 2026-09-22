// CoreS3 OS —— 多应用外壳（L1 单固件应用壳）
//
// 主题：RETRO PIXEL / CYBERPUNK / TERMINAL（v0.2.0 起）
//
// 桌面六宫格 + 六个应用。顶部 36px 是导航条（蓝牙 / 电量 / 时间），
// 进入应用后导航条左侧变成返回按钮。
// 返回上一级有三种方式：点导航条左侧块 / 屏幕左缘向右滑 / 键盘 Esc。
// Keyboard3：桌面上按 1-6 直接启动应用。
//
// 串口命令：status / home / back / apps / launch <n> / unpair / bright <0-255> / sleep <秒, 0=常亮>
//           / focus <0-6> / rec <0|1> / clear <0|1|2> / reboot / rec <0|1>

#include "shell.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_partition.h>

// ─── 8x8 像素图标（每字节一行，bit7 = 最左像素）──────────────────────────
static const uint8_t ICON_KEYBOARD[8] = { 0x00,0x7E,0x42,0x7E,0x42,0x7E,0x00,0x00 };
static const uint8_t ICON_TOMATO[8]   = { 0x18,0x24,0x3C,0x7E,0x7E,0x7E,0x3C,0x00 };
// 游戏：十字方向键（d-pad）
static const uint8_t ICON_GAME[8]     = { 0x00,0x18,0x18,0x7E,0x7E,0x18,0x18,0x00 };
static const uint8_t ICON_WAVE[8]     = { 0x30,0x48,0x48,0x30,0x06,0x09,0x09,0x06 };
static const uint8_t ICON_GEAR[8]     = { 0x18,0x3C,0x7E,0x5A,0x5A,0x7E,0x3C,0x18 };
// 记事本：一张带横线的纸
static const uint8_t ICON_NOTE[8]     = { 0xFE,0x82,0xBA,0x82,0xBA,0x82,0xFE,0x00 };

// ─── 全局对象与状态 ───────────────────────────────────────────────────────
static M5Faces_Keyboard3 g_kb;

HijelHID_BLEKeyboard g_ble(DEVICE_NAME, "M5Stack", 100);

App g_apps[] = {
  { S_APP_CONTROLLER, C_CYAN,    ICON_KEYBOARD,
    appControllerStart, appControllerLoop, appControllerTouch, appControllerKey, appControllerStop, nullptr },
  { S_APP_NOTES,     C_LIME,    ICON_NOTE,
    appNoteStart,      appNoteLoop,      appNoteTouch,      appNoteKey,      appNoteStop,      nullptr },
  { S_APP_POMODORO,  C_AMBER,   ICON_TOMATO,
    appPomodoroStart,  appPomodoroLoop,  appPomodoroTouch,  appPomodoroKey,  appPomodoroStop,  nullptr },
  { S_APP_GAMES,     C_MAGENTA, ICON_GAME,
    appGamesStart,     appGamesLoop,     appGamesTouch,     appGamesKey,     appGamesStop,     appGamesBack },
  { S_APP_MUSIC,     C_VIOLET,  ICON_WAVE,
    appMusicStart,     appMusicLoop,     appMusicTouch,     appMusicKey,     appMusicStop,     nullptr },
  { S_APP_SETTINGS,  C_RED,     ICON_GEAR,
    appSettingsStart,  appSettingsLoop,  appSettingsTouch,  appSettingsKey,  appSettingsStop,  nullptr },
};
const uint8_t g_appCount = sizeof(g_apps) / sizeof(g_apps[0]);

const char* appName(uint8_t idx) { return S(g_apps[idx].nameId); }

int8_t   g_curApp     = -1;
bool     g_bleConn    = false;
bool     g_kbOk       = false;
uint8_t  g_brightness = 150;
uint16_t g_sleepSec   = 60;
uint8_t  g_focusMode  = FOCUS_MODE_DEFAULT;
uint16_t g_focusDelayMs = NEW_FOCUS_DELAY_MS;   // 新建后等多久再动手（串口 fdelay 可调）
uint8_t  g_recMode    = REC_MODE_DEFAULT;
uint8_t  g_clearMode  = CLEAR_MODE_DEFAULT;
uint32_t g_lastInput  = 0;
bool     g_dimmed     = false;

// ─── 桌面几何 ────────────────────────────────────────────────────────────
// 桌面网格：3 列 × 2 行，8 + 3*94 + 2*9 = 320，左右各留 8px。
// 时钟被记事本取代后仍是 6 个应用，所以 4 列的临时方案撤掉了，
// tile 回到 94px 宽 —— 英文应用名也能用全称（"CONTROLLER" 80px 装得下）。
static const int HGX = 8, HGY = 44, HTW = 94, HTH = 78, HGAP = 9;
static const int HOME_COLS = 3;

// ─── 公共能力实现 ────────────────────────────────────────────────────────
void setPxFont()    { M5.Display.setFont(&fonts::Font8x8C64); M5.Display.setTextSize(1); }
void setSmallFont() { setPxFont(); }
void setCnFont()    { M5.Display.setFont(&fonts::efontCN_16); M5.Display.setTextSize(1); }

// UTF-8 里 >= 0x80 的字节 = 非 ASCII（中文都在这个范围）
bool hasCJK(const char* s) {
  for (const char* p = s; *p; p++) if ((uint8_t)*p >= 0x80) return true;
  return false;
}
void setAutoFont(const char* s) { if (hasCJK(s)) setCnFont(); else setPxFont(); }
int  textHeight(const char* s)  { return hasCJK(s) ? 16 : 8; }

void drawCenterText(int cx, int y, int h, const char* s, uint32_t color) {
  setAutoFont(s);
  M5.Display.setTextColor(color);
  M5.Display.drawCenterString(s, cx, y + (h - textHeight(s)) / 2);
}

uint8_t batPercent() {
  int v = M5.Power.getBatteryLevel();
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  return (uint8_t)v;
}

// CRT 网格底纹：全局固定坐标（x,y 都取 16k+8），局部擦除也能补回来
static void drawGridAt(int x0, int y0, int w, int h) {
  for (int y = 8; y < 240; y += 16) {
    if (y < y0 || y >= y0 + h) continue;
    for (int x = 8; x < 320; x += 16) {
      if (x < x0 || x >= x0 + w) continue;
      M5.Display.fillRect(x, y, 1, 1, C_GRID);
    }
  }
}

void clearCanvas() { clearArea(0, CANVAS_Y, 320, CANVAS_H); }

void clearArea(int x, int y, int w, int h) {
  M5.Display.fillRect(x, y, w, h, C_BG);
  drawGridAt(x, y, w, h);
}

void beep(uint16_t freq, uint16_t ms) { M5.Speaker.tone((float)freq, ms); }

void wakeScreen() {
  if (g_dimmed) {
    M5.Display.setBrightness(g_brightness);
    g_dimmed = false;
  }
}

bool hitBox(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

// 像素图标：scale 是单个像素点的边长（3 → 24x24）
void drawPxIcon(int cx, int cy, const uint8_t* bmp, int scale, uint32_t color) {
  int ox = cx - 4 * scale, oy = cy - 4 * scale;
  for (int r = 0; r < 8; r++) {
    uint8_t b = bmp[r];
    for (int c = 0; c < 8; c++) {
      if (b & (0x80 >> c))
        M5.Display.fillRect(ox + c * scale, oy + r * scale, scale, scale, color);
    }
  }
}

// HUD 边框：1px 细框 + 四角 3px 实心角标
void drawFrame(int x, int y, int w, int h, uint32_t color) {
  M5.Display.drawRect(x, y, w, h, color);
  M5.Display.fillRect(x,         y,         3, 3, color);
  M5.Display.fillRect(x + w - 3, y,         3, 3, color);
  M5.Display.fillRect(x,         y + h - 3, 3, 3, color);
  M5.Display.fillRect(x + w - 3, y + h - 3, 3, 3, color);
}

// 块状进度条：每 6px 一块 + 2px 间隔，复古分段感
void drawProgressBar(int x, int y, int w, int h, float ratio, uint32_t color) {
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;
  M5.Display.drawRect(x, y, w, h, C_DARK);
  const int bw = 6, gap = 2;
  int inner = x + 2, avail = w - 4;
  int end   = inner + (int)(avail * ratio);
  for (int px = inner; px < inner + avail && px < end; px += bw + gap) {
    int seg = bw;
    if (px + seg > end) seg = end - px;
    if (seg > 0) M5.Display.fillRect(px, y + 2, seg, h - 4, color);
  }
}

// 终端列表行：label 暗色靠左，value 霓虹靠右（8x8 ASCII）
// label 是中文时用 16px 点阵、纯英文时用 8x8，所以基线要按实际字高算
void drawRow(int y, const char* label, const char* val, uint32_t accent) {
  setAutoFont(label);
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString(label, 12, y + (16 - textHeight(label)) / 2);
  setPxFont();
  M5.Display.setTextColor(accent);
  M5.Display.drawRightString(val, 308, y + 4);
}

// 标题行：`> TITLE`
void drawTitle(int y, const char* title, uint32_t accent) {
  setPxFont();
  M5.Display.setTextColor(accent);
  M5.Display.drawString(">", 12, y + 4);
  setAutoFont(title);
  M5.Display.setTextColor(accent);
  M5.Display.drawString(title, 26, y + (16 - textHeight(title)) / 2);
}

// 方角按键块（像素风，不用圆角）
void drawKeyBox(int x, int y, int w, int h, const char* label,
                uint32_t accent, bool filled) {
  M5.Display.fillRect(x, y, w, h, filled ? accent : C_PANEL);
  drawFrame(x, y, w, h, accent);
  setAutoFont(label);
  M5.Display.setTextColor(filled ? C_BG : accent);
  M5.Display.drawCenterString(label, x + w / 2,
                              y + (h - textHeight(label)) / 2);
}

// ─── 导航条 ──────────────────────────────────────────────────────────────
void drawStatusBar() {
  uint32_t ac = (g_curApp >= 0) ? g_apps[g_curApp].accent : C_GREEN;
  M5.Display.fillRect(0, 0, 320, CANVAS_Y, C_BAR_BG);
  M5.Display.fillRect(0, CANVAS_Y - 2, 320, 1, C_DARK);
  M5.Display.fillRect(0, CANVAS_Y - 1, 320, 1, ac);   // 霓虹分隔线

  const int ty = (CANVAS_Y - 16) / 2;   // 16px 中文垂直居中
  const int py = (CANVAS_Y - 8)  / 2;   // 8x8 ASCII 垂直居中

  if (g_curApp < 0) {
    setPxFont();
    M5.Display.setTextColor(g_bleConn ? C_GREEN : C_DIM);
    M5.Display.drawString(g_bleConn ? "BT:LINK" : "BT:WAIT", 12, py);
    setCnFont();
    M5.Display.setTextColor(C_DIM);
    M5.Display.drawCenterString("CoreS3 OS", 176, ty);
  } else {
    drawFrame(6, 4, 78, CANVAS_Y - 10, C_GREEN);
    setPxFont();
    M5.Display.setTextColor(C_GREEN);
    M5.Display.drawString("<", 16, py);
    const char* home = S(S_HOME);
    setAutoFont(home);
    M5.Display.setTextColor(C_GREEN);
    M5.Display.drawString(home, 30, ty + (16 - textHeight(home)) / 2);
    const char* nm = appName(g_curApp);
    setAutoFont(nm);
    M5.Display.setTextColor(ac);
    M5.Display.drawCenterString(nm, 186, ty + (16 - textHeight(nm)) / 2);
  }

  char right[24];
  auto t = M5.Rtc.getTime();
  if (t.hours < 0) snprintf(right, sizeof(right), "--:-- %d%%", batPercent());
  else snprintf(right, sizeof(right), "%02d:%02d %d%%", t.hours, t.minutes, batPercent());
  setPxFont();
  M5.Display.setTextColor(C_TEXT);
  M5.Display.drawRightString(right, 308, py);
}

// ─── 桌面 ────────────────────────────────────────────────────────────────
static void tileRect(uint8_t i, int& x, int& y) {
  x = HGX + (i % HOME_COLS) * (HTW + HGAP);
  y = HGY + (i / HOME_COLS) * (HTH + HGAP);
}

void drawHome() {
  M5.Display.fillScreen(C_BG);
  drawGridAt(0, 0, 320, 240);
  drawStatusBar();
  for (uint8_t i = 0; i < g_appCount; i++) {
    int x, y;
    tileRect(i, x, y);
    uint32_t ac = g_apps[i].accent;
    M5.Display.fillRect(x, y, HTW, HTH, C_PANEL);
    drawFrame(x, y, HTW, HTH, ac);
    setPxFont();                       // 左上角编号 [1]
    M5.Display.setTextColor(ac);
    char no[4];
    snprintf(no, sizeof(no), "[%d]", i + 1);
    M5.Display.drawString(no, x + 6, y + 5);
    drawPxIcon(x + HTW / 2, y + 34, g_apps[i].icon, 3, ac);
    const char* nm = appName(i);
    setAutoFont(nm);
    M5.Display.setTextColor(C_TEXT);
    // 标签在一个 16px 高的条里垂直居中（英文 8px 时下移 4px 才对齐）
    M5.Display.drawCenterString(nm, x + HTW / 2,
                                y + HTH - 22 + (16 - textHeight(nm)) / 2);
  }
  drawCenterText(160, 214, 16, S(S_HINT_HOME), C_DIM);
}

void launchApp(uint8_t idx) {
  if (idx >= g_appCount) return;
  if (g_curApp >= 0 && g_apps[g_curApp].onStop) g_apps[g_curApp].onStop();
  g_curApp = idx;
  clearCanvas();
  drawStatusBar();
  if (g_apps[idx].onStart) g_apps[idx].onStart();
  Serial.printf("[shell] launch -> %s\n", appName(idx));
}

void goHome() {
  if (g_curApp >= 0 && g_apps[g_curApp].onStop) g_apps[g_curApp].onStop();
  g_curApp = -1;
  drawHome();
  Serial.println("[shell] back to home");
}

// 「返回上一级」统一入口：先给应用一次机会（比如二级页返回），
// 应用不处理（nullptr 或返回 false）才回桌面。
void goBack() {
  if (g_curApp >= 0 && g_apps[g_curApp].onBack && g_apps[g_curApp].onBack()) {
    Serial.println("[shell] back within app");
    return;
  }
  goHome();
}

// ─── 输入 ────────────────────────────────────────────────────────────────
// 单击派发：导航条 / 桌面图标 / 应用画布
static void dispatchTap(int16_t x, int16_t y) {
  g_lastInput = millis();
  wakeScreen();

  if (y < CANVAS_Y) {
    // 应用内：导航条左半边（返回块 6..84 + 余量）都能返回
    if (g_curApp >= 0 && x < 110) goBack();
    return;
  }
  if (g_curApp < 0) {
    for (uint8_t i = 0; i < g_appCount; i++) {
      int tx, ty;
      tileRect(i, tx, ty);
      if (hitBox(x, y, tx, ty, HTW, HTH)) { launchApp(i); return; }
    }
    return;
  }
  if (g_apps[g_curApp].onTouch) g_apps[g_curApp].onTouch(x, y);
}

// 触摸总入口：按下 → 滑动（手势）→ 抬手（派发点击）
//
// 关键改动：点击从「按下即派发」改成「抬手才派发」。
// 否则按下瞬间就被应用消费掉了，手势根本没有介入的机会；
// 改成抬手派发后，按下后可以靠滑动取消这次点击（跟手机一样）。
static bool g_touchActive = false;   // 手指是否还按着
static bool g_touchBusy   = false;   // 本次触摸已被手势消费

static void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {                       // ① 按下：只记状态，不派发
    g_touchActive = true;
    g_touchBusy = false;
    g_lastInput = millis();
    wakeScreen();
    return;
  }
  if (!g_touchActive) return;

  if (!g_touchBusy && t.isFlicking()) {       // ② 滑动中：左缘右滑 = 返回
    int dx = t.distanceX(), dy = t.distanceY();
    if (g_curApp >= 0 &&
        t.base_x <= GEST_EDGE &&
        dx >= GEST_MIN_DX &&
        (dy < 0 ? -dy : dy) <= GEST_MAX_DY) {
      g_touchBusy = true;
      Serial.printf("[gest] back swipe: from(%d,%d) dx=%d dy=%d\n",
                    t.base_x, t.base_y, dx, dy);
      goBack();
      return;
    }
  }

  if (t.wasReleased()) {                      // ③ 抬手：没被手势吃掉就是点击
    g_touchActive = false;
    if (!g_touchBusy) dispatchTap(t.base_x, t.base_y);
  } else if (!t.isPressed()) {                // 兜底：状态已归零，别卡住
    g_touchActive = false;
  }
}

static void handleKeyboard() {
  if (!g_kbOk) return;
  g_kb.update();
  uint8_t raw = g_kb.getKey();
  if (raw == 0xFF) return;

  g_lastInput = millis();
  wakeScreen();

  if (raw == 0x1B) {                       // Esc = 返回上一级
    if (g_curApp >= 0) goBack();
    return;
  }
  if (g_curApp < 0) {                      // 桌面上数字键直接启动
    if (raw >= '1' && raw <= '9') {
      uint8_t idx = raw - '1';
      if (idx < g_appCount) launchApp(idx);
    }
    return;
  }
  if (g_apps[g_curApp].onKey) g_apps[g_curApp].onKey(raw);
}

// ─── RTC ─────────────────────────────────────────────────────────────────
// CoreS3 的 RTC（BM8563）没有后备电池，掉电 / 重烧之后时间会归零，
// 而设备自己没有任何对时来源（没有 Wi-Fi、BLE 那边也不给时间），
// 所以只能靠串口从 Mac 喂进来。配套脚本 tools/rtc.sh，
// fw.sh 每次烧完也会顺手同步一次。
// 星期几由日期算出来（Sakamoto 算法），不用 Mac 端传 —— 少一个出错点。
static int rtcWeekday(int y, int m, int d) {   // 0=周日 … 6=周六
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void rtcPrint() {
  auto t = M5.Rtc.getTime();
  auto d = M5.Rtc.getDate();
  if (t.hours < 0 || d.year <= 0 || d.year > 2099) {
    Serial.println("[rtc] <unset>  用法: time 2026-09-20 23:04:08");
  } else {
    Serial.printf("[rtc] %04d-%02d-%02d %02d:%02d:%02d wd=%d\n",
                  (int)d.year, (int)d.month, (int)d.date,
                  (int)t.hours, (int)t.minutes, (int)t.seconds, (int)d.weekDay);
  }
}

// RTC 里的时间能不能信（v0.6.2）：没有后备电池，掉电 / 重烧会归零 ——
// 这时要么年份离谱、要么 hours < 0。判断不成立就别往笔记里烙时间戳，
// 宁可让 Bridge 记成「送达时刻」，也不能记成 2000-01-01。
bool rtcIsValid() {
  auto t = M5.Rtc.getTime();
  auto d = M5.Rtc.getDate();
  return (d.year >= 2000 && d.year <= 2099 && d.month >= 1 && d.month <= 12 &&
          d.date >= 1 && d.date <= 31 && t.hours >= 0 && t.hours <= 23 &&
          t.minutes >= 0 && t.minutes <= 59);
}

// 真正写 RTC 的动作：串口 `time` 命令和 BLE 自动对时（v0.6.2）共用一条路。
void rtcSetYMDHMS(int Y, int Mo, int D, int H, int Mi, int S) {
  m5::rtc_date_t d((int16_t)Y, (int8_t)Mo, (int8_t)D, (int8_t)rtcWeekday(Y, Mo, D));
  m5::rtc_time_t t((int8_t)H, (int8_t)Mi, (int8_t)S);
  M5.Rtc.setDateTime(&d, &t);
  M5.Rtc.setSystemTimeFromRtc();     // 顺手把 ESP32 自己的系统时间也对上
  Serial.printf("[rtc] set %04d-%02d-%02d %02d:%02d:%02d wd=%d\n",
                Y, Mo, D, H, Mi, S, rtcWeekday(Y, Mo, D));
  drawStatusBar();                   // 导航条右上角就是时间，立刻刷新
  if (g_curApp < 0) drawHome();
  else if (g_apps[g_curApp].onStart) g_apps[g_curApp].onStart();
}

static void rtcSet(const char* arg) {
  char a1[24] = {0}, a2[24] = {0};
  sscanf(arg, "%23s %23s", a1, a2);
  int Y = 0, Mo = 0, D = 0, H = 0, Mi = 0, S = 0;
  bool ok = (sscanf(a1, "%d-%d-%d", &Y, &Mo, &D) == 3);
  if (ok && a2[0]) ok = (sscanf(a2, "%d:%d:%d", &H, &Mi, &S) == 3);
  if (!ok || Y < 2000 || Y > 2099 || Mo < 1 || Mo > 12 || D < 1 || D > 31 ||
      H < 0 || H > 23 || Mi < 0 || Mi > 59 || S < 0 || S > 59) {
    Serial.printf("[rtc] bad \"%s\"  用法: time 2026-09-20 23:04:08\n", arg);
    return;
  }
  rtcSetYMDHMS(Y, Mo, D, H, Mi, S);
}

static void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd == "status") {
      Serial.printf("[status] app=%d ble=%d kb=%d bat=%d bright=%d sleep=%us lang=%s rec=%s clear=%s focus=%s delay=%ums\n",
                  g_curApp, (int)g_bleConn, (int)g_kbOk,
                  batPercent(), g_brightness, g_sleepSec,
                  g_lang == LANG_EN ? "en" : "cn",
                  g_recMode == REC_MODE_WB ? "WB(Cmd+D)" : "SYS(dictation)",
                  clearModeText(), focusModeText(), (unsigned)g_focusDelayMs);
  } else if (cmd == "home") {
    goHome();
  } else if (cmd == "back") {
    goBack();
  } else if (cmd == "apps") {
    for (uint8_t i = 0; i < g_appCount; i++)
      Serial.printf("  %d %s\n", i, appName(i));
  } else if (cmd.startsWith("lang")) {          // lang [cn|en]，不带参数 = 切换
    String arg = cmd.substring(4);
    arg.trim();
    uint8_t l;
    if (arg == "cn" || arg == "zh")  l = LANG_CN;
    else if (arg == "en")            l = LANG_EN;
    else                             l = (g_lang == LANG_CN) ? LANG_EN : LANG_CN;
    langSet(l);
    drawStatusBar();
    if (g_curApp < 0) drawHome();
    else if (g_apps[g_curApp].onStart) g_apps[g_curApp].onStart();
  } else if (cmd.startsWith("launch")) {
    int n = cmd.substring(6).toInt();
    launchApp((uint8_t)n);
  } else if (cmd == "unpair") {
    Serial.println("[cmd] restart advertising");
    g_ble.end();
    delay(300);
    g_ble.begin();
  } else if (cmd.startsWith("bright")) {
    g_brightness = (uint8_t)constrain(cmd.substring(6).toInt(), 8, 255);
    M5.Display.setBrightness(g_brightness);
    g_dimmed = false;
    Serial.printf("[cmd] brightness=%d\n", g_brightness);
  } else if (cmd.startsWith("sleep")) {
    g_sleepSec = (uint16_t)cmd.substring(5).toInt();
    Serial.printf("[cmd] sleep=%us\n", g_sleepSec);
  } else if (cmd.startsWith("focus")) {          // focus [0-7] = 新建后聚焦方式（见 config.h）
    focusModeSave((uint8_t)constrain(cmd.substring(5).toInt(), 0, FOCUS_MODE_COUNT - 1));
    Serial.printf("[cmd] focusMode=%d (%s)\n", (int)g_focusMode, focusModeText());
  } else if (cmd.startsWith("fdelay")) {         // fdelay <ms> = 新建后等多久再聚焦
    focusDelaySave((uint16_t)constrain(cmd.substring(6).toInt(),
                                       FOCUS_DELAY_MIN_MS, FOCUS_DELAY_MAX_MS));
    Serial.printf("[cmd] focusDelay=%ums\n", (unsigned)g_focusDelayMs);
  } else if (cmd.startsWith("rec")) {            // rec [0|1] = 录音方式 0=WB(Cmd+D) 1=系统听写
    int m = cmd.substring(3).toInt();
    recModeSave((m == REC_MODE_SYS) ? REC_MODE_SYS : REC_MODE_WB);
    Serial.printf("[cmd] recMode=%s\n",
                  g_recMode == REC_MODE_WB ? "WB(Cmd+D)" : "SYS(dictation)");
  } else if (cmd.startsWith("clear")) {          // clear [0|1|2] = 取消键
    int m = cmd.substring(5).toInt();            // 0=ESC 1=WIPE 2=ESC+WIPE(默认)
    clearModeSave((m == CLEAR_MODE_ESC)  ? CLEAR_MODE_ESC  :
                  (m == CLEAR_MODE_WIPE) ? CLEAR_MODE_WIPE : CLEAR_MODE_ESC_WIPE);
    Serial.printf("[cmd] clearMode=%s\n", clearModeText());
  // ── 以下是一组「焦点探针」调试命令 ────────────────────────────────────
  // 存在的理由：WorkBuddy 的 send-message / Cmd+A 都只在【焦点在输入框内】时生效，
  // 而焦点到底在哪，设备侧完全看不到（BLE 键盘没有回传）。只能靠这几个命令
  // 从串口手动发键、再在 Mac 上截图看字打进去了没有，把「猜」变成「测」。
  } else if (cmd.startsWith("txt ")) {           // txt <ascii>  直接打字
    String s = cmd.substring(4);
    s.trim();
    g_ble.print(s.c_str());
    Serial.printf("[cmd] typed \"%s\"\n", s.c_str());
  } else if (cmd == "new") {                     // 只发 Cmd+N，不带任何聚焦补按
    g_ble.tap(NEW_CHAT_KEY, NEW_CHAT_MODS);
    Serial.println("[cmd] Cmd+N (no nudge)");
  } else if (cmd.startsWith("stab")) {           // stab <n>  Shift+Tab xN（反向遍历）
    int n = constrain(cmd.substring(4).toInt(), 1, 12);
    for (int i = 0; i < n; i++) { g_ble.tap(KEY_TAB, KEY_MOD_LSHIFT); delay(NEW_FOCUS_TAB_GAP); }
    Serial.printf("[cmd] Shift+Tab x%d\n", n);
  } else if (cmd.startsWith("tab")) {            // tab <n>   Tab xN（正向遍历）
    int n = constrain(cmd.substring(3).toInt(), 1, 12);
    for (int i = 0; i < n; i++) { g_ble.tap(KEY_TAB); delay(NEW_FOCUS_TAB_GAP); }
    Serial.printf("[cmd] Tab x%d\n", n);
  } else if (cmd == "wake") {                    // Cmd+D 双击（WAKE 档的聚焦动作）
    g_ble.tap(WB_VOICE_KEY, WB_VOICE_MODS);
    delay(FOCUS_WAKE_GAP_MS);
    g_ble.tap(WB_VOICE_KEY, WB_VOICE_MODS);
    Serial.println("[cmd] wake: Cmd+D x2");
  } else if (cmd == "newq") {                    // Cmd+Shift+N：快速问答型新对话
    g_ble.tap(NEW_CHAT_KEY, KEY_MOD_LGUI | KEY_MOD_LSHIFT);
    Serial.println("[cmd] Cmd+Shift+N (quick new)");
  } else if (cmd == "prev") {                    // Cmd+[：上一个对话
    g_ble.tap(PREV_CHAT_KEY, PREV_CHAT_MODS);
    Serial.println("[cmd] Cmd+[ (prev)");
  } else if (cmd == "next") {                    // Cmd+]：下一个对话
    g_ble.tap(NEXT_CHAT_KEY, NEXT_CHAT_MODS);
    Serial.println("[cmd] Cmd+] (next)");
  } else if (cmd == "esc") {
    g_ble.tap(KEY_ESCAPE);
    Serial.println("[cmd] Esc");
  } else if (cmd == "ret") {
    g_ble.tap(KEY_RETURN);
    Serial.println("[cmd] Return");
  } else if (cmd == "copy") {                    // Cmd+A + Cmd+C：把当前选区读出来
    // 探针用：焦点在输入框里的话，剪贴板会是输入框的内容；
    // 焦点在 body 上是 Chromium 的 select-all，拷到的是整页文字。两者一眼可分。
    g_ble.tap(CLEAR_WIPE_KEY, CLEAR_WIPE_MODS);
    delay(CLEAR_WIPE_GAP_MS);
    g_ble.tap(KEY_C, KEY_MOD_LGUI);
    Serial.println("[cmd] copy (Cmd+A, Cmd+C)");
  } else if (cmd.startsWith("act")) {            // act rec|send|cancel|new|prev|next
    controllerAct(cmd.substring(3).c_str());     // 跑固件真实按键路径（不是手动拼键）
  } else if (cmd.startsWith("key")) {            // key <0-255> 模拟 Keyboard3 物理键
    // 走的就是 handleKeyboard() 里那条的同一条 onKey 派发路径，所以能验证
    // 「键码 → 动作」映射表本身对不对（例：key 127 应当等于按了 del 键）。
    int raw = constrain(cmd.substring(3).toInt(), 0, 255);
    Serial.printf("[cmd] key raw=0x%02X (%d)\n", raw, raw);
    if (g_curApp >= 0 && g_apps[g_curApp].onKey) g_apps[g_curApp].onKey((uint8_t)raw);
    else Serial.println("[cmd] no app open (onKey 只在应用内派发)");
  } else if (cmd.startsWith("touch")) {          // touch <x> <y> 模拟一次点屏
    // 跟 M5.Touch 回调走同一条 onTouch 派发路径，所以能验证「按钮热区对不对」，
    // 不用真的拿手指戳屏幕（v0.7.1 加静音开关时用这个验了三个热区）。
    String tp = cmd.substring(5);
    tp.trim();
    int sp = tp.indexOf(' ');
    int tx = (sp > 0) ? tp.substring(0, sp).toInt() : -1;
    int ty = (sp > 0) ? tp.substring(sp + 1).toInt() : -1;
    Serial.printf("[cmd] touch (%d,%d)\n", tx, ty);
    if (tx < 0 || ty < 0) Serial.println("[cmd] 用法：touch <x> <y>");
    else if (g_curApp >= 0 && g_apps[g_curApp].onTouch) g_apps[g_curApp].onTouch((int16_t)tx, (int16_t)ty);
    else Serial.println("[cmd] no app open (onTouch 只在应用内派发)");
  } else if (cmd == "wipe") {                    // Cmd+A + Backspace
    g_ble.tap(CLEAR_WIPE_KEY, CLEAR_WIPE_MODS);
    delay(CLEAR_WIPE_GAP_MS);
    g_ble.tap(KEY_BACKSPACE);
    Serial.println("[cmd] wipe (Cmd+A, Backspace)");
  } else if (cmd == "blur") {                    // Cmd+Tab 往返：窗口失焦再聚焦
    // 用途：WorkBuddy 里有个 useFocusInputOnWindowFocus 钩子——
    // window 重新获得 focus、且 activeElement 是 body 时，它会自动 focusInput()。
    // 所以「切走再切回来」理论上能拿到焦点，且不依赖 Tab 顺序。
    g_ble.tap(KEY_TAB, KEY_MOD_LGUI);
    delay(400);
    g_ble.tap(KEY_TAB, KEY_MOD_LGUI);
    Serial.println("[cmd] blur: Cmd+Tab x2");
  } else if (cmd == "spot") {                    // Cmd+Space + Esc：另一种失焦往返
    g_ble.tap(KEY_SPACE, KEY_MOD_LGUI);
    delay(400);
    g_ble.tap(KEY_ESCAPE);
    Serial.println("[cmd] spot: Cmd+Space, Esc");
  } else if (cmd == "music") {                   // 打印当前收到的歌曲状态与封面进度
    musicPrintStatus();
  } else if (cmd == "fs2") {                     // 底层体检：裸分区擦写 + 各种 open 写法
    const esp_partition_t* p2 = esp_partition_find_first(
        (esp_partition_type_t)0x01, (esp_partition_subtype_t)0x83, nullptr);
    Serial.printf("[fs2] littlefs 分区=%s\n", p2 ? "找到" : "找不到");
    if (p2) {
      Serial.printf("[fs2] @0x%06lX size=%luKB encrypted=%d\n",
                    (unsigned long)p2->address, (unsigned long)(p2->size / 1024), (int)p2->encrypted);
      uint8_t buf[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
      esp_err_t e1 = esp_partition_erase_range(p2, 0, 4096);
      esp_err_t e2 = esp_partition_write(p2, 0, buf, sizeof(buf));
      uint8_t rb[8] = { 0 };
      esp_err_t e3 = esp_partition_read(p2, 0, rb, sizeof(rb));
      Serial.printf("[fs2] erase=%d write=%d read=%d (%02X %02X)\n",
                    (int)e1, (int)e2, (int)e3, rb[0], rb[1]);
    }
    Serial.printf("[fs2] total=%lluKB\n", (unsigned long long)(LittleFS.totalBytes() / 1024));
    if (LittleFS.totalBytes() == 0) {
      Serial.printf("[fs2] begin=%d\n", (int)LittleFS.begin(true, NOTE_FS_MOUNT, 10, "lfs"));
    }
    Serial.printf("[fs2] existsRoot=%d\n", (int)LittleFS.exists("/littlefs"));
    File a1 = LittleFS.open("/littlefs/_t.txt", FILE_WRITE);
    Serial.printf("[fs2] open(\"/littlefs/_t.txt\", w)=%d\n", (int)(bool)a1);
    if (a1) { a1.write((const uint8_t*)"abc", 3); a1.close(); }
    File a2 = LittleFS.open("/_t.txt", FILE_WRITE);
    Serial.printf("[fs2] open(\"/_t.txt\", w)=%d\n", (int)(bool)a2);
    if (a2) { a2.close(); }
    Serial.printf("[fs2] mkdir=%d rm=%d\n",
                  (int)LittleFS.mkdir("/littlefs/d"), (int)LittleFS.rmdir("/littlefs/d"));
    File d2 = LittleFS.open("/littlefs");
    Serial.printf("[fs2] openRoot=%d isDir=%d\n", (int)(bool)d2, (int)(d2 && d2.isDirectory()));
    if (d2 && d2.isDirectory()) {
      File e;
      int n = 0;
      while ((e = d2.openNextFile())) { Serial.printf("[fs2] entry %s\n", e.name()); e.close(); n++; }
      d2.close();
      Serial.printf("[fs2] 共 %d 项\n", n);
    }

  } else if (cmd == "fs") {                      // flash 分区体检：分区信息 / 挂载 / 写测试 / 重新格式化
    // 分区 subtype 0x83 = littlefs（0x82 是 fat）。label 在 partitions.csv 里写的是 "lfs"。
    // 找不到分区时把整张表打出来，一眼能看出是不是 CSV 没生效 / subtype 写错。
    const esp_partition_t* pp = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "lfs");
    if (!pp) {
      Serial.println("[fs] ✗ 找不到 data/littlefs 分区（label=lfs），现有 data 分区：");
      esp_partition_iterator_t it = esp_partition_find(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
      for (; it; it = esp_partition_next(it)) {
        const esp_partition_t* q = esp_partition_get(it);
        Serial.printf("[fs]   @0x%06lX size=%luKB sub=0x%02X label=%s\n",
                      (unsigned long)q->address, (unsigned long)(q->size / 1024),
                      (unsigned)q->subtype, q->label);
      }
      esp_partition_iterator_release(it);
    } else {
      Serial.printf("[fs] part @0x%06lX size=%luKB sub=0x%02X label=%s\n",
                    (unsigned long)pp->address, (unsigned long)(pp->size / 1024),
                    (unsigned)pp->subtype, pp->label);
    }
    Serial.printf("[fs] total=%lluKB used=%lluKB\n",
                  (unsigned long long)(LittleFS.totalBytes() / 1024),
                  (unsigned long long)(LittleFS.usedBytes() / 1024));
    if (LittleFS.totalBytes() == 0) {
      Serial.printf("[fs] 未挂载 → begin=%d\n", (int)LittleFS.begin(true, NOTE_FS_MOUNT, 10, "lfs"));
    }
    String tp = String(NOTE_FS_ROOT) + "_t.txt";
    File ff = LittleFS.open(tp, FILE_WRITE);
    Serial.printf("[fs] openWrite=%d\n", (int)(bool)ff);
    if (ff) {
      size_t w = ff.write((const uint8_t*)"abc", 3);
      ff.close();
      Serial.printf("[fs] wrote=%u exists=%d\n", (unsigned)w, (int)LittleFS.exists(tp));
      File fr = LittleFS.open(tp, FILE_READ);
      if (fr) { Serial.printf("[fs] readBack=%s\n", fr.readString().c_str()); fr.close(); }
      LittleFS.remove(tp);
      // 顺便列一下根目录，确认队列文件名能扫到
      File d = LittleFS.open(NOTE_FS_ROOT);
      if (d && d.isDirectory()) {
        File e;
        while ((e = d.openNextFile())) { Serial.printf("[fs] entry %s\n", e.name()); e.close(); }
        d.close();
      }
    } else {
      Serial.println("[fs] 写不了 → 尝试 format() 后重新挂载");
      Serial.printf("[fs] format=%d\n", (int)LittleFS.format());
      Serial.printf("[fs] rebegin=%d\n", (int)LittleFS.begin(false, NOTE_FS_MOUNT, 10, "lfs"));
      File f2 = LittleFS.open(tp, FILE_WRITE);
      Serial.printf("[fs] openWrite2=%d\n", (int)(bool)f2);
      if (f2) { f2.write((const uint8_t*)"abc", 3); f2.close(); LittleFS.remove(tp); }
    }

  } else if (cmd.startsWith("note")) {           // note           打印记事本状态
    String na = cmd.substring(4);                // note push <文本>  走真实发送路径
    na.trim();
    if (na.startsWith("push")) {
      String nt = na.substring(4);
      nt.trim();
      if (nt.length() == 0) Serial.println("[cmd] 用法: note push <文本>");
      else {
        Serial.printf("[cmd] push %u 字节\n", (unsigned)nt.length());
        noteSerialPush(nt.c_str());
      }
    } else if (na.startsWith("type")) {
      // note type <文本>  —— 逐字符喂给 appNoteKey()，再补一个 ALT+S(155)。
      // 这条走的是「真键盘 → 编辑缓冲 → 发送」全链路，
      // 不用手按 Keyboard3 也能验证按键映射（含 0x08/0x7F 退格、0x0D 换行）。
      String nt = na.substring(4);
      nt.trim();
      if (nt.length() == 0) Serial.println("[cmd] 用法: note type <文本>");
      else {
        for (unsigned i = 0; i < nt.length(); i++) appNoteKey((uint8_t)nt[i]);
        appNoteKey(155);                     // ALT+S = 发送
        Serial.printf("[cmd] 已模拟按键 %u 个 + ALT+S\n", (unsigned)nt.length());
      }
    } else {
      notePrintStatus();
    }
  } else if (cmd.startsWith("game")) {           // game           打印两个游戏的状态
    String ga = cmd.substring(4);                // game <0|1|2>   0=菜单 1=方块 2=2048
    ga.trim();                                   // game mute      切静音
    if (ga.length() == 0) gamePrintStatus();
    else if (ga == "mute" || ga == "m") {
      if (g_curApp >= 0 && g_apps[g_curApp].onKey == appGamesKey) gameToggleMute();
      else Serial.println("[cmd] 先 launch 3 进游戏（静音开关只在游戏应用里）");
      gamePrintStatus();
    } else {
      int m = constrain(ga.toInt(), 0, 2);
      if (g_curApp >= 0 && g_apps[g_curApp].onKey == appGamesKey) gameGoto((uint8_t)m);
      else Serial.println("[cmd] 先 launch 3 进游戏再切（game 只在游戏应用里生效）");
    }
  } else if (cmd.startsWith("mprev")) {          // 不碰屏幕直接发媒体键（自测用）
    if (g_curApp >= 0) g_apps[g_curApp].onKey('1');
  } else if (cmd.startsWith("time")) {           // time [YYYY-MM-DD HH:MM:SS]
    String arg = cmd.substring(4);               // 不带参数 = 打印当前 RTC
    arg.trim();
    if (arg.length() == 0) rtcPrint();
    else                   rtcSet(arg.c_str());
  } else if (cmd == "reboot") {                  // 烧录后统一走这条来重启，比拔电稳
    Serial.println("[cmd] rebooting...");
    Serial.flush();
    delay(50);
    ESP.restart();
  }
}

// 新建后的聚焦方式也存 NVS（跟语言同一个 namespace）
void focusModeSave(uint8_t n) {
  g_focusMode = (n >= FOCUS_MODE_COUNT) ? FOCUS_MODE_DEFAULT : n;
  Preferences p;
  p.begin("cores3", false);
  p.putUChar("fmode", g_focusMode);
  p.end();
}

void focusModeLoad() {
  Preferences p;
  p.begin("cores3", true);
  uint8_t v = p.getUChar("fmode", FOCUS_MODE_DEFAULT);
  p.end();
  g_focusMode = (v >= FOCUS_MODE_COUNT) ? FOCUS_MODE_DEFAULT : v;
}

// 设置页上显示的名字：必须纯 ASCII，中文模式下会切 16px 点阵，宽了放不下
const char* focusModeText() {
  switch (g_focusMode) {
    case FOCUS_S_TAB1: return "S-TAB1";
    case FOCUS_S_TAB2: return "S-TAB2";
    case FOCUS_S_TAB3: return "S-TAB3";
    case FOCUS_TAB1:   return "TAB1";
    case FOCUS_TAB2:   return "TAB2";
    case FOCUS_TAB3:   return "TAB3";
    case FOCUS_WAKE:   return "WAKE";
    default:           return "OFF";
  }
}

void focusDelaySave(uint16_t ms) {
  g_focusDelayMs = constrain((int)ms, FOCUS_DELAY_MIN_MS, FOCUS_DELAY_MAX_MS);
  Preferences p;
  p.begin("cores3", false);
  p.putUShort("fdelay", g_focusDelayMs);
  p.end();
}

void focusDelayLoad() {
  Preferences p;
  p.begin("cores3", true);
  uint16_t v = p.getUShort("fdelay", NEW_FOCUS_DELAY_MS);
  p.end();
  g_focusDelayMs = constrain((int)v, FOCUS_DELAY_MIN_MS, FOCUS_DELAY_MAX_MS);
}

void recModeSave(uint8_t m) {
  g_recMode = (m == REC_MODE_SYS) ? REC_MODE_SYS : REC_MODE_WB;
  Preferences p;
  p.begin("cores3", false);
  p.putUChar("recmode", g_recMode);
  p.end();
}

void recModeLoad() {
  Preferences p;
  p.begin("cores3", true);
  uint8_t v = p.getUChar("recmode", REC_MODE_DEFAULT);
  p.end();
  g_recMode = (v == REC_MODE_SYS) ? REC_MODE_SYS : REC_MODE_WB;
}

void clearModeSave(uint8_t m) {
  g_clearMode = (m == CLEAR_MODE_ESC)       ? CLEAR_MODE_ESC  :
                (m == CLEAR_MODE_ESC_WIPE)  ? CLEAR_MODE_ESC_WIPE : CLEAR_MODE_WIPE;
  Preferences p;
  p.begin("cores3", false);
  p.putUChar("clrmode", g_clearMode);
  p.end();
}

void clearModeLoad() {
  Preferences p;
  p.begin("cores3", true);
  uint8_t v = p.getUChar("clrmode", CLEAR_MODE_DEFAULT);
  p.end();
  g_clearMode = (v == CLEAR_MODE_ESC)      ? CLEAR_MODE_ESC  :
                (v == CLEAR_MODE_ESC_WIPE) ? CLEAR_MODE_ESC_WIPE : CLEAR_MODE_WIPE;
}

const char* clearModeText() {
  switch (g_clearMode) {
    case CLEAR_MODE_ESC:      return "ESC";
    case CLEAR_MODE_ESC_WIPE: return "ESC+WIPE";
    default:                  return "WIPE";
  }
}

// ─── setup / loop ────────────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(g_brightness);
  M5.Display.fillScreen(C_BG);
  M5.Touch.setFlickThresh(GEST_FLICK_THRESH);   // 手势灵敏度：小于它的抖动仍算点击

  langInit();                                   // 语言从 NVS 读回（掉电不丢）
  focusModeLoad();                              // 新建后的聚焦方式同理
  focusDelayLoad();                             // 聚焦前的等待同理
  recModeLoad();                                // 录音方式同理
  clearModeLoad();                              // 取消键方式同理

  setPxFont();
  M5.Display.setTextColor(C_GREEN);
  M5.Display.drawString("> BOOTING CoreS3 OS", 12, 8);
  setCnFont();
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawCenterString("SYSTEM INIT", 160, 108);

  Serial.println("\n[shell] CoreS3 OS v" FW_VERSION);

  uint8_t lvl = batPercent();
  g_ble.setBatteryLevel(lvl);
  Serial.printf("[sys] battery=%d%% flash=%uMB psram=%uKB\n",
                lvl, (unsigned)(ESP.getFlashChipSize() / 1024 / 1024),
                (unsigned)(ESP.getPsramSize() / 1024));

  m5faces_err_t e = g_kb.begin(&M5.In_I2C);
  g_kbOk = (e == M5FACES_OK);
  Serial.printf("[kb] begin -> %d (ok=%d)\n", (int)e, (int)g_kbOk);

  g_ble.setLogLevel(HIDLogLevel::Normal);
  g_ble.begin();
  Serial.println("[ble] advertising as " DEVICE_NAME);

  // 在 HID 所在的同一个 NimBLE server 上再挂一个自定义服务，
  // 用于收 Mac 端推来的歌曲元数据 / 封面（详见 app_music.ino 顶部说明）。
  musicGattInit();
  // 记事本那条是反向的（设备 notify、Mac 订阅），同样挂在 server 上
  noteGattInit();

  g_lastInput = millis();
  drawHome();
  Serial.println("[shell] ready");
}

void loop() {
  M5.update();

  // 自定义 GATT 服务（音乐元数据）需要 Mac 端的 Bridge 程序再连一条，
  // HID 那条是系统连的、跟它不是同一个 client。而 NimBLE 默认「一连上就停广播」，
  // 停了之后 Mac 端 CoreBluetooth 就扫不到设备了 —— 所以周期性地把广播拉起来，
  // 让第二路连接有机会建上。已连接时重复调用是幂等的，不会踢掉现有连接。
  static uint32_t lastAdv = 0;
  if (millis() - lastAdv > 15000) {
    lastAdv = millis();
    NimBLEDevice::startAdvertising();
  }

  bool nowConn = g_ble.isConnected();
  if (nowConn != g_bleConn) {
    g_bleConn = nowConn;
    Serial.printf("[ble] connected=%d\n", (int)g_bleConn);
    g_ble.setBatteryLevel(batPercent());
    drawStatusBar();
    if (g_curApp >= 0 && g_apps[g_curApp].onStart) g_apps[g_curApp].onStart();
  }

  handleTouch();
  handleKeyboard();
  handleSerial();

  if (g_curApp >= 0 && g_apps[g_curApp].onLoop) g_apps[g_curApp].onLoop();

  static uint32_t lastBar = 0;
  if (millis() - lastBar > 5000) {
    lastBar = millis();
    static uint8_t lastBat = 255;
    uint8_t b = batPercent();
    if (b != lastBat) { lastBat = b; g_ble.setBatteryLevel(b); }
    drawStatusBar();
  }

  if (g_sleepSec > 0 && !g_dimmed &&
      millis() - g_lastInput > (uint32_t)g_sleepSec * 1000UL) {
    M5.Display.setBrightness(8);
    g_dimmed = true;
  }

  delay(20);
}

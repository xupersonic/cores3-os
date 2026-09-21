// 应用 6：设置（亮度 / 语言 / 自动休眠 / 蓝牙广播 / 关于）
// 行式布局：左边标签，右边方角按键块（无圆角，像素风）
//
// 屏幕只有 240px 高、导航条吃掉 36px，加语言这一行后共 5 行，
// 所以行高从 46 压到 40、按钮高从 38 压到 32，最后一行的「关于」才放得下。

#include "shell.h"

static const uint16_t SLEEP_OPTS[4] = { 30, 60, 120, 0 };

// 又加了一行「录音方式」，共 7 行，所以行高 34→28、按键 28→24 才放得下
// （画布可用高度 = 240 - 36 = 204；40 + 7*28 = 236，刚好贴底）
static const int SET_Y0 = 40, SET_ROW_H = 28, SET_BTN_H = 24;
static const int SET_BX = 176, SET_BW = 136;
static const int SET_MIN_W = 40, SET_VAL_W = 44, SET_PLUS_W = 44;

enum { ROW_BRIGHT, ROW_LANG, ROW_SLEEP, ROW_REC,
       ROW_FOCUS, ROW_BT, ROW_ABOUT, ROW_COUNT };

static int setRowY(int i) { return SET_Y0 + i * SET_ROW_H; }
static int setBtnY(int i) { return setRowY(i) + (SET_ROW_H - SET_BTN_H) / 2; }

static uint8_t sleepIndex() {
  for (uint8_t i = 0; i < 4; i++) if (SLEEP_OPTS[i] == g_sleepSec) return i;
  return 1;
}

// 左列标签：中文用 16px 点阵、英文用 8x8，基线按实际字高算才对齐
static void setLabel(int i, const char* s, uint32_t color) {
  setAutoFont(s);
  M5.Display.setTextColor(color);
  M5.Display.drawString(s, 12, setRowY(i) + (SET_ROW_H - textHeight(s)) / 2);
}

static void drawSettings() {
  clearCanvas();

  clearArea(0, 38, 320, 20);
  drawTitle(40, S(S_SETTINGS_TITLE), C_RED);

  // 行 0：亮度 [-] [值] [+]
  setLabel(ROW_BRIGHT, S(S_BRIGHTNESS), C_TEXT);
  drawKeyBox(SET_BX,                             setBtnY(ROW_BRIGHT),
             SET_MIN_W,  SET_BTN_H, "-", C_RED, false);
  drawKeyBox(SET_BX + SET_MIN_W + 4,             setBtnY(ROW_BRIGHT),
             SET_VAL_W,  SET_BTN_H, String(g_brightness).c_str(), C_DIM, false);
  drawKeyBox(SET_BX + SET_MIN_W + 8 + SET_VAL_W, setBtnY(ROW_BRIGHT),
             SET_PLUS_W, SET_BTN_H, "+", C_RED, false);

  // 行 1：语言（点击在 简中 / ENGLISH 之间切换，立即重绘全界面）
  setLabel(ROW_LANG, S(S_LANGUAGE), C_TEXT);
  drawKeyBox(SET_BX, setBtnY(ROW_LANG), SET_BW, SET_BTN_H,
             S(g_lang == LANG_CN ? S_LANG_CN : S_LANG_EN), C_CYAN, false);

  // 行 2：自动休眠（点击循环切换，共 4 档，标签在 i18n 表里连续）
  setLabel(ROW_SLEEP, S(S_AUTO_SLEEP), C_TEXT);
  drawKeyBox(SET_BX, setBtnY(ROW_SLEEP), SET_BW, SET_BTN_H,
             S(S_SLEEP_30S + sleepIndex()), C_RED, false);

  // 行 3：录音方式（WB 内置 Cmd+D / 系统听写，写入 NVS）
  setLabel(ROW_REC, S(S_REC_MODE), C_TEXT);
  drawKeyBox(SET_BX, setBtnY(ROW_REC), SET_BW, SET_BTN_H,
             S(g_recMode == REC_MODE_WB ? S_REC_WB : S_REC_SYS),
             g_recMode == REC_MODE_WB ? C_GREEN : C_AMBER, false);

  // 行 4：新建后怎么把焦点挪回输入框（OFF / S-TAB1-3 / TAB1-3，纯 ASCII 短名）
  // send-message 只在输入框内部的 onKeyDown 里处理，焦点不在输入框时 Enter 无效、
  // Cmd+A 会变成「全选整个页面」。所以这一项决定新建对话后能不能直接按确认发送。
  setLabel(ROW_FOCUS, S(S_FOCUS_TAB), C_TEXT);
  drawKeyBox(SET_BX, setBtnY(ROW_FOCUS), SET_BW, SET_BTN_H,
             focusModeText(), C_CYAN, false);

  // 行 5：蓝牙
  setLabel(ROW_BT, S(g_bleConn ? S_BT_LINKED : S_BT_UNLINKED), C_TEXT);
  drawKeyBox(SET_BX, setBtnY(ROW_BT), SET_BW, SET_BTN_H,
             S(S_READVERTISE), C_CYAN, false);

  // 行 6：关于（纯 ASCII，两种语言一样）
  clearArea(0, setRowY(ROW_ABOUT), 320, SET_ROW_H);
  setPxFont();
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString("CoreS3 OS  v" FW_VERSION, 12, setRowY(ROW_ABOUT) + 4);
  M5.Display.drawString(__DATE__, 12, setRowY(ROW_ABOUT) + 18);
}

void appSettingsStart() { drawSettings(); }

void appSettingsLoop() {
  static uint32_t last = 0;
  static bool lastConn = false;
  if (uiTick(last) && lastConn != g_bleConn) { lastConn = g_bleConn; drawSettings(); }
}

void appSettingsTouch(int16_t x, int16_t y) {
  if (y < CANVAS_Y) return;

  // 行 0：亮度三块
  if (hitBox(x, y, 0, setRowY(ROW_BRIGHT), 320, SET_ROW_H)) {
    int by = setBtnY(ROW_BRIGHT);
    if (hitBox(x, y, SET_BX, by, SET_MIN_W, SET_BTN_H)) {
      g_brightness = (uint8_t)constrain((int)g_brightness - 16, 8, 255);
    } else if (hitBox(x, y, SET_BX + SET_MIN_W + 8 + SET_VAL_W, by,
                      SET_PLUS_W, SET_BTN_H)) {
      g_brightness = (uint8_t)constrain((int)g_brightness + 16, 8, 255);
    } else {
      return;
    }
    M5.Display.setBrightness(g_brightness);
    g_dimmed = false;
    drawSettings();
    return;
  }

  // 行 1：切语言 —— 导航条的应用名也要跟着变，所以顺带重画状态栏
  if (hitBox(x, y, 0, setRowY(ROW_LANG), 320, SET_ROW_H)) {
    if (!hitBox(x, y, SET_BX, setBtnY(ROW_LANG), SET_BW, SET_BTN_H)) return;
    langSet(g_lang == LANG_CN ? LANG_EN : LANG_CN);
    drawStatusBar();
    drawSettings();
    return;
  }

  // 行 2：自动休眠循环
  if (hitBox(x, y, 0, setRowY(ROW_SLEEP), 320, SET_ROW_H)) {
    if (!hitBox(x, y, SET_BX, setBtnY(ROW_SLEEP), SET_BW, SET_BTN_H)) return;
    g_sleepSec = SLEEP_OPTS[(sleepIndex() + 1) % 4];
    drawSettings();
    return;
  }

  // 行 3：录音方式（两档循环，写入 NVS）
  if (hitBox(x, y, 0, setRowY(ROW_REC), 320, SET_ROW_H)) {
    if (!hitBox(x, y, SET_BX, setBtnY(ROW_REC), SET_BW, SET_BTN_H)) return;
    recModeSave(g_recMode == REC_MODE_WB ? REC_MODE_SYS : REC_MODE_WB);
    drawSettings();
    return;
  }

  // 行 4：新建后 Tab 次数（0-3 循环，写入 NVS）
  if (hitBox(x, y, 0, setRowY(ROW_FOCUS), 320, SET_ROW_H)) {
    if (!hitBox(x, y, SET_BX, setBtnY(ROW_FOCUS), SET_BW, SET_BTN_H)) return;
    focusModeSave((g_focusMode + 1) % FOCUS_MODE_COUNT);
    drawSettings();
    return;
  }

  // 行 5：重新广播
  if (hitBox(x, y, 0, setRowY(ROW_BT), 320, SET_ROW_H)) {
    if (!hitBox(x, y, SET_BX, setBtnY(ROW_BT), SET_BW, SET_BTN_H)) return;
    Serial.println("[set] restart advertising");
    g_ble.end();
    delay(300);
    g_ble.begin();
    drawSettings();
  }
}

void appSettingsKey(uint8_t raw) {}
void appSettingsStop() {}

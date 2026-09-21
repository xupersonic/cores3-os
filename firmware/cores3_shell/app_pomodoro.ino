// 应用 3：番茄钟（25 分钟专注 / 5 分钟休息，结束响三声）
// 剩余时间用 C64 点阵放大 4 倍；进度条是 6px 一块的分段块，复古仪表感

#include "shell.h"

enum { PH_WORK, PH_BREAK };
static uint8_t  g_phase = PH_WORK;
static bool     g_run   = false;
static uint32_t g_left  = (uint32_t)POMODORO_WORK_MIN * 60000UL;
static uint32_t g_total = (uint32_t)POMODORO_WORK_MIN * 60000UL;
static uint32_t g_lastMs = 0;
static uint8_t  g_done   = 0;

static const int PMO_X = 8,  PMO_Y = 40, PMO_W = 304, PMO_H = 100;
static const int PMO_ROW_Y = 148, PMO_ROW_H = 44;

static const char* phaseName() { return S(g_phase == PH_WORK ? S_FOCUS : S_BREAK); }

static uint32_t pmoAccent() { return g_phase == PH_WORK ? C_AMBER : C_GREEN; }

static void draw(bool full) {
  if (full) clearCanvas();

  uint32_t ac = pmoAccent();
  M5.Display.fillRect(PMO_X, PMO_Y, PMO_W, PMO_H, C_PANEL);
  drawFrame(PMO_X, PMO_Y, PMO_W, PMO_H, ac);

  uint32_t secs = g_left / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", (int)(secs / 60), (int)(secs % 60));

  setPxFont();
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(ac);
  M5.Display.drawCenterString(buf, 160, PMO_Y + 12);
  M5.Display.setTextSize(1);

  char line[48];
  snprintf(line, sizeof(line), S(S_POMO_STATUS), phaseName(),
           S(g_run ? S_RUNNING : S_PAUSED), g_done);
  drawCenterText(160, PMO_Y + 54, 20, line, C_DIM);

  // 分段进度条
  drawProgressBar(16, PMO_Y + 78, 288, 12,
                  1.0f - (float)g_left / (float)g_total, ac);

  drawKeyBox(9,   PMO_ROW_Y, 147, PMO_ROW_H, S(S_RESET), C_DIM, false);
  drawKeyBox(164, PMO_ROW_Y, 148, PMO_ROW_H, S(g_run ? S_PAUSE : S_START),
             g_run ? C_RED : C_GREEN, g_run);

  clearArea(0, 200, 320, 22);
  drawCenterText(160, 202, 16, S(S_POMO_FOOT), C_DIM);
}

static void finish() {
  for (int i = 0; i < 3; i++) { beep(880, 180); delay(220); }
  if (g_phase == PH_WORK) {
    g_done++;
    g_phase = PH_BREAK;
    g_total = (uint32_t)POMODORO_BREAK_MIN * 60000UL;
  } else {
    g_phase = PH_WORK;
    g_total = (uint32_t)POMODORO_WORK_MIN * 60000UL;
  }
  g_left = g_total;
  g_run  = false;
  Serial.printf("[pomo] phase -> %s done=%d\n", phaseName(), g_done);
}

void appPomodoroStart() { g_lastMs = millis(); draw(true); }

void appPomodoroLoop() {
  static uint32_t lastDraw  = 0;
  static int32_t  lastShown = -1;   // 上次画出来的剩余秒数
  static int8_t   lastPhase = -1;
  static bool     lastRun   = false;

  if (g_run) {
    uint32_t now = millis();
    uint32_t dt  = now - g_lastMs;
    g_lastMs = now;
    if (g_left > dt) g_left -= dt;
    else { g_left = 0; finish(); draw(true); lastShown = -1; return; }
  }

  if (!uiTick(lastDraw)) return;

  int32_t shown = (int32_t)(g_left / 1000);
  if (shown == lastShown && g_phase == (uint8_t)lastPhase && g_run == lastRun) return;
  lastShown = shown;
  lastPhase = (int8_t)g_phase;
  lastRun   = g_run;
  draw(false);
}

void appPomodoroTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, 9, PMO_ROW_Y, 147, PMO_ROW_H)) {          // 重置
    g_run = false;
    g_left = g_total;
    draw(false);
    return;
  }
  if (hitBox(x, y, 164, PMO_ROW_Y, 148, PMO_ROW_H)) {        // 开始 / 暂停
    g_run = !g_run;
    g_lastMs = millis();
    draw(false);
    return;
  }
  if (hitBox(x, y, PMO_X, PMO_Y, PMO_W, PMO_H)) {            // 大区域同样切换
    g_run = !g_run;
    g_lastMs = millis();
    draw(false);
  }
}

void appPomodoroKey(uint8_t raw) {
  if (raw == ' ' || raw == 0x0D) { g_run = !g_run; g_lastMs = millis(); draw(false); }
}

void appPomodoroStop() {}

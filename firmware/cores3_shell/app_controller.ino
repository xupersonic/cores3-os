// 应用 1：BLE 控制器（把设备变成 WorkBuddy 的物理遥控器）
//
// 形态对齐 VibeKey：
//   旋钮左右旋转 → 上下两个切换键（发 Cmd+[ / Cmd+]，上一个 / 下一个对话）
//   旋钮按下     → 右侧「+」键（新任务）
//   下面三个键   → 录音 / 确认 / 取消（取消 = 清空输入框，不是 Esc）
//   中间屏幕     → 只显示状态，不显示任务名（BLE 键盘收不到回传）
//
// 语音识别靠 macOS 系统听写，设备只负责发快捷键。

#include "shell.h"

enum { ST_IDLE, ST_REC, ST_SENT, ST_CLEARED };
static uint8_t  g_state      = ST_IDLE;
static bool     g_dict       = false;    // 是否正在听写
static uint32_t g_stateUntil = 0;        // 临时状态（已发送 / 已清空）的到期时间

// 布局：切换键左右并排（各 148x44，更好点） + 状态区（右侧新任务）+ 底部三键
static const int CTL_SW_Y = 44, CTL_SW_H = 44, CTL_SW_W = 148;
static const int CTL_SW_L_X = 8, CTL_SW_R_X = 164;
static const int CTL_ST_X = 8,  CTL_ST_Y = 96, CTL_ST_W = 248, CTL_ST_H = 68;
static const int CTL_NEW_X = 264, CTL_NEW_W = 48;
static const int CTL_ACT_Y = 172, CTL_ACT_H = 44, CTL_ACT_W = 96, CTL_ACT_GAP = 8;

static int actX(int i) { return 8 + i * (CTL_ACT_W + CTL_ACT_GAP); }

static uint32_t stateColor() {
  switch (g_state) {
    case ST_REC:     return C_AMBER;
    case ST_SENT:    return C_GREEN;
    case ST_CLEARED: return C_RED;
    default:         return C_CYAN;
  }
}

static const char* stateText() {
  switch (g_state) {
    case ST_REC:     return S(S_ST_REC);
    case ST_SENT:    return S(S_ST_SENT);
    case ST_CLEARED: return S(S_ST_CLEAR);
    default:         return S(S_ST_IDLE);
  }
}

// ─── 绘制 ────────────────────────────────────────────────────────────────

// 状态区：标题 + 大状态色块 + 蓝牙/电量
static void drawStatus() {
  uint32_t ac = stateColor();
  M5.Display.fillRect(CTL_ST_X, CTL_ST_Y, CTL_ST_W, CTL_ST_H, C_PANEL);
  drawFrame(CTL_ST_X, CTL_ST_Y, CTL_ST_W, CTL_ST_H, ac);

  drawTitle(CTL_ST_Y + 2, S(S_ST_TITLE), ac);

  // 大字状态：反白色块，一眼可见
  M5.Display.fillRect(CTL_ST_X + 8, CTL_ST_Y + 24, CTL_ST_W - 16, 24, ac);
  drawCenterText(CTL_ST_X + CTL_ST_W / 2, CTL_ST_Y + 24, 24, stateText(), C_BG);

  // 底部一行：录音方式 + 取消方式 + 蓝牙 + 电量（纯 ASCII，8x8 点阵画不出中文）
  char line[32];
  snprintf(line, sizeof(line), "%s %s %s %d%%",
           g_recMode == REC_MODE_WB ? "WB-VOICE" : "SYS-DICT",
           clearModeText(),
           g_bleConn ? "BLE" : "--", batPercent());
  setPxFont();
  M5.Display.setTextColor(g_bleConn ? C_DIM : C_RED);
  M5.Display.drawCenterString(line, CTL_ST_X + CTL_ST_W / 2, CTL_ST_Y + 52);
}

// 新任务键：像素十字（+）
static void drawNewBtn() {
  uint32_t ac = C_GREEN;
  M5.Display.fillRect(CTL_NEW_X, CTL_ST_Y, CTL_NEW_W, CTL_ST_H, C_PANEL);
  drawFrame(CTL_NEW_X, CTL_ST_Y, CTL_NEW_W, CTL_ST_H, ac);
  int cx = CTL_NEW_X + CTL_NEW_W / 2, cy = CTL_ST_Y + CTL_ST_H / 2;
  M5.Display.fillRect(cx - 10, cy - 3, 21, 6, ac);
  M5.Display.fillRect(cx - 3, cy - 10, 6, 21, ac);
}

static void ctlHint(const char* s) {
  clearArea(0, 222, 320, 18);
  drawCenterText(160, 222, 16, s, C_DIM);
}

// 左右指示三角：左键画朝左的、右键画朝右的（放在各自外侧，不压文字）
static void drawArrow(int bx, int bw, bool left) {
  int cy = CTL_SW_Y + CTL_SW_H / 2;
  int tip = left ? bx + 12 : bx + bw - 12;
  int back = left ? tip + 8 : tip - 8;
  M5.Display.fillTriangle(tip, cy, back, cy - 8, back, cy + 8, C_CYAN);
}

static void drawSwitchRow() {
  drawKeyBox(CTL_SW_L_X, CTL_SW_Y, CTL_SW_W, CTL_SW_H, S(S_PREV_CHAT), C_CYAN, false);
  drawKeyBox(CTL_SW_R_X, CTL_SW_Y, CTL_SW_W, CTL_SW_H, S(S_NEXT_CHAT), C_CYAN, false);
  drawArrow(CTL_SW_L_X, CTL_SW_W, true);
  drawArrow(CTL_SW_R_X, CTL_SW_W, false);
}

static void drawActionRow() {
  drawKeyBox(actX(0), CTL_ACT_Y, CTL_ACT_W, CTL_ACT_H, S(S_REC),    C_CYAN, false);
  drawKeyBox(actX(1), CTL_ACT_Y, CTL_ACT_W, CTL_ACT_H, S(S_SEND),   C_GREEN, false);
  drawKeyBox(actX(2), CTL_ACT_Y, CTL_ACT_W, CTL_ACT_H, S(S_CANCEL), C_RED,  false);
}

void appControllerStart() {
  clearCanvas();
  drawSwitchRow();
  drawStatus();
  drawNewBtn();
  drawActionRow();
  ctlHint(S(S_CTL_HINT_MAIN));
}

void appControllerStop() {}

// ─── 动作 ────────────────────────────────────────────────────────────────

static bool requireConn() {
  if (g_bleConn) return true;
  Serial.println("[ctrl] BLE not connected, ignored");
  M5.Display.fillRect(CTL_ST_X, CTL_ST_Y, CTL_ST_W, CTL_ST_H, C_RED);
  drawFrame(CTL_ST_X, CTL_ST_Y, CTL_ST_W, CTL_ST_H, C_RED);
  drawCenterText(CTL_ST_X + CTL_ST_W / 2, CTL_ST_Y + 20, 24, S(S_CTL_NO_BT), C_BG);
  delay(600);
  appControllerStart();
  return false;
}

static void flash(uint32_t color) {
  M5.Display.fillRect(CTL_ST_X, CTL_ST_Y, CTL_ST_W, CTL_ST_H, color);
  delay(90);
  appControllerStart();
}

static void setState(uint8_t s) {
  g_state = s;
  g_stateUntil = (s == ST_SENT || s == ST_CLEARED) ? millis() + 2000 : 0;
  drawStatus();
}

static void actPrev() {
  if (!requireConn()) return;
  g_ble.tap(PREV_CHAT_KEY, PREV_CHAT_MODS);
  Serial.println("[ctrl] prev chat (Cmd+[)");
  flash(C_CYAN);
}

static void actNext() {
  if (!requireConn()) return;
  g_ble.tap(NEXT_CHAT_KEY, NEXT_CHAT_MODS);
  Serial.println("[ctrl] next chat (Cmd+])");
  flash(C_CYAN);
}

static void actNew() {
  if (!requireConn()) return;
  g_ble.tap(NEW_CHAT_KEY, NEW_CHAT_MODS);
  g_dict = false;
  Serial.println("[ctrl] new task (Cmd+N)");
  flash(C_GREEN);

  // 焦点必须挪回输入框 —— 这一项跟录音方式【无关】：
  //   Cmd+D（WB 内置语音）挂在 window 上，没焦点也能开，所以录音看着是正常的；
  //   但 send-message 只在输入框内部的 onKeyDown 里处理，Cmd+A 同理，
  //   焦点不对 → Enter 发不出去、Cmd+A 变成「全选整个页面」。
  focusNudge(g_focusDelayMs);
}

// 把焦点挪回输入框（v0.4.6 重写：默认改成 WAKE，Tab 遍历降为兜底）
//
//  FOCUS_WAKE（默认）：Cmd+D 双击。WorkBuddy 语音结果的回调是
//    text => { appendInputText(text); focusInput(); }
//  一次完整的录音开关结束时应用会主动 focusInput()，我们借这个副作用聚焦，
//  不依赖 Tab 顺序、不依赖新对话什么时候渲染完。
//
//  其余档走 Tab 遍历：反向（Shift+Tab）从文档末尾开始，输入框在页面底部，
//  命中率高于正向 Tab（正向先撞侧边栏一堆按钮）。落点不对就到
//  设置 → 新建后聚焦 里换档，或用串口 focus [0-7]。
//
//  waitMs：动手前先等的时间。新建时等新对话渲染完（1.8s）；取消时只等 250ms。
static void focusNudge(uint16_t waitMs) {
  if (g_focusMode == FOCUS_OFF) return;
  delay(waitMs);

  if (g_focusMode == FOCUS_WAKE) {
    g_ble.tap(WB_VOICE_KEY, WB_VOICE_MODS);
    delay(FOCUS_WAKE_GAP_MS);
    g_ble.tap(WB_VOICE_KEY, WB_VOICE_MODS);
    Serial.printf("[ctrl] focus wake: Cmd+D x2 (gap %dms)\n", FOCUS_WAKE_GAP_MS);
    return;
  }

  bool    rev = (g_focusMode >= FOCUS_S_TAB1 && g_focusMode <= FOCUS_S_TAB3);
  uint8_t n   = rev ? g_focusMode : (g_focusMode - FOCUS_TAB1 + 1);
  for (uint8_t i = 0; i < n; i++) {
    g_ble.tap(KEY_TAB, rev ? KEY_MOD_LSHIFT : 0);
    delay(NEW_FOCUS_TAB_GAP);
  }
  Serial.printf("[ctrl] focus nudge: %s x%d (wait %ums)\n",
                rev ? "Shift+Tab" : "Tab", (int)n, (unsigned)waitMs);
}

// 录音：两种模式，见 config.h 里的说明
//   WB  = Cmd+D，WorkBuddy 内置语音，window 级监听 → 不需要输入框焦点（默认）
//   SYS = macOS 系统听写，必须有落点，配合「新建后 Tab」使用
// 两个都是 toggle：按一次开，再按一次停。
static void actRec() {
  if (!requireConn()) return;
  if (g_recMode == REC_MODE_WB) {
    g_ble.tap(WB_VOICE_KEY, WB_VOICE_MODS);
    Serial.println("[ctrl] WB voice (Cmd+D)");
  } else {
    g_ble.tap(DICT_KEY, DICT_MODS);
    Serial.println("[ctrl] macOS dictation");
  }
  g_dict = !g_dict;
  Serial.printf("[ctrl] rec -> %d\n", (int)g_dict);
  setState(g_dict ? ST_REC : ST_IDLE);
}

static void actSend() {
  if (!requireConn()) return;
  g_ble.tap(KEY_RETURN);
  g_dict = false;
  Serial.println("[ctrl] send (Return)");
  setState(ST_SENT);
}

// 取消 = 停止 + 清空草稿（默认 CLEAR_MODE_ESC_WIPE，两连击）
//  ① Esc   —— 对齐 VibeKey，也是 WorkBuddy 的 stop-generation 默认绑定，
//              不依赖焦点，先中断正在跑的任务 / 退出输入态；
//  ② Cmd+A + Backspace —— 把输入框里的草稿真正清掉，需要焦点在输入框内。
//
//  ★ v0.4.8：这里【不再】做任何焦点唤醒。已有对话里焦点本来就在输入框
//    （能打字就是证据），唤醒只会把它弄丢 —— 见 config.h 里 CANCEL_WAKE_FOCUS
//    那段实测注释（wake+wipe 只删掉一个字符，esc+wipe 才真清空）。
//    真的需要唤醒（比如以后解决新建对话时）把 CANCEL_WAKE_FOCUS 改成 1。
//
// 另两档见 config.h：CLEAR_MODE_ESC（只要 VibeKey 原生行为）、CLEAR_MODE_WIPE（只清草稿）。
static void actClear() {
  if (!requireConn()) return;

  if (g_clearMode != CLEAR_MODE_WIPE) {
    g_ble.tap(CLEAR_ESC_KEY, CLEAR_ESC_MODS);
    Serial.println("[ctrl] cancel (ESC)");
    if (g_clearMode == CLEAR_MODE_ESC_WIPE) delay(CLEAR_ESC_GAP_MS);
  }

  if (g_clearMode != CLEAR_MODE_ESC) {
#if CANCEL_WAKE_FOCUS
    if (g_focusMode == FOCUS_WAKE) focusNudge(CLEAR_FOCUS_WAIT_MS);
#endif
    g_ble.tap(CLEAR_WIPE_KEY, CLEAR_WIPE_MODS);   // Cmd+A 全选
    delay(CLEAR_WIPE_GAP_MS);
    g_ble.tap(KEY_BACKSPACE);                     // 删除（Mac 上这颗键才是 delete）
    Serial.println("[ctrl] wipe draft (Cmd+A, Backspace)");
  }

  g_dict = false;
  setState(ST_CLEARED);
}

// 串口自测入口（v0.4.8）：`act rec|send|cancel|new|prev|next`
// 存在的理由：以前验证一个按键只能手动拼命令（esc / wipe / ret …），拼出来的
// 序列跟固件真正走的分支不是一回事 —— v0.4.6 的取消 bug 正是这么漏掉的
// （手动测的是 esc+wipe，固件实际还夹了一次 WAKE）。现在能直接跑真实代码路径。
void controllerAct(const char* which) {
  String w = String(which);
  w.trim();
  if      (w == "rec")    actRec();
  else if (w == "send")   actSend();
  else if (w == "cancel") actClear();
  else if (w == "new")    actNew();
  else if (w == "prev")   actPrev();
  else if (w == "next")   actNext();
  else Serial.println("[cmd] act: rec|send|cancel|new|prev|next");
}

// ─── 输入 ────────────────────────────────────────────────────────────────

void appControllerTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, CTL_SW_L_X, CTL_SW_Y, CTL_SW_W, CTL_SW_H)) { actPrev(); return; }
  if (hitBox(x, y, CTL_SW_R_X, CTL_SW_Y, CTL_SW_W, CTL_SW_H)) { actNext(); return; }
  if (hitBox(x, y, CTL_NEW_X, CTL_ST_Y, CTL_NEW_W, CTL_ST_H)) { actNew();  return; }

  for (int i = 0; i < 3; i++) {
    if (!hitBox(x, y, actX(i), CTL_ACT_Y, CTL_ACT_W, CTL_ACT_H)) continue;
    if (i == 0) actRec();
    else if (i == 1) actSend();
    else actClear();
    return;
  }
}

// Keyboard3 物理键 → 动作（v0.4.9 按用户要求定死三颗主力键）
//   空格 SPACE 0x20 = 录音（开 / 停，既是 toggle）
//   回车 ENTER 0x0D = 确认（发送）
//   del   DEL  0x7F = 取消（停止 + 清空草稿）
// 另外保留：0x08(BS，键盘上另一颗删除键) 同样当取消用，两颗都能按；
// 1/2/3 分别是上一个 / 下一个 / 新任务。
//
// ★ 坑记（v0.4.9）：Keyboard3 上标着「del」的那颗键发的是 0x7F，不是 0x08。
//   0x08 是标着「BS / ⌫」的那颗。之前只映射了 0x08，所以按 del 完全没反应。
//   键码见 M5Faces_Keyboard3.hpp 里的 keyboard3_key_t 枚举。
void appControllerKey(uint8_t raw) {
  if (raw == KB_KEY_REC)                                  { actRec();   return; }
  if (raw == KB_KEY_SEND)                                 { actSend();  return; }
  if (raw == KB_KEY_CANCEL || raw == KB_KEY_CANCEL2)      { actClear(); return; }
  if (raw == KB_KEY_PREV)                                 { actPrev();  return; }
  if (raw == KB_KEY_NEXT)                                 { actNext();  return; }
  if (raw == KB_KEY_NEW)                                  { actNew();   return; }
}

// ─── 循环：临时状态 2 秒后回落，电量/蓝牙 1Hz 刷新 ────────────────────────

void appControllerLoop() {
  static uint32_t last = 0;
  if (!uiTick(last)) return;

  if (g_state == ST_SENT || g_state == ST_CLEARED) {
    if (millis() > g_stateUntil) { g_state = ST_IDLE; drawStatus(); }
    return;
  }
  drawStatus();
}

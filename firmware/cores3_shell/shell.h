// CoreS3 OS —— 外壳公共定义
//
// 主题：RETRO PIXEL / CYBERPUNK / TERMINAL
//   底：近黑 + 暗蓝灰网格点（CRT 底纹）
//   字：ASCII 用 C64 8x8 点阵，中文用 efont 16x16 点阵（两者都是原生点阵，放大不糊）
//   饰：1px 霓虹边框 + 四角实心角标（HUD 感）、方括号标签、块状进度条
#pragma once

#include <M5Unified.h>
#include <M5Faces.h>
#include <HijelHID_BLEKeyboard.h>
#include "config.h"
#include "i18n.h"

// 一个应用 = 名字（字符串 ID，跟随语言）+ 霓虹主色 + 8x8 像素图标 + 生命周期回调
// onStart/onStop 负责申请与释放资源；onLoop 每帧调用（自己控制刷新频率）；
// onTouch 收到的是屏幕坐标（y >= CANVAS_Y 才会派发，导航条归外壳处理）；
// onKey 收到 Keyboard3 的原始键码（0x0D 回车 / 0x1B Esc / 0x20 空格 / 0x08 BS / 0x7F del）；
// 注意 0x7F 才是标着「del」的那颗，0x08 是标着「⌫」的那颗，别搞混；
// onBack 可选：应用内部的「返回上一级」（二级页返回）。返回 true = 已消费，
// 外壳就不再回桌面；返回 false 或不填（nullptr）= 直接回桌面。
struct App {
  uint8_t       nameId;  // StrID，取名字一律用 appName(i)，别直接读
  uint32_t      accent;  // 霓虹主色
  const uint8_t* icon;   // 8x8 位图：8 字节，每字节一行，bit7 = 最左像素
  void (*onStart)();
  void (*onLoop)();
  void (*onTouch)(int16_t x, int16_t y);
  void (*onKey)(uint8_t raw);
  void (*onStop)();
  bool (*onBack)();
};

extern App          g_apps[];
extern const uint8_t g_appCount;
extern int8_t       g_curApp;     // -1 = 桌面
extern bool         g_bleConn;
extern bool         g_kbOk;
extern HijelHID_BLEKeyboard g_ble;
extern uint8_t      g_brightness;
extern uint16_t     g_sleepSec;
extern uint8_t      g_focusMode;    // 新建对话后怎么把焦点挪回输入框（见 config.h 的 FOCUS_*）
extern uint16_t     g_focusDelayMs; // 动手前的等待（新建用；取消用 CLEAR_FOCUS_WAIT_MS）
void focusDelaySave(uint16_t ms);   // 写入 NVS
void focusDelayLoad();              // 开机时读回
void focusModeSave(uint8_t n);      // 写入 NVS
void focusModeLoad();               // 开机时读回
const char* focusModeText();        // 设置页上显示的纯 ASCII 短名
extern uint8_t      g_recMode;      // REC_MODE_WB / REC_MODE_SYS
void recModeSave(uint8_t m);
void recModeLoad();
extern uint8_t      g_clearMode;    // CLEAR_MODE_ESC / WIPE / ESC_WIPE
void clearModeSave(uint8_t m);
void clearModeLoad();
const char* clearModeText();
extern bool         g_dimmed;

// 应用名（跟随当前语言）
const char* appName(uint8_t idx);

// 屏幕分区：顶部 36px 是导航条（返回 / 标题 / 时间电量），其余是应用画布
static const int CANVAS_Y = 36;
static const int CANVAS_H = 240 - CANVAS_Y;

// ─── 配色：深底 + 霓虹 ───────────────────────────────────────────────────
static const uint32_t C_BG      = 0x05070Au;   // 屏幕底（近黑）
static const uint32_t C_PANEL   = 0x0B1017u;   // 面板底
static const uint32_t C_GRID    = 0x16202Cu;   // 网格点 / 分隔线
static const uint32_t C_BAR_BG  = 0x0A0F16u;   // 导航条底
static const uint32_t C_TEXT    = 0xB8C7D6u;   // 主文字（冷白）
static const uint32_t C_DIM     = 0x5A6B7Cu;   // 次级文字
static const uint32_t C_DARK    = 0x2A3644u;   // 暗部（边框未选中）
// 霓虹六色，一应用一色
static const uint32_t C_CYAN    = 0x22D3EEu;
static const uint32_t C_GREEN   = 0x39FF88u;   // 终端绿（系统主色）
static const uint32_t C_AMBER   = 0xFFB000u;
static const uint32_t C_MAGENTA = 0xFF2E88u;
static const uint32_t C_VIOLET  = 0xA855F7u;
static const uint32_t C_RED     = 0xFF4D4Du;
static const uint32_t C_LIME    = 0x9EFF00u;   // 记事本（接了时钟让出来的绿色位）

// ─── 手势：屏幕左缘向右滑 = 返回上一级 ────────────────────────────────────
// M5Unified 的 Touch_Class 自带 flick / drag / hold 状态机（底层判定「手指移动了多少」），
// 但不含「这个动作是什么意思」，语义这层由外壳自己写。判定条件三条同时满足：
//   ① 起手点 base_x 在屏幕左侧 GEST_EDGE 内；
//   ② 向右位移 distanceX() >= GEST_MIN_DX；
//   ③ 上下跑偏 |distanceY()| <= GEST_MAX_DY（避免竖向滚动被误判）。
static const int      GEST_EDGE   = 40;  // 左缘起手区宽度
static const int      GEST_MIN_DX = 44;  // 向右最小位移
static const int      GEST_MAX_DY = 44;  // 允许的上下偏移
// 交给 M5Unified 的「开始算移动」阈值：小于它的抖动仍算点击，不作手势
static const uint16_t GEST_FLICK_THRESH = 16;

// 刷新节拍（★ 通用约定，所有应用都要守）：
//   1. 界面上没有「自己会变」的内容（静态界面）→ 根本不要挂 uiTick，
//      改成事件驱动：输入/结果/状态字段变化时标脏再重画。
//      挂了节拍就等于每秒整屏重绘一次，看着一闪一闪（记事本 v0.6.1 就是这么修的）。
//   2. 有动态内容（时钟、倒计时、进度）→ 才挂 uiTick，而且只重画那一块，别整屏。
// uiTick 只负责「到点」，应用自己还要再做内容脏检查——内容没变就不重绘，
// 这样暂停的番茄钟、不显示秒的时钟可以做到 0 重绘，屏幕不闪、也省电。
static const uint32_t UI_REFRESH_MS = 1000;

static inline bool uiTick(uint32_t &last) {
  uint32_t now = millis();
  if (now - last < UI_REFRESH_MS) return false;
  last = now;
  return true;
}

// —— 外壳提供的公共能力 ——
void setPxFont();      // C64 8x8 点阵（ASCII / 数字 / 符号）
void setCnFont();      // 16x16 中文点阵
void setSmallFont();   // 同 pxFont，语义别名

// —— 双语下的字体选择（重要）——
// 8x8 点阵里没有汉字，写进去是空白。所以拿到字符串先判断有没有非 ASCII 字节，
// 有就用中文点阵，没有就用 ASCII 点阵。中英混排同样适用。
bool hasCJK(const char* s);
void setAutoFont(const char* s);            // 按内容自动选字体
int  textHeight(const char* s);             // 16（含中文）或 8（纯 ASCII）
// 水平居中 + 垂直居中（在 y..y+h 这个框里），自动选字体
void drawCenterText(int cx, int y, int h, const char* s, uint32_t color);
uint8_t batPercent();
void clearCanvas();    // 清画布 + 铺 CRT 网格底纹
void clearArea(int x, int y, int w, int h);  // 清局部（会补回网格，避免底纹缺口）
void drawStatusBar();
void drawHome();
void goHome();
void goBack();   // 「返回上一级」：先问应用，不接就回桌面
void launchApp(uint8_t idx);

// 像素图标：bmp = 8 字节，scale 是每个像素点的边长（3 → 24x24）
void drawPxIcon(int cx, int cy, const uint8_t* bmp, int scale, uint32_t color);
// HUD 边框：1px 细框 + 四角 3px 实心角标
void drawFrame(int x, int y, int w, int h, uint32_t color);
// 块状进度条：每 6px 一块，复古分段感
void drawProgressBar(int x, int y, int w, int h, float ratio, uint32_t color);
// 终端列表行：label 暗色左对齐，value 亮色右对齐
void drawRow(int y, const char* label, const char* val, uint32_t accent);
// 标题行：`> TITLE` 风格，左上起
void drawTitle(int y, const char* title, uint32_t accent);
// 按键块：方角按钮（无圆角，像素风）
void drawKeyBox(int x, int y, int w, int h, const char* label,
                uint32_t accent, bool filled);
bool hitBox(int x, int y, int bx, int by, int bw, int bh);
void beep(uint16_t freq, uint16_t ms);
void wakeScreen();

// —— 应用入口声明 ——
void appControllerStart();
void appControllerLoop();
void appControllerTouch(int16_t x, int16_t y);
void appControllerKey(uint8_t raw);
void controllerAct(const char* which); // 串口自测：act rec|send|cancel|new|prev|next
void appControllerStop();

// 记事本（v0.6.0）—— 取代了时钟（时钟源码留作 app_clock.ino.bak，随时可换回来）
void appNoteStart();
void appNoteLoop();
void appNoteTouch(int16_t x, int16_t y);
void appNoteKey(uint8_t raw);
void appNoteStop();

void appPomodoroStart();
void appPomodoroLoop();
void appPomodoroTouch(int16_t x, int16_t y);
void appPomodoroKey(uint8_t raw);
void appPomodoroStop();

// 游戏（v0.7.0）—— 取代了系统信息（源码留作 app_sysinfo.ino.bak，随时可换回来）
// 菜单是纯静态（画一次就不再画）；2048 纯事件驱动；只有俄罗斯方块的自动下落
// 会自己变，而且是「擦 4 格 + 画 4 格」的局部重画，不是整屏。
void appGamesStart();
void appGamesLoop();
void appGamesTouch(int16_t x, int16_t y);
void appGamesKey(uint8_t raw);
bool appGamesBack();
void appGamesStop();
void gameGoto(uint8_t m);      // 串口 `game <0|1|2>`：0=菜单 1=方块 2=2048
void gameToggleMute();         // 串口 `game mute`：切静音（只在游戏应用里调）
void gamePrintStatus();        // 串口 `game`

// 音乐遥控（v0.5.0 取代传感器模块）
struct MusicState {
  bool     hasData;                        // 收到过 Mac 推来的元数据
  bool     playing;
  char     title[MUSIC_TITLE_MAX + 1];
  char     artist[MUSIC_ARTIST_MAX + 1];
  char     album[MUSIC_ALBUM_MAX + 1];
  uint32_t posMs, durMs;
  uint32_t updatedAt;                      // 本地快照时刻（进度靠它自己往前走）
  uint8_t* art;                            // 封面 JPEG 缓冲（PSRAM）
  size_t   artLen, artTotal;
  bool     artReady;
  bool     dirty;                          // 内容变了 → 下一拍整屏重画（进度自己走不算）
};
extern MusicState g_music;
void musicGattInit();       // 在 g_ble.begin() 之后调用：往同一个 server 上挂自定义服务
void musicPrintStatus();    // 串口 music 命令

void appMusicStart();
void appMusicLoop();
void appMusicTouch(int16_t x, int16_t y);
void appMusicKey(uint8_t raw);
void appMusicStop();

// 记事本（v0.6.0）：本地打字 → BLE notify → Mac 落盘
struct NoteState {
  bool fsOk;         // LittleFS 挂载成功（草稿 / 队列能用）
  bool subscribed;   // Mac 订阅了 notify（没人订阅就只排队不发）
  bool sending;      // 已发出、正在等回执
  int  queued;       // 队列里待发条数
  int  sent;         // 本次开机成功推送条数
  char last[40];     // 最近一次状态（界面底部显示）
};
extern NoteState g_note;
void noteGattInit();                  // 跟 musicGattInit 一样，在 g_ble.begin() 之后调
void notePrintStatus();               // 串口 note 命令
void noteSerialPush(const char* txt); // 串口 note push <文本>：走真实发送路径，用于自测
void noteFlushQueue();                // 有订阅时把队列里的补发出去

// RTC（v0.6.2）：CoreS3 的 BM8563 没有后备电池，掉电 / 重烧会归零，
// 所以除了串口 `time` 命令，BLE 连上 Mac 时也会由 Bridge 自动对时一次。
void rtcSetYMDHMS(int Y, int Mo, int D, int H, int Mi, int S);
bool rtcIsValid();                    // RTC 里的时间能不能信（归零 / 年份离谱 = 不可信）

void appSettingsStart();
void appSettingsLoop();
void appSettingsTouch(int16_t x, int16_t y);
void appSettingsKey(uint8_t raw);
void appSettingsStop();

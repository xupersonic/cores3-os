// CoreS3 OS —— 双语字符串表
//
// 两张表的顺序必须和 i18n.h 里 StrID 枚举完全一致（末尾有编译期检查）。
// 中文串可以含 ASCII；英文串必须纯 ASCII，否则 8x8 点阵画不出字。

#include "shell.h"
#include <Preferences.h>

uint8_t g_lang = LANG_CN;

static Preferences g_prefs;
static const char* NVS_NS   = "cores3";
static const char* NVS_LANG = "lang";

// 故意不写长度：下面用 static_assert 跟 STR_COUNT 对账，多一个少一个都编译不过
static const char* const STR_CN[] = {
  /* 应用名 */       "控制器", "记事本", "番茄钟", "游戏", "音乐", "设置",
  /* 桌面 */         "桌面", "[1-6] 键启动 · 左滑返回",

  /* 控制器 */       "语音输入", "按一下唤起 Mac 听写", "空格键同样可用",
                     "正在听写", "说完再按一次结束", "然后按「确认」",
                     "录音", "确认", "取消", "新任务",
                     "上一个对话", "下一个对话",
                     "状态", "待命", "录音中", "已发送", "已清空",
                     "空格录音 回车确认 Del取消",
                     "请先配对蓝牙",

  /* 番茄钟 */       "专注", "休息", "进行中", "已暂停",
                     "重置", "暂停", "开始", "点大区域开始 / 暂停",
                     "%s · %s · 完成 %d",

  /* 游戏 */         "选一个", "俄罗斯方块", "2048",
                     "得分", "消行", "等级", "下一个", "最高",
                     "新局", "结束", "通关", "暂停", "点一下",
                     "上", "下", "左", "右", "旋转", "落下",
                     "[1-2] 开始 · M 静音", "N 新局 · M 静音",
                     "声音 开", "已静音",

  /* 音乐 */         "音乐", "歌手", "等待 Mac 推送元数据", "专辑", "暂无播放",

  /* 记事本 */       "记事本", "发送", "清空", "队列", "已发",
                     "发送中", "已送达", "已排队", "超时",
                     "失败", "空白", "已清空", "存储不可用",

  /* 设置 */         "设置", "屏幕亮度", "自动休眠",
                     "30 秒", "60 秒", "2 分钟", "常亮",
                     "蓝牙已连接", "蓝牙未连接", "重新广播",
                     "语言", "简中", "ENGLISH",
                     "新建后聚焦",
                     "录音方式", "内置语音", "系统听写",
};

static const char* const STR_EN[] = {
  /* app names */    "CONTROLLER", "NOTES", "POMODORO", "GAMES", "MUSIC", "SETTINGS",
  /* home */         "HOME", "[1-6] LAUNCH / SWIPE BACK",

  /* controller */   "VOICE INPUT", "TAP TO START DICTATION", "SPACE KEY ALSO WORKS",
                     "LISTENING", "TAP AGAIN WHEN DONE", "THEN PRESS SEND",
                     "REC", "SEND", "CANCEL", "NEW TASK",
                     "PREV CHAT", "NEXT CHAT",
                     "STATUS", "IDLE", "RECORDING", "SENT", "CLEARED",
                     "SPC=REC RET=SEND DEL=CANCEL",
                     "PAIR BLUETOOTH FIRST",

  /* pomodoro */     "FOCUS", "BREAK", "RUNNING", "PAUSED",
                     "RESET", "PAUSE", "START", "TAP BIG AREA TO START",
                     "%s / %s / DONE %d",

  /* games */        "PICK ONE", "TETRIS", "2048",
                     "SCORE", "LINES", "LEVEL", "NEXT", "BEST",
                     "NEW", "GAME OVER", "YOU WIN", "PAUSED", "TAP",
                     "UP", "DN", "LF", "RT", "ROT", "DROP",
                     "[1-2] OR TAP / M=MUTE", "N=NEW / M=MUTE",
                     "SOUND ON", "MUTED",

  /* music */        "MUSIC", "ARTIST", "WAITING FOR MAC PUSH", "ALBUM", "NO PLAYBACK",

  /* notepad */      "NOTES", "SEND", "CLEAR", "QUEUE", "SENT",
                     "SENDING", "DELIVERED", "QUEUED", "TIMEOUT",
                     "FAILED", "EMPTY", "CLEARED", "NO STORAGE",

  /* settings */     "SETTINGS", "BRIGHTNESS", "AUTO SLEEP",
                     "30S", "60S", "2M", "ALWAYS",
                     "BT CONNECTED", "BT DISCONNECTED", "RE-ADVERTISE",
                     "LANGUAGE", "CHINESE", "ENGLISH",
                     "FOCUS AFTER NEW",
                     "VOICE MODE", "BUILT-IN", "SYS DICTATION",
};

// 两张表都必须跟 StrID 枚举一一对应，多一个少一个都编译不过（比运行时串味好查得多）
static_assert(sizeof(STR_CN) / sizeof(STR_CN[0]) == STR_COUNT, "中文表与 StrID 数量不一致");
static_assert(sizeof(STR_EN) / sizeof(STR_EN[0]) == STR_COUNT, "英文表与 StrID 数量不一致");

const char* S(uint8_t id) {
  if (id >= STR_COUNT) return "";
  return (g_lang == LANG_EN) ? STR_EN[id] : STR_CN[id];
}

void langInit() {
  g_prefs.begin(NVS_NS, false);
  uint8_t v = g_prefs.getUChar(NVS_LANG, LANG_CN);
  g_lang = (v <= LANG_EN) ? v : LANG_CN;
  Serial.printf("[i18n] lang=%d\n", (int)g_lang);
}

void langSet(uint8_t lang) {
  if (lang > LANG_EN) return;
  g_lang = lang;
  g_prefs.putUChar(NVS_LANG, lang);
  Serial.printf("[i18n] lang -> %d\n", (int)lang);
}

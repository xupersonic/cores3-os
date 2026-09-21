// CoreS3 OS —— 双语（简中 / 英文）
//
// 用法：所有界面文字都通过 S(StrID) 取，不要写死字符串。
// 切换语言：langSet(LANG_EN) —— 会写进 NVS，掉电不丢。
//
// 字体自动选择：8x8 点阵里没有汉字，所以取到字符串后要判断「有没有中日韩字符」，
// 有就用 16x16 中文点阵，没有就用 C64 8x8 ASCII 点阵（见 setAutoFont）。
// 判断依据是 UTF-8 里 >= 0x80 的字节，中英文混排也能正确处理。
#pragma once

#include <stdint.h>

enum Lang : uint8_t { LANG_CN = 0, LANG_EN = 1 };

// 字符串 ID。注意：成组的 ID 必须连续，代码里靠「基址 + 下标」取
// （星期 7 个、预设 4 个、休眠档 4 个）。
enum StrID : uint8_t {
  // 顺序 = 桌面顺序（v0.7.0 游戏取代系统信息，仍是 6 个）
  S_APP_CONTROLLER, S_APP_NOTES, S_APP_POMODORO,
  S_APP_GAMES, S_APP_MUSIC, S_APP_SETTINGS,

  S_HOME, S_HINT_HOME,

  S_CTL_VOICE, S_CTL_VOICE_SUB, S_CTL_VOICE_SUB2,
  S_CTL_LISTEN, S_CTL_LISTEN_SUB, S_CTL_LISTEN_SUB2,
  S_REC, S_SEND, S_CANCEL, S_NEWCHAT,
  S_PREV_CHAT, S_NEXT_CHAT,
  S_ST_TITLE, S_ST_IDLE, S_ST_REC, S_ST_SENT, S_ST_CLEAR,
  S_CTL_HINT_MAIN,
  S_CTL_NO_BT,

  S_FOCUS, S_BREAK, S_RUNNING, S_PAUSED,
  S_RESET, S_PAUSE, S_START, S_POMO_FOOT, S_POMO_STATUS,

  // 游戏（v0.7.0）：菜单 + 俄罗斯方块 + 2048
  S_GAME_PICK, S_GAME_TETRIS, S_GAME_2048,
  S_GAME_SCORE, S_GAME_LINES, S_GAME_LEVEL, S_GAME_NEXT, S_GAME_BEST,
  S_GAME_NEW, S_GAME_OVER, S_GAME_WIN, S_GAME_PAUSE, S_GAME_TAP,
  S_GAME_UP, S_GAME_DOWN, S_GAME_LEFT, S_GAME_RIGHT, S_GAME_ROT, S_GAME_DROP,
  S_GAME_FOOTM, S_GAME_FOOT2,
  S_GAME_SNDON, S_GAME_MUTED,          // v0.7.1 静音开关的两个状态

  S_MUSIC_TITLE, S_MUSIC_ARTIST, S_MUSIC_FOOT, S_MUSIC_ALBUM, S_MUSIC_NOPLAY,

  // 记事本（v0.6.0）。注意 S_NOTE_* 会被拼进状态行，中英混排由 setAutoFont 处理
  S_NOTE_TITLE, S_NOTE_SEND, S_NOTE_CLEAR, S_NOTE_QUEUE, S_NOTE_DONE,
  S_NOTE_SENDING, S_NOTE_ARRIVED, S_NOTE_QUEUED, S_NOTE_TIMEOUT,
  S_NOTE_FAIL, S_NOTE_EMPTY, S_NOTE_CLEARED, S_NOTE_NOFS,

  S_SETTINGS_TITLE, S_BRIGHTNESS, S_AUTO_SLEEP,
  S_SLEEP_30S, S_SLEEP_60S, S_SLEEP_2M, S_SLEEP_ALWAYS,             // 连续 4 个
  S_BT_LINKED, S_BT_UNLINKED, S_READVERTISE,
  S_LANGUAGE, S_LANG_CN, S_LANG_EN,
  S_FOCUS_TAB,
  S_REC_MODE, S_REC_WB, S_REC_SYS,

  STR_COUNT
};

extern uint8_t g_lang;

const char* S(uint8_t id);          // 取当前语言的字符串
void langInit();                    // 开机时从 NVS 读回语言
void langSet(uint8_t lang);         // 切换并写入 NVS

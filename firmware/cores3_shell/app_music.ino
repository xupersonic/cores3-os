// 应用 5：音乐遥控（v0.5.0 起取代传感器模块）
//
// 设备【不解码音频】，只当遥控器。声音永远由 Mac 出，设备只做两件事：
//
//   ① 控制 = HID Consumer Control 媒体键（播放/暂停、上下曲、音量）
//      —— BLE 库原生支持，零 Mac 端软件、零权限。前提是播放器注册了
//         MPRemoteCommandCenter（QQ 音乐 11.9.1 二进制里有
//         -[QQMusicMainPlayerController setupMPRemoteCommand]，确认已注册）。
//
//   ② 显示 = 同一个 BLE 连接上的自定义 GATT 服务（config.h 的 MUSIC_*）
//      —— Mac 端常驻程序读 media-control（MediaRemote）拿到歌名/进度/封面，
//         推过来。不用配 Wi-Fi、不依赖局域网、换了网也不会断。
//
// 封面走分包（见 config.h 的协议注释），收到后由 M5GFX 的 drawJpg 解码显示。

#include "shell.h"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>

MusicState g_music;

// ─── 布局 ────────────────────────────────────────────────────────────────
static const int MUS_ART_X = 12, MUS_ART_Y = 50, MUS_ART_S = 88;   // 封面框
static const int MUS_TX    = 112;                                   // 右侧文本列
static const int MUS_TX_W  = 320 - 12 - MUS_TX;                     // 文本可用宽度
static const int MUS_TIME_Y = 122;                                  // 播放时间文字
static const int MUS_BAR_Y = 136, MUS_BAR_H = 6;                    // 进度条
// 播放时每秒要动的只有「时间 + 进度条」这一条，单独划出来局部重画，
// 别整屏重画（整屏会连封面一起重解码，肉眼可见地闪一下）
static const int MUS_STRIP_Y = 118, MUS_STRIP_H = 28;
static const int MUS_B1_Y = 150, MUS_B1_W = 92, MUS_B1_H = 42;      // 三大键
static const int MUS_B2_Y = 198, MUS_B2_W = 60, MUS_B2_H = 34;      // 两个音量键

static const int MUS_BOX_PREV[] = {12,  MUS_B1_Y, MUS_B1_W, MUS_B1_H};
static const int MUS_BOX_PLAY[] = {114, MUS_B1_Y, MUS_B1_W, MUS_B1_H};
static const int MUS_BOX_NEXT[] = {216, MUS_B1_Y, MUS_B1_W, MUS_B1_H};
static const int MUS_BOX_VDN[]  = {12,  MUS_B2_Y, MUS_B2_W, MUS_B2_H};
static const int MUS_BOX_VUP[]  = {248, MUS_B2_Y, MUS_B2_W, MUS_B2_H};

// ─── 控制 ────────────────────────────────────────────────────────────────
// 媒体键走 HID Consumer Control，不依赖焦点、不依赖前台应用。
static void musMedia(uint16_t usage, const char* what) {
  if (!g_bleConn) {
    Serial.println("[music] BLE 未连接，媒体键丢弃");
    return;
  }
  g_ble.tap(usage);
  Serial.printf("[music] %s (0x%04X)\n", what, (unsigned)usage);
}

static void musToggle() { musMedia(MEDIA_PLAY_PAUSE,   "play/pause"); }
static void musPrev()   { musMedia(MEDIA_PREV_TRACK,   "prev"); }
static void musNext()   { musMedia(MEDIA_NEXT_TRACK,   "next"); }
static void musVolUp()  { musMedia(MEDIA_VOLUME_UP,    "vol+"); }
static void musVolDn()  { musMedia(MEDIA_VOLUME_DOWN,  "vol-"); }

// ─── 元数据解析 ──────────────────────────────────────────────────────────
static void musParseMeta(const char* s) {
  // title|artist|album|state|posMs|durMs
  char buf[MUSIC_META_MAX + 1];
  snprintf(buf, sizeof(buf), "%s", s);
  char* seg[6];
  int n = 0;
  char* p = buf;
  seg[n++] = p;
  while (*p && n < 6) {
    if (*p == '|') { *p = 0; seg[n++] = p + 1; }
    p++;
  }
  if (n < 1) return;

  // 进度由本地时间自己往前走，Mac 端只用来纠偏 ——
  // 所以这里要区分「曲目/状态变了」（要整屏重画）和「只是进度刷新」（只重画进度条）。
  // Mac 端每 2s 推一次 meta，若不区分就会每 2s 整屏闪一次。
  bool same = g_music.hasData
           && strncmp(g_music.title,  seg[0], MUSIC_TITLE_MAX) == 0
           && strncmp(g_music.artist, (n > 1) ? seg[1] : "", MUSIC_ARTIST_MAX) == 0
           && strncmp(g_music.album,  (n > 2) ? seg[2] : "", MUSIC_ALBUM_MAX) == 0
           && g_music.playing == ((n > 3) && (atoi(seg[3]) != 0))
           && g_music.durMs   == ((n > 5) ? (uint32_t)atol(seg[5]) : 0);

  snprintf(g_music.title,  sizeof(g_music.title),  "%s", seg[0]);
  if (n > 1) snprintf(g_music.artist, sizeof(g_music.artist), "%s", seg[1]);
  else       g_music.artist[0] = 0;
  if (n > 2) snprintf(g_music.album,  sizeof(g_music.album),  "%s", seg[2]);
  else       g_music.album[0] = 0;
  g_music.playing = (n > 3) && (atoi(seg[3]) != 0);
  g_music.posMs   = (n > 4) ? (uint32_t)atol(seg[4]) : 0;
  g_music.durMs   = (n > 5) ? (uint32_t)atol(seg[5]) : 0;
  g_music.updatedAt = millis();
  g_music.hasData = true;
  if (!same) g_music.dirty = true;
  Serial.printf("[music] meta: %s / %s / %s | %s %lu/%lu ms\n",
                g_music.title, g_music.artist, g_music.album,
                g_music.playing ? "PLAY" : "PAUSE",
                (unsigned long)g_music.posMs, (unsigned long)g_music.durMs);
}

static void musArtAlloc() {
  if (g_music.art) return;
  size_t cap = MUSIC_ART_MAX;
  if (psramFound()) g_music.art = (uint8_t*)ps_malloc(cap);
  else              g_music.art = (uint8_t*)malloc(cap);
  if (!g_music.art) Serial.println("[music] 封面缓冲分配失败");
}

static void musArtAppend(const uint8_t* d, size_t n) {
  if (!g_music.art || n == 0) return;
  if (g_music.artLen + n > MUSIC_ART_MAX) n = MUSIC_ART_MAX - g_music.artLen;
  memcpy(g_music.art + g_music.artLen, d, n);
  g_music.artLen += n;
  if (g_music.artTotal && g_music.artLen >= g_music.artTotal) {
    g_music.artReady = true;
    g_music.dirty    = true;              // 让下一拍把封面画出来
    Serial.printf("[music] 封面收齐 %u 字节\n", (unsigned)g_music.artLen);
  }
}

static void musArtFeed(const uint8_t* d, size_t n) {
  if (n == 0) return;
  musArtAlloc();
  if (!g_music.art) return;
  if (d[0] == MUSIC_ART_FIRST) {
    if (n < MUSIC_ART_HDR) return;
    uint32_t total = (uint32_t)d[1] | ((uint32_t)d[2] << 8) |
                     ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 24);
    if (total == 0 || total > MUSIC_ART_MAX) {
      // 以前这里只是把 artLen 清零就 return，续包却照收不误 ——
      // 结果是缓冲被塞满、artTotal 还是 0、artReady 永远起不来（显示 NO ART）。
      // 现在明确把「这次传输作废」记下来，续包一律丢弃。
      Serial.printf("[music] 封面 %u 字节超限(>%u)，丢弃\n",
                    (unsigned)total, (unsigned)MUSIC_ART_MAX);
      g_music.artLen   = 0;
      g_music.artTotal = 0;
      g_music.artReady = false;
      return;
    }
    g_music.artLen   = 0;
    g_music.artTotal = total;
    g_music.artReady = false;
    musArtAppend(d + MUSIC_ART_HDR, n - MUSIC_ART_HDR);
  } else if (d[0] == MUSIC_ART_NEXT) {
    // 没收到合法首包（artTotal==0）就来的续包一律丢弃；
    // 已经收齐了还在来的重复包也丢（否则会越过 artTotal 继续往缓冲里写）。
    if (g_music.artTotal == 0 || g_music.artReady) return;
    musArtAppend(d + 1, n - 1);
  }
}

// ─── GATT（在 HID 所在的同一个 server 上追加一个服务）─────────────────────
class MusCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue val = pChar->getValue();
    const uint8_t* d = (const uint8_t*)val.data();
    size_t n = val.size();
    if (n == 0) return;
    if (pChar->getUUID().equals(NimBLEUUID(MUSIC_CHR_META))) {
      char tmp[MUSIC_META_MAX + 1];
      size_t m = (n < MUSIC_META_MAX) ? n : MUSIC_META_MAX;
      memcpy(tmp, d, m);
      tmp[m] = 0;
      musParseMeta(tmp);
    } else if (pChar->getUUID().equals(NimBLEUUID(MUSIC_CHR_ART))) {
      musArtFeed(d, n);
    }
  }
};
static MusCharCallbacks g_musCb;

void musicGattInit() {
  NimBLEServer* srv = NimBLEDevice::getServer();
  if (!srv) { Serial.println("[music] 拿不到 NimBLE server，跳过 GATT"); return; }
  NimBLEService* svc = srv->createService(MUSIC_SVC_UUID);
  if (!svc) { Serial.println("[music] createService 失败"); return; }
  NimBLECharacteristic* cMeta = svc->createCharacteristic(
      MUSIC_CHR_META, NIMBLE_PROPERTY::WRITE);
  NimBLECharacteristic* cArt = svc->createCharacteristic(
      MUSIC_CHR_ART, NIMBLE_PROPERTY::WRITE);
  cMeta->setCallbacks(&g_musCb);
  cArt->setCallbacks(&g_musCb);
  svc->start();
  // GATT 数据库变了，重启广播让新服务句柄对外可见
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::startAdvertising();
  Serial.println("[music] GATT 已就绪 (svc/meta/art)");
}

void musicPrintStatus() {
  Serial.printf("[music] hasData=%d playing=%d pos=%lu/%lu art=%u/%u %s\n",
                (int)g_music.hasData, (int)g_music.playing,
                (unsigned long)g_music.posMs, (unsigned long)g_music.durMs,
                (unsigned)g_music.artLen, (unsigned)g_music.artTotal,
                g_music.artReady ? "(ready)" : "");
}

// ─── 绘制辅助 ────────────────────────────────────────────────────────────
// 按像素宽度截断（8x8 ASCII 每字 8px，中文点阵每字 16px），超宽补 ..
static void musText(int x, int y, const char* s, int maxW, uint32_t color) {
  setAutoFont(s);
  M5.Display.setTextColor(color);
  char out[MUSIC_TITLE_MAX + 8];
  int oi = 0, px = 0;
  const unsigned char* p = (const unsigned char*)s;
  while (*p && oi < (int)sizeof(out) - 5) {
    unsigned char c = *p;
    int adv, cw;
    if      (c >= 0xF0) { adv = 4; cw = 16; }   // emoji 等 4 字节
    else if (c >= 0x80) { adv = 3; cw = 16; }   // CJK 3 字节
    else                { adv = 1; cw = 8;  }
    if (px + cw > maxW) { out[oi++] = '.'; out[oi++] = '.'; break; }
    for (int k = 0; k < adv && *p; k++) out[oi++] = *p++;
    px += cw;
  }
  out[oi] = 0;
  M5.Display.drawString(out, x, y);
}

static void musTime(char* out, size_t cap, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(out, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// 进度条位置：Mac 推一次快照，之后本地按时间往前走
static uint32_t musPosNow() {
  if (!g_music.hasData) return 0;
  uint32_t p = g_music.posMs;
  if (g_music.playing) {
    uint32_t dt = millis() - g_music.updatedAt;
    if (dt < MUSIC_STALE_MS) p += dt;
  }
  if (g_music.durMs && p > g_music.durMs) p = g_music.durMs;
  return p;
}

static void drawMusicArt() {
  int x = MUS_ART_X, y = MUS_ART_Y, s = MUS_ART_S;
  M5.Display.fillRect(x, y, s, s, C_PANEL);
  drawFrame(x, y, s, s, C_GRID);
  if (g_music.artReady && g_music.art && g_music.artLen > 0) {
    if (M5.Display.drawJpg(g_music.art, (uint32_t)g_music.artLen,
                           x + 1, y + 1, s - 2, s - 2, 0, 0)) {
      static bool logged = false;                 // 只在第一张上打一次，别刷屏
      if (!logged) {
        logged = true;
        Serial.printf("[music] 封面解码成功并上屏 (%u 字节)\n", (unsigned)g_music.artLen);
      }
    } else {
      Serial.printf("[music] ✗ 封面解码失败 (%u 字节，首字节 %02X %02X)\n",
                    (unsigned)g_music.artLen, g_music.art[0], g_music.art[1]);
      setPxFont();
      M5.Display.setTextColor(C_DARK);
      M5.Display.drawCenterString("BAD JPG", x + s / 2, y + s / 2 - 4);
    }
  } else {
    setPxFont();
    M5.Display.setTextColor(C_DARK);
    M5.Display.drawCenterString("NO ART", x + s / 2, y + s / 2 - 4);
  }
}

// 音乐页的刷新状态（放这儿是因为 drawMusic() 整屏重画后也要同步一下秒数）
static uint32_t s_lastSec = 0xFFFFFFFF;
static int      s_lastKey = -1;

// 只重画「时间 + 进度条」这一条：clearArea 会把 CRT 网格底纹补回去，看不出接缝
static void drawMusicTime() {
  clearArea(0, MUS_STRIP_Y, 320, MUS_STRIP_H);
  char t1[12], t2[12];
  uint32_t pos = musPosNow();
  musTime(t1, sizeof(t1), pos);
  musTime(t2, sizeof(t2), g_music.durMs);
  setPxFont();
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString(t1, 12, MUS_TIME_Y);
  M5.Display.drawString(t2, 308 - 8 * (int)strlen(t2), MUS_TIME_Y);
  float ratio = (g_music.durMs > 0) ? (float)pos / (float)g_music.durMs : 0.0f;
  drawProgressBar(12, MUS_BAR_Y, 296, MUS_BAR_H, ratio, C_VIOLET);
}

static void drawMusic() {
  clearCanvas();
  clearArea(0, 38, 320, 20);
  drawTitle(40, S(S_MUSIC_TITLE), C_VIOLET);

  drawMusicArt();

  if (!g_music.hasData) {
    musText(MUS_TX, 62, S(S_MUSIC_NOPLAY), MUS_TX_W, C_DIM);
    musText(MUS_TX, 86, S(S_MUSIC_FOOT), MUS_TX_W, C_DARK);
  } else {
    musText(MUS_TX, 52, g_music.title,  MUS_TX_W, C_TEXT);
    musText(MUS_TX, 76, g_music.artist, MUS_TX_W, C_DIM);
    musText(MUS_TX, 100, g_music.album, MUS_TX_W, C_DARK);
  }

  drawMusicTime();

  // 三大键 + 两个音量键
  drawKeyBox(MUS_BOX_PREV[0], MUS_BOX_PREV[1], MUS_BOX_PREV[2], MUS_BOX_PREV[3],
             "PREV", C_VIOLET, false);
  drawKeyBox(MUS_BOX_PLAY[0], MUS_BOX_PLAY[1], MUS_BOX_PLAY[2], MUS_BOX_PLAY[3],
             g_music.playing ? "PAUSE" : "PLAY", C_VIOLET, g_music.playing);
  drawKeyBox(MUS_BOX_NEXT[0], MUS_BOX_NEXT[1], MUS_BOX_NEXT[2], MUS_BOX_NEXT[3],
             "NEXT", C_VIOLET, false);
  drawKeyBox(MUS_BOX_VDN[0], MUS_BOX_VDN[1], MUS_BOX_VDN[2], MUS_BOX_VDN[3],
             "VOL-", C_DIM, false);
  drawKeyBox(MUS_BOX_VUP[0], MUS_BOX_VUP[1], MUS_BOX_VUP[2], MUS_BOX_VUP[3],
             "VOL+", C_DIM, false);

  // 中间状态
  const char* st = g_bleConn
                 ? (g_music.playing ? "PLAYING" : (g_music.hasData ? "PAUSED" : "IDLE"))
                 : "NO BT";
  setPxFont();
  M5.Display.setTextColor(g_bleConn ? C_GREEN : C_RED);
  M5.Display.drawCenterString(st, 160, MUS_B2_Y + 13);

  s_lastSec = musPosNow() / 1000;   // 整屏刚画过，秒数已是最新的
}

// ─── 生命周期 ────────────────────────────────────────────────────────────
// 音乐页的刷新策略（v0.5.1 改）：
//   以前是每秒整屏重画一次 —— 封面 JPEG 每秒重新解码一遍，屏幕肉眼可见地闪，
//   而真正需要动的只有进度条。现在分三档：
//     ① 曲目/播放状态/蓝牙变了、或新封面收齐 → 整屏重画（dirty 由 GATT 回调置位）
//     ② 只是播放时间往前走了一秒 → 只重画「时间 + 进度条」那一条
//     ③ 暂停中、或还没走到下一秒 → 一个像素都不动
void appMusicStart() {
  s_lastSec = 0xFFFFFFFF;      // 进应用时让循环自己算一次，别沿用上次的值
  s_lastKey = -1;
  drawMusic();
}

void appMusicLoop() {
  static uint32_t last = 0;
  if (!uiTick(last)) return;

  int key = (int)g_music.hasData * 4 + (int)g_music.playing * 2 + (int)g_bleConn;
  if (key != s_lastKey || g_music.dirty) {
    g_music.dirty = false;
    s_lastKey = key;
    s_lastSec = musPosNow() / 1000;
    drawMusic();
    return;
  }

  uint32_t sec = musPosNow() / 1000;
  if (sec == s_lastSec) return;      // 暂停中 / 还没到下一秒 → 完全不动
  s_lastSec = sec;
  drawMusicTime();
}

void appMusicTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, MUS_BOX_PREV[0], MUS_BOX_PREV[1], MUS_BOX_PREV[2], MUS_BOX_PREV[3])) {
    musPrev();
  } else if (hitBox(x, y, MUS_BOX_PLAY[0], MUS_BOX_PLAY[1], MUS_BOX_PLAY[2], MUS_BOX_PLAY[3])) {
    musToggle();
  } else if (hitBox(x, y, MUS_BOX_NEXT[0], MUS_BOX_NEXT[1], MUS_BOX_NEXT[2], MUS_BOX_NEXT[3])) {
    musNext();
  } else if (hitBox(x, y, MUS_BOX_VDN[0], MUS_BOX_VDN[1], MUS_BOX_VDN[2], MUS_BOX_VDN[3])) {
    musVolDn();
  } else if (hitBox(x, y, MUS_BOX_VUP[0], MUS_BOX_VUP[1], MUS_BOX_VUP[2], MUS_BOX_VUP[3])) {
    musVolUp();
  } else {
    return;
  }
  drawMusic();
}

// Keyboard3：空格/2 = 播放暂停，1 = 上一首，3 = 下一首，del = 静音
void appMusicKey(uint8_t raw) {
  if      (raw == ' ')  musToggle();
  else if (raw == '1')  musPrev();
  else if (raw == '2')  musToggle();
  else if (raw == '3')  musNext();
  else if (raw == 0x7F) musMedia(MEDIA_MUTE, "mute");
  else return;
  drawMusic();
}

void appMusicStop() {}

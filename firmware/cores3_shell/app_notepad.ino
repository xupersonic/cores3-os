// 应用 7：记事本（v0.6.0）
//
// 干什么：用 Keyboard3 打一段 ASCII 速记 → 按发送 → 经 BLE 推给 Mac →
//        Mac 端 Bridge 追加写进 Obsidian 的 face3notes.md。
//
// 为什么走 BLE notify 而不是 HID 打字：
//   HID 那条路要求光标正好在目标输入框里（v0.4.x 一整段坑都在跟焦点搏斗），
//   而 notify 是「设备喊话、Mac 听着」，不关心前台是谁、光标在哪。
//   代价是要有一个常驻的 Mac 端程序；所以草稿和待发队列都落 FFat，
//   Bridge 没开的时候照样能写，等它连上了再补发。
//
// 三段状态机（别搞复杂）：
//   编辑 → 发送（分包 notify，等一个 ACK 字节）→ ACK 到就出队并删文件；
//   超时没 ACK → 留在队列里，界面显示「已排队」，等下次补发。
//
// 中文：Keyboard3 只能出 ASCII（0x20-0x7E），设备端没有输入法，
//       所以这一版只做 ASCII 速记。中文等后面再说（用 Mac 侧转写或直接 HID 直通）。
//
// 重绘（v0.6.1）：界面是静态的，不挂 1Hz 心跳 —— 只有「内容真变了」才重画。
//       见文件末尾 noteSnapStatus() 那一段的完整说明。通用约定：
//       静态界面一律不按节拍重绘，动态字段（时钟/倒计时/进度）才挂节拍，且只重画那一块。

#include "shell.h"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLECharacteristic.h>
#include <NimBLEConnInfo.h>
#include <LittleFS.h>
#include <Preferences.h>

NoteState g_note;

// ─── 布局（画布从 y=36 开始）──────────────────────────────────────────────
static const int NTE_X = 12,  NTE_Y = 58;               // 文本区左上
static const int NTE_W = 296, NTE_H = 120;              // 文本区尺寸
static const int NTE_LINE_H = 10;                       // 8x8 点阵 + 2px 行距
static const int NTE_COLS   = 37;                       // 296 / 8
static const int NTE_ROWS   = 12;                       // 120 / 10
static const int NTE_BTN_Y  = 182, NTE_BTN_H = 32;
static const int NTE_ST_Y   = 220;                      // 底部状态行
static const int NTE_MAX_LINES = 220;

static const int NTE_BOX_SEND[] = {12,  NTE_BTN_Y, 140, NTE_BTN_H};
static const int NTE_BOX_CLR[]  = {168, NTE_BTN_Y, 140, NTE_BTN_H};

// ─── 编辑缓冲 ─────────────────────────────────────────────────────────────
static char g_buf[NOTE_MAX + 1];
static int  g_len = 0;
static int  g_cur = 0;          // 光标（字符下标，总是在 [0, g_len]）
static int  g_top = 0;          // 视口顶行（可视行下标）
static bool g_dirty = false;    // 要重画（v0.6.1：只由「内容真变了」驱动，不挂 1Hz 心跳）
static bool g_needSave = false; // 草稿有改动、还没落盘
static uint32_t g_editAt = 0;   // 最后一次按键时刻（停手 2 秒才写 flash，别每键都写）
static uint32_t g_savedAt = 0;  // 上次落盘时间
static uint32_t g_draws = 0;    // 重绘次数（串口 `note` 里能看：静止时它不该涨）

struct VLine { int start, end; };   // 可视行 = buf 的 [start, end)
static VLine g_lines[NTE_MAX_LINES];
static int   g_nLines = 0;
static int   g_curLine = 0;

// ─── BLE ──────────────────────────────────────────────────────────────────
static NimBLECharacteristic* g_chOut = nullptr;   // NOTIFY
static NimBLECharacteristic* g_chAck = nullptr;   // WRITE
static uint16_t g_payload  = 20;                  // 单包可用字节（MTU-3）
static bool     g_ackGot   = false;
static uint32_t g_sendAt   = 0;
static String   g_pendingPath = "";               // 正在发的那条来自哪个队列文件
static String   g_lastSent    = "";               // 直接发送的内容（超时补队列用）

static Preferences g_nvs;
static uint32_t    g_seq = 0;                     // 队列文件编号（NVS 里单调自增）

// ═══ 自动换行 ═════════════════════════════════════════════════════════════
// 贪心按列宽切；如果正好切在单词中间，就往回找空格退一点（最多退半行），
// 避免把单词劈成两半。切在 '\n' 上就直接换行。
static void noteWrap() {
  g_nLines = 0;
  g_curLine = 0;
  int i = 0;
  while (i < g_len && g_nLines < NTE_MAX_LINES) {
    int lineStart = i;
    int taken = 0;
    while (i < g_len && taken < NTE_COLS) {
      char c = g_buf[i++];
      taken++;
      if (c == '\n') break;
    }
    int breakAt = i;
    bool midWord = (i < g_len) && (g_buf[i] != ' ') && (g_buf[i] != '\n')
                   && (i > 0) && (g_buf[i - 1] != ' ');
    if (midWord) {
      for (int k = i - 1; k > lineStart + NTE_COLS / 2; k--) {
        if (g_buf[k] == ' ') { breakAt = k + 1; break; }
      }
    }
    g_lines[g_nLines].start = lineStart;
    g_lines[g_nLines].end   = breakAt;
    if (g_cur >= lineStart && g_cur <= breakAt) g_curLine = g_nLines;
    g_nLines++;
    i = breakAt;
  }
  if (g_nLines == 0) {                       // 空内容也要有一行，光标才有着落
    g_lines[0].start = 0; g_lines[0].end = 0; g_nLines = 1;
  } else if (g_cur >= g_len) {
    g_curLine = g_nLines - 1;
  }
  // 视口跟着光标走
  if (g_curLine < g_top)                  g_top = g_curLine;
  if (g_curLine >= g_top + NTE_ROWS)      g_top = g_curLine - NTE_ROWS + 1;
  if (g_top > g_nLines - 1)               g_top = (g_nLines > 0) ? g_nLines - 1 : 0;
  if (g_top < 0)                          g_top = 0;
}

// ═══ 文本编辑 ═════════════════════════════════════════════════════════════
static void noteTouched() {          // 内容变了：标脏 + 预约一次落盘
  g_dirty = true;
  g_needSave = true;
  g_editAt = millis();
}

static void noteInsert(char c) {
  if (g_len >= NOTE_MAX || c < 0x20 || c > 0x7E) return;
  memmove(g_buf + g_cur + 1, g_buf + g_cur, g_len - g_cur + 1);
  g_buf[g_cur++] = c;
  g_len++;
  noteTouched();
}

static void noteBackspace() {
  if (g_cur <= 0) return;
  memmove(g_buf + g_cur - 1, g_buf + g_cur, g_len - g_cur + 1);
  g_cur--;
  g_len--;
  noteTouched();
}

static void noteClearAll() {
  g_len = g_cur = g_top = 0;
  g_buf[0] = 0;
  noteTouched();
}

// ═══ FFat：草稿 + 队列 ════════════════════════════════════════════════════
// 分区是 app3M_fat9M_16MB，有 9.9MB 的 ffat 分区，草稿和队列都放这儿，
// 跟那张（挂不上的 exFAT）TF 卡没关系。
static bool noteFsInit() {
  if (g_note.fsOk) return true;
  if (!LittleFS.begin(false, NOTE_FS_MOUNT, 10, "lfs")) {
    Serial.println("[note] LittleFS 挂载失败，尝试格式化（9.9MB，首次要几秒）");
    if (!LittleFS.begin(true, NOTE_FS_MOUNT, 10, "lfs")) {
      Serial.println("[note] ✗ LittleFS 不可用，草稿/队列功能关闭");
      return false;
    }
  }
  g_nvs.begin("cores3", false);
  g_seq = g_nvs.getUInt("nseq", 0);
  g_note.fsOk = true;
  // 探针：根目录到底能不能写（子目录 mkdir 在这块分区上失败，先确认不是分区只读）
  File t = LittleFS.open(String(NOTE_FS_ROOT) + "_wtest.txt", FILE_WRITE);
  bool rw = (bool)t;
  if (t) { t.write((const uint8_t*)"x", 1); t.close(); LittleFS.remove((String(NOTE_FS_ROOT) + "_wtest.txt").c_str()); }
  g_note.fsOk = true;
  Serial.printf("[note] LittleFS 就绪 total=%lluKB used=%lluKB seq=%u rootWrite=%d\n",
                (unsigned long long)(LittleFS.totalBytes() / 1024),
                (unsigned long long)(LittleFS.usedBytes() / 1024),
                (unsigned)g_seq, (int)rw);
  return true;
}

static String noteDraftPath()      { return String(NOTE_FS_ROOT) + "note_draft.txt"; }
static String noteQueueFile(uint32_t n) {
  char p[48];
  snprintf(p, sizeof(p), "%s%s%08lu%s", NOTE_FS_ROOT, NOTE_Q_PREFIX, (unsigned long)n, NOTE_Q_SUFFIX);
  return String(p);
}

// File::name() 有的实现给全路径、有的只给文件名，统一取 basename 再判断
static const char* baseName(const char* p) {
  const char* s = strrchr(p, '/');
  return s ? s + 1 : p;
}
static bool isQueueName(const char* p) {
  const char* nm = baseName(p);
  if (strncmp(nm, NOTE_Q_PREFIX, 2) != 0) return false;
  const char* ext = strstr(nm, NOTE_Q_SUFFIX);
  return ext != nullptr;
}

static void noteDraftSave() {
  if (!g_note.fsOk) return;
  File f = LittleFS.open(noteDraftPath(), FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)g_buf, g_len);
  f.close();
  g_savedAt = millis();
  g_needSave = false;
}

static void noteDraftLoad() {
  if (!g_note.fsOk) return;
  File f = LittleFS.open(noteDraftPath(), FILE_READ);
  if (!f) return;
  size_t n = f.read((uint8_t*)g_buf, NOTE_MAX);
  f.close();
  g_buf[n] = 0;
  g_len = (int)n;
  g_cur = g_len;
  Serial.printf("[note] 草稿已恢复 %d 字节\n", (int)n);
}

static int noteQueueCount() {
  if (!g_note.fsOk) return 0;
  File d = LittleFS.open(NOTE_FS_ROOT);
  if (!d || !d.isDirectory()) return 0;
  int n = 0;
  File f;
  while ((f = d.openNextFile())) {
    if (isQueueName(f.name())) n++;
    f.close();
  }
  d.close();
  return n;
}

// 队列里编号最小的那个（文件名零填充，字典序 = 编号序）
static bool noteQueueFirst(String& out) {
  if (!g_note.fsOk) return false;
  File d = LittleFS.open(NOTE_FS_ROOT);
  if (!d || !d.isDirectory()) return false;
  String best = "";
  File f;
  while ((f = d.openNextFile())) {
    String nm = String(baseName(f.name()));
    f.close();
    if (!isQueueName(nm.c_str())) continue;
    if (best.length() == 0 || nm < best) best = nm;
  }
  d.close();
  if (best.length() == 0) return false;
  out = String(NOTE_FS_ROOT) + best;
  return true;
}

static bool noteEnqueue(const char* text) {
  if (!noteFsInit()) return false;
  if (noteQueueCount() >= NOTE_QUEUE_MAX) {
    Serial.println("[note] 队列满了，丢弃最早的内容？—— 不，直接拒绝，先手动清");
    return false;
  }
  String p = noteQueueFile(++g_seq);
  g_nvs.putUInt("nseq", g_seq);
  File f = LittleFS.open(p, FILE_WRITE);
  if (!f) {
    Serial.printf("[note] ✗ 入队写入失败 %s (dirExists=%d)\n", p.c_str(), (int)LittleFS.exists(NOTE_FS_ROOT));
    return false;
  }
  size_t wrote = f.write((const uint8_t*)text, strlen(text));
  f.close();
  Serial.printf("[note] 入队 %s (%u 字节，实写 %u)\n", p.c_str(), (unsigned)strlen(text), (unsigned)wrote);
  g_note.queued = noteQueueCount();
  return true;
}

static void noteDropPending() {
  if (g_pendingPath.length() == 0) return;
  LittleFS.remove(g_pendingPath);
  Serial.printf("[note] 已出队 %s\n", g_pendingPath.c_str());
  g_pendingPath = "";
  g_note.queued = noteQueueCount();
}

// ═══ BLE 发送 ═════════════════════════════════════════════════════════════
// 分包 notify。ATT 层本身是可靠的，所以不等逐包 ACK，只在整条发完后等一个字节回执。
// notify() 返回 false = 协议栈缓冲区满了，退一小会儿再试几次。
static bool noteSendRaw(const char* text) {
  if (!g_chOut) return false;
  size_t len = strlen(text);
  if (len == 0) return false;
  if (len > 0xFFFF) len = 0xFFFF;

  size_t cap = g_payload;
  if (cap < 24) cap = 24;                 // 太小的 MTU 也保证有基本载荷
  uint8_t  pkt[NOTE_CHUNK_MAX];
  size_t   off = 0;
  bool     first = true;

  g_ackGot = false;
  while (off < len) {
    size_t hdr = first ? (size_t)NOTE_PKT_HDR : 1u;
    size_t n   = (cap > hdr) ? (cap - hdr) : 1;
    if (n > NOTE_CHUNK_MAX - hdr) n = NOTE_CHUNK_MAX - hdr;
    if (n > len - off) n = len - off;

    size_t p = 0;
    if (first) {
      pkt[p++] = NOTE_PKT_FIRST;
      pkt[p++] = (uint8_t)(len & 0xFF);
      pkt[p++] = (uint8_t)((len >> 8) & 0xFF);
    } else {
      pkt[p++] = NOTE_PKT_NEXT;
    }
    memcpy(pkt + p, text + off, n);
    p += n;

    bool ok = false;
    for (int r = 0; r < 8 && !ok; r++) {
      ok = g_chOut->notify(pkt, p);
      if (!ok) delay(5);
    }
    if (!ok) {
      Serial.println("[note] ✗ notify 失败（缓冲区满），本条转队列");
      return false;
    }
    off += n;
    first = false;
  }
  g_note.sending = true;
  g_sendAt = millis();
  Serial.printf("[note] 已发出 %u 字节，等回执\n", (unsigned)len);
  return true;
}

// ═══ 书写时间戳（v0.6.2）═══════════════════════════════════════════════════
// 离线写的笔记要等连上才补发，Mac 收到时已经是「送达时刻」了 —— 时间戳记下来
// 全是晚上那一刻，看不出是哪会儿写的。所以在内容离开编辑区那一刻就把书写时间
// 烙进正文，格式固定 19 字节：
//
//     "[YYYY-MM-DD HH:MM] " + 正文
//      ^              ^  ^ ^
//      0            17 18 19(正文从这开始)
//
// Bridge 认这个前缀就拿它当条目时间和日期小标题；没有前缀（老固件 / RTC 归零）
// 就退回 Mac 当前时间，向后兼容。
// RTC 不可信时坚决不加 —— 宁可让时间记成送达时刻，也不能记成 2000-01-01。
static String noteStamp(const char* text) {
  if (!rtcIsValid()) return String(text);
  auto t = M5.Rtc.getTime();
  auto d = M5.Rtc.getDate();
  char stamp[24];
  snprintf(stamp, sizeof(stamp), "[%04d-%02d-%02d %02d:%02d] ",
           (int)d.year, (int)d.month, (int)d.date, (int)t.hours, (int)t.minutes);
  return String(stamp) + String(text);
}

// 对外：能发就发，发不了（没订阅 / 正在发）就入队。返回 true = 内容有着落了（已发或已入队）
static bool noteSend(const char* text) {
  if (strlen(text) == 0) {
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_EMPTY));
    g_dirty = true;
    return false;
  }
  String stamped = noteStamp(text);   // 烙时间戳只在这一处，队列文件里带着走
  if (!g_note.subscribed || g_note.sending || !g_chOut) {
    if (noteEnqueue(stamped.c_str())) {
      snprintf(g_note.last, sizeof(g_note.last), "%s (%d)", S(S_NOTE_QUEUED), g_note.queued);
      Serial.printf("[note] Mac 未订阅/正忙 → 入队，当前 %d 条\n", g_note.queued);
      g_dirty = true;
      return true;
    }
    // 入队失败（队列满 / fs 不可用）→ 内容没着落，返回 false 让上层别清编辑区
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_NOFS));
    g_dirty = true;
    return false;
  }
  g_pendingPath = "";
  g_lastSent = stamped;               // 超时要补进队列，先把内容留住
  if (!noteSendRaw(stamped.c_str())) { // 发送路径失败 → 转队列兜底
    noteEnqueue(stamped.c_str());
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_FAIL));
    g_dirty = true;
    return true;
  }
  snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_SENDING));
  g_dirty = true;
  return true;
}

void noteFlushQueue() {
  if (!g_note.fsOk || !g_note.subscribed || g_note.sending) return;
  if (!g_chOut) return;
  String p;
  if (!noteQueueFirst(p)) return;
  File f = LittleFS.open(p, FILE_READ);
  if (!f) return;
  String txt = f.readString();
  f.close();
  g_pendingPath = p;
  Serial.printf("[note] 补发队列 %s\n", p.c_str());
  if (!noteSendRaw(txt.c_str())) g_pendingPath = "";
}

static void noteOnAck() {
  g_note.sending = false;
  g_note.sent++;
  noteDropPending();
  snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_ARRIVED));
  g_dirty = true;
  beep(880, 40);
  Serial.println("[note] ✓ Mac 已接收");
}

static void noteOnTimeout() {
  g_note.sending = false;
  // 队列里没有对应文件（是刚打的直接发的）→ 补进队列，别丢
  if (g_pendingPath.length() == 0 && g_lastSent.length() > 0) {
    noteEnqueue(g_lastSent.c_str());
    snprintf(g_note.last, sizeof(g_note.last), "%s (%d)", S(S_NOTE_TIMEOUT), g_note.queued);
  } else {
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_TIMEOUT));
  }
  g_pendingPath = "";
  g_dirty = true;
  Serial.println("[note] ✗ 等回执超时，留队列");
}

// ═══ GATT ═════════════════════════════════════════════════════════════════
class NoteCharCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pChr, NimBLEConnInfo& ci, uint16_t sub) override {
    if (!pChr->getUUID().equals(NimBLEUUID(NOTE_CHR_OUT))) return;
    g_note.subscribed = (sub & 0x0001) != 0;          // CCCD bit0 = notify
    uint16_t mtu = ci.getMTU();
    g_payload = (mtu > 3) ? (uint16_t)(mtu - 3) : 20;
    Serial.printf("[note] 订阅=%d MTU=%u 单包载荷=%u\n",
                  (int)g_note.subscribed, (unsigned)mtu, (unsigned)g_payload);
    if (g_note.subscribed) {
      noteFsInit();
      g_note.queued = noteQueueCount();
      if (g_note.queued > 0) noteFlushQueue();
    }
  }
  void onWrite(NimBLECharacteristic* pChr, NimBLEConnInfo& ci) override {
    NimBLEAttValue v = pChr->getValue();
    if (v.size() >= 1 && v[0] == NOTE_ACK_OK) noteOnAck();
    // Mac → 设备：连上顺手对一次时（v0.6.2）。没有后备电池，掉电会归零，
    // 不对时的话离线笔记烙的时间戳就是 2000-01-01。
    if (v.size() >= 7 && v[0] == NOTE_ACK_TIME) {
      int Y = 2000 + (int)v[1], Mo = v[2], D = v[3], H = v[4], Mi = v[5], S = v[6];
      if (Y >= 2000 && Y <= 2099 && Mo >= 1 && Mo <= 12 && D >= 1 && D <= 31 &&
          H >= 0 && H <= 23 && Mi >= 0 && Mi <= 59 && S >= 0 && S <= 59) {
        rtcSetYMDHMS(Y, Mo, D, H, Mi, S);
        Serial.println("[note] ✓ 已从 Mac 自动对时");
      }
    }
  }
};
static NoteCharCallbacks g_noteCb;

void noteGattInit() {
  NimBLEServer* srv = NimBLEDevice::getServer();
  if (!srv) { Serial.println("[note] 拿不到 NimBLE server，跳过 GATT"); return; }
  NimBLEService* svc = srv->createService(NOTE_SVC_UUID);
  if (!svc) { Serial.println("[note] createService 失败"); return; }
  g_chOut = svc->createCharacteristic(NOTE_CHR_OUT, NIMBLE_PROPERTY::NOTIFY);
  g_chAck = svc->createCharacteristic(NOTE_CHR_ACK, NIMBLE_PROPERTY::WRITE);
  if (g_chOut) g_chOut->setCallbacks(&g_noteCb);
  if (g_chAck) g_chAck->setCallbacks(&g_noteCb);
  svc->start();
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::startAdvertising();
  Serial.println("[note] GATT 已就绪 (svc/out/ack)");
}

void notePrintStatus() {
  Serial.printf("[note] fs=%d sub=%d sending=%d queued=%d sent=%d len=%d/%d draws=%u\n",
                (int)g_note.fsOk, (int)g_note.subscribed, (int)g_note.sending,
                g_note.queued, g_note.sent, g_len, (int)NOTE_MAX,
                (unsigned)g_draws);
  Serial.printf("[note] last=%s\n", g_note.last);
}

void noteSerialPush(const char* txt) { noteFsInit(); noteSend(txt); }

// ═══ 绘制 ═════════════════════════════════════════════════════════════════
static void drawNote(bool full) {
  g_draws++;
  if (full) clearCanvas();

  drawTitle(42, S(S_NOTE_TITLE), C_LIME);

  // 右上角字数（先清再画：位数变短时否则会留残影 —— 现在不再每秒重绘，残影会更明显）
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%d/%d", g_len, (int)NOTE_MAX);
  clearArea(196, 40, 116, 18);
  setPxFont();
  M5.Display.setTextColor(C_DIM);
  M5.Display.drawString(cnt, 308 - 8 * (int)strlen(cnt), 42);

  // 文本区
  clearArea(NTE_X - 4, NTE_Y - 4, NTE_W + 8, NTE_H + 8);
  drawFrame(NTE_X - 4, NTE_Y - 4, NTE_W + 8, NTE_H + 8, C_DARK);
  noteWrap();
  setPxFont();
  for (int r = 0; r < NTE_ROWS; r++) {
    int li = g_top + r;
    if (li >= g_nLines) break;
    int s = g_lines[li].start, e = g_lines[li].end;
    int n = e - s;
    if (n <= 0) continue;
    if (n > NTE_COLS) n = NTE_COLS;
    char tmp[NTE_COLS + 1];
    memcpy(tmp, g_buf + s, n);
    tmp[n] = 0;
    M5.Display.setTextColor(C_TEXT);
    M5.Display.drawString(tmp, NTE_X, NTE_Y + r * NTE_LINE_H);
  }
  // 光标（复古终端风格：一条实心下划线，不闪，省得为它单独做重绘节拍）
  if (g_curLine >= g_top && g_curLine < g_top + NTE_ROWS) {
    int col = g_cur - g_lines[g_curLine].start;
    if (col < 0) col = 0;
    if (col > NTE_COLS) col = NTE_COLS;
    M5.Display.fillRect(NTE_X + col * 8, NTE_Y + (g_curLine - g_top) * NTE_LINE_H + 8,
                        8, 2, C_LIME);
  }

  // 两个按钮
  drawKeyBox(NTE_BOX_SEND[0], NTE_BOX_SEND[1], NTE_BOX_SEND[2], NTE_BOX_SEND[3],
             S(S_NOTE_SEND), C_LIME, true);
  drawKeyBox(NTE_BOX_CLR[0], NTE_BOX_CLR[1], NTE_BOX_CLR[2], NTE_BOX_CLR[3],
             S(S_NOTE_CLEAR), C_DARK, false);

  // 状态行
  clearArea(0, NTE_ST_Y - 2, 320, 18);
  char st[48];
  snprintf(st, sizeof(st), "%s %d / %s %d / %s",
           S(S_NOTE_QUEUE), g_note.queued, S(S_NOTE_DONE), g_note.sent,
           g_note.subscribed ? "BLE OK" : "BLE --");
  setAutoFont(st);          // 状态行中英混排（"队列 2 / 已发 5"），按内容选字体
  M5.Display.setTextColor(g_note.subscribed ? C_DIM : C_RED);
  M5.Display.drawString(st, 12, NTE_ST_Y);
  if (g_note.last[0]) {
    setAutoFont(g_note.last);
    M5.Display.setTextColor(C_LIME);
    M5.Display.drawString(g_note.last, 200, NTE_ST_Y);
  }
}

// ═══ 重绘策略（v0.6.1）════════════════════════════════════════════════════
// 这个界面是「静态」的：标题、按钮、状态行都没有随时间自变的内容，
// 所以不挂 1Hz 心跳。之前挂了 uiTick，等于每秒整屏重绘一次，看着一闪一闪。
//
// 现在的规则只有一条：内容真变了才重绘。
//   · 输入类（打字 / 退格 / 清空 / 移光标）→ noteTouched() 标脏；
//   · 结果类（发送成功 / 超时 / 入队）→ 各自标脏；
//   · 状态行那些非输入的字段（BLE 连接、订阅、队列数、已发数、上一条结果）
//     用一份快照比对，任一变化才算脏。这样断连、Mac 订阅、补发出队都能自动跟上。
// 定时器只留两个：回执超时判定、队列补发轮询——它们都不碰屏幕。
static int  s_kConn = -1, s_kSub = -1, s_kQueued = -1, s_kSent = -1;
static char s_kLast[sizeof(g_note.last)] = {0};

static void noteSnapStatus() {
  s_kConn   = g_bleConn ? 1 : 0;
  s_kSub    = g_note.subscribed ? 1 : 0;
  s_kQueued = g_note.queued;
  s_kSent   = g_note.sent;
  memcpy(s_kLast, g_note.last, sizeof(s_kLast));
}
static bool noteStatusChanged() {
  return (g_bleConn ? 1 : 0) != s_kConn
      || (g_note.subscribed ? 1 : 0) != s_kSub
      || g_note.queued != s_kQueued
      || g_note.sent != s_kSent
      || memcmp(s_kLast, g_note.last, sizeof(s_kLast)) != 0;
}

// ═══ 生命周期 ═════════════════════════════════════════════════════════════
void appNoteStart() {
  noteFsInit();
  g_len = g_cur = g_top = 0;
  g_buf[0] = 0;
  noteDraftLoad();
  g_note.queued = noteQueueCount();
  g_needSave = false;
  g_dirty = true;
  drawNote(true);
  noteSnapStatus();
  g_dirty = false;
}

void appNoteLoop() {
  uint32_t now = millis();
  // 回执超时判定要准点，单独跑（不重绘，等超时回调自己标脏）
  if (g_note.sending && (now - g_sendAt > NOTE_ACK_WAIT_MS)) noteOnTimeout();

  // 补发队列 2 秒一查：noteQueueFirst() 要扫一遍目录，
  // 每帧都扫会把 flash 读爆，这里必须节流。
  static uint32_t lastFlush = 0;
  if (!g_note.sending && (now - lastFlush > 2000)) {
    lastFlush = now;
    noteFlushQueue();
  }

  // 草稿落盘：停手 2 秒后写一次（别每敲一个键都写 flash）
  if (g_needSave && (now - g_editAt > 2000)) noteDraftSave();

  // 状态行字段变了 → 也算脏
  if (noteStatusChanged()) g_dirty = true;

  if (!g_dirty) return;          // 静态界面：没事就一帧都不画
  g_dirty = false;
  noteSnapStatus();
  drawNote(false);
}

void appNoteTouch(int16_t x, int16_t y) {
  if (hitBox(x, y, NTE_BOX_SEND[0], NTE_BOX_SEND[1], NTE_BOX_SEND[2], NTE_BOX_SEND[3])) {
    if (g_len == 0) return;
    if (noteSend(g_buf)) { noteClearAll(); noteDraftSave(); }
    return;
  }
  if (hitBox(x, y, NTE_BOX_CLR[0], NTE_BOX_CLR[1], NTE_BOX_CLR[2], NTE_BOX_CLR[3])) {
    noteClearAll();
    noteDraftSave();
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_CLEARED));
    g_dirty = true;
  }
}

void appNoteKey(uint8_t raw) {
  if (raw >= 0x20 && raw <= 0x7E) { noteInsert((char)raw); return; }
  if (raw == 0x08 || raw == 0x7F) { noteBackspace();      return; }
  if (raw == 0x0D)                { noteInsert('\n');     return; }
  if (raw == 0x09)                { noteInsert(' ');      return; }   // Tab 当空格，省得乱跳
  if (raw == 155) {                                                    // ALT+S = 发送
    if (g_len > 0 && noteSend(g_buf)) { noteClearAll(); noteDraftSave(); }
    return;
  }
  if (raw == 156) {                                                    // ALT+D = 清空
    noteClearAll();
    noteDraftSave();
    snprintf(g_note.last, sizeof(g_note.last), "%s", S(S_NOTE_CLEARED));
    g_dirty = true;
    return;
  }
  if (raw == 191 && g_cur > 0)   { g_cur--; g_dirty = true; return; }  // FN+←
  if (raw == 193 && g_cur < g_len) { g_cur++; g_dirty = true; return; } // FN+→
}

void appNoteStop() {
  if (g_needSave) noteDraftSave();   // 退出前把没落盘的改动补上（没改动就不写）
}

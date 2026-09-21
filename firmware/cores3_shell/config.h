// CoreS3 OS —— 全局配置
#pragma once

#define FW_VERSION "0.7.1"

// —— 蓝牙设备名（Mac 蓝牙列表里显示的名字）——
#define DEVICE_NAME "CoreS3 Controller"

// —— Keyboard3 物理键映射（控制器应用内）——
// 键码取自 M5Faces_Keyboard3.hpp 的 keyboard3_key_t 枚举：
//   BS=0x08  TAB=0x09  ENTER=0x0D  ESC=0x1B  SPACE=0x20  DEL=0x7F
//
// ★ 最大的坑（v0.4.9 修）：键盘上标着「del」的那颗发的是 0x7F，
//   标着「⌫/BS」的那颗才是 0x08。v0.4.8 及以前只映射了 0x08，
//   于是用户按 del 键一点反应都没有 —— 改键前先确认它发的到底是哪个码。
#define KB_KEY_REC     ' '     // 空格 = 录音（按一下开、再按一下停）
#define KB_KEY_SEND    0x0D    // 回车 = 确认（发送）
#define KB_KEY_CANCEL  0x7F    // del  = 取消（停止生成 + 清空草稿）
#define KB_KEY_CANCEL2 0x08    // BS   = 同上，两颗删除键都能取消，按哪个都行
#define KB_KEY_PREV    '1'     // 上一个对话
#define KB_KEY_NEXT    '2'     // 下一个对话
#define KB_KEY_NEW     '3'     // 新任务

// —— 录音（语音输入）——
// 两种模式，在「设置 → 录音方式」里切换，存 NVS：
//
//  REC_MODE_WB  = WorkBuddy 内置语音（Cmd+D）★ 默认
//    从应用包里读出来的：快捷键表 id = "toggle-voice-recording"，
//    defaultMac = "Meta+D"，scope = "app"，处理代码是
//    window.addEventListener("keydown", ...) —— 挂在 window 上，
//    所以【不需要输入框有焦点】也能触发，正好绕开「新建对话后焦点丢失」的坑。
//    录音走 WorkBuddy 自己的 ASR，识别结果由应用自己写进输入框。
//
//  REC_MODE_SYS = macOS 系统听写（默认 Control+Option+D）
//    这个是系统级的，必须有可编辑的输入点，焦点不在输入框时会提示无落点。
//    用它就要配合下面的「新建后 Tab」把焦点挪回去。
#define REC_MODE_WB    0
#define REC_MODE_SYS   1
#define REC_MODE_DEFAULT REC_MODE_WB

#define WB_VOICE_MODS KEY_MOD_LGUI
#define WB_VOICE_KEY  KEY_D         // Cmd+D

// 系统听写快捷键：要在 Mac「系统设置 → 键盘 → 听写 → 快捷键」里设成同一个组合
#define DICT_MODS  (KEY_MOD_LCTRL | KEY_MOD_LALT)
#define DICT_KEY   KEY_D

// —— 切换对话（VibeKey 旋钮语义：左旋 = 上一个，右旋 = 下一个）——
// 发的是 Cmd+[ 与 Cmd+]。已确认这就是 WorkBuddy 内建的 prev-task / next-task
// 默认绑定（应用包里的快捷键定义表：Meta+[ / Meta+]），不用在 Mac 侧再映射。
#define PREV_CHAT_MODS KEY_MOD_LGUI
#define PREV_CHAT_KEY  KEY_LEFTBRACE    // [
#define NEXT_CHAT_MODS KEY_MOD_LGUI
#define NEXT_CHAT_KEY  KEY_RIGHTBRACE   // ]

// —— 新任务（旋钮点击）——
// Cmd+N 同样是 WorkBuddy 内建的 new-conversation。
#define NEW_CHAT_MODS KEY_MOD_LGUI
#define NEW_CHAT_KEY  KEY_N

// —— 新建后把焦点挪回输入框（v0.4.3 重写）——
//
// 根因（从 WorkBuddy 应用包里读出来的）：
//   APP_SHORTCUT_IDS = [open-settings, open-global-search, new-conversation,
//     new-quick-qa-conversation, prev-task, next-task, locate-current-task,
//     open-search, toggle-sidebar, toggle-artifacts, toggle-fullscreen]
//   —— 这份是挂在 window 上的全局快捷键白名单，【里面没有 send-message】。
//   send-message 的处理代码是输入框内部的 React onKeyDown（handleInsertNewline
//   匹配到 Enter 才 return true 触发发送），所以 Enter 只在【焦点在输入框内】时有效。
//
//   同理 Cmd+A：焦点在输入框 = 全选输入内容；焦点在 body = Chromium 的 select-all，
//   也就是「全选整个页面的文字」——这正是 v0.4.2 里取消键的表现。
//
//   所以「已有对话正常、新建对话不正常」跟新建无关，只跟【焦点在不在输入框】有关：
//   已有对话之所以正常，是因为你之前用鼠标点过输入框，焦点一直在那儿。
//
// 修法：Cmd+N 之后把焦点挪进输入框，共 8 档（0 = 不补按）。
//
//  v0.4.1 ~ v0.4.5 一直走 Tab 遍历，实测都落不进输入框：Electron 里 Tab 的落点
//  跟 DOM 顺序强相关（侧边栏一堆按钮排在前面），而且新对话的输入框什么时候挂载
//  也不确定，800ms 时很可能还没渲染出来。所以 v0.4.6 新增 FOCUS_WAKE 并设为默认：
//
//  FOCUS_WAKE = 发一次 Cmd+D、隔 500ms 再发一次，借 WorkBuddy 自己的代码路径聚焦。
//    原理：应用包里语音结果的回调是  text => { appendInputText(text); focusInput(); }
//    —— 一次完整的录音开关结束时，应用会主动把焦点放回输入框。
//
//    ★ v0.4.8 实测证伪（OCR 对照，见同目录实验记录）：无声时 WorkBuddy 走的是
//    「未检测到语音内容，请重试」这条分支，焦点并没有回到输入框，反而被语音面板
//    带走了 —— 表现是紧随其后的 Cmd+A 全选失效、Backspace 只删掉一个字符。
//    它正是 v0.4.6/0.4.7 里「已有对话取消失效」的元凶（取消当时跟着这个设置跑）。
//    所以默认档改回 FOCUS_OFF：宁可不补按，也别发会把焦点弄丢的键。
//    WAKE 档本身保留，只是不再默认，想试就串口 focus 7。
//
//  其余 6 档（S-TAB1~3 / TAB1~3）保留作兜底，可在「设置 → 新建后聚焦」里循环切换。
#define NEW_FOCUS_DELAY_MS 1800   // 等新对话渲染完再动手（旧值 800ms，实测太早）
#define NEW_FOCUS_TAB_GAP  60
#define FOCUS_WAKE_GAP_MS  500    // WAKE 档两次 Cmd+D 之间的间隔
#define FOCUS_DELAY_MIN_MS 200
#define FOCUS_DELAY_MAX_MS 5000

// 聚焦模式编号（存 NVS），0 = 不补按
enum {
  FOCUS_OFF    = 0,
  FOCUS_S_TAB1 = 1,   // Shift+Tab x1（反向遍历，v0.4.5 及以前的默认）
  FOCUS_S_TAB2 = 2,
  FOCUS_S_TAB3 = 3,
  FOCUS_TAB1   = 4,   // Tab x1（正向，兜底）
  FOCUS_TAB2   = 5,
  FOCUS_TAB3   = 6,
  FOCUS_WAKE   = 7    // Cmd+D 双击，借 WorkBuddy 的 focusInput() ★ 默认
};
#define FOCUS_MODE_COUNT 8
#define FOCUS_MODE_DEFAULT FOCUS_OFF   // v0.4.8：WAKE 实测会带走焦点，默认改回不补按

// —— 取消键 ——
// 对齐 VibeKey：它的取消键映射的就是【ESC】。
//
//  CLEAR_MODE_ESC_WIPE（★ 默认）：Esc + 清空草稿，两连击。
//    Esc   = stop-generation（WorkBuddy 快捷键表里 Escape 的默认绑定），
//            不依赖输入框焦点，先中断正在跑的任务 / 退出输入态；
//    WIPE  = Cmd+A + Backspace，把输入框里的草稿真正清掉，
//            只在前一步之后焦点确实在输入框里时才生效。
//    （v0.4.6：删除键从 KEY_DELETE 改成 KEY_BACKSPACE。HID 的 Delete(0x4C) 在 Mac
//      上等于 Fn+Delete「向前删」，Electron/Chromium 里选区存在时经常不生效；
//      Backspace(0x2A) 才是 Mac 键盘上那颗 delete 键真正发的码。）
//    （焦点在 body 时 Cmd+A 会退化成 Chromium 的 select-all —— 只是全选页面文字、
//      不会删除任何东西，属于「没清掉」而非「误删」，可接受。）
//
//  CLEAR_MODE_ESC：只发一个 Esc，不清草稿（VibeKey 原生行为）。
//
//  CLEAR_MODE_WIPE：只发 Cmd+A + Delete，跳过 Esc。
//
// 串口 clear [0|1|2] 切换，存 NVS（key clrmode）。
#define CLEAR_MODE_ESC      0
#define CLEAR_MODE_WIPE     1
#define CLEAR_MODE_ESC_WIPE 2
#define CLEAR_MODE_DEFAULT  CLEAR_MODE_ESC_WIPE

#define CLEAR_ESC_MODS  0
#define CLEAR_ESC_KEY   KEY_ESCAPE

#define CLEAR_WIPE_MODS KEY_MOD_LGUI
#define CLEAR_WIPE_KEY  KEY_A

// 两连击之间的间隔：Esc 先让界面停下来，再全选删除，太快会抢在状态切换之前
#define CLEAR_ESC_GAP_MS  220
#define CLEAR_WIPE_GAP_MS 80
// 取消时若走 WAKE 唤醒焦点，只等这么久（新建那次等的是 g_focusDelayMs）
#define CLEAR_FOCUS_WAIT_MS 250

// —— 取消前要不要先「唤醒焦点」（v0.4.8 新增，★默认 0 = 不要）——
//
// 这是 v0.4.6 / v0.4.7 把已有对话的取消弄坏的直接原因，实测经过：
//   当时 actClear() 会跟着「新建后聚焦」的设置跑一次唤醒，而该设置默认是
//   FOCUS_WAKE（Cmd+D 双击）→ 每次按取消都真的开关一次 WorkBuddy 语音，
//   语音面板把焦点从输入框带走 → 后面的 Cmd+A 全选失效、Backspace 退化成
//   「删掉光标前一个字符」，表现就是「取消清不掉草稿，只少了一个字母」。
//   （OCR 对照：打字 QZDD → wake+wipe 后只剩 QZD；esc+wipe 则清空到 placeholder。）
//
//   而已有对话里焦点本来就好好地在输入框内（你能打字就说明在），根本不需要唤醒，
//   唤醒只是把焦点弄丢。所以默认 0：取消时【一个多余按键都不发】。
//
// 设成 1 才会在 WIPE 之前跑一次当前 focus 设置的唤醒动作（Cmd+D 双击 / Tab 等）。
// 只有一种场景值得开：新建对话那种焦点确实丢了、且你确认 WAKE 不会碍事。
// 注意 NVS 不随重烧重置，改这里要重新烧录才生效。
#define CANCEL_WAKE_FOCUS 0

// —— 番茄钟时长（分钟）——
#define POMODORO_WORK_MIN  25
#define POMODORO_BREAK_MIN 5

// —— 音乐遥控（v0.5.0 起取代传感器模块）——
//
// 思路：设备【不解码音频】，只当遥控器。声音永远由 Mac 出，设备只做两件事：
//   ① 发 HID Consumer Control 媒体键（播放/暂停、上下曲、音量）—— 零 Mac 端软件；
//   ② 收 Mac 端推过来的元数据（歌名/歌手/专辑/进度/封面）用于显示。
//
// ② 走的是同一个 BLE 连接上的【自定义 GATT 服务】—— 不用配 Wi-Fi、不依赖局域网。
//   HijelHID 库在 begin() 里已经建好 NimBLEServer（HID 就是挂在它上面的服务），
//   我们拿 NimBLEDevice::getServer() 回来再 createService 一个即可，两者共存。
//
// UUID 用自定义 128 位（别跟 NUS 6e400001 撞，也别跟 HID 0x1812 撞）。
#define MUSIC_SVC_UUID "c3a10001-5f4e-4a3b-9d2c-1e0f8a7b6d5c"
#define MUSIC_CHR_META "c3a10002-5f4e-4a3b-9d2c-1e0f8a7b6d5c"   // WRITE：文本元数据
#define MUSIC_CHR_ART  "c3a10003-5f4e-4a3b-9d2c-1e0f8a7b6d5c"   // WRITE：封面 JPEG（分包）

// 元数据文本格式（| 分隔，Mac 端按这个拼）：
//   title|artist|album|state|posMs|durMs
//   state: 0=暂停 1=播放
// 例：晴天|周杰伦|叶惠美|1|42000|267000
#define MUSIC_TITLE_MAX  56
#define MUSIC_ARTIST_MAX 40
#define MUSIC_ALBUM_MAX  40
#define MUSIC_META_MAX   200

// 封面：Mac 端压成小 JPEG（160x160 q0.6）后分包推过来。
// 分包协议：首包 [0]=0xA5 + [1..4]=总长(u32 小端)，数据从 [5] 开始；
//           续包 [0]=0x5A，数据从 [1] 开始；收够总长就置 artReady。
//
// ⚠ 缓冲上限必须比实际 JPEG 大：QQ 音乐的封面实测压出来是 14777 字节，
//   而早期这里写的是 12288 → 首包里的 total 一进来就被判「超限」直接丢弃，
//   后面的续包却照样往缓冲里塞，结果 artLen 塞满、artTotal 还是 0，
//   artReady 永远置不起来，界面上一直显示 NO ART（串口看就是 art=12288/0）。
//   现在放到 32KB（PSRAM 有 8MB，随便用），并加了 artStarted 门禁（见 app_music.ino）。
#define MUSIC_ART_MAX    32768    // 封面缓冲上限（CoreS3 有 PSRAM，用 ps_malloc）
#define MUSIC_ART_HDR    5        // 首包头部字节数
#define MUSIC_ART_FIRST  0xA5
#define MUSIC_ART_NEXT   0x5A

// 进度条：Mac 端推一次快照，之后设备按本地时间自己往前走，
// 超过这个时长还没收到新快照就认为已停止更新（避免进度条一直爬）。
#define MUSIC_STALE_MS   15000

// —— 记事本（v0.6.0）：Keyboard3 打字 → BLE 推给 Mac → 落进 Obsidian ——
//
// 方向跟音乐模块相反：音乐是 Mac 写、设备读；记事本是【设备 notify、Mac 订阅】。
// 挂服务的方式完全一样（拿 NimBLEDevice::getServer() 在同一个 server 上 createService）。
//
// 通道选 BLE 而不是别的，理由记一下：
//   · 不依赖焦点（HID 打字那条路要盯着光标在哪，前面踩过太多坑）；
//   · 不依赖插线（USB CDC 要连着线才能推）；
//   · 断线能排队（草稿和待发队列都落 FFat，重连后补发，不丢内容）。
#define NOTE_SVC_UUID "c3a10011-5f4e-4a3b-9d2c-1e0f8a7b6d5c"
#define NOTE_CHR_OUT  "c3a10012-5f4e-4a3b-9d2c-1e0f8a7b6d5c"   // NOTIFY：设备 → Mac
#define NOTE_CHR_ACK  "c3a10013-5f4e-4a3b-9d2c-1e0f8a7b6d5c"   // WRITE：Mac → 设备（回执）

// 一条速记的上限。2000 字节在 8x8 点阵下是 13 行 × 38 列的量，够写一段话。
// 缓冲区放 PSRAM，别占 512KB 的内部 RAM。
#define NOTE_MAX        2000
// 分包协议（跟封面那条对称，只是方向反过来）：
//   首包 [0]=0xA5 + [1..2]=总长(u16 小端)，数据从 [3] 开始；
//   续包 [0]=0x5A，数据从 [1] 开始。
// Mac 端收够总长就拼起来落盘，然后往 ACK 写一个字节。
#define NOTE_PKT_FIRST  0xA5
#define NOTE_PKT_NEXT   0x5A
#define NOTE_PKT_HDR    3        // 首包头部字节数（1 字节标记 + 2 字节总长）
#define NOTE_ACK_OK     0x01     // Mac 已落盘
// v0.6.2：Mac → 设备「顺手对时」。复用 ACK 这条 WRITE 通道，不新增特征：
//   [0]=0x02, [1]=年-2000, [2]=月, [3]=日, [4]=时, [5]=分, [6]=秒（共 7 字节）
// 传年月日而不是 epoch —— 设备端没设 TZ，走 epoch 要处理时区，多一个出错点。
#define NOTE_ACK_TIME   0x02
#define NOTE_ACK_WAIT_MS 5000    // 发完等多久没回执就算失败（留在队列里）
#define NOTE_CHUNK_MAX  240      // 单包载荷上限（保守值，MTU 没协商大也安全）

// 队列：每条待发 / 发送失败的速记单独存一个文件，编号用 NVS 里的单调计数器，
// 这样重开机也能按序补发。分区是 app3M_fat9M_16MB，有 9.9MB 的 ffat 可用。
// 存储用 LittleFS（partitions.csv 里那块 subtype 0x83 的 9.9MB 分区，label=lfs）。
// ★★ 挂载点和 open() 路径是两回事，别搞混：
//   NOTE_FS_MOUNT 给 begin() 用（VFS 挂载点，必须是 "/littlefs" 这种，注册到 "/" 会被拒）；
//   NOTE_FS_ROOT  拼 open() 路径用 —— 这个 core 里 open() 传的是 littlefs 内部路径，
//   再套一层挂载点前缀（"/littlefs/xxx"）必然打不开。
//   （FFat 当初"能挂载但写不了"是同一个坑，不是文件系统坏了。）
#define NOTE_FS_MOUNT   "/littlefs"
#define NOTE_FS_ROOT    "/"
#define NOTE_Q_PREFIX   "nq"
#define NOTE_Q_SUFFIX   ".txt"
#define NOTE_QUEUE_MAX  32

// —— Bottom3 两侧灯带 ——
// 必须保持 0：Adafruit NeoPixel 在 ESP32-S3 上有已知崩溃 bug（官方 issue #429），
// show() 走 RMT，无线电（BLE/Wi-Fi）繁忙时信号量超时触发断言并重启。
#define ENABLE_BOTTOM3_LED 0
#define BOTTOM3_LED_PIN    25
#define BOTTOM3_LED_COUNT  10

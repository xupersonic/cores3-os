# CoreS3 OS

**[简体中文](#-简体中文) · [English](#-english)**

把 M5Stack CoreS3 + Faces Bottom3 + Keyboard3 变成一台能控电脑、能记事、能玩的掌上终端。
六宫格桌面 · 纯 BLE（不用 Wi-Fi、不用在 Mac 上装驱动）· 中英双语 · 固件 v0.7.1

> `docs/` 下的四个页面都自带 **中文 / EN 切换**（右上角按钮），并非只有一份简介。

---

<a id="-简体中文"></a>
## 🇨🇳 简体中文

```
┌───────────────┬───────────────┬───────────────┐
│   控制器      │   记事本      │   番茄钟      │
│  BLE HID      │  → Mac 速记   │  25/5 计时    │
├───────────────┼───────────────┼───────────────┤
│   游戏        │   音乐遥控    │   设置        │
│ 方块 + 2048   │ 媒体键 + 封面 │ 亮度/语言/…   │
└───────────────┴───────────────┴───────────────┘
```

> ⚠️ **免责声明**：本项目会改写 flash 分区表（3MB app + 9.9MB littlefs），请按自己的风险操作，
> 作者不对设备损坏或数据丢失负责。烧录前确认你买的是 **16MB Flash 的 CoreS3**。
>
> 快捷键映射基于 **2026-09 的 WorkBuddy 版本**实测有效；对方版本更新后可能失效
> （WorkBuddy 是第三方产品，本项目与其非官方、无关联）。

### 一、硬件

| 部件 | 说明 |
|---|---|
| M5Stack **CoreS3** | ESP32-S3，16MB Flash + 8MB PSRAM，320×240 触屏 |
| **Faces Bottom3** | 底座，供电 + 扩展 |
| **Keyboard3** | faces 套件里的实体键盘（本项目的主要输入方式） |

组装步骤、拨档开关位置、首次上电 → 见 [`docs/hardware-setup.html`](docs/hardware-setup.html)。

### 二、六个应用

| 应用 | 干什么 | 主要操作 |
|---|---|---|
| **控制器** | BLE HID 键盘 + 消费类控制键，远程驱动 Mac 上的 WorkBuddy | `空格`=录音开始/停止，`回车`=确认，`del`=取消；另有录音/发送/取消/新建/上一条/下一条 六个快捷键 |
| **记事本** | Keyboard3 打字 → 经 BLE 推到 Mac，追加进一个 markdown 文件（可指向 Obsidian vault）。**离线也照写**，连上后自动补发，且带的是**书写时间**不是送达时间 | 键盘直接打字，`Enter`=分段发送 |
| **番茄钟** | 25 分钟专注 / 5 分钟休息，结束蜂鸣 | 点屏开始/暂停/重置 |
| **游戏** | 俄罗斯方块（10×18，7 种方块，消行加分、等级越高落越快）+ 2048（4×4，最高分存 NVS 掉电不丢） | `WASD`/方向键操作，`空格`=硬降，`P`=暂停，`N`=新局，**`M`=静音开关**（存 NVS） |
| **音乐遥控** | 媒体键（播放/暂停、上下曲、音量）+ 把 Mac 上「正在播放」的**歌名与封面**回传到屏上 | 屏上按钮或键盘 |
| **设置** | 亮度 / 语言 / 自动休眠 / 蓝牙广播开关 / 关于 | 点屏 |

导航：左边缘右滑 = 返回，`Esc` = 返回；二级页（游戏菜单）会先回一级再回桌面。

### 三、编译与烧录

```bash
# 1) 装依赖（后四个在索引里，最后一个不在）
arduino-cli lib install M5Unified M5GFX M5Faces NimBLE-Arduino
git clone https://github.com/HijelHub/HijelHID_BLEKeyboard ~/Documents/Arduino/libraries/

# 2) 加上 ESP32 板卡支持（只需一次）
arduino-cli config add board_manager.additional_urls \
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32

# 3) 编译 + 烧录（脚本自动探测串口 /dev/cu.usbmodem*，并在烧完自动重启 + 同步 RTC）
cd tools && ./fw.sh
```

- FQBN：`m5stack:esp32:m5stack_cores3`
- 分区表来自 `firmware/cores3_shell/partitions.csv`（platform 的 prebuild hook 会优先用它）
- `./fw.sh --fast` 跳过 clean（**改了 `.h` 别用**，arduino 缓存会吃旧值）；`--no-up` 只编译
- **为什么烧完必须重启**：CoreS3 走 native USB(CDC)，没有 EN/RTS 硬件复位线，esptool 的软复位偶尔不彻底，
  所以脚本统一发固件自带的 `reboot` 串口命令硬重启一次
- arduino-cli / python3 都是**自动探测**的（环境变量 `ARDUINO_CLI` / `PYTHON` → PATH → 常见安装位置），
  不用改脚本
- 本机实测（v0.7.1）：`1383859 B`（43% Flash）、`43748 B`（13% RAM）

### 四、Mac 端 Bridge（可选，但记事本和音乐封面要用它）

```bash
cd mac-bridge && ./build.sh && open MusicBridge.app
./bridge.sh install     # 设开机自启（launchd，崩了自动拉起）
```

- 首次启动要在 **系统设置 → 隐私与安全性 → 蓝牙** 里给它授权，否则日志会停在 `✗ 蓝牙未授权`
- 速记落点默认 `~/Documents/face3notes.md`；想直接写进 Obsidian：
  `export CORES3_NOTES_FILE="$HOME/Documents/Obsidian_Vault/MyVault/face3notes.md"`
  （LaunchAgent 自启时在 plist 的 `EnvironmentVariables` 里设同一个变量）
- 仓库里带的是模板 `com.cores3os.bridge.plist.example`（不含任何人的真实路径），
  `./bridge.sh install` 会自动把它生成本地 plist 并填上当前路径
- 歌名/封面依赖可选的 `media-control`（Homebrew）；没装也能用媒体键，只是屏上不显示曲目

### 五、串口命令（`screen`/`minicom` 或 `tools/ser.py`）

```bash
python3 tools/ser.py 'status'      # 一条命令一次，省得开串口工具
```

| 命令 | 作用 |
|---|---|
| `status` / `apps` | 固件信息 / 列出六个应用 |
| `launch <n>` | 直接进第 n 个应用 |
| `key <0-255>` | 模拟一次物理按键（走真实 `onKey` 映射，验键位用） |
| `touch <x> <y>` | 模拟一次点屏（走真实 `onTouch`，**验按钮热区不用动手戳**） |
| `game` / `game <0\|1\|2>` / `game mute` | 游戏状态 + 盘面转储 / 切菜单·方块·2048 / 切静音 |
| `note` / `music` | 记事本队列状态 / 当前曲目与封面进度 |
| `time [YYYY-MM-DD HH:MM:SS]` | RTC 对时（**RTC 没有后备电池**，重烧后时间归零） |
| `lang [cn\|en]` / `bright <n>` / `sleep <n>` | 语言 / 亮度 / 自动休眠 |
| `focus <0-7>` / `fdelay <ms>` / `rec <0\|1>` / `clear <0\|1\|2>` | 控制器行为微调（新建后聚焦策略、发送延迟、录音方式、取消键） |
| `fs` / `fs2` | flash 文件系统体检 |
| `unpair` / `reboot` | 清蓝牙绑定 / 重启 |

### 六、踩过的坑（本仓库最有价值的部分）

1. **`del` 键是 `0x7F` 不是 `0x08`** —— Keyboard3 上标 `del` 的那颗发 `0x7F`（`0x08` 是 `⌫/BS`）。
   只映射 `0x08` 的话按 del 一点反应都没有。
2. **littlefs 分区 subtype 必须是 `0x83`**（`0x82` 是 fat）。写错会报
   `No data partition with subtype littlefs found`；而 FFat 在这块板上能挂载但**写不了文件**。
3. **LittleFS 的 `open()` 路径不能带挂载点前缀**：`open("/x.txt")` 行，`open("/littlefs/x.txt")` 必失败。
4. **`partitions.csv` 不能有 UTF-8 BOM**（很多文本工具写出来带），否则 `gen_esp32part` 报
   `Error at line 1: Value 'Type' is not valid`。
5. **RTC 无后备电池**：掉电/重烧后时间归零，而设备自己没有任何对时来源（没 Wi-Fi、BLE 不给时间），
   只能从串口喂 —— `fw.sh` 每次烧完会自动调 `rtc.sh`。
6. **静态界面不要按节拍重绘**：界面上没有「自己会变」的内容时挂 1Hz tick = 每秒整屏重绘一次，
   肉眼看着一闪一闪。改成事件驱动 + 标脏；只有时钟/倒计时/方块下落这种真动态的才 tick，
   而且**只重画那一块**。配套两条：局部重画前先 `clearArea`（否则数字位数变短会留残影），
   「要重画」和「要存盘」用两个标志（否则重绘清标志会把存盘预约一起丢掉）。
7. **自测「会自己走」的固件行为必须用长连接串口会话**：逐条发命令时每条要重开串口、等 1.5 秒，
   这 1.5 秒里重力已经把方块吃掉了 —— 你会得到「消行不触发」这种**假阴性**。
8. **烧录后必须重启**（native USB 无硬件复位线）。
9. **BLE 广播**：NimBLE 默认「一连上就停广播」，而自定义 GATT 服务和系统 HID 不是同一个 client，
   所以断连后要手动 `NimBLEDevice::startAdvertising()` 才能重连。

### 七、目录结构

```
m5stack-face3keyboard-os/
├── README.md  LICENSE  NOTICE  CHANGELOG.md  .gitignore
├── firmware/cores3_shell/
│   ├── cores3_shell.ino    外壳：桌面 / 导航 / 串口命令 / NVS
│   ├── config.h            ★ 所有可调常量（改键、改行为先翻这里）
│   ├── shell.h             extern 与公共函数声明
│   ├── i18n.h / i18n.ino   中英双语表（static_assert 对账，漏一个编译不过）
│   ├── partitions.csv      3MB app + 9.9MB littlefs
│   ├── app_controller.ino  BLE HID 控制器
│   ├── app_notepad.ino     速记 → BLE → Mac
│   ├── app_pomodoro.ino
│   ├── app_games.ino       俄罗斯方块 + 2048
│   ├── app_music.ino       媒体键 + 歌名/封面（思路参考见 NOTICE §6）
│   ├── app_settings.ino
│   └── *.ino.bak           下线应用存档（时钟 / 传感器 / 系统信息）
│                           —— 桌面固定 6 格，每加一个就得换掉一个，留着存档说明怎么腾的
├── tools/      fw.sh（编译+烧录+冒烟） rtc.sh（对时） ser.py（串口助手）
├── mac-bridge/ Bridge.swift  build.sh  bridge.sh  com.cores3os.bridge.plist.example
└── docs/       hardware-setup.html  cheat-sheet.html  controller-guide.html
                pairing.html（v0.3 时代 Wi-Fi 方案的存档，已被 BLE 取代）
```

### 八、致谢

- **音乐遥控**的整体思路参考了 [isalicema/m5-faces-core-s3](https://github.com/isalicema/m5-faces-core-s3)（MIT）：
  「设备只做控制与显示，声音永远由 Mac 出；控制走 HID 媒体键，正在播放的信息回传到设备屏幕」。
  本仓库的实现是独立编写的（NimBLE 自定义 GATT 回传歌名/歌手/专辑/进度，封面走 160×160 JPEG 分包协议），
  未复制其代码。感谢原作者的分享。
- 组装手册里的产品图来自 M5Stack 官方文档与产品页，版权归 M5Stack Technology Co., Ltd.。
- 其余第三方依赖与商标声明见 [NOTICE](NOTICE)。

### 九、许可

MIT，见 [LICENSE](LICENSE)。第三方依赖与商标声明见 [NOTICE](NOTICE)。

---

<a id="-english"></a>
## 🇬🇧 English

A pocket terminal built on **M5Stack CoreS3 + Faces Bottom3 + Keyboard3**: a 6-tile
desktop, pure BLE (no Wi-Fi, no driver or daemon needed on the Mac), bilingual CN/EN.
Firmware v0.7.1.

```
┌───────────────┬───────────────┬───────────────┐
│  CONTROLLER   │     NOTES     │   POMODORO    │
│   BLE HID     │ → Mac (md)    │  25/5 timer   │
├───────────────┼───────────────┼───────────────┤
│    GAMES      │     MUSIC     │   SETTINGS    │
│ Tetris + 2048 │ media + art   │ bright/lang/… │
└───────────────┴───────────────┴───────────────┘
```

> ⚠️ **Disclaimer**: this project **rewrites the flash partition table**
> (3MB app + 9.9MB littlefs). Use it at your own risk; the author is not
> responsible for damaged hardware or lost data. Make sure you own a
> **16MB-flash CoreS3** before flashing.
>
> The shortcut mapping was verified against the **2026-09 build of WorkBuddy**
> and may break after updates. WorkBuddy is a third-party product; this project
> is unofficial and unaffiliated.

### 1. Hardware

| Part | Notes |
|---|---|
| M5Stack **CoreS3** | ESP32-S3, 16MB flash + 8MB PSRAM, 320×240 touch screen |
| **Faces Bottom3** | base: battery, charging, expansion ports |
| **Keyboard3** | the physical keyboard panel from the Faces kit — the main input device here |

Assembly steps, switch positions and first power-on → [`docs/hardware-setup.html`](docs/hardware-setup.html).

### 2. The six apps

| App | What it does | How you drive it |
|---|---|---|
| **Controller** | BLE HID keyboard + consumer-control keys that drive WorkBuddy on a Mac | `Space` = start/stop recording, `Enter` = confirm, `del` = cancel; plus six shortcuts: record / send / cancel / new / prev / next |
| **Notes** | Type on the Keyboard3 → shipped over BLE to the Mac → appended to a markdown file (can point at an Obsidian vault). **Works offline**: queued and replayed on reconnect, keeping the *writing* time, not the delivery time | Just type; `Enter` = send the paragraph |
| **Pomodoro** | 25 min focus / 5 min break, beeps at the end, counts completed rounds | Tap start/pause/reset |
| **Games** | Tetris (10×18 board, 7 pieces, line clears score, faster as the level rises) + 2048 (4×4, best score kept in NVS across power loss) | `WASD` / arrow keys, `Space` = hard drop, `P` = pause, `N` = new game, **`M` = mute toggle** (stored in NVS) |
| **Music** | Media keys (play/pause, prev/next, volume) + pushes the Mac's **now-playing title and artwork** back to the screen | On-screen buttons or the keyboard |
| **Settings** | Brightness / language / auto-sleep / BLE advertising / about | Tap |

Navigation: swipe right from the left edge = back, `Esc` = back. Second-level pages
(e.g. the games menu) go back one level first, then to the desktop.

### 3. Build and flash

```bash
# 1) Install libraries (the last one is NOT in the index)
arduino-cli lib install M5Unified M5GFX M5Faces NimBLE-Arduino
git clone https://github.com/HijelHub/HijelHID_BLEKeyboard ~/Documents/Arduino/libraries/

# 2) Add ESP32 board support (once)
arduino-cli config add board_manager.additional_urls \
  https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32

# 3) Build + flash (auto-detects /dev/cu.usbmodem*, reboots and syncs the RTC afterwards)
cd tools && ./fw.sh
```

- FQBN: `m5stack:esp32:m5stack_cores3`
- The partition table comes from `firmware/cores3_shell/partitions.csv` (the platform's
  prebuild hook picks it up first)
- `./fw.sh --fast` skips `--clean` (**don't use it after editing a `.h`** — the Arduino
  cache will happily reuse stale values); `--no-up` compiles only
- **Why a reboot is mandatory after flashing**: CoreS3 uses native USB (CDC) with no
  EN/RTS reset line, so esptool's software reset is sometimes incomplete. The script
  therefore always sends the firmware's own `reboot` serial command.
- `arduino-cli` and `python3` are **auto-detected** (env vars `ARDUINO_CLI` / `PYTHON`
  → PATH → common install locations); no need to edit the scripts
- Measured here (v0.7.1): `1383859 B` (43% flash), `43748 B` (13% RAM)

### 4. Mac-side Bridge (optional, but Notes and artwork need it)

```bash
cd mac-bridge && ./build.sh && open MusicBridge.app
./bridge.sh install     # launchd auto-start, restarted automatically if it crashes
```

- On first launch you must grant permission in **System Settings → Privacy & Security →
  Bluetooth**, otherwise the log stops at `✗ 蓝牙未授权` (Bluetooth not authorised)
- Notes land in `~/Documents/face3notes.md` by default. To write straight into Obsidian:
  `export CORES3_NOTES_FILE="$HOME/Documents/Obsidian_Vault/MyVault/face3notes.md"`
  (for the launchd job, set the same variable in the plist's `EnvironmentVariables`)
- The repo ships a template `com.cores3os.bridge.plist.example` (no real paths);
  `./bridge.sh install` generates the local plist with your paths filled in
- Title/artwork rely on the optional `media-control` CLI (Homebrew). Without it the media
  keys still work, the screen just won't show the track.

### 5. Serial commands (via `screen`, `minicom`, or `tools/ser.py`)

```bash
python3 tools/ser.py 'status'      # one command per run, no serial terminal needed
```

| Command | Effect |
|---|---|
| `status` / `apps` | firmware info / list the six apps |
| `launch <n>` | jump into app n |
| `key <0-255>` | simulate one physical key (goes through the real `onKey` mapping) |
| `touch <x> <y>` | simulate one tap (goes through the real `onTouch` — **test button hot-zones without touching the screen**) |
| `game` / `game <0\|1\|2>` / `game mute` | game status + board dump / switch menu·Tetris·2048 / toggle mute |
| `note` / `music` | notepad queue status / current track and artwork progress |
| `time [YYYY-MM-DD HH:MM:SS]` | set the RTC (**it has no backup battery** — time resets after flashing) |
| `lang [cn\|en]` / `bright <n>` / `sleep <n>` | language / brightness / auto-sleep |
| `focus <0-7>` / `fdelay <ms>` / `rec <0\|1>` / `clear <0\|1\|2>` | controller tuning (focus-after-new, send delay, recording mode, cancel key) |
| `fs` / `fs2` | flash filesystem health check |
| `unpair` / `reboot` | clear BLE bonding / reboot |

### 6. Gotchas (the most useful part of this repo)

1. **`del` is `0x7F`, not `0x08`** — on the Keyboard3 the key labelled `del` sends `0x7F`
   (`0x08` is `⌫/BS`). Map only `0x08` and the del key does nothing at all.
2. **The littlefs partition subtype must be `0x83`** (`0x82` is fat). Get it wrong and you
   get `No data partition with subtype littlefs found`; FFat mounts on this board but
   **cannot write files**.
3. **LittleFS `open()` paths must not carry the mount prefix**: `open("/x.txt")` works,
   `open("/littlefs/x.txt")` always fails.
4. **`partitions.csv` must not have a UTF-8 BOM** (many editors add one), otherwise
   `gen_esp32part` reports `Error at line 1: Value 'Type' is not valid`.
5. **The RTC has no backup battery**: time resets on power loss or reflash, and the device
   has no way to fetch time itself (no Wi-Fi, BLE doesn't carry time) — feed it over serial.
   `fw.sh` calls `rtc.sh` after every flash.
6. **Don't repaint static screens on a timer.** With nothing animating, a 1 Hz tick means a
   full-screen repaint every second, and you can *see* it flicker. Go event-driven with a
   dirty flag; only genuinely dynamic things (clock, countdown, falling piece) may tick, and
   then **repaint only that block**. Two corollaries: `clearArea` before a partial repaint
   (otherwise a shortening number leaves ghost digits), and keep "needs redraw" and
   "needs save" as **two separate flags** (clearing one must not drop the other).
7. **Testing self-running firmware behaviour requires one long-lived serial session.**
   Sending commands one at a time reopens the port and waits ~1.5 s each — during which
   gravity has already dropped the piece. You'll conclude "line clears never fire", which is
   a false negative.
8. **Always reboot after flashing** (native USB has no hardware reset line).
9. **BLE advertising**: NimBLE stops advertising once connected, and the custom GATT service
   and the system HID client are different peers — after a disconnect you must call
   `NimBLEDevice::startAdvertising()` again to be able to reconnect.

### 7. Layout

```
m5stack-face3keyboard-os/
├── README.md  LICENSE  NOTICE  CHANGELOG.md  .gitignore
├── firmware/cores3_shell/
│   ├── cores3_shell.ino    shell: desktop / navigation / serial commands / NVS
│   ├── config.h            ★ every tunable constant (start here to remap keys or behaviour)
│   ├── shell.h             externs and shared helpers
│   ├── i18n.h / i18n.ino   CN + EN string tables (static_assert keeps them in sync)
│   ├── partitions.csv      3MB app + 9.9MB littlefs
│   ├── app_controller.ino  BLE HID controller
│   ├── app_notepad.ino     quick notes → BLE → Mac
│   ├── app_pomodoro.ino
│   ├── app_games.ino       Tetris + 2048
│   ├── app_music.ino       media keys + title/artwork (see NOTICE §6 for credits)
│   ├── app_settings.ino
│   └── *.ino.bak           retired apps kept as archives (clock / sensors / system info)
│                           —— the desktop is fixed at 6 tiles: adding one means replacing one
├── tools/      fw.sh (build+flash+smoke)  rtc.sh (clock sync)  ser.py (serial helper)
├── mac-bridge/ Bridge.swift  build.sh  bridge.sh  com.cores3os.bridge.plist.example
└── docs/       hardware-setup.html  cheat-sheet.html  controller-guide.html
                pairing.html (archive of the v0.3 Wi-Fi design, replaced by BLE)
```

### 8. Acknowledgements

- The **music remote** takes its overall design from
  [isalicema/m5-faces-core-s3](https://github.com/isalicema/m5-faces-core-s3) (MIT):
  "the device only controls and displays, audio always comes from the Mac; control goes over
  HID media keys, now-playing info is pushed back to the device screen".
  The implementation here was written independently (NimBLE custom GATT for
  title/artist/album/position, artwork sent as a chunked 160×160 JPEG); no code was copied.
  Thanks to the original author for sharing.
- Product photos in the assembly manual come from M5Stack's official docs and product pages;
  copyright M5Stack Technology Co., Ltd.
- Full dependency and trademark statements: [NOTICE](NOTICE).

### 9. License

MIT — see [LICENSE](LICENSE). Third-party dependencies and trademarks: [NOTICE](NOTICE).

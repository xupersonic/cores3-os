#!/usr/bin/env bash
# CoreS3 OS 一键烧录：编译 → 上传 → 重启 → 抓启动日志冒烟
#
# 用法：
#   ./fw.sh              # 完整流程（clean 编译 + 上传 + 重启 + 冒烟）
#   ./fw.sh --fast       # 跳过 --clean（改了 .h 时别用，arduino 缓存会吃旧值）
#   ./fw.sh --no-up      # 只编译
#
# arduino-cli / python3 都是自动探测的（不写死任何人的绝对路径）：
#   ① 环境变量 ARDUINO_CLI / PYTHON  ② PATH 里的  ③ 常见安装位置
# 找不到就报错退出，不会闷头往下跑。
#
# 为什么烧录后必须重启：ESP32-S3 走的是 native USB(CDC)，没有 EN/RTS 硬件复位线，
# esptool 上传结束后的软复位偶尔不彻底（BLE/NVS 会留旧状态），
# 所以统一用固件自带的 reboot 串口命令做一次硬重启。

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$(cd "$HERE/../firmware/cores3_shell" && pwd)"
FQBN="m5stack:esp32:m5stack_cores3"

# ── 工具链探测：环境变量 → PATH → 常见安装位置 ──────────────────────────
find_tool() {                       # $1=环境变量里的值  $2=命令名  $3..=候选绝对路径
  local envval="$1" cmd="$2"; shift 2
  if [ -n "${envval:-}" ] && [ -x "$envval" ]; then echo "$envval"; return 0; fi
  local p
  p="$(command -v "$cmd" 2>/dev/null || true)"
  if [ -n "$p" ]; then echo "$p"; return 0; fi
  for c in "$@"; do [ -x "$c" ] && { echo "$c"; return 0; }; done
  return 1
}

CLI="$(find_tool "${ARDUINO_CLI:-}" arduino-cli \
      "$HOME/bin/arduino-cli" /usr/local/bin/arduino-cli /opt/homebrew/bin/arduino-cli)" || {
  echo "✗ 找不到 arduino-cli。先装一个（https://arduino.github.io/arduino-cli/）："
  echo "    brew install arduino-cli       # 或"
  echo "    export ARDUINO_CLI=/path/to/arduino-cli"
  exit 1
}
echo "→ arduino-cli: $CLI"
# 分区表来自 sketch 目录里的 partitions.csv（platform 的 prebuild hook 会优先用它）：
# app 3MB + 9.9MB 数据分区，subtype 0x83 = littlefs（★ 0x82 是 fat，别写错，
# 写错会报 "No data partition with subtype littlefs found"）。
# 原来那张 app3M_fat9M_16MB 给的是 subtype=fat，FFat 在这块板上能挂载但写不了文件。
# ★ CSV 不能有 UTF-8 BOM（用文本工具写出来常带），否则 gen_esp32part 报
#   "Error at line 1: Value 'Type' is not valid"。
LOG="/tmp/cores3_boot.log"

CLEAN=1
UPLOAD=1
for a in "$@"; do
  case "$a" in
    --fast)   CLEAN=0 ;;
    --no-up)  UPLOAD=0 ;;
  esac
done

# 端口自动探测（CoreS3 是 native USB CDC，固定是 cu.usbmodem*）
PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"
if [ -z "$PORT" ]; then
  echo "✗ 找不到 CoreS3（/dev/cu.usbmodem* 为空），检查 USB 线是否插上"
  exit 1
fi
echo "→ 端口 $PORT"

cd "$SKETCH"
VER="$(grep -m1 'FW_VERSION' config.h | sed -E 's/.*"([^"]+)".*/\1/')"
echo "→ 固件版本 v$VER"

echo "→ 编译 clean=${CLEAN}"
if [ "$CLEAN" = 1 ]; then
  "$CLI" compile --fqbn "$FQBN" --port "$PORT" --clean . 2>&1 | tail -6
else
  "$CLI" compile --fqbn "$FQBN" --port "$PORT" . 2>&1 | tail -6
fi
[ "${PIPESTATUS[0]}" -ne 0 ] && { echo "✗ 编译失败"; exit 1; }

if [ "$UPLOAD" = 0 ]; then echo "→ 只编译，跳过上传"; exit 0; fi

echo "→ 上传"
"$CLI" upload --fqbn "$FQBN" --port "$PORT" . 2>&1 | tail -4
[ "${PIPESTATUS[0]}" -ne 0 ] && { echo "✗ 上传失败"; exit 1; }

echo "→ 等待设备起来并重启"
rm -f "$LOG"
( cat "$PORT" > "$LOG" 2>&1 & echo $! > /tmp/cores3_cat.pid )
sleep 3
printf 'status\nreboot\n' > "$PORT"
sleep 7
kill "$(cat /tmp/cores3_cat.pid)" 2>/dev/null
rm -f /tmp/cores3_cat.pid

echo "──────── 启动日志 ────────"
grep -v '^[[:space:]]*$' "$LOG" | tail -40
echo "──────────────────────────"
grep -q "CoreS3 OS" "$LOG" && echo "✓ 启动正常" || echo "⚠ 日志里没看到 boot banner，手动确认一下"
grep -qi "assert\|Guru Meditation\|rst:.*panic" "$LOG" && echo "✗ 检测到崩溃" || echo "✓ 无崩溃"

# RTC 没有后备电池，重启后时间会归零 —— 顺手把 Mac 的时间同步过去
echo "→ 同步 RTC"
"$HERE/rtc.sh" 2>&1 | sed 's/^/   /'

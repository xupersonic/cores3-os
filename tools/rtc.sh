#!/usr/bin/env bash
# 把 Mac 的当前时间写进 CoreS3 板载 RTC（BM8563）。
#
# 为什么需要：CoreS3 的 RTC 没有后备电池，掉电 / 重烧之后时间会归零，
# 而设备自己没有任何对时来源（没 Wi-Fi、BLE 也不给时间），只能从串口喂。
# fw.sh 每次烧完会自动调一次这个脚本；平时想单独对时直接 ./rtc.sh。
#
# 用法：
#   ./rtc.sh              # 同步成 Mac 当前时间，并打印设备回读的时间
#   ./rtc.sh --show       # 只打印设备当前 RTC，不改
#
# python3 自动探测（不写死绝对路径）：环境变量 PYTHON → PATH → 常见位置。

set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SER="$HERE/ser.py"

PY="${PYTHON:-}"
if [ -z "$PY" ] || [ ! -x "$PY" ]; then
  PY="$(command -v python3 2>/dev/null || true)"
fi
if [ -z "$PY" ]; then
  for c in /opt/homebrew/bin/python3 /usr/local/bin/python3 /usr/bin/python3; do
    [ -x "$c" ] && { PY="$c"; break; }
  done
fi
if [ -z "$PY" ]; then echo "✗ 找不到 python3（可 export PYTHON=/path/to/python3）"; exit 1; fi

if [ "$#" -ge 1 ] && [ "$1" = "--show" ]; then
  "$PY" "$SER" "time" 1.2
  exit 0
fi

NOW="$(date '+%Y-%m-%d %H:%M:%S')"
echo "-> sync RTC: $NOW"
"$PY" "$SER" "time $NOW" 1.2
echo "-> readback:"
"$PY" "$SER" "time" 1.0

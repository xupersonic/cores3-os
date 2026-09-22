#!/usr/bin/env bash
# CoreS3 Music Bridge 的开机自启管理（launchd LaunchAgent）
#
#   ./bridge.sh install   装 LaunchAgent 并立刻启动（首次装完要确认蓝牙授权）
#   ./bridge.sh start     启动（已 install 过）
#   ./bridge.sh stop      停掉（launchd 不会再自动拉起）
#   ./bridge.sh restart   重启（build.sh 重建完用这个）
#   ./bridge.sh status    看在不在跑 + 日志尾
#   ./bridge.sh log       实时跟日志
#   ./bridge.sh uninstall 彻底卸掉自启
#
# ⚠️ 坑1（环境）：`launchctl bootstrap/bootout/kickstart` 在「没有 GUI 审计会话」的
#    进程里一律返回 `Bootstrap failed: 5: Input/output error`（AI 工具的后台 shell、
#    ssh 会话都中招）。本脚本会自动降级：编译一个 AppleScript 小程序，用 `open` 把它
#    送进 GUI 会话去跑同一条命令。所以别手工敲 launchctl，走这个脚本。
# ⚠️ 坑2（蓝牙授权）：二进制重新签名后 cdhash 会变，TCC 可能要重新授权一次。
#    症状是日志停在 `[bridge] scanning…` 后面跟 `✗ 蓝牙未授权`。
#    解决：系统设置 → 隐私与安全性 → 蓝牙 → 勾上 CoreS3 Music Bridge，然后 restart。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"                 # 仓库根，用来把占位路径替换成真实路径
LABEL="com.cores3os.bridge"
PLIST="$HOME/Library/LaunchAgents/${LABEL}.plist"
TPL="${HERE}/${LABEL}.plist.example"           # 仓库里带的是模板（不含任何人的真实路径）
SRC="${HERE}/${LABEL}.plist"                   # 本地生成的那份，已被 .gitignore 排除
BIN="${HERE}/MusicBridge.app/Contents/MacOS/MusicBridge"
LOG="/tmp/cores3-bridge.log"
DOM="gui/$(id -u)"

# launchctl 代理：先在本 shell 里试；EIO 就交给 GUI 会话里的小程序跑一次。
LC_APP="/tmp/.cores3-lc-runner.app"
LC_SH="/tmp/.cores3-lc.sh"
LC_OUT="/tmp/.cores3-lc.out"
lc() {
  : > "$LC_OUT"
  if launchctl "$@" >>"$LC_OUT" 2>&1; then return 0; fi
  grep -q "Input/output error" "$LC_OUT" 2>/dev/null || { cat "$LC_OUT"; return 1; }
  # 降级路径
  {
    echo '#!/bin/bash'
    printf 'launchctl %s > %s 2>&1\n' "$*" "$LC_OUT"
    printf 'echo "__done__" >> %s\n' "$LC_OUT"
  } > "$LC_SH"
  rm -rf "$LC_APP"
  osacompile -o "$LC_APP" -e "do shell script \"bash $LC_SH\"" >/dev/null 2>&1
  : > "$LC_OUT"
  open "$LC_APP" 2>/dev/null
  local i
  for i in $(seq 1 40); do
    grep -q "__done__" "$LC_OUT" 2>/dev/null && break
    sleep 0.25
  done
  grep -v "__done__" "$LC_OUT" 2>/dev/null
  grep -q "__done__" "$LC_OUT" 2>/dev/null
}

loaded() { launchctl print "${DOM}/${LABEL}" >/dev/null 2>&1; }
running() { pgrep -f "$BIN" | head -1; }

case "${1:-status}" in
  install)
    # 本地还没有真实 plist 就从模板生成一份，顺手把 /PATH/TO/m5stack-face3keyboard-os 换成当前仓库路径
    if [ ! -f "$SRC" ]; then
      [ -f "$TPL" ] || { echo "✗ 找不到模板 $TPL"; exit 1; }
      sed "s|/PATH/TO/m5stack-face3keyboard-os|${ROOT}|g" "$TPL" > "$SRC"
      echo "→ 已从模板生成 $(basename "$SRC")（路径替换成 ${ROOT}）"
    fi
    mkdir -p "$HOME/Library/LaunchAgents"
    cp -f "$SRC" "$PLIST"
    # 先把手动 open 起来的实例让位，免得两个进程抢同一个 BLE 连接
    pkill -f "$BIN" 2>/dev/null; sleep 1
    lc bootout "${DOM}/${LABEL}" 2>/dev/null
    rm -rf "$LC_APP"
    if lc bootstrap "$DOM" "$PLIST"; then echo "✓ 已装载 ${LABEL}（开机/登录自动启动）"; fi
    sleep 3
    exec "$0" status
    ;;
  start)
    if loaded; then
      lc kickstart -k "${DOM}/${LABEL}" >/dev/null && echo "✓ 已启动"
    else
      echo "✗ 还没 install，先跑: $0 install"; exit 1
    fi
    ;;
  stop)
    lc bootout "${DOM}/${LABEL}" >/dev/null 2>&1
    pkill -f "$BIN" 2>/dev/null
    echo "✓ 已停止（launchd 不会再拉起）"
    ;;
  restart)
    if loaded; then
      lc kickstart -k "${DOM}/${LABEL}" >/dev/null && echo "✓ 已重启"
    else
      exec "$0" install
    fi
    sleep 3
    exec "$0" status
    ;;
  uninstall)
    lc bootout "${DOM}/${LABEL}" >/dev/null 2>&1
    pkill -f "$BIN" 2>/dev/null
    rm -f "$PLIST"
    echo "✓ 已卸载自启（$PLIST 已删）"
    ;;
  status)
    PID="$(running)"
    if [ -n "${PID:-}" ]; then
      echo "✓ 运行中 pid=${PID}  $(loaded && echo '（launchd 托管：开机自启 + 崩了自动拉起）' || echo '（手动启动，退出后不会自启）')"
    else
      echo "✗ 没在跑  $(loaded && echo '（launchd 已装载但进程不在，看日志）' || echo '（未装载自启）')"
    fi
    echo "--- 日志尾 ---"
    tail -6 "$LOG" 2>/dev/null || echo "(暂无日志)"
    ;;
  log) exec tail -f "$LOG" ;;
  *) echo "用法: $0 install|start|stop|restart|status|log|uninstall"; exit 1 ;;
esac

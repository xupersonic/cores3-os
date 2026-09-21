#!/usr/bin/env bash
# 重新编译 Bridge 并塞回 .app 里重新签名
#
# ⚠️ 两个要点：
#   1. 必须 ad-hoc 重新签名（codesign -s - -f --deep），否则 Info.plist=not bound → 直接 SIGABRT；
#   2. 启动一律用 `open MusicBridge.app`（走 LaunchServices），这样蓝牙授权弹窗才出得来。
#      改了二进制会换 cdhash，TCC 有可能要重新授权一次，属正常。
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

echo "→ 编译"
swiftc -O Bridge.swift -o MusicBridge || { echo "✗ 编译失败"; exit 1; }

# .app 包骨架不在仓库里（二进制不入库），这里现搭一个
echo "→ 准备 app 包骨架"
mkdir -p MusicBridge.app/Contents/MacOS
cp -f Info.plist MusicBridge.app/Contents/Info.plist

echo "→ 装进 app 包"
cp -f MusicBridge MusicBridge.app/Contents/MacOS/MusicBridge

echo "→ 重新签名"
codesign -s - -f --deep MusicBridge.app 2>&1 | tail -2

echo "✓ 完成。启动：open $HERE/MusicBridge.app"
echo "  日志：tail -f /tmp/cores3-bridge.log"

# 如果已经配了开机自启（bridge.sh install 过），顺手把新二进制顶上去，
# 否则跑着的还是旧的那份。
if launchctl print "gui/$(id -u)/com.cores3os.bridge" >/dev/null 2>&1; then
  echo "→ 自启服务在跑，重启生效"
  "$HERE/bridge.sh" restart
fi

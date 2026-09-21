#!/usr/bin/env python3
"""CoreS3 串口小工具（纯标准库，不依赖 pyserial）。

用法：
    ser.py '<命令>' [读几秒] [端口]

例：
    ser.py status            # 打印一条状态
    ser.py 'time 2026-09-20 23:04:08' 1.5
    ser.py '' 3.0            # 不发命令，纯抓日志（看启动输出用）

端口会自动探测 /dev/cu.usbmodem*：CoreS3 是 native USB CDC，
串口号每次插拔 / 换 USB 口都可能变（实测见过 101 和 2101），别硬编码。
"""
import os, sys, time, termios, select, glob

cands = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.wchusbserial*"))
PORT = sys.argv[3] if len(sys.argv) > 3 else (cands[0] if cands else "/dev/cu.usbmodem101")
cmd = sys.argv[1] if len(sys.argv) > 1 else ""
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5

if not cands and len(sys.argv) <= 3:
    sys.stderr.write("找不到 CoreS3（/dev/cu.usbmodem* 为空），检查 USB 线\n")
    sys.exit(1)

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
attrs[0] = 0  # iflag
attrs[1] = 0  # oflag
attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
attrs[3] = 0  # lflag
attrs[4] = termios.B115200
attrs[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, attrs)
termios.tcflush(fd, termios.TCIOFLUSH)

if cmd:
    os.write(fd, (cmd + "\n").encode())

end = time.time() + secs
out = b""
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            break
        out += chunk
sys.stdout.write(out.decode("utf-8", "replace"))
os.close(fd)

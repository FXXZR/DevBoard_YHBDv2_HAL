#!/usr/bin/env python3
"""
串口命令行工具 — 供 Claude Code 通过 Bash 调用，与 STM32 板子通信。

收发内容同时输出到:
  1. stderr (带 [TX]/[RX] 标记，对话中用户可见)
  2. stdout  (原始数据，Claude Code 解析用)
  3. --log <文件> (追加写入，用户可用 tail -f 实时监控)

用法:
    python tools/serial_cli.py --send "hello\r\n" --read 1.0
    python tools/serial_cli.py --send "cmd" --read 2.0 --log build/serial.log
"""

import argparse
import sys
import time
from datetime import datetime

import serial


def main():
    parser = argparse.ArgumentParser(description="Serial CLI for STM32")
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--send", default=None, help="字符串 (支持 \\r\\n 转义)")
    parser.add_argument("--read", type=float, default=0.5, help="读取等待秒数")
    parser.add_argument("--timeout", type=float, default=0.05)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--log", default=None, help="日志文件路径 (追加写入)")
    args = parser.parse_args()

    log_file = None
    if args.log:
        try:
            log_file = open(args.log, "a", encoding="utf-8")
        except OSError:
            pass

    def log(msg: str):
        ts = datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        if not args.quiet:
            sys.stderr.write(line + "\n")
            sys.stderr.flush()
        if log_file:
            log_file.write(line + "\n")
            log_file.flush()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=args.timeout, write_timeout=1.0)
        ser.dtr = False
        ser.rts = False
    except serial.SerialException as e:
        log(f"SERIAL ERROR: {e}")
        sys.exit(1)

    try:
        ser.reset_input_buffer()

        if args.send:
            text = args.send.encode("utf-8").decode("unicode_escape")
            data = text.encode("utf-8")
            ser.write(data)
            log(f"TX {len(data)}B | {repr(text)}")

        deadline = time.time() + args.read
        output = bytearray()
        while time.time() < deadline:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            ser.timeout = max(0.01, remaining)
            try:
                chunk = ser.read(4096)
                if chunk:
                    output.extend(chunk)
            except serial.SerialException:
                break

        if output:
            text_out = bytes(output).decode("utf-8", errors="replace").rstrip()
            log(f"RX {len(output)}B | {repr(text_out)}")

        # stdout: 原始数据给 Claude Code 解析
        if output:
            sys.stdout.buffer.write(bytes(output))
            sys.stdout.flush()

    finally:
        ser.close()
        if log_file:
            log_file.close()


if __name__ == "__main__":
    main()

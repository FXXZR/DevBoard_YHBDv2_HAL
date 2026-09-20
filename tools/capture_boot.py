#!/usr/bin/env python3
"""
复位 STM32 并捕获上电启动串口输出。

收发内容同时输出到:
  1. stderr (带 [capture] 标记，对话中用户可见)
  2. stdout  (原始数据，Claude Code 解析用)
  3. --log <文件> (追加写入，用户可用 tail -f 实时监控)

用法:
    python tools/capture_boot.py
    python tools/capture_boot.py --log build/serial.log
"""

import argparse
import subprocess
import sys
import threading
import time
from datetime import datetime

import serial

from serial_port import resolve_port


def main():
    parser = argparse.ArgumentParser(description="Reset STM32 and capture boot banner")
    parser.add_argument("--port", default=None,
                        help="串口号 (如 COM7); 不填则自动识别 CH340")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait", type=float, default=4.0)
    parser.add_argument("--target", default="stm32f103ze")
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

    port = resolve_port(args.port, log=log)
    if port is None:
        sys.exit(1)

    log(f"capture: Opening {port} @ {args.baud} ...")

    try:
        ser = serial.Serial(port, args.baud, timeout=0.05)
        ser.dtr = False
        ser.rts = False
    except serial.SerialException as e:
        log(f"capture ERROR: {e}")
        sys.exit(1)

    try:
        ser.reset_input_buffer()

        output = bytearray()
        stop = threading.Event()

        def reader():
            while not stop.is_set():
                chunk = ser.read(4096)
                if chunk:
                    output.extend(chunk)

        t = threading.Thread(target=reader, daemon=True)
        t.start()

        log("capture: Resetting (hw + attach)...")
        result = subprocess.run(
            ["pyocd", "reset", "-t", args.target, "-m", "hw", "-M", "attach"],
            capture_output=True, timeout=15
        )
        if result.returncode != 0:
            err = result.stderr.decode(errors="replace").strip()
            if err:
                log(f"capture: pyocd: {err}")

        log(f"capture: Waiting {args.wait}s for boot output...")
        time.sleep(args.wait)

        stop.set()
        t.join(timeout=1.0)

        text = bytes(output).decode("utf-8", errors="replace")
        if text:
            log(f"capture RX {len(output)}B\n{text}")
            sys.stdout.write(text)
            sys.stdout.flush()
        else:
            log("capture: (no output)")

    finally:
        ser.close()
        if log_file:
            log_file.close()


if __name__ == "__main__":
    main()

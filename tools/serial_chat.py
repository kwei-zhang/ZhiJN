#!/usr/bin/env python3
"""
纸巾 (ZhiJN) 串口聊天客户端

相比 PlatformIO 自带的串口监视器，本脚本保证 UTF-8 中文正常输入与显示。
用法：
    .venv/bin/python tools/serial_chat.py
    .venv/bin/python tools/serial_chat.py --port /dev/cu.usbserial-0001
"""

from __future__ import annotations

import argparse
import sys
import threading
import time

import serial
from serial.tools import list_ports

PREFERRED_PORT_KEYWORDS = (
    "usbserial",
    "SLAB_USBtoUART",
    "wchusbserial",
    "usbmodem",
    "ttyUSB",
    "ttyACM",
)


def detect_esp32_ports() -> list[list_ports.ListPortInfo]:
    ports = list(list_ports.comports())
    preferred = [
        p
        for p in ports
        if any(kw.lower() in p.device.lower() for kw in PREFERRED_PORT_KEYWORDS)
    ]
    return preferred if preferred else ports


def choose_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port

    ports = detect_esp32_ports()
    # 过滤掉 macOS 内置蓝牙/调试口，避免误连
    usb_ports = [
        p
        for p in ports
        if not any(
            skip in p.device
            for skip in ("Bluetooth-Incoming-Port", "debug-console", "wlan-debug")
        )
    ]

    if not usb_ports:
        print("未检测到 ESP32 USB 串口设备！请排查：")
        print("  1. ESP32 是否已插上电脑？")
        print("  2. USB 线是否是「能传数据」的线？（很多充电线不能传数据）")
        print("  3. 经典 ESP32-WROOM 常见 CP2102 或 CH340 芯片，macOS 可能需要装对应驱动。")
        print("\n你也可以运行 `ls /dev/cu.*` 查看全部设备，并用 `--port /dev/cu.xxx` 手动指定。")
        sys.exit(1)

    if len(usb_ports) == 1:
        return usb_ports[0].device

    print("检测到多个串口设备，请选择编号：")
    for idx, p in enumerate(usb_ports, start=1):
        print(f"  [{idx}] {p.device}  ({p.description})")

    while True:
        raw = input("输入编号（默认 1）: ").strip()
        if not raw:
            return usb_ports[0].device
        if raw.isdigit() and 1 <= int(raw) <= len(usb_ports):
            return usb_ports[int(raw) - 1].device
        print("无效编号，请重试。")


def reader_loop(ser: serial.Serial, stop_event: threading.Event) -> None:
    while not stop_event.is_set():
        try:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            print(f"\r{text}\n你> ", end="", flush=True)
        except serial.SerialException as exc:
            if not stop_event.is_set():
                print(f"\n[串口断开] {exc}")
                stop_event.set()
            break
        except OSError:
            break


def main() -> None:
    parser = argparse.ArgumentParser(description="纸巾 (ZhiJN) ESP32 中文串口聊天客户端")
    parser.add_argument("--port", help="串口设备路径，例如 /dev/cu.usbserial-0001")
    parser.add_argument("--baud", type=int, default=115200, help="波特率（默认 115200）")
    args = parser.parse_args()

    port = choose_port(args.port)
    print(f"正在连接 {port} (波特率 {args.baud})...")
    print("提示：输入文字按回车发送；输入 /reset 清空记忆，/status 查状态，/quit 退出。\n")

    try:
        ser = serial.Serial(port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"打开串口失败: {exc}")
        print("提示：检查是否还有其他串口监视器（如 pio device monitor）占用了该端口。")
        sys.exit(1)

    # 给板子重启并打印开场白留一点时间
    time.sleep(0.3)

    stop_event = threading.Event()
    thread = threading.Thread(target=reader_loop, args=(ser, stop_event), daemon=True)
    thread.start()

    try:
        while not stop_event.is_set():
            try:
                user_input = input("你> ")
            except EOFError:
                break

            line = user_input.strip()
            if not line:
                continue
            if line in ("/quit", "/exit"):
                break

            ser.write((line + "\n").encode("utf-8"))
            ser.flush()
    except KeyboardInterrupt:
        print("\n已退出聊天。")
    finally:
        stop_event.set()
        ser.close()


if __name__ == "__main__":
    main()

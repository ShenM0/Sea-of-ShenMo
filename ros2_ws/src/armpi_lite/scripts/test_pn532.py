#!/usr/bin/env python3
"""PN532 读卡链路诊断脚本（不依赖 ROS，直接通过串口验证 上位机 -> STM32 -> PN532）。

用法:
  python3 scripts/test_pn532.py [串口] [波特率]
  默认: /dev/ttyUSB0 1000000

功能:
  1. 发送 I2C 扫描命令（功能号 0x11 子命令 0x02），列出 I2C1 总线上所有设备地址，
     用于确认 PN532 的实际地址（本模块实测 0x24，也可能为 0x48 等）。
  2. 发送单次寻卡命令（子命令 0x04），读卡 UID 并打印。
  3. 被动监听 10 秒，打印固件主动上报的刷卡帧（子命令 0x01）。

现象提示:
  - 扫描结果里没有 PN532 地址 -> 检查接线（SDA=PB7, SCL=PB6, VCC=5V或3.3V, GND）
    以及 PN532 模块是否处于 I2C 模式（拨码/跳线）。
  - 扫描能扫到地址但寻卡返回"无卡" -> 卡片未贴紧天线，或卡类型不受支持；
    若返回"寻卡错误(err=N)" -> PN532 地址不对，或模块不在 I2C 模式。
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from armpi_lite.serial_protocol import (
    PACKET_FUNC_NFC,
    NFC_SUBCMD_CARD,
    NFC_SUBCMD_SCAN,
    NFC_SUBCMD_POLL,
    NFC_SUBCMD_RAW_READ,
    NFC_SUBCMD_DUMP,
    build_nfc_scan_frame,
    build_nfc_poll_frame,
    build_nfc_raw_read_frame,
    build_nfc_dump_frame,
    extract_frames,
)


def format_uid(payload):
    """payload = [uid_len, uid...] -> 'A1 B2 C3 D4'"""
    uid_len = payload[0]
    return ' '.join(f'{b:02X}' for b in payload[1:1 + uid_len])


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 1000000

    import serial
    ser = serial.Serial(port=port, baudrate=baud, timeout=0.1,
                        xonxoff=False, rtscts=False, dsrdtr=False)
    # usbipd 转发的 CH340：DTR 为高会阻塞数据收发，必须拉低
    ser.setDTR(False)
    ser.setRTS(False)
    print(f'已打开 {port} @ {ser.baudrate}bps')
    time.sleep(0.5)
    ser.reset_input_buffer()

    # ---------- 1) I2C 扫描 ----------
    print('\n[1] 扫描 I2C1 总线 ...')
    ser.write(build_nfc_scan_frame())
    ser.flush()
    buf = b''
    scan_payload = None
    deadline = time.time() + 5.0
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for func, payload in frames:
                if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_SCAN:
                    scan_payload = payload
                    break
        if scan_payload is not None:
            break
    if scan_payload is None:
        print('  未收到扫描应答（检查串口链路与固件是否已烧录新版本）')
    else:
        n = scan_payload[1]
        addrs = scan_payload[2:2 + n]
        print(f'  发现 {n} 个 I2C 设备: {[f"0x{a:02X}" for a in addrs]}')
        if any(0x24 <= a <= 0x50 for a in addrs):
            print('  -> 其中应包含 PN532（本模块实测 0x24）。')
        else:
            print('  -> 未发现 PN532 地址，请检查接线与模块 I2C 模式设置。')

    # ---------- 1b) 原始字节诊断（定位 err=2 用） ----------
    print('\n[1b] 原始字节诊断 ...')

    # 不写命令，直接读 8 字节（看空闲状态 / 是否支持多字节读）
    ser.write(build_nfc_raw_read_frame(8))
    ser.flush()
    got = None
    deadline = time.time() + 2.0
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for func, payload in frames:
                if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_RAW_READ:
                    got = payload
                    break
        if got is not None:
            break
    if got is None:
        print('  原始读(不写命令,8字节): 无应答')
    else:
        n = got[1]
        data = got[2:2 + n]
        print(f'  原始读(不写命令,8字节): 读到{n}字节 数据={data.hex(" ")}')

    # 写 InListPassiveTarget，延时后原始读 32 字节
    ser.write(build_nfc_dump_frame())
    ser.flush()
    got = None
    deadline = time.time() + 3.0
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for func, payload in frames:
                if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_DUMP:
                    got = payload
                    break
        if got is not None:
            break
    if got is None:
        print('  写命令后原始读(32字节): 无应答')
    else:
        n = got[1]
        data = got[2:2 + n]
        print(f'  写命令后原始读(32字节): 读到{n}字节 数据={data.hex(" ")}')

    # ---------- 2) 单次寻卡 ----------
    print('\n[2] 单次寻卡（请把卡贴在 PN532 天线上）...')
    for attempt in range(3):
        ser.write(build_nfc_poll_frame())
        ser.flush()
        got = None
        deadline = time.time() + 1.5
        while time.time() < deadline:
            chunk = ser.read(128)
            if chunk:
                buf += chunk
                frames, buf = extract_frames(buf)
                for func, payload in frames:
                    if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_POLL:
                        got = payload
                        break
            if got is not None:
                break
        if got is None:
            print(f'  第{attempt + 1}次: 无应答')
            continue
        found = got[1]
        if found:
            print(f'  第{attempt + 1}次: 读到卡 UID = {format_uid(got[2:])}')
        else:
            err = got[2] if len(got) >= 3 else 0
            if err == 0:
                print(f'  第{attempt + 1}次: 无卡（把卡贴紧天线再试）')
            else:
                print(f'  第{attempt + 1}次: 寻卡错误(err={err})，检查 PN532 地址/接线/I2C模式')
        time.sleep(0.3)

    # ---------- 3) 被动监听固件主动上报 ----------
    print('\n[3] 监听固件主动上报 10 秒（期间刷卡应自动打印）...')
    deadline = time.time() + 10.0
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for func, payload in frames:
                if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_CARD:
                    print(f'  [刷卡上报] UID = {format_uid(payload[1:])}')

    print('\n完成。')
    ser.close()


if __name__ == '__main__':
    main()

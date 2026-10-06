#!/usr/bin/env python3
"""ESP32-S3-Cam ISBN 链路诊断脚本（不依赖 ROS，直接通过串口验证 上位机 -> STM32 -> ESP32）。

用法:
  python3 scripts/test_isbn.py [串口] [波特率]
  默认: /dev/ttyUSB0 1000000

功能:
  1. 扫描 I2C1 总线（功能号 0x12 子命令 0x02），确认 ESP32 从机地址（应为 0x52）。
  2. 读 ESP32 从机寄存器（子命令 0x03）：STATUS/LEN/TYPE。
  3. 单次读结果（子命令 0x04）：把书背条码对准摄像头后应返回 13 位数字。
  4. 被动监听 15 秒，打印固件主动上报的 ISBN 帧（子命令 0x01）。

现象提示:
  - 扫描没有 0x52 -> 检查 ESP32 接线（SDA=PB7, SCL=PB6, 5V/GND）与 I2C 从机固件是否烧录；
  - 扫描有 0x52 但读寄存器 err != 0 -> 检查是否共地、杜邦线接触、是否接了上拉；
  - 单次读一直"暂无结果" -> 条码未对准/距离太远/光线太暗/条码方向接近 90°。
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from armpi_lite.serial_protocol import (
    PACKET_FUNC_ISBN,
    ISBN_SUBCMD_RESULT,
    ISBN_SUBCMD_SCAN,
    ISBN_SUBCMD_READ_REG,
    ISBN_SUBCMD_READ_ONCE,
    build_isbn_scan_frame,
    build_isbn_read_reg_frame,
    build_isbn_read_once_frame,
    extract_frames,
)


def read_until(ser, func, subcmd, timeout_s):
    """读取直到收到 (func, subcmd) 帧，返回其 payload；超时返回 None。"""
    buf = b''
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for f, payload in frames:
                if f == func and payload and payload[0] == subcmd:
                    return payload
    return None


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
    ser.write(build_isbn_scan_frame())
    ser.flush()
    payload = read_until(ser, PACKET_FUNC_ISBN, ISBN_SUBCMD_SCAN, 5.0)
    if payload is None:
        print('  未收到扫描应答（检查串口链路与固件是否已烧录新版本）')
    else:
        n = payload[1]
        addrs = payload[2:2 + n]
        print(f'  发现 {n} 个 I2C 设备: {[f"0x{a:02X}" for a in addrs]}')
        if 0x52 in addrs:
            print('  -> 已发现 ESP32-S3-Cam 从机 (0x52)。')
        else:
            print('  -> 未发现 0x52，检查 ESP32 接线/供电/从机固件是否烧录。')

    # ---------- 2) 读寄存器 ----------
    print('\n[2] 读 ESP32 从机寄存器 ...')
    for reg, name in [(0x00, 'STATUS'), (0x01, 'LEN'), (0x02, 'TYPE')]:
        ser.write(build_isbn_read_reg_frame(reg))
        ser.flush()
        payload = read_until(ser, PACKET_FUNC_ISBN, ISBN_SUBCMD_READ_REG, 2.0)
        if payload is None:
            print(f'  {name}(0x{reg:02X}): 无应答')
        elif payload[2] != 0:
            print(f'  {name}(0x{reg:02X}): 读失败 err={payload[2]}')
        else:
            data = payload[3:]
            print(f'  {name}(0x{reg:02X}): {data.hex(" ")}')

    # ---------- 3) 单次读结果 ----------
    print('\n[3] 单次读结果（把书背条码对准摄像头，保持几秒）...')
    for attempt in range(5):
        ser.write(build_isbn_read_once_frame())
        ser.flush()
        payload = read_until(ser, PACKET_FUNC_ISBN, ISBN_SUBCMD_READ_ONCE, 2.0)
        if payload is None:
            print(f'  第{attempt + 1}次: 无应答')
            continue
        found = payload[1]
        if found:
            l = payload[3]
            data = payload[4:4 + l]
            print(f'  第{attempt + 1}次: ISBN = {data.decode("ascii", errors="replace")}')
        else:
            err = payload[2] if len(payload) >= 3 else 0
            if err == 0:
                print(f'  第{attempt + 1}次: 暂无结果（条码未识别/未对准/距离不对）')
            else:
                print(f'  第{attempt + 1}次: 读错误(err={err})')
        time.sleep(1.0)

    # ---------- 4) 被动监听 ----------
    print('\n[4] 监听固件主动上报 15 秒（条码持续对准会自动上报）...')
    buf = b''
    deadline = time.time() + 15.0
    while time.time() < deadline:
        chunk = ser.read(128)
        if chunk:
            buf += chunk
            frames, buf = extract_frames(buf)
            for f, payload in frames:
                if f == PACKET_FUNC_ISBN and payload and payload[0] == ISBN_SUBCMD_RESULT:
                    l = payload[2]
                    data = payload[3:3 + l]
                    print(f'  [上报] ISBN = {data.decode("ascii", errors="replace")}')

    print('\n完成。')
    ser.close()


if __name__ == '__main__':
    main()

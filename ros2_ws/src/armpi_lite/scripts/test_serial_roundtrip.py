#!/usr/bin/env python3
"""串口链路回环诊断：不依赖 ROS，直接验证 上位机 -> STM32 -> 舵机总线 全链路。

原理：发送总线舵机"读位置"命令（功能号 0x05，子命令 0x05），固件收到后会
通过同一串口回送应答帧（见固件 packet_handle.c case 0x05）。
- 能收到合法应答  -> 串口链路完全正常（问题在别处，如动作参数/舵机限位）
- 收不到任何字节  -> 上位机到 STM32 的链路不通（波特率/设备号/透传/固件）
- 收到乱码字节    -> 波特率不匹配

用法:
  python3 scripts/test_serial_roundtrip.py [串口] [波特率]
  默认: /dev/ttyUSB0 1000000

附加现象提示：固件每收到一帧 CRC 正确的数据，板上 LED2 会短闪一次
（packet.c 中 led_flash(&leds[1], 10, 50, 1)）。发命令时观察该灯可辅助判断。
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from armpi_lite.serial_protocol import crc8_maxim

FUNC_BUS_SERVO = 0x05
SUBCMD_READ_POSITION = 0x05
SERVO_IDS = (1, 2, 3, 4, 5, 6)


def build_read_position_frame(servo_id):
    payload = bytes([SUBCMD_READ_POSITION, servo_id])
    head = bytes([0xAA, 0x55, FUNC_BUS_SERVO, len(payload)])
    crc_input = bytes([FUNC_BUS_SERVO, len(payload)]) + payload
    return head + payload + bytes([crc8_maxim(crc_input)])


def parse_frames(buf):
    """从字节流里解析合法帧，返回 [(func, payload), ...]，并自动校验 CRC。"""
    frames = []
    i = 0
    while i + 5 <= len(buf):
        if buf[i] == 0xAA and buf[i + 1] == 0x55:
            func, length = buf[i + 2], buf[i + 3]
            end = i + 4 + length + 1
            if end > len(buf):
                break  # 帧不完整，等更多数据
            payload = buf[i + 4:i + 4 + length]
            crc = buf[i + 4 + length]
            if crc8_maxim(bytes([func, length]) + payload) == crc:
                frames.append((func, payload))
            i = end
        else:
            i += 1
    return frames


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
    time.sleep(0.5)  # 等固件稳定（若板子因 DTR 复位）
    ser.reset_input_buffer()

    rx_all = b''
    answered = 0
    for servo_id in SERVO_IDS:
        frame = build_read_position_frame(servo_id)
        ser.write(frame)
        ser.flush()
        deadline = time.time() + 0.5
        got = None
        while time.time() < deadline:
            chunk = ser.read(64)
            if chunk:
                rx_all += chunk
                for func, payload in parse_frames(rx_all):
                    if (func == FUNC_BUS_SERVO and len(payload) >= 5
                            and payload[0] == servo_id
                            and payload[1] == SUBCMD_READ_POSITION):
                        got = payload
                        break
            if got is not None:
                break
        if got is None:
            print(f'  舵机 {servo_id}: 无应答')
        else:
            success = got[2]
            position = got[3] | (got[4] << 8)
            if success == 0:
                print(f'  舵机 {servo_id}: 位置 = {position}')
                answered += 1
            else:
                print(f'  舵机 {servo_id}: 固件应答但读取失败 (success={success})，'
                      f'说明 STM32 收到了命令但舵机总线无响应')

    print()
    if answered == len(SERVO_IDS):
        print(f'结论: 6/6 个舵机全部应答，串口链路正常（{port} @ {baud}）。')
        print('机械臂不动的问题不在通信链路，请检查动作参数或舵机本身。')
    elif answered > 0:
        print(f'结论: 仅 {answered}/6 个舵机应答，链路部分正常，检查无应答舵机的接线/ID。')
    else:
        if rx_all:
            print(f'结论: 没有合法应答帧，但收到 {len(rx_all)} 字节原始数据: {rx_all.hex()}')
            print('收到乱码通常意味着波特率不匹配（固件固定 1000000bps）。')
        else:
            print('结论: 串口完全无应答（0 字节返回）。')
            print('可能原因:')
            print('  1. 虚拟机里这个 ttyUSB 设备不是机械臂主控板（核对 lsusb 应有 1a86:7523）')
            print('  2. VMware USB 透传丢数据 —— 在"可移动设备"里断开再重连 CH340')
            print('  3. 板子固件未运行（重新上电/复位板子后再试）')
            print(f'  4. 波特率被改动过（当前尝试 {baud}，固件应为 1000000）')
            print('对照实验：发送命令时观察板子 LED2 是否短闪；'
                  '再用之前"能动"的串口助手在同一系统/同一端口发同样的读位置命令对比。')

    ser.close()


if __name__ == '__main__':
    main()

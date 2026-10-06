"""STM32 串口协议（移植自 hwcompt/yolov5_d435i_detection-main/arm_uart.py）。

帧格式: 0xAA 0x55 | 功能码 | 载荷长度 | 载荷 | CRC8-Maxim
CRC 计算范围: 功能码 + 载荷长度 + 载荷

运动学在上位机解算后，用总线舵机命令(功能号 0x05, 子命令 0x01)直接下发各关节脉宽，
固件侧 packet_serial_servo_handle() 原样转发到舵机总线，无需修改固件。
"""

import glob
import os
import struct

START_BYTE_1 = 0xAA
START_BYTE_2 = 0x55
PACKET_FUNC_BUS_SERVO = 0x05
BUS_SERVO_CMD_SET_POSITION = 0x01

# PN532 NFC（固件功能号 0x11）
PACKET_FUNC_NFC = 0x11
NFC_SUBCMD_CARD = 0x01   # 固件主动上报: [0x01, uid_len, uid...]
NFC_SUBCMD_SCAN = 0x02   # 请求扫描 I2C1 总线
NFC_SUBCMD_POLL = 0x04   # 请求单次寻卡
NFC_SUBCMD_RAW_READ = 0x06  # 调试: 原始读 n 字节（不写命令）
NFC_SUBCMD_DUMP = 0x07      # 调试: 写寻卡命令后原始读回复

# ESP32-S3-Cam ISBN(EAN-13)（固件功能号 0x12）
PACKET_FUNC_ISBN = 0x12
ISBN_SUBCMD_RESULT = 0x01   # 固件主动上报: [0x01, type, len, data...]
ISBN_SUBCMD_SCAN = 0x02     # 请求扫描 I2C1 总线
ISBN_SUBCMD_READ_REG = 0x03 # 请求读 ESP32 从机寄存器 [0x03, reg]
ISBN_SUBCMD_READ_ONCE = 0x04  # 请求单次读结果

DEFAULT_SERIAL_CANDIDATES = [
    "/dev/ttyUSB0",
    "/dev/ttyACM0",
    "/dev/serial0",
    "/dev/ttyAMA0",
    "/dev/ttyS0",
]


def list_available_serial_ports():
    ports = []
    seen = set()

    for candidate in DEFAULT_SERIAL_CANDIDATES:
        if os.path.exists(candidate) and candidate not in seen:
            ports.append(candidate)
            seen.add(candidate)

    dynamic_candidates = sorted(
        glob.glob("/dev/ttyUSB*")
        + glob.glob("/dev/ttyACM*")
        + glob.glob("/dev/ttyAMA*")
        + glob.glob("/dev/ttyS*")
    )
    for device in dynamic_candidates:
        if os.path.exists(device) and device not in seen:
            ports.append(device)
            seen.add(device)

    return ports


def resolve_serial_port(port=None):
    if port:
        return port

    env_port = os.environ.get("ARM_SERIAL_PORT")
    if env_port:
        return env_port

    available_ports = list_available_serial_ports()
    if available_ports:
        return available_ports[0]

    raise RuntimeError(
        "未找到可用串口。请确认 USB 串口设备 /dev/ttyUSB0 或 /dev/ttyACM0，"
        "也可显式设置参数 serial_port 或环境变量 ARM_SERIAL_PORT。"
    )


def crc8_maxim(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x01:
                crc = ((crc >> 1) ^ 0x8C) & 0xFF
            else:
                crc = (crc >> 1) & 0xFF
    return crc


def _build_frame(func, payload):
    head = bytes([START_BYTE_1, START_BYTE_2, func, len(payload)])
    crc_input = bytes([func, len(payload)]) + payload
    return head + payload + bytes([crc8_maxim(crc_input)])


def build_bus_servo_position_frame(duration_ms, positions):
    """总线舵机位置命令帧（与固件 packet_serial_servo_handle case 0x01 对应）。

    duration_ms: 运动时间(毫秒)；positions: [(舵机ID, 脉宽), ...]，脉宽范围 [0, 1000]。
    """
    if not 0 <= int(duration_ms) <= 65535:
        raise ValueError(f"duration_ms 超出范围: {duration_ms}")
    payload = struct.pack("<BHB", BUS_SERVO_CMD_SET_POSITION, int(duration_ms), len(positions))
    for servo_id, position in positions:
        if not 0 <= int(position) <= 1000:
            raise ValueError(f"舵机 {servo_id} 脉宽超出 [0,1000]: {position}")
        payload += struct.pack("<BH", servo_id, int(position))
    return _build_frame(PACKET_FUNC_BUS_SERVO, payload)


def claw_angle_to_position(open_angle):
    """夹爪开合角度 [0,90] → 1号舵机脉宽（映射同固件 robot_arm_claw_set）。

    90=完全张开(脉宽200)，0=完全闭合(脉宽700)。
    """
    open_angle = max(0.0, min(90.0, open_angle))
    return 700 - int(5.555555555555556 * open_angle)


def camera_to_arm_xyz_cm(camera_xyz, x_offset_cm=0.0, y_offset_cm=0.0, z_offset_cm=4.0):
    """相机坐标系(RealSense: x右/y下/z前, 米) → 机械臂坐标系(前/左/上, 厘米)。

    注意：这是固定轴映射 + 手工偏移的近似方案（沿用 hwcompt 的做法）。
    如需更高精度，应替换为手眼标定矩阵（见 README）。
    """
    camera_x_m, camera_y_m, camera_z_m = camera_xyz

    target_x = camera_z_m * 100.0 + x_offset_cm
    target_y = -camera_x_m * 100.0 + y_offset_cm
    target_z = -camera_y_m * 100.0 + z_offset_cm

    return [round(target_x, 2), round(target_y, 2), round(target_z, 2)]


def build_nfc_scan_frame():
    """请求固件扫描 I2C1 总线（用于确认 PN532 实际地址）。"""
    return _build_frame(PACKET_FUNC_NFC, bytes([NFC_SUBCMD_SCAN]))


def build_nfc_poll_frame():
    """请求固件立即执行一次寻卡。"""
    return _build_frame(PACKET_FUNC_NFC, bytes([NFC_SUBCMD_POLL]))


def build_nfc_raw_read_frame(n):
    """调试: 请求固件原始读 n 字节（不写命令）。"""
    return _build_frame(PACKET_FUNC_NFC, bytes([NFC_SUBCMD_RAW_READ, n]))


def build_nfc_dump_frame():
    """调试: 请求固件写寻卡命令后原始读回复。"""
    return _build_frame(PACKET_FUNC_NFC, bytes([NFC_SUBCMD_DUMP]))


def build_isbn_scan_frame():
    """请求固件扫描 I2C1 总线（用于确认 ESP32 从机地址 0x52）。"""
    return _build_frame(PACKET_FUNC_ISBN, bytes([ISBN_SUBCMD_SCAN]))


def build_isbn_read_reg_frame(reg):
    """调试: 请求固件读 ESP32 从机某个寄存器（0x00~0x03）。"""
    return _build_frame(PACKET_FUNC_ISBN, bytes([ISBN_SUBCMD_READ_REG, reg]))


def build_isbn_read_once_frame():
    """调试: 请求固件立即读取一次 ISBN 结果。"""
    return _build_frame(PACKET_FUNC_ISBN, bytes([ISBN_SUBCMD_READ_ONCE]))


def extract_frames(buf):
    """从字节流头部解析尽量多的完整合法帧。

    返回 (frames, remaining)：frames 为 [(func, payload), ...]，CRC 校验通过；
    remaining 为未消费的尾部字节（帧不完整或乱码对齐前的部分）。
    """
    frames = []
    i = 0
    n = len(buf)
    while i + 5 <= n:
        if buf[i] == START_BYTE_1 and buf[i + 1] == START_BYTE_2:
            func = buf[i + 2]
            length = buf[i + 3]
            end = i + 4 + length + 1
            if end > n:
                break  # 帧不完整，等更多数据
            payload = buf[i + 4:i + 4 + length]
            crc = buf[i + 4 + length]
            if crc8_maxim(bytes([func, length]) + payload) == crc:
                frames.append((func, payload))
            i = end
        else:
            i += 1
    return frames, buf[i:]

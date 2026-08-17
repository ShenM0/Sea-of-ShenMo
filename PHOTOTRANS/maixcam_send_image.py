#!/usr/bin/env python3
"""
MaixCam Pro → ESP32 图像发送脚本

功能：
  1. 连接 ESP32 的 WiFi 热点
  2. 通过摄像头采集图像，编码为 JPEG
  3. 通过 TCP Socket 发送 JPEG 数据到 ESP32

协议：[4B length LE][JPEG data]，然后等待 ESP32 回复 "OK"

运行方式（在 MaixCam Pro 上）：
  python3 maixcam_send_image.py
"""

import socket
import struct
import time
import os
import sys
import subprocess
import re

# ============ 配置 ============
ESP32_SSID         = "esp32_s3_test"       # ESP32 WiFi 名称
ESP32_PASS         = "12345678"            # ESP32 WiFi 密码
ESP32_IP           = "192.168.4.1"         # ESP32 AP 模式默认 IP
ESP32_PORT         = 8888                   # TCP 端口，与 ESP32 一致

# 发送间隔（秒），0 表示尽可能快
SEND_INTERVAL      = 0

# JPEG 编码质量（1-100，越大越清晰、数据量越大）
JPEG_QUALITY       = 80

# 图像采集分辨率
CAM_WIDTH          = 320
CAM_HEIGHT         = 240

# ============ WiFi 连接 ============
def run_cmd(cmd):
    """执行 shell 命令，返回 (returncode, stdout, stderr)"""
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=10)
        return r.returncode, r.stdout, r.stderr
    except Exception as e:
        return -1, "", str(e)

def get_wifi_iface():
    """查找可用的 WiFi 接口名"""
    # 常见接口名
    for iface in ['wlan0', 'wlan1', 'mlan0', 'ra0']:
        ret, out, _ = run_cmd(f'ifconfig {iface} 2>/dev/null')
        if ret == 0 and out:
            return iface
        ret, out, _ = run_cmd(f'ip link show {iface} 2>/dev/null')
        if ret == 0 and out:
            return iface
    # 兜底
    return 'wlan0'

def get_ip(iface):
    """获取接口的 IP 地址"""
    ret, out, _ = run_cmd(f'ifconfig {iface} 2>/dev/null')
    if ret == 0:
        m = re.search(r'inet\s+(\d+\.\d+\.\d+\.\d+)', out)
        if m:
            return m.group(1)
    ret, out, _ = run_cmd(f'ip addr show {iface} 2>/dev/null')
    if ret == 0:
        m = re.search(r'inet\s+(\d+\.\d+\.\d+\.\d+)', out)
        if m:
            return m.group(1)
    return None

def connect_wifi():
    """
    连接 ESP32 WiFi 热点，确保拿到 IP 后再返回
    """
    iface = get_wifi_iface()
    print(f"[WiFi] 使用接口: {iface}")

    # ---- 方式1: nmcli ----
    ret, out, err = run_cmd('which nmcli 2>/dev/null')
    if ret == 0:
        print(f"[WiFi] 使用 nmcli 连接 {ESP32_SSID}...")
        ret, out, err = run_cmd(
            f'nmcli dev wifi connect "{ESP32_SSID}" password "{ESP32_PASS}" ifname {iface}'
        )
        if ret == 0:
            print(f"[WiFi] nmcli 连接成功，等待 DHCP...")
            return wait_for_ip(iface)

    # ---- 方式2: wpa_cli + udhcpc ----
    ret, out, err = run_cmd('which wpa_cli 2>/dev/null')
    if ret == 0:
        # 先断开现有连接
        run_cmd(f'wpa_cli -i {iface} disconnect 2>/dev/null')

        # 检查是否已有此网络配置
        ret, out, _ = run_cmd(f'wpa_cli -i {iface} list_networks 2>/dev/null')
        net_id = None
        for line in out.split('\n'):
            if ESP32_SSID in line:
                net_id = line.split()[0]
                break

        if net_id is None:
            ret, out, _ = run_cmd(f'wpa_cli -i {iface} add_network 2>/dev/null')
            net_id = out.strip()
            if not net_id or not net_id.isdigit():
                print(f"[WiFi] ⚠️ add_network 失败: {out}")
            else:
                run_cmd(f'wpa_cli -i {iface} set_network {net_id} ssid \'"{ESP32_SSID}"\'')
                run_cmd(f'wpa_cli -i {iface} set_network {net_id} psk \'"{ESP32_PASS}"\'')
                run_cmd(f'wpa_cli -i {iface} set_network {net_id} key_mgmt WPA-PSK')

        if net_id and net_id.isdigit():
            print(f"[WiFi] 启用网络 {net_id}, 连接 {ESP32_SSID}...")
            run_cmd(f'wpa_cli -i {iface} enable_network {net_id}')
            run_cmd(f'wpa_cli -i {iface} select_network {net_id}')
            run_cmd(f'wpa_cli -i {iface} reconnect')

        # 等待 wpa_supplicant 完成关联
        print("[WiFi] 等待 WiFi 关联...")
        for i in range(20):
            time.sleep(0.5)
            ret, status, _ = run_cmd(f'wpa_cli -i {iface} status 2>/dev/null')
            if 'wpa_state=COMPLETED' in status:
                print(f"[WiFi] ✅ WiFi 关联成功!")
                break
            if 'wpa_state=' in status:
                state = re.search(r'wpa_state=(\S+)', status)
                if state:
                    print(f"  状态: {state.group(1)}, 等待中...")
        else:
            print("[WiFi] ⚠️ WiFi 关联超时")

    # ---- 方式3: iwconfig 手动 ----
    # 检查当前关联状态
    ret, status, _ = run_cmd(f'wpa_cli -i {iface} status 2>/dev/null')
    if 'wpa_state=COMPLETED' not in status:
        print("[WiFi] 尝试 iwconfig 手动连接...")
        run_cmd(f'iwconfig {iface} essid "{ESP32_SSID}" key "{ESP32_PASS}"')
        time.sleep(2)

    # ---- 关键: 申请 DHCP IP ----
    return wait_for_ip(iface)

def wait_for_ip(iface, timeout=15):
    """等待 DHCP 分配 IP，并验证能 ping 通 ESP32"""
    print(f"[WiFi] 等待 DHCP 分配 IP...")

    # 先尝试主动申请 DHCP
    run_cmd(f'udhcpc -i {iface} -t 5 -n 2>/dev/null')
    run_cmd(f'dhclient {iface} 2>/dev/null')

    for i in range(timeout * 2):
        ip = get_ip(iface)
        if ip:
            print(f"[WiFi] ✅ 获取到 IP: {ip}")

            # 验证能 ping 通 ESP32
            print(f"[WiFi] 验证到 {ESP32_IP} 的连通性...")
            ret, _, _ = run_cmd(f'ping -c 2 -W 1 {ESP32_IP} 2>/dev/null')
            if ret == 0:
                print(f"[WiFi] ✅ 可 ping 通 ESP32 ({ESP32_IP})")
                return True
            else:
                print(f"[WiFi] ⚠️ 有 IP ({ip}) 但 ping 不通 {ESP32_IP}，继续等待...")
        time.sleep(0.5)

    # 超时，打印诊断信息
    ip = get_ip(iface)
    if ip:
        print(f"[WiFi] 当前 IP: {ip}")
    print(f"[WiFi] ❌ 无法连接到 ESP32。诊断信息:")
    ret, out, _ = run_cmd(f'ifconfig {iface} 2>/dev/null')
    print(f"  {iface} 状态:\n{out[:300]}")
    ret, out, _ = run_cmd('route -n 2>/dev/null || ip route 2>/dev/null')
    print(f"  路由表:\n{out[:300]}")
    return False


# ============ 摄像头采集 → JPEG ============
_maix_cam = None

def _capture_maixpy():
    """方式1: MaixPy v4 (MaixCam 官方 API)，直接编码 JPEG"""
    global _maix_cam
    from maix import camera, image
    if _maix_cam is None:
        _maix_cam = camera.Camera(CAM_WIDTH, CAM_HEIGHT)
    img = _maix_cam.read()
    if img is None:
        return None
    # 不同版本 MaixPy 的 to_jpeg 参数形式不同，逐个尝试
    for encode in (lambda: img.to_jpeg(quality=JPEG_QUALITY),
                   lambda: img.to_jpeg(JPEG_QUALITY)):
        try:
            data = encode()
            if data:
                return bytes(data)
        except Exception:
            continue
    return None

def capture_jpeg():
    """
    采集一帧图像并编码为 JPEG
    返回: jpeg_bytes 或 None
    """
    # --- 方式1: 使用 MaixPy camera 模块 ---
    try:
        result = _capture_maixpy()
        if result is not None:
            return result
    except Exception:
        pass

    # --- 方式2: 使用 V4L2 + OpenCV ---
    try:
        import cv2
        global _cv2_cap
        if '_cv2_cap' not in globals() or _cv2_cap is None:
            _cv2_cap = cv2.VideoCapture(0)
            _cv2_cap.set(cv2.CAP_PROP_FRAME_WIDTH, CAM_WIDTH)
            _cv2_cap.set(cv2.CAP_PROP_FRAME_HEIGHT, CAM_HEIGHT)

        ret, frame = _cv2_cap.read()
        if ret:
            ok, buf = cv2.imencode('.jpg', frame,
                                   [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY])
            if ok:
                return buf.tobytes()
    except Exception:
        pass

    # --- 方式3: PIL/Pillow ---
    try:
        from PIL import Image
        from io import BytesIO
        # 尝试从 V4L2 抓 JPEG 再用 PIL 重编码
        tmp_file = '/tmp/capture.jpg'
        ret = os.system(f'v4l2-ctl --device /dev/video0 '
                        f'--set-fmt-video=width={CAM_WIDTH},height={CAM_HEIGHT},pixelformat=MJPG '
                        f'--stream-mmap --stream-count=1 --stream-to={tmp_file} 2>/dev/null')
        if ret != 0 or not os.path.exists(tmp_file):
            ret = os.system(f'fswebcam -r {CAM_WIDTH}x{CAM_HEIGHT} --jpeg {JPEG_QUALITY} '
                            f'{tmp_file} 2>/dev/null')
        if os.path.exists(tmp_file) and os.path.getsize(tmp_file) > 0:
            img = Image.open(tmp_file)
            out = BytesIO()
            img.save(out, 'JPEG', quality=JPEG_QUALITY)
            return out.getvalue()
    except Exception:
        pass

    return None


# ============ 主循环：发送图像 ============
def main():
    print("=" * 50)
    print("  MaixCam Pro → ESP32 图像发送器")
    print("=" * 50)

    # 1. 连接 WiFi（内部已等待 DHCP + ping 验证）
    if not connect_wifi():
        print("[错误] WiFi 连接失败，退出")
        print("[提示] 请检查:")
        print("  1. ESP32 是否已烧录并上电")
        print("  2. WiFi SSID/密码是否匹配")
        print("  3. ESP32 和 MaixCam Pro 距离是否过远")
        sys.exit(1)

    # 2. 连接 ESP32 TCP 服务器（带重试和诊断）
    sock = None
    retry_count = 0
    while sock is None:
        retry_count += 1
        try:
            print(f"[TCP] 正在连接 {ESP32_IP}:{ESP32_PORT} (第{retry_count}次)...")
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect((ESP32_IP, ESP32_PORT))
            sock.settimeout(None)  # 恢复阻塞模式
            print(f"[TCP] ✅ 已连接到 ESP32 {ESP32_IP}:{ESP32_PORT}")
        except Exception as e:
            print(f"[TCP] ❌ 连接失败: {e}")
            if sock:
                sock.close()
                sock = None

            if retry_count >= 10:
                print("[TCP] 重试 10 次仍失败，请检查 ESP32 是否正常运行")
                print("[诊断] 尝试 telnet 测试端口:")
                ret, out, _ = run_cmd(
                    f'timeout 3 bash -c "echo >/dev/tcp/{ESP32_IP}/{ESP32_PORT}" 2>&1 '
                    f'|| echo "PORT CLOSED"'
                )
                print(f"  {out.strip()}")
                retry_count = 0  # 继续重试

            time.sleep(2)

    # 3. 持续采集并发送
    frame_count = 0
    failed_count = 0

    try:
        while True:
            t_start = time.time()

            # 采集图像 → JPEG
            jpeg_data = capture_jpeg()
            if jpeg_data is None:
                print("[摄像头] ⚠️ 采集失败")
                time.sleep(0.5)
                continue

            data_len = len(jpeg_data)

            try:
                # 发送协议: [4B length LE][JPEG data]
                header = struct.pack('<I', data_len)
                sock.sendall(header + jpeg_data)

                # 等待 ESP32 确认
                ack = sock.recv(2)
                if ack == b'OK':
                    frame_count += 1
                    t_elapsed = (time.time() - t_start) * 1000
                    print(f"[发送] #{frame_count}  JPEG "
                          f"{data_len}B  {t_elapsed:.0f}ms ✅")
                    failed_count = 0
                else:
                    print(f"[发送] ⚠️ 收到异常回复: {ack}")

            except (BrokenPipeError, ConnectionResetError, socket.timeout) as e:
                print(f"[TCP] 连接断开: {e}，尝试重连...")
                sock.close()
                sock = None

                # 重连
                while sock is None:
                    time.sleep(2)
                    try:
                        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                        sock.settimeout(5)
                        sock.connect((ESP32_IP, ESP32_PORT))
                        sock.settimeout(None)
                        print(f"[TCP] 重新连接成功 ✅")
                    except Exception:
                        print("[TCP] 重连中...")
                        if sock:
                            sock.close()
                            sock = None

            # 控制发送速率
            if SEND_INTERVAL > 0:
                elapsed = time.time() - t_start
                if elapsed < SEND_INTERVAL:
                    time.sleep(SEND_INTERVAL - elapsed)

    except KeyboardInterrupt:
        print("\n[退出] 用户中断")

    finally:
        if sock:
            sock.close()
        print("[退出] 连接已关闭")


if __name__ == '__main__':
    main()

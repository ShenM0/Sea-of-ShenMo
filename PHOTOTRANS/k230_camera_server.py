# 立创·庐山派K230-CanMV 无线图传 - 摄像头服务端
# 功能：摄像头采集 + WiFi AP热点 + MJPEG HTTP流服务
# 运行平台：CanMV MicroPython (K230)
#
# 使用说明：
# 1. 将本文件保存到K230开发板（通过CanMV IDE）
# 2. 修改下方的 AP_SSID 和 AP_KEY 为你想要的热点名称和密码
# 3. 修改下方的摄像头参数（分辨率、帧率等）以适配你的需求
# 4. 运行代码，K230将创建WiFi热点并启动MJPEG视频流服务
# 5. ESP32客户端连接到热点后，访问 http://192.168.4.1:80 获取视频流

import network
import socket
import time
import _thread
from media.sensor import *
from media.media import *

# ============================================================
# 配置参数 - 根据实际情况修改
# ============================================================

# WiFi AP 热点配置
AP_SSID = "K230_Video_Link"    # 热点名称（ESP32将连接此热点）
AP_KEY = "12345678"            # 热点密码（至少8位）

# 摄像头配置
CAMERA_ID = 2                   # 庐山派默认摄像头CSI2，即id=2
FRAME_WIDTH = 640               # 输出图像宽度（建议640，平衡画质和传输速度）
FRAME_HEIGHT = 480              # 输出图像高度
FRAME_FPS = 30                  # 摄像头帧率
JPEG_QUALITY = 65               # JPEG压缩质量（1-100，越高画质越好但数据量越大）
                                # 65是传输速度和画质的较好平衡点

# HTTP 服务器配置
HTTP_PORT = 80                  # HTTP端口
STREAM_PATH = "/stream"         # MJPEG流路径
SNAPSHOT_PATH = "/snapshot"     # 单帧快照路径
MAX_CLIENTS = 3                 # 最大同时连接数

# ============================================================
# 全局变量
# ============================================================
sensor = None
media_manager = None
stream_clients = []             # 当前连接的MJPEG客户端列表
stream_lock = _thread.allocate_lock()
running = True

# MJPEG 边界标记
BOUNDARY = b"--FRAME_BOUNDARY\r\n"
HEADER_JPG = b"Content-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n"


# ============================================================
# WiFi AP 初始化
# ============================================================
def init_wifi_ap():
    """初始化K230为WiFi热点（AP模式）"""
    print("[WiFi] 正在创建WiFi热点...")

    ap = network.WLAN(network.AP_IF)

    # 激活AP模式
    if not ap.active():
        ap.active(True)

    # 配置热点参数（CanMV 的 ap.config 只支持 ssid 和 key，不支持 channel 等参数）
    ap.config(ssid=AP_SSID, key=AP_KEY)

    # 等待热点启动
    time.sleep(2)

    ip_info = ap.ifconfig()
    print("[WiFi] 热点已创建:")
    print(f"        SSID: {AP_SSID}")
    print(f"        密码: {AP_KEY}")
    print(f"        IP地址: {ip_info[0]}")
    print(f"        子网掩码: {ip_info[1]}")
    print(f"        网关: {ip_info[2]}")

    return ap


# ============================================================
# 摄像头初始化
# ============================================================
def init_camera():
    """初始化摄像头（GC2093传感器）"""
    global sensor

    print("[Camera] 正在初始化摄像头...")

    # 创建Sensor对象（id=2为默认CSI2接口）
    # 注意：构造函数只接受id，宽高和帧率通过set_framesize设置
    sensor = Sensor(id=CAMERA_ID)
    sensor.reset()

    # 设置输出通道0 - 用于网络传输（JPEG格式由MediaManager处理）
    sensor.set_framesize(chn=CAM_CHN_ID_0, width=FRAME_WIDTH, height=FRAME_HEIGHT)
    # 使用RGB888以获得最佳兼容性，后续可转JPEG
    sensor.set_pixformat(Sensor.RGB888, chn=CAM_CHN_ID_0)

    # 设置水平镜像和垂直翻转（根据摄像头安装方向调整）
    # sensor.set_hmirror(True)
    # sensor.set_vflip(True)

    # 启动摄像头
    sensor.run()

    print(f"[Camera] 摄像头已就绪: {FRAME_WIDTH}x{FRAME_HEIGHT} @ {FRAME_FPS}fps")
    return sensor


# ============================================================
# 媒体管理器初始化
# ============================================================
def init_media():
    """初始化CanMV的MediaManager（用于JPEG编码等图像处理）"""
    global media_manager
    MediaManager.init()
    media_manager = MediaManager
    print("[Media] 媒体管理器已初始化")


# ============================================================
# 图像采集与JPEG编码
# ============================================================
def capture_jpeg():
    """
    从摄像头捕获一帧并编码为JPEG
    返回: bytes - JPEG图像数据
    """
    # 从通道0捕获一帧
    img = sensor.snapshot(chn=CAM_CHN_ID_0)

    if img is None:
        return None

    # 将图像压缩为JPEG格式
    # CanMV的image对象支持compress()方法进行JPEG压缩
    jpeg_data = img.compress(quality=JPEG_QUALITY)

    return jpeg_data


# ============================================================
# MJPEG 流处理线程 - 持续采集并向所有连接的客户端发送帧
# ============================================================
def stream_broadcast_thread():
    """
    后台线程：持续采集摄像头图像，并向所有MJPEG客户端广播
    """
    global running, stream_clients

    print("[Stream] MJPEG广播线程已启动")
    frame_count = 0
    start_time = time.time()

    while running:
        try:
            # 采集一帧JPEG
            jpeg_data = capture_jpeg()

            if jpeg_data is None:
                time.sleep(0.01)
                continue

            # 构建MJPEG帧
            frame = BOUNDARY
            frame += HEADER_JPG % len(jpeg_data)
            frame += jpeg_data
            frame += b"\r\n"

            # 广播给所有已连接的客户端
            with stream_lock:
                dead_clients = []
                for client_sock in stream_clients:
                    try:
                        # 必须 sendall：大帧（几十KB）send 可能只发一部分，导致客户端解析错乱
                        client_sock.sendall(frame)
                    except Exception:
                        # 客户端已断开，标记移除
                        dead_clients.append(client_sock)

                # 清理断开的客户端
                for dead in dead_clients:
                    try:
                        dead.close()
                    except Exception:
                        pass
                    if dead in stream_clients:
                        stream_clients.remove(dead)

            # 帧率统计
            frame_count += 1
            if frame_count % 30 == 0:
                elapsed = time.time() - start_time
                fps = 30 / elapsed if elapsed > 0 else 0
                print(f"[Stream] FPS: {fps:.1f}, 客户端数: {len(stream_clients)}")
                start_time = time.time()

        except Exception as e:
            print(f"[Stream] 错误: {e}")
            time.sleep(0.1)


# ============================================================
# HTTP 请求处理
# ============================================================
def parse_http_request(request):
    """
    解析HTTP请求，提取请求路径
    返回: tuple (method, path) 或 None
    """
    try:
        request_str = request.decode('utf-8')
        lines = request_str.split('\r\n')
        if len(lines) > 0:
            parts = lines[0].split(' ')
            if len(parts) >= 2:
                return parts[0], parts[1]
    except Exception:
        pass
    return None, None


def build_http_response(status_code, content_type, body, extra_headers=""):
    """构建HTTP响应头"""
    status_messages = {
        200: "OK",
        404: "Not Found",
        500: "Internal Server Error"
    }
    msg = status_messages.get(status_code, "Unknown")
    response = f"HTTP/1.1 {status_code} {msg}\r\n"
    response += f"Content-Type: {content_type}\r\n"
    if isinstance(body, bytes):
        response += f"Content-Length: {len(body)}\r\n"
    if extra_headers:
        response += extra_headers
    response += "\r\n"
    if isinstance(body, str):
        response += body
    else:
        response = response.encode('utf-8') + body
    return response if isinstance(body, str) else response


def http_server_thread():
    """
    HTTP服务器主线程：接受客户端连接，根据请求路径分发处理
    """
    global running, stream_clients

    # 创建TCP Socket
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('0.0.0.0', HTTP_PORT))
    server_sock.listen(MAX_CLIENTS + 1)
    server_sock.settimeout(1.0)  # 1秒超时，便于检查running状态

    print(f"[HTTP] 服务器已启动，端口: {HTTP_PORT}")
    print(f"[HTTP] MJPEG流地址: http://192.168.4.1{STREAM_PATH}")
    print(f"[HTTP] 快照地址:   http://192.168.4.1{SNAPSHOT_PATH}")

    while running:
        try:
            conn, addr = server_sock.accept()
            print(f"[HTTP] 新连接: {addr}")

            # 接收HTTP请求
            conn.settimeout(0.5)
            try:
                request = conn.recv(1024)
            except Exception:
                conn.close()
                continue

            method, path = parse_http_request(request)

            if path == STREAM_PATH:
                # MJPEG流请求
                print(f"[HTTP] MJPEG流客户端连接: {addr}")
                response = build_http_response(200, "multipart/x-mixed-replace; boundary=FRAME_BOUNDARY", b"")
                conn.send(response if isinstance(response, bytes) else response.encode('utf-8'))

                # 将客户端加入流列表
                with stream_lock:
                    if len(stream_clients) < MAX_CLIENTS:
                        stream_clients.append(conn)
                    else:
                        conn.close()
                        print(f"[HTTP] MJPEG流客户端已达上限({MAX_CLIENTS})，拒绝连接")

            elif path == SNAPSHOT_PATH:
                # 单帧快照请求
                jpeg_data = capture_jpeg()
                if jpeg_data:
                    conn.send(b"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(jpeg_data))
                    conn.send(jpeg_data)
                else:
                    conn.send(b"HTTP/1.1 500 Internal Server Error\r\n\r\n")
                conn.close()

            elif path == "/" or path == "/index.html":
                # 默认页面 - 显示简单的状态页面
                html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>K230 无线图传</title>
<style>
  body {{ font-family: Arial, sans-serif; text-align: center; background: #1a1a2e; color: #eee; margin: 0; padding: 20px; }}
  h1 {{ color: #e94560; }}
  .info {{ background: #16213e; padding: 15px; border-radius: 10px; margin: 15px 0; }}
  img {{ max-width: 95%; border: 2px solid #e94560; border-radius: 10px; }}
</style>
</head>
<body>
<h1>K230 无线图传服务</h1>
<div class="info">
  <p>摄像头分辨率: {FRAME_WIDTH}x{FRAME_HEIGHT}</p>
  <p>已连接客户端: {len(stream_clients)}/{MAX_CLIENTS}</p>
</div>
<h2>实时画面</h2>
<img src="{STREAM_PATH}" alt="实时视频流">
</body>
</html>"""
                response = build_http_response(200, "text/html; charset=utf-8", html)
                conn.send(response.encode('utf-8') if isinstance(response, str) else response)
                conn.close()

            else:
                # 未知路径
                body = "404 Not Found"
                response = build_http_response(404, "text/plain", body)
                conn.send(response.encode('utf-8') if isinstance(response, str) else response)
                conn.close()

        except OSError as e:
            # accept超时，这是正常的（检查running状态）
            pass
        except Exception as e:
            print(f"[HTTP] 错误: {e}")

    # 清理
    server_sock.close()
    print("[HTTP] 服务器已关闭")


# ============================================================
# 主函数
# ============================================================
def main():
    global running

    print("=" * 50)
    print("  K230 无线图传 - 摄像头服务端")
    print("  庐山派 CanMV K230")
    print("=" * 50)

    try:
        # 1. 初始化WiFi热点
        init_wifi_ap()

        # 2. 初始化摄像头
        init_camera()

        # 3. 初始化媒体管理器
        init_media()

        # 4. 启动MJPEG广播线程
        _thread.start_new_thread(stream_broadcast_thread, ())

        # 5. 启动HTTP服务器（主线程阻塞）
        print("\n[System] 系统已就绪，等待ESP32客户端连接...")
        print("[System] 按 Ctrl+C 停止服务\n")
        http_server_thread()

    except KeyboardInterrupt:
        print("\n[System] 正在停止服务...")
    except Exception as e:
        print(f"[System] 致命错误: {e}")
    finally:
        running = False

        # 清理资源
        if sensor:
            sensor.stop()

        if media_manager:
            MediaManager.deinit()

        print("[System] 服务已停止")


# ============================================================
# 程序入口
# ============================================================
if __name__ == "__main__":
    main()

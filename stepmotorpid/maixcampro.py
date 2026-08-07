from maix import image, camera, display, app, gpio, uart, time
from maix import pinmap, sys, err
from maix import nn
import os

try:
    cam = camera.Camera(640, 480)
except Exception as e:
    print(f"Camera init failed: {e}")
    exit()
try:
    disp = display.Display()
except Exception as e:
    print(f"Display init failed: {e}")
    exit()
try:
     serial_dev = uart.UART("/dev/ttyS0", 115200)
except Exception as e:
     print(f"Serial init failed: {e}")
     exit()

# device_id = sys.device_id()
# if device_id == "maixcam2":
#     pin_function = {
#         "A21": "UART4_TX",
#         "A22": "UART4_RX"
#         # "B0": "UART2_TX",
#         # "B1": "UART2_RX"
#     }
#     device = "/dev/ttyS4"
#     # device = "/dev/ttyS2"
# else:
#     pin_function = {
#         #"A16": "UART0_TX",
#         #"A17": "UART0_RX"
#          "A19": "UART1_TX",
#          "A18": "UART1_RX",

#     }
#     device = "/dev/ttyS1"

# for pin, func in pin_function.items():
#     err.check_raise(pinmap.set_pin_function(pin, func), 
#             f"Failed set pin{pin} function to {func}")
# serial_dev = uart.UART(device, 115200)

pin_name = "B25" if sys.device_id() == "maixcam2" else "B3"
gpio_name = "GPIOB25" if sys.device_id() == "maixcam2" else "GPIOB3"
err.check_raise(pinmap.set_pin_function(pin_name, gpio_name), "set pin failed")
led = gpio.GPIO(gpio_name, gpio.Mode.OUT)
led.value(0)

#模型初始化
model_path = "yolov5s_steel.mud"
if not os.path.exists(model_path):
    #model_path = "/root/models/yolov5s_steel.mud"
    model_path = "/root/models/model_7730.mud"
try:
    detector = nn.YOLOv5(model=model_path, dual_buff=True)
    print(f"模型加载成功: {model_path}")
except Exception as e:
    print(f"模型加载失败: {e}")
    app.set_exit_flag(True)

# 坐标系与标准位置
O_x = 364#中心点O的标准x轴坐标（0cm）
L5 = 260#标准-5cm的像素x坐标（换算系数用，同pro_three）
R5 = 470#标准+5cm的像素x坐标（换算系数用，同pro_three）
CM_PER_PX = 10.0 / (R5 - L5)#像素→厘米 换算系数（10cm / 126px）

#全局偏差值计算函数
def calc_deviation(current_x, target_x):
    """
    计算小球当前位置与标准位置之间的偏差，返回值适合单片机 PID 运算输入。
    参数：
        current_x : 当前小球的 x 轴像素坐标
        target_x  : 标准位置(O_x=364)的 x 轴像素坐标
    返回：
        float 偏差值（单位：厘米）。正值表示球在目标右侧，负值表示在左侧。
    """
    return (current_x - target_x) * CM_PER_PX

#串口命令读取函数
def get_serial_cmd(serial_dev):
    """非阻塞读取串口，返回识别到的命令字符串或 None。"""
    if not serial_dev:
        return None
    try:
        data = serial_dev.read()
        if not data:
            return None
        text = data.decode("utf-8", errors="ignore").strip().lower()
        if "four" in text:
            print("four")
            img.draw_string(600, 400, f"REV", color=image.COLOR_GREEN, scale=1.5)
            return "four"
        if "pass_b" in text:
            return "pass_b"
    except Exception:
        pass
    return None

#状态枚举类
class TaskFourState:
    IDLE = 0#等待串口命令
    STABILIZE_O = 1#稳定在O点，小车行驶至B
    DONE = 2#任务完成
#状态机参数
PRO_FOUR_TOTAL_MS = 8000#AB间行驶时间 ≤ 8 秒
PRO_FOUR_MAX_ERR_CM = 1.0#最大允许误差 1cm
#EMA滤波参数
FILTER_ALPHA = 0.5#平滑系数(0~1)，越小越平滑但滞后越大；响应慢调大(0.5~0.7)，数值跳动大调小
DEADBAND_CM  = 0.15#死区(cm)，偏差绝对值小于此值强制输出0
#发送限频
SEND_INTERVAL_MS = 20#串口发送最小间隔ms（20ms=50Hz）

#题目四状态机函数
def pro_four(ball_cx, lock, deviation_out, state):
    """
    题目四状态机 —— 每帧调用一次，不阻塞主循环。
    小球稳定在中心点O，小车沿黑线行驶至B位置。

    参数：
        ball_cx        : 当前小球中心 x 像素坐标(lock=0 时无效)
        lock           : 是否追踪到球 (0/1)
        deviation_out  : 传出的偏差列表 [deviation_cm]，用于串口发送
        state          : 传入的当前状态 TaskFourState
    返回：
        (new_state, state_changed)
        new_state      : 更新后的状态
        state_changed  : 本帧是否发生了状态切换
    """
    deviation_out[0] = 0.0

    if state == TaskFourState.IDLE:
        # 空闲，等待外部触发（由主循环中收到 "four" 切换）
        return state, False

    elif state == TaskFourState.STABILIZE_O:
        # 阶段1：小车行驶过程中，稳定小球在中心点O (O_x=364)
        if lock:
            deviation_out[0] = calc_deviation(ball_cx, O_x)
            # 偏差绝对值 ≤ 1cm 时算稳定（仅用于显示，不切换状态）
        # 状态切换由主循环中的 B位置检测 或 超时 触发
        return state, False

    return state, False

#状态机变量
task_state = TaskFourState.IDLE
task_t_start = 0#任务启动时刻(ticks_ms)
deviation_out = [0.0]#列表传参，让pro_four写入偏差值
#滤波变量
filtered_dev = [0.0]#EMA滤波后的偏差值(列表传参避免global)
last_send_ms = [0]#上次串口发送时刻(ticks_ms)，列表包装

#M0在线调参: 参数运行时每2秒自动下发给单片机, 不用重烧M0
# KP=外环P(响应速度主项,慢→调大) KD=外环D(振荡→调大,发黏→调小) KI=外环I(零位自学习)
# TILT=摆角限幅(度,最大纠正力度) KPIN=内环P SPD=内环速度上限(度/秒)
# 注意: 这里是兜底默认值; 实际调参改 /root/m0_params.txt (存在时优先, 热加载)
M0_PARAMS = {
    "KP": 0.4,
    "KD": 0.15,
    "KI": 0.2,
    "TILT": 2.0,
    "KPIN": 150,
    "SPD": 200,
}
M0_PARAM_INTERVAL_MS = 2000#参数下发间隔ms
last_param_push_ms = [0]#上次参数下发时刻

def push_m0_params():
    """把 M0_PARAMS 逐行下发给单片机 (#名字,值)。"""
    for k, v in M0_PARAMS.items():
        try:
            serial_dev.write(f"#{k},{v}\n".encode())
        except Exception:
            pass
        time.sleep_ms(5)

#参数文件热加载: 改文件保存即生效(0.5秒内), 文件本身就是持久保存
PARAM_FILE = "/root/m0_params.txt"
PARAM_RELOAD_MS = 500#文件改动检测间隔ms
last_param_check_ms = [0]#上次文件检查时刻
param_file_mtime = [None]#上次看到的文件修改时间

def load_params_file():
    """从 PARAM_FILE 读 键=值 每行一条, 只接受 M0_PARAMS 已有的键, 支持#注释。"""
    try:
        with open(PARAM_FILE) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                k = k.strip().upper()
                if k in M0_PARAMS:
                    M0_PARAMS[k] = float(v)
        return True
    except (OSError, ValueError):
        return False

def get_param_mtime():
    try:
        return os.stat(PARAM_FILE).st_mtime
    except OSError:
        return None

#启动: 文件存在则读取覆盖默认值; 不存在则用默认值生成一份(方便直接改)
if not load_params_file() and not os.path.exists(PARAM_FILE):
    try:
        with open(PARAM_FILE, "w") as f:
            f.write("# M0在线调参文件: 改数值保存即生效, 脚本下次启动也会读取\n")
            for k, v in M0_PARAMS.items():
                f.write(f"{k}={v}\n")
    except OSError:
        pass
param_file_mtime[0] = get_param_mtime()

#FPS 计算变量
fps_count = 0
fps_timer = time.ticks_ms()
fps_current = 0.0

print("等待串口命令four")
try:
    while not app.need_exit():
        img = cam.read()
        led.value(1)
        img.draw_string(600, 400, f"REv", color=image.COLOR_GREEN, scale=2)
        if img is None:
            time.sleep_ms(10)
            continue

        objs = detector.detect(img, conf_th=0.6, iou_th=0.5)
        lock = 0#是否追踪到球 (0/1)
        ball_center_cx = 0
        ball_center_cy = 0
        #参考线
        img.draw_line(0, 220, 640, 220, color=image.COLOR_GREEN)
        img.draw_line(0, 260, 640, 260, color=image.COLOR_GREEN)
        img.draw_line(364, 0, 364, 480, color=image.COLOR_RED)#0点
        for obj in objs:
            img.draw_rect(obj.x, obj.y, obj.w, obj.h, color=image.COLOR_RED)
            msg = f'steel: {obj.score:.2f}'
            img.draw_string(obj.x, obj.y, msg, color=image.COLOR_RED)
            if obj.class_id == 0:
                ball_center_cx = obj.x + obj.w / 2
                ball_center_cy = obj.y + obj.h / 2
                lock = 1
                break

        # 串口命令接收
        cmd = get_serial_cmd(serial_dev)

        #任务状态机
        now_ms = time.ticks_ms()

        if task_state == TaskFourState.IDLE:
            if cmd == "four":
                task_state = TaskFourState.STABILIZE_O
                task_t_start = now_ms
                filtered_dev[0] = 0.0   # 重置滤波
                last_send_ms[0] = 0     # 重置发送计时
                push_m0_params()        # 立即下发一次调参参数
                last_param_push_ms[0] = now_ms
                print("稳定在O点")

        elif task_state == TaskFourState.STABILIZE_O:
            #调用状态机（计算偏差值）
            task_state, _ = pro_four(ball_center_cx, lock, deviation_out, task_state)

            #B位置检测：收到pass_b则完成任务
            if cmd == "pass_b":
                total_elapsed = time.ticks_ms() - task_t_start
                print(f"小车到达B，完成。总耗时:{total_elapsed}ms")
                task_state = TaskFourState.DONE
                deviation_out[0] = 0.0

            #超时保护（超过8秒强制结束）
            # if (task_state == TaskFourState.STABILIZE_O and
            #     time.ticks_ms() - task_t_start > PRO_FOUR_TOTAL_MS):
            #     print("超时强制结束")
            #     task_state = TaskFourState.DONE
            #     deviation_out[0] = 0.0

        elif task_state == TaskFourState.DONE:
            #完成后自动复位到IDLE，等待下一次命令
            task_state = TaskFourState.IDLE
            deviation_out[0] = 0.0

        #发送偏差值到串口（EMA滤波+死区+限频）
        if task_state == TaskFourState.STABILIZE_O:
            try:
                #EMA低通滤波
                filtered_dev[0] = FILTER_ALPHA * deviation_out[0] + (1 - FILTER_ALPHA) * filtered_dev[0]
                #死区：偏差小于阈值强制置零
                if abs(filtered_dev[0]) < DEADBAND_CM:
                    filtered_dev[0] = 0.0
                #限频发送
                now_send = time.ticks_ms()
                if now_send - last_send_ms[0] >= SEND_INTERVAL_MS:
                    serial_dev.write(f"{filtered_dev[0]:.4f}\n".encode())
                    print(f"{filtered_dev[0]:.4f}")
                    last_send_ms[0] = now_send
            except Exception:
                pass

        #定时下发调参参数 (M0中途复位后也能自动恢复为字典值)
        if task_state == TaskFourState.STABILIZE_O:
            if now_ms - last_param_push_ms[0] >= M0_PARAM_INTERVAL_MS:
                push_m0_params()
                last_param_push_ms[0] = now_ms

        #参数文件热加载: 文件被改动立即重新读取并下发 (改文件即调参, 文件即保存)
        if now_ms - last_param_check_ms[0] >= PARAM_RELOAD_MS:
            last_param_check_ms[0] = now_ms
            m = get_param_mtime()
            if m is not None and m != param_file_mtime[0]:
                param_file_mtime[0] = m
                if load_params_file():
                    push_m0_params()
                    last_param_push_ms[0] = now_ms
                    print(f"参数已从{PARAM_FILE}更新: {M0_PARAMS}")

        #FPS计算
        fps_count += 1
        now = time.ticks_ms()
        if now - fps_timer >= 1000:
            fps_current = fps_count * 1000.0 / (now - fps_timer)
            fps_count = 0
            fps_timer = now

        #显示
        img.draw_string(5, 4, f"{fps_current:.0f}FPS", color=image.COLOR_GREEN, scale=1.5)
        state_names = ["IDLE", "STABILIZE_O", "DONE"]
        img.draw_string(5, 23, f"S:{state_names[task_state]}", color=image.COLOR_YELLOW, scale=1.5)

        #显示计时和偏差
        if task_state == TaskFourState.STABILIZE_O:
            elapsed = now_ms - task_t_start
            img.draw_string(5, 45, f"T:{elapsed/1000:.1f}s dev:{deviation_out[0]:.2f}cm",
                            color=image.COLOR_YELLOW, scale=1.5)

        disp.show(img)
        time.sleep_ms(1)  # 释放CPU，防止串口缓冲区溢出

finally:
    disp.close()
    serial_dev.close()
    led.value(0)
    cam.close()
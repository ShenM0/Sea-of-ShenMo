from maix import image, camera, display, app, gpio, uart, time
from maix import pinmap, sys, err
from maix import nn
import os

try:
    cam = camera.Camera(320, 240)
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
pin_name = "B25" if sys.device_id() == "maixcam2" else "B3"
gpio_name = "GPIOB25" if sys.device_id() == "maixcam2" else "GPIOB3"
err.check_raise(pinmap.set_pin_function(pin_name, gpio_name), "set pin failed")
led = gpio.GPIO(gpio_name, gpio.Mode.OUT)
led.value(0)

#模型初始化
model_path = "yolov5s_steel.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/yolov5s_steel.mud"
try:
    detector = nn.YOLOv5(model=model_path, dual_buff=True)
    print(f"[模型] 加载成功: {model_path}")
except Exception as e:
    print(f"模型加载失败: {e}")
    app.set_exit_flag(True)

# 坐标系与标准位置
O_x = 157#中心点O的标准x轴坐标（0cm）
L5 = 84#标准-5cm的像素x坐标（换算系数用，同pro_three）
R5 = 210#标准+5cm的像素x坐标（换算系数用，同pro_three）
CM_PER_PX = 10.0 / (R5 - L5)#像素→厘米 换算系数（10cm / 126px）

#全局偏差值计算函数
def calc_deviation(current_x, target_x):
    """
    计算小球当前位置与标准位置之间的偏差，返回值适合单片机 PID 运算输入。
    参数：
        current_x : 当前小球的 x 轴像素坐标
        target_x  : 标准位置(O_x=157)的 x 轴像素坐标
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
        # 阶段1：小车行驶过程中，稳定小球在中心点O (O_x=157)
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

#FPS 计算变量
fps_count = 0
fps_timer = time.ticks_ms()
fps_current = 0.0

print("等待串口命令four")
try:
    while not app.need_exit():
        img = cam.read()
        led.value(1)
        if img is None:
            time.sleep_ms(10)
            continue

        objs = detector.detect(img, conf_th=0.6, iou_th=0.5)
        lock = 0#是否追踪到球 (0/1)
        ball_center_cx = 0
        ball_center_cy = 0
        #参考线
        img.draw_line(0, 110, 320, 110, color=image.COLOR_GREEN)
        img.draw_line(0, 130, 320, 130, color=image.COLOR_GREEN)
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
                print("稳定在O点")

        elif task_state == TaskFourState.STABILIZE_O:
            #调用状态机（计算偏差值）
            old_state = task_state
            task_state, switched = pro_four(ball_center_cx, lock, deviation_out, task_state)

            #超时保护（超过8秒强制结束）
            if time.ticks_ms() - task_t_start > PRO_FOUR_TOTAL_MS:
                print("超时强制结束")
                task_state = TaskFourState.DONE
                deviation_out[0] = 0.0

        elif task_state == TaskFourState.DONE:
            #完成后自动复位到IDLE，等待下一次命令
            task_state = TaskFourState.IDLE
            deviation_out[0] = 0.0

        #发送偏差值到串口
        if task_state == TaskFourState.STABILIZE_O:
            try:
                serial_dev.write(f"{deviation_out[0]:.4f}\n".encode())
            except Exception:
                pass

        #FPS计算
        fps_count += 1
        now = time.ticks_ms()
        if now - fps_timer >= 1000:
            fps_current = fps_count * 1000.0 / (now - fps_timer)
            fps_count = 0
            fps_timer = now

        #显示
        img.draw_string(250, 0, f"{fps_current:.0f}FPS", color=image.COLOR_GREEN, scale=1.5)
        state_names = ["IDLE", "STABILIZE_O", "DONE"]
        img.draw_string(5, 0, f"S:{state_names[task_state]}", color=image.COLOR_YELLOW, scale=1.5)

        #显示计时和偏差
        if task_state == TaskFourState.STABILIZE_O:
            elapsed = now_ms - task_t_start
            img.draw_string(5, 20, f"T:{elapsed/1000:.1f}s dev:{deviation_out[0]:.2f}cm",
                            color=image.COLOR_YELLOW, scale=1.5)

        disp.show(img)

finally:
    disp.close()
    serial_dev.close()
    led.value(0)
    cam.close()
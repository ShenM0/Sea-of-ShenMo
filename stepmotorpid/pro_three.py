from maix import image, camera, display, app, gpio, uart, time
from maix import pinmap, sys, err
from maix import nn
import os

try:
    cam = camera.Camera(320, 240)#可改
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

# 模型初始化
model_path = "yolov5s_steel.mud"
if not os.path.exists(model_path):
    model_path = "/root/models/yolov5s_steel.mud"
try:
    detector = nn.YOLOv5(model=model_path, dual_buff=True)
    print(f"[模型] 加载成功: {model_path}")
except Exception as e:
    print(f"模型加载失败: {e}")
    app.set_exit_flag(True)

#坐标系与标准位置
L5 = 84#标准-5cm的像素x坐标
R5 = 210#标准+5cm的像素x坐标
O_x = (L5 + R5) // 2 #中心点O的像素坐标（0cm）
CM_PER_PX = 10.0 / (R5 - L5)#像素→厘米 换算系数（10cm / 126px）

#全局偏差值计算函数
def calc_deviation(current_x, target_x):
    """
    计算小球当前位置与标准位置之间的偏差，返回值适合单片机 PID 运算输入。
    参数：
        current_x : 当前小球的 x 轴像素坐标
        target_x  : 标准位置(L5=84 或 R5=210)的 x 轴像素坐标
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
        #解码：b"three" → "three"，"three\n" → "three"，"THREE" → "three"
        text = data.decode("utf-8", errors="ignore").strip().lower()
        if "three" in text:
            print("right")
            return "three"
    except Exception:
        pass
    return None

#状态枚举类
class TaskThreeState:
    IDLE = 0#等待串口命令
    GOTO_P5 = 1#从 O 向 +5cm 运动
    GOTO_M5 = 2#从 +5cm 折返向 -5cm 运动
    STABILIZE = 3#在 -5cm 附近稳定 2 秒
    DONE = 4#任务完成
#状态机参数
PRO_THREE_TOTAL_MS = 5000#总运行时间 ≤ 5 秒
PRO_THREE_STABLE_MS = 2000#稳定时长 2 秒
PRO_THREE_MAX_ERR_CM = 1.0#最大允许误差 1cm
#题目三状态机函数
def pro_three(ball_cx, lock, deviation_out, state):
    """
    题目三状态机 —— 每帧调用一次，不阻塞主循环。
    参数：
        ball_cx        : 当前小球中心 x 像素坐标(lock=0 时无效)
        lock           : 是否追踪到球 (0/1)
        deviation_out  : 传出的偏差列表 [deviation_cm]，用于串口发送
        state          : 传入的当前状态 TaskThreeState
    返回：
        (new_state, state_changed)
        new_state      : 更新后的状态
        state_changed  : 本帧是否发生了状态切换
    """
    deviation_out[0] = 0.0

    if state == TaskThreeState.IDLE:
        #空闲，等待外部触发（由主循环中收到 "three" 切换）
        return state, False

    elif state == TaskThreeState.GOTO_P5:
        #阶段1：向+5cm (R5=210)运动
        if lock:
            deviation_out[0] = calc_deviation(ball_cx, R5)
            # 到达+5cm（误差 ≤ 1cm）则状态切换
            if abs(deviation_out[0]) <= PRO_THREE_MAX_ERR_CM:
                print(f"到达+5cm,折返-5cm")
                return TaskThreeState.GOTO_M5, True#"当前帧发生了状态切换，请切换到新状态"
        return state, False#"当前帧没有发生状态切换，请继续执行当前状态"

    elif state == TaskThreeState.GOTO_M5:
        #阶段2：从+5cm折返向-5cm(L5=84)运动
        if lock:
            deviation_out[0] = calc_deviation(ball_cx, L5)
            #到达-5cm（误差 ≤ 1cm）则状态切换至稳定阶段
            if abs(deviation_out[0]) <= PRO_THREE_MAX_ERR_CM:
                print(f"到达-5cm,开始稳定{PRO_THREE_STABLE_MS/1000}s")
                return TaskThreeState.STABILIZE, True
        return state, False

    elif state == TaskThreeState.STABILIZE:
        #阶段3：在-5cm附近稳定2秒
        if lock:
            deviation_out[0] = calc_deviation(ball_cx, L5)
            #检查是否偏离超过容差
            #abs(deviation_out[0])：取偏差值的绝对值，确保判断方向
            if abs(deviation_out[0]) > PRO_THREE_MAX_ERR_CM:
                #偏离超过1cm，重置稳定计时器
                return TaskThreeState.GOTO_M5, True  # 回到折返阶段重新稳定
        #稳定计时在主循环中处理（需要非局部变量 tracking elapsed）
        return state, False   #时间判断在主循环

    return state, False

#状态机变量
task_state = TaskThreeState.IDLE#
task_t_start = 0#任务启动时刻(ticks_ms)
stable_t_start = 0#稳定阶段起始时刻(ticks_ms)
deviation_out = [0.0]#列表传参，让pro_three写入偏差值

# FPS 计算变量
fps_count = 0
fps_timer = time.ticks_ms()
fps_current = 0.0

print("等待串口命令three")
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

        #串口命令接收
        cmd = get_serial_cmd(serial_dev)
        #任务状态机
        now_ms = time.ticks_ms()

        if task_state == TaskThreeState.IDLE:
            if cmd == "three":
                task_state = TaskThreeState.GOTO_P5
                task_t_start = now_ms#记录任务启动时刻
                print(f"去+5cm")
            elif lock:
                #默认模式: 未触发题目三时, 持续发送相对中心 O 的偏差,
                #单片机闭环稳球在中点; 丢球(lock=0)时不发, 单片机超时停电机
                deviation_out[0] = calc_deviation(ball_center_cx, O_x)
                try:
                    serial_dev.write(f"{deviation_out[0]:.4f}\n".encode())
                except Exception:
                    pass

        elif task_state == TaskThreeState.DONE:
            pass  # 已完成，无操作

        else:
            #活动状态：调用状态机
            old_state = task_state
            #switched：是pro_three()函数返回的 第二个返回值 ，一个布尔值（True/False ）。
            task_state, switched = pro_three(ball_center_cx, lock, deviation_out, task_state)

            #处理STABILIZE的时间判定
            if task_state == TaskThreeState.STABILIZE:
                if old_state != TaskThreeState.STABILIZE:
                    stable_t_start = now_ms#刚进入，记录起始时间

                #检查稳定时长是否达到2秒
                elapsed_stable = time.ticks_ms() - stable_t_start

                if elapsed_stable >= PRO_THREE_STABLE_MS:
                    task_state = TaskThreeState.DONE
                    deviation_out[0] = 0.0#此处作用是 任务完成时清零偏差值，停止向 PID 发送运动指令
                    total_elapsed = time.ticks_ms() - task_t_start
                    print(f"完成，总耗时:{total_elapsed}ms")
                    task_state = TaskThreeState.IDLE  # 复位，等待下一次

            #超时保护（超过5秒强制结束）
            if task_state not in (TaskThreeState.IDLE, TaskThreeState.DONE):
                if time.ticks_ms() - task_t_start > PRO_THREE_TOTAL_MS:
                    print(f"超时，强制结束")
                    task_state = TaskThreeState.IDLE
                    deviation_out[0] = 0.0

            #发送偏差值到串口
            if task_state not in (TaskThreeState.IDLE, TaskThreeState.DONE):
                try:
                    serial_dev.write(f"{deviation_out[0]:.4f}\n".encode())
                except Exception:
                    pass

        #FPS 计算
        fps_count += 1
        now = time.ticks_ms()
        if now - fps_timer >= 1000:
            #计算当前帧率：帧计数 × 1000ms/已用毫秒数，得到每秒帧数
            fps_current = fps_count * 1000.0 / (now - fps_timer)
            #重置帧计数器，开始下一秒的计数
            fps_count = 0
            #重置计时起点为当前时刻 
            fps_timer = now

        #显示
        # 右上角显示帧率
        img.draw_string(5, 20, f"{fps_current:.0f}", color=image.COLOR_GREEN, scale=1.5)
        # if lock:
        #     img.draw_circle(int(ball_center_cx), int(ball_center_cy), 4, color=image.COLOR_RED)
        # state_names = ["IDLE", "GOTO+5", "GOTO-5", "STABLE", "DONE"]
        # img.draw_string(0, 0, f"S:{state_names[task_state]}", color=image.COLOR_YELLOW, scale=2)
        if task_state not in (TaskThreeState.IDLE, TaskThreeState.DONE):
            now_ms2 = time.ticks_ms()
            elapsed = now_ms2 - task_t_start#任务耗时
            img.draw_string(5, 40, f"T:{elapsed/1000:.1f}s dev:{deviation_out[0]:.2f}cm",
                           color=image.COLOR_YELLOW, scale=1.5)
        # elif task_state == TaskThreeState.DONE:
        #     img.draw_string(0, 20, "DONE", color=image.COLOR_GREEN, scale=2)
        # if cmd:
        #     img.draw_string(0, 40, f"CMD:{cmd}", color=image.COLOR_CYAN, scale=1.5)

        disp.show(img)

finally:
    disp.close()
    serial_dev.close()
    led.value(0)
    cam.close()

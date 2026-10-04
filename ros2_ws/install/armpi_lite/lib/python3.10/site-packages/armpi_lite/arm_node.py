"""机械臂运动节点（运动学在上位机解算）。

订阅:
  /armpi_lite/target    geometry_msgs/PointStamped  目标坐标(厘米) -> 执行完整抓取流程
  /armpi_lite/home_cmd  std_msgs/Empty              -> 回工作原位
发布:
  /armpi_lite/status    std_msgs/String  执行状态（给调试界面显示）

抓取流程（与原固件 MOVE_AND_CLAW 一致）:
  移动到目标 -> 合爪 -> 回工作原位 -> 松爪

IK 在本节点解算（armpi_lite.kinematics），通过总线舵机命令(功能号5)直接下发
各关节脉宽，STM32 固件只负责转发到舵机总线，不做逆解。

dry_run=true 时只打印数据帧不实际发送（无硬件调试）。
"""

import queue
import threading
import time

import rclpy
from geometry_msgs.msg import PointStamped
from rclpy.node import Node
from std_msgs.msg import Empty, String

from armpi_lite.kinematics import (
    ARM_RANGE, HOME_PITCH, HOME_X, HOME_Y, HOME_Z, solve_ik,
)
from armpi_lite.serial_protocol import (
    NFC_SUBCMD_CARD,
    PACKET_FUNC_NFC,
    build_bus_servo_position_frame,
    claw_angle_to_position,
    extract_frames,
    resolve_serial_port,
)

CLAW_SERVO_ID = 1
CLAW_RELEASE_ANGLE = 90.0   # 完全张开
CLAW_RELEASE_TIME_MS = 500


class ArmNode(Node):
    def __init__(self):
        super().__init__('arm_controller')

        self.declare_parameter('serial_port', '')
        self.declare_parameter('baudrate', 1000000)
        self.declare_parameter('pitch', 45.0)            # 抓取时末端俯仰角（度）
        self.declare_parameter('min_pitch', -90.0)
        self.declare_parameter('max_pitch', 90.0)
        self.declare_parameter('move_time_ms', 1500)     # 移动到目标的时间
        self.declare_parameter('claw_close_angle', 30.0) # 合爪角度（0=闭合，90=张开）
        self.declare_parameter('claw_time_ms', 500)
        self.declare_parameter('return_time_ms', 1500)   # 回原位的时间
        self.declare_parameter('step_delay_ms', 20)      # 步骤间额外等待
        self.declare_parameter('dry_run', False)
        self.declare_parameter('reconnect_interval_s', 3.0)
        self.declare_parameter('nfc_enabled', True)

        p = lambda name: self.get_parameter(name).value
        self.port_name = p('serial_port')
        self.baudrate = p('baudrate')
        self.step_delay_ms = p('step_delay_ms')
        self.dry_run = p('dry_run')

        self.serial = None
        self.cmd_queue = queue.Queue(maxsize=1)

        self.status_pub = self.create_publisher(String, '/armpi_lite/status', 10)
        self.nfc_pub = self.create_publisher(String, '/armpi_lite/nfc_card', 10)
        self.create_subscription(PointStamped, '/armpi_lite/target', self.on_target, 10)
        self.create_subscription(Empty, '/armpi_lite/home_cmd', self.on_home_cmd, 10)
        self.create_timer(p('reconnect_interval_s'), self.ensure_serial)

        self.worker = threading.Thread(target=self.work_loop, daemon=True)
        self.worker.start()

        if self.get_parameter('nfc_enabled').value:
            self.reader = threading.Thread(target=self.serial_read_loop, daemon=True)
            self.reader.start()

        if self.dry_run:
            self.report('dry_run=true：只打印数据帧，不实际发送（无硬件调试模式）')
        self.ensure_serial()
        self.report('运动节点就绪（上位机解算运动学）')

    # ---------- 串口 ----------

    def ensure_serial(self):
        """打开并重置串口；usbipd 转发的 CH340 必须显式拉低 DTR/RTS，否则收不到数据。"""
        if self.dry_run or (self.serial is not None and self.serial.is_open):
            return
        try:
            import serial
            port = resolve_serial_port(self.port_name or None)
            self.serial = serial.Serial(
                port=port, baudrate=self.baudrate,
                timeout=0.1, write_timeout=0.5,
                xonxoff=False, rtscts=False, dsrdtr=False)
            # usbipd 转发的 CH340：DTR 为高会阻塞串口数据（实测心跳/应答全丢），
            # 必须显式拉低 DTR/RTS 才能正常收发。
            self.serial.setDTR(False)
            self.serial.setRTS(False)
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            self.report(f'串口已打开: {port} @ {self.baudrate}bps')
        except Exception as e:
            self.serial = None
            self.get_logger().warn(f'串口连接失败，稍后重试: {e}', throttle_duration_sec=10.0)

    def serial_read_loop(self):
        """后台读取串口上行数据，解析固件主动上报的 NFC 刷卡帧（功能号 0x11）。"""
        buf = b''
        while True:
            ser = self.serial
            if ser is None or not ser.is_open:
                time.sleep(0.2)
                buf = b''
                continue
            try:
                chunk = ser.read(256)
            except Exception:
                time.sleep(0.2)
                continue
            if chunk:
                buf += chunk
                if len(buf) > 4096:
                    buf = buf[-2048:]
            elif len(buf) > 2048:
                buf = buf[-1024:]

            frames, buf = extract_frames(buf)
            for func, payload in frames:
                if func == PACKET_FUNC_NFC and payload and payload[0] == NFC_SUBCMD_CARD:
                    self.handle_nfc_card(payload)

    def handle_nfc_card(self, payload):
        """处理刷卡上报: [0x01, uid_len, uid...]，发布 UID 到 /armpi_lite/nfc_card。"""
        if len(payload) < 2:
            return
        uid_len = payload[1]
        uid = payload[2:2 + uid_len]
        uid_str = ' '.join(f'{b:02X}' for b in uid)
        msg = String()
        msg.data = uid_str
        self.nfc_pub.publish(msg)
        self.report(f'刷卡: UID {uid_str}')

    def send_positions(self, duration_ms, positions, label):
        """positions: {舵机ID: 脉宽}"""
        items = sorted(positions.items())
        frame = build_bus_servo_position_frame(duration_ms, items)
        if self.dry_run:
            self.get_logger().info(f'[dry_run] {label}: {items} -> 帧: {frame.hex()}')
            return True
        if self.serial is None or not self.serial.is_open:
            self.report('串口未连接，动作中止')
            return False
        try:
            written = self.serial.write(frame)
            self.serial.flush()
            if written != len(frame):
                raise RuntimeError(f'写入不完整: {written}/{len(frame)} 字节')
            self.get_logger().info(f'{label}: {items}')
            return True
        except Exception as e:
            self.get_logger().error(f'串口发送失败: {e}')
            try:
                self.serial.close()
            except Exception:
                pass
            self.serial = None
            return False

    # ---------- 状态上报 ----------

    def report(self, text):
        """写日志并发布 /armpi_lite/status 状态话题（供调试界面显示）。"""
        self.get_logger().info(text)
        msg = String()
        msg.data = text
        self.status_pub.publish(msg)

    # ---------- 命令入口 ----------

    def enqueue(self, cmd):
        """动作指令入队（容量1）；上一动作未完成时丢弃并提示。"""
        try:
            self.cmd_queue.put_nowait(cmd)
        except queue.Full:
            self.report('上一个动作尚未完成，忽略本次指令')

    def on_target(self, msg):
        """目标坐标订阅回调：忽略全零坐标，范围越界仅警告，然后入队抓取流程。"""
        xyz = (msg.point.x, msg.point.y, msg.point.z)
        if xyz == (0.0, 0.0, 0.0):
            self.get_logger().warn('收到全零坐标，忽略')
            return
        for axis, value in zip('xyz', xyz):
            lo, hi = ARM_RANGE[axis]
            if not (lo <= value <= hi):
                self.report(f'警告: {axis}={value:.1f}cm 超出常规工作范围 [{lo}, {hi}]')
        self.enqueue(('grasp', xyz))

    def on_home_cmd(self, _msg):
        self.enqueue(('home', None))

    # ---------- 动作执行 ----------

    def move_to(self, x, y, z, pitch, duration_ms, label):
        result = solve_ik(x, y, z, pitch,
                          self.get_parameter('min_pitch').value,
                          self.get_parameter('max_pitch').value)
        if result is None:
            self.report(f'{label}: IK 无解 ({x:.1f}, {y:.1f}, {z:.1f}) pitch={pitch}')
            return False
        thetas, positions, alpha = result
        self.report(f'{label}: 关节角 {[round(t, 1) for t in thetas]}，'
                    f'脉宽 {dict(sorted(positions.items()))}（实际pitch={alpha:.1f}）')
        return self.send_positions(duration_ms, positions, label)

    def run_grasp_sequence(self, xyz):
        """完整抓取流程：移动到目标 → 合爪 → 回工作原位 → 松爪（逐步等待完成）。"""
        x, y, z = xyz
        move_ms = self.get_parameter('move_time_ms').value
        claw_ms = self.get_parameter('claw_time_ms').value
        return_ms = self.get_parameter('return_time_ms').value
        pitch = self.get_parameter('pitch').value
        close_angle = self.get_parameter('claw_close_angle').value

        self.report(f'开始抓取流程，目标 ({x:.1f}, {y:.1f}, {z:.1f}) pitch={pitch}')

        if not self.move_to(x, y, z, pitch, move_ms, '步骤1/4 移动到目标'):
            return
        time.sleep((move_ms + self.step_delay_ms) / 1000.0)

        claw_pos = claw_angle_to_position(close_angle)
        if not self.send_positions(claw_ms, {CLAW_SERVO_ID: claw_pos}, '步骤2/4 合爪'):
            return
        time.sleep((claw_ms + self.step_delay_ms) / 1000.0)

        if not self.move_to(HOME_X, HOME_Y, HOME_Z, HOME_PITCH, return_ms, '步骤3/4 回工作原位'):
            return
        time.sleep((return_ms + self.step_delay_ms) / 1000.0)

        release_pos = claw_angle_to_position(CLAW_RELEASE_ANGLE)
        if not self.send_positions(CLAW_RELEASE_TIME_MS, {CLAW_SERVO_ID: release_pos}, '步骤4/4 松爪'):
            return
        self.report('抓取流程完成')

    def run_home(self):
        """回工作原位（固件 go_home 等价动作）。"""
        move_ms = self.get_parameter('move_time_ms').value
        if self.move_to(HOME_X, HOME_Y, HOME_Z, HOME_PITCH, move_ms, '回工作原位'):
            self.report('已回工作原位')

    def work_loop(self):
        """工作线程：串行消费动作队列，保证动作流程不被并发打断。"""
        while True:
            cmd, arg = self.cmd_queue.get()
            if cmd == 'grasp':
                self.run_grasp_sequence(arg)
            elif cmd == 'home':
                self.run_home()

    def destroy_node(self):
        if self.serial is not None and self.serial.is_open:
            self.serial.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = ArmNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()

"""可视化调试界面节点（PyQt5）。

两种调试模式（界面下拉切换；相机/检测节点是否启动由 launch 参数 use_camera 决定）：
- 无相机调试：手动输入 X/Y/Z/pitch，点击“移动到目标”，机械臂移动到目标后停留
- 相机调试：显示检测画面与稳定目标坐标，可点击“执行抓取”

公共按钮：回工作原位；夹爪角度滑动条（上位机主动调节，默认张开）；底部为状态日志。
"""

import threading
import time

import numpy as np
import rclpy
from geometry_msgs.msg import PointStamped
from rcl_interfaces.srv import SetParameters
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.parameter import Parameter
from sensor_msgs.msg import Image
from std_msgs.msg import Empty, Float32, String

from PyQt5.QtCore import QTimer, Qt, pyqtSignal
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtWidgets import (
    QApplication, QComboBox, QDoubleSpinBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton, QSlider,
    QVBoxLayout, QWidget,
)


class GuiNode(Node):
    def __init__(self):
        super().__init__('arm_gui')
        self.target_pub = self.create_publisher(PointStamped, '/armpi_lite/target', 10)
        self.home_pub = self.create_publisher(Empty, '/armpi_lite/home_cmd', 10)
        self.claw_pub = self.create_publisher(Float32, '/armpi_lite/claw_cmd', 10)
        self.pitch_client = self.create_client(SetParameters, '/arm_controller/set_parameters')

        self.create_subscription(String, '/armpi_lite/status', self.on_status, 10)
        self.create_subscription(PointStamped, '/armpi_lite/target_stable', self.on_stable, 10)
        self.create_subscription(Image, '/armpi_lite/detection_image', self.on_image, 2)
        self.create_subscription(String, '/armpi_lite/nfc_card', self.on_nfc, 10)
        self.create_subscription(String, '/armpi_lite/isbn_result', self.on_isbn, 10)

        self.status_callback = None
        self.stable_callback = None
        self.image_callback = None
        self.nfc_callback = None
        self.isbn_callback = None

    def on_status(self, msg):
        if self.status_callback:
            self.status_callback(msg.data)

    def on_stable(self, msg):
        if self.stable_callback:
            self.stable_callback((msg.point.x, msg.point.y, msg.point.z))

    def on_image(self, msg):
        if self.image_callback:
            self.image_callback(msg)

    def on_nfc(self, msg):
        if self.nfc_callback:
            self.nfc_callback(msg.data)

    def on_isbn(self, msg):
        if self.isbn_callback:
            self.isbn_callback(msg.data)

    def set_pitch(self, pitch):
        """通过参数服务临时修改 arm_controller 节点的 pitch 参数（不改变配置文件）。"""
        if not self.pitch_client.service_is_ready():
            return
        req = SetParameters.Request()
        req.parameters = [
            Parameter('pitch', Parameter.Type.DOUBLE, float(pitch)).to_parameter_msg()]
        future = self.pitch_client.call_async(req)
        deadline = time.time() + 1.0
        while not future.done() and time.time() < deadline:
            time.sleep(0.02)

    def send_target(self, xyz):
        """发布抓取目标坐标（机械臂坐标系，厘米）。"""
        msg = PointStamped()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'arm_base_cm'
        msg.point.x, msg.point.y, msg.point.z = (float(v) for v in xyz)
        self.target_pub.publish(msg)

    def send_home(self):
        """发布回工作原位指令。"""
        self.home_pub.publish(Empty())

    def send_claw(self, angle):
        """发布夹爪张开角度（度，0=闭合，90=张开）。"""
        msg = Float32()
        msg.data = float(angle)
        self.claw_pub.publish(msg)


class MainWindow(QMainWindow):
    status_received = pyqtSignal(str)
    stable_received = pyqtSignal(tuple)
    image_received = pyqtSignal(object)
    nfc_received = pyqtSignal(str)
    isbn_received = pyqtSignal(str)

    def __init__(self, node):
        super().__init__()
        self.node = node
        # WSLg 标题栏对 CJK 渲染会乱码（Windows RAIL 外壳限制），用英文标题规避；
        # 应用内所有控件仍为中文。
        self.setWindowTitle('Arm Grasp Debug')
        self.stable_xyz = None

        node.status_callback = self.status_received.emit
        node.stable_callback = self.stable_received.emit
        node.image_callback = self.image_received.emit
        node.nfc_callback = self.nfc_received.emit
        node.isbn_callback = self.isbn_received.emit
        self.status_received.connect(self.append_status)
        self.stable_received.connect(self.update_stable)
        self.image_received.connect(self.update_image)
        self.nfc_received.connect(self.update_nfc)
        self.isbn_received.connect(self.update_isbn)

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        # 模式选择
        mode_row = QHBoxLayout()
        mode_row.addWidget(QLabel('调试模式:'))
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(['无相机调试（手动输入坐标）', '相机调试（视觉识别）'])
        self.mode_combo.currentIndexChanged.connect(self.on_mode_changed)
        mode_row.addWidget(self.mode_combo)
        mode_row.addStretch()
        root.addLayout(mode_row)

        # 手动输入区
        manual_box = QGroupBox('手动坐标输入（机械臂坐标系，厘米 / 度）')
        form = QFormLayout(manual_box)
        self.x_spin = self._spin(5.0, 25.0, 10.0)
        self.y_spin = self._spin(-15.0, 15.0, 0.0)
        self.z_spin = self._spin(-2.0, 25.0, 15.0)
        self.pitch_spin = self._spin(-90.0, 90.0, 45.0, step=5.0)
        form.addRow('X (前):', self.x_spin)
        form.addRow('Y (左):', self.y_spin)
        form.addRow('Z (上):', self.z_spin)
        form.addRow('pitch (俯仰角):', self.pitch_spin)
        btn_row = QHBoxLayout()
        run_btn = QPushButton('移动到目标')
        run_btn.clicked.connect(self.on_run_clicked)
        home_btn = QPushButton('回工作原位')
        home_btn.clicked.connect(self.node.send_home)
        btn_row.addWidget(run_btn)
        btn_row.addWidget(home_btn)
        form.addRow(btn_row)
        root.addWidget(manual_box)

        # 夹爪控制（上位机主动调节张开角度，默认张开）
        claw_box = QGroupBox('夹爪控制（0=闭合，90=张开）')
        claw_layout = QHBoxLayout(claw_box)
        claw_layout.addWidget(QLabel('张开角度:'))
        self.claw_slider = QSlider(Qt.Horizontal)
        self.claw_slider.setRange(0, 90)
        self.claw_slider.setValue(90)
        self.claw_slider.valueChanged.connect(self.on_claw_slider_changed)
        self.claw_slider.sliderReleased.connect(self.on_claw_released)
        claw_layout.addWidget(self.claw_slider)
        self.claw_label = QLabel('90° 张开')
        self.claw_label.setMinimumWidth(80)
        claw_layout.addWidget(self.claw_label)
        claw_btn = QPushButton('应用')
        claw_btn.clicked.connect(self.on_claw_apply)
        claw_layout.addWidget(claw_btn)
        root.addWidget(claw_box)

        # 相机调试区
        self.camera_box = QGroupBox('相机调试')
        cam_layout = QVBoxLayout(self.camera_box)
        self.image_label = QLabel('等待检测图像...\n（需 use_camera:=true 启动相机与检测节点）')
        self.image_label.setAlignment(Qt.AlignCenter)
        self.image_label.setMinimumSize(640, 480)
        self.image_label.setStyleSheet('background-color: #222; color: #aaa;')
        cam_layout.addWidget(self.image_label)
        stable_row = QHBoxLayout()
        self.stable_label = QLabel('稳定目标: 无')
        stable_row.addWidget(self.stable_label)
        self.grasp_btn = QPushButton('执行抓取')
        self.grasp_btn.setEnabled(False)
        self.grasp_btn.clicked.connect(self.on_grasp_clicked)
        stable_row.addWidget(self.grasp_btn)
        stable_row.addStretch()
        cam_layout.addLayout(stable_row)
        cam_layout.addWidget(QLabel('提示: config 中 arm_detect.auto_trigger=true 时检测稳定后自动执行'))
        self.camera_box.setVisible(False)
        root.addWidget(self.camera_box)

        # NFC 刷卡显示（订阅 /armpi_lite/nfc_card，由 arm_node 后台读取串口上报）
        nfc_box = QGroupBox('NFC 刷卡')
        nfc_layout = QVBoxLayout(nfc_box)
        self.nfc_label = QLabel('暂无刷卡')
        self.nfc_label.setAlignment(Qt.AlignCenter)
        self.nfc_label.setStyleSheet(
            'font-size: 24px; font-weight: bold; color: #3a3;')
        nfc_layout.addWidget(self.nfc_label)
        nfc_layout.addWidget(QLabel('提示: 卡片靠近 PN532 后自动显示卡号 UID'))
        root.addWidget(nfc_box)

        # ISBN 识别显示（订阅 /armpi_lite/isbn_result，由 arm_node 后台读取串口上报）
        isbn_box = QGroupBox('ISBN 识别')
        isbn_layout = QVBoxLayout(isbn_box)
        self.isbn_label = QLabel('暂无识别')
        self.isbn_label.setAlignment(Qt.AlignCenter)
        self.isbn_label.setStyleSheet(
            'font-size: 24px; font-weight: bold; color: #36c;')
        isbn_layout.addWidget(self.isbn_label)
        isbn_layout.addWidget(QLabel('提示: 书背条码(EAN-13)对准 ESP32-S3-Cam 后自动显示'))
        root.addWidget(isbn_box)

        # 状态日志
        status_box = QGroupBox('状态')
        status_layout = QVBoxLayout(status_box)
        self.status_text = QPlainTextEdit()
        self.status_text.setReadOnly(True)
        self.status_text.setMaximumBlockCount(200)
        status_layout.addWidget(self.status_text)
        root.addWidget(status_box)

        self.resize(700, 800)

    @staticmethod
    def _spin(lo, hi, value, step=0.5):
        spin = QDoubleSpinBox()
        spin.setRange(lo, hi)
        spin.setSingleStep(step)
        spin.setDecimals(1)
        spin.setValue(value)
        return spin

    def on_mode_changed(self, index):
        self.camera_box.setVisible(index == 1)

    def on_run_clicked(self):
        xyz = (self.x_spin.value(), self.y_spin.value(), self.z_spin.value())
        self.node.set_pitch(self.pitch_spin.value())
        self.node.send_target(xyz)
        self.append_status(f'[界面] 移动到目标 ({xyz[0]:.1f}, {xyz[1]:.1f}, {xyz[2]:.1f}) '
                           f'pitch={self.pitch_spin.value():.1f}')

    def on_claw_slider_changed(self, value):
        state = '张开' if value >= 80 else ('闭合' if value <= 10 else '半开')
        self.claw_label.setText(f'{value}° {state}')

    def on_claw_released(self):
        self.apply_claw()

    def on_claw_apply(self):
        self.apply_claw()

    def apply_claw(self):
        self.node.send_claw(self.claw_slider.value())
        self.append_status(f'[界面] 设置夹爪角度 {self.claw_slider.value()}°')

    def on_grasp_clicked(self):
        if self.stable_xyz is None:
            return
        self.node.set_pitch(self.pitch_spin.value())
        self.node.send_target(self.stable_xyz)
        self.append_status(f'[界面] 执行抓取 {self.stable_xyz}')

    def append_status(self, text):
        self.status_text.appendPlainText(time.strftime('%H:%M:%S ') + text)

    def update_stable(self, xyz):
        self.stable_xyz = xyz
        self.stable_label.setText(f'稳定目标: ({xyz[0]:.1f}, {xyz[1]:.1f}, {xyz[2]:.1f}) cm')
        self.grasp_btn.setEnabled(True)

    def update_nfc(self, uid_str):
        self.nfc_label.setText(f'UID: {uid_str}')
        self.append_status(f'[NFC] 刷卡 UID: {uid_str}')

    def update_isbn(self, text):
        self.isbn_label.setText(f'ISBN: {text}')
        self.append_status(f'[ISBN] {text}')

    def update_image(self, msg):
        img = np.frombuffer(msg.data, dtype=np.uint8).reshape(msg.height, msg.width, 3)
        img = np.ascontiguousarray(img[:, :, ::-1])  # BGR -> RGB
        qimg = QImage(img.data, msg.width, msg.height, msg.width * 3,
                      QImage.Format_RGB888).copy()
        self.image_label.setPixmap(QPixmap.fromImage(qimg).scaled(
            self.image_label.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))


def main(args=None):
    import sys
    rclpy.init(args=args)
    node = GuiNode()

    app = QApplication(sys.argv)
    # WSLg 下显式指定中文字体与更大字号（应用内控件）
    from PyQt5.QtGui import QFont
    font = QFont('Noto Sans CJK SC', 13)
    font.setBold(False)
    app.setFont(font)
    window = MainWindow(node)
    window.show()

    def spin_ros():
        try:
            rclpy.spin(node)
        except ExternalShutdownException:
            pass

    spin_thread = threading.Thread(target=spin_ros, daemon=True)
    spin_thread.start()
    shutdown_timer = QTimer()
    shutdown_timer.timeout.connect(lambda: app.quit() if not rclpy.ok() else None)
    shutdown_timer.start(100)

    exit_code = app.exec_()

    node.destroy_node()
    if rclpy.ok():
        rclpy.shutdown()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()

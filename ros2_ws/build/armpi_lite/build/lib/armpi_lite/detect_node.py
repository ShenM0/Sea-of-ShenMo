"""YOLOv5 目标检测 + 坐标转换 + 抓取触发节点。

订阅（ApproximateTime 同步）：
  /camera/color/image_raw          sensor_msgs/Image  (bgr8)
  /camera/aligned_depth/image_raw  sensor_msgs/Image  (16UC1, 毫米)
  /camera/color/camera_info        sensor_msgs/CameraInfo

发布：
  /armpi_lite/target          geometry_msgs/PointStamped  抓取执行指令(厘米)，稳定后仅发一次（auto_trigger=true 时）
  /armpi_lite/target_stable   geometry_msgs/PointStamped  稳定目标坐标(厘米)，供调试界面显示/手动确认执行
  /armpi_lite/target_camera   geometry_msgs/PointStamped  相机坐标系目标点(米)，每帧都发(调试用)
  /armpi_lite/detection_image sensor_msgs/Image           标注后的检测画面(调试用)

检测模型使用 hwcompt 原版 YOLOv5 代码（yolov5_vendor 目录），保证与 best.pt 权重兼容。
"""

import os
import sys
from collections import deque

import numpy as np
import rclpy
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import PointStamped
from message_filters import ApproximateTimeSynchronizer, Subscriber
from rclpy.node import Node
from sensor_msgs.msg import CameraInfo, Image

from armpi_lite.serial_protocol import camera_to_arm_xyz_cm


def load_yolov5(weights_path, conf_thres, iou_thres, input_size, device_name, logger):
    """加载 vendor 的 YOLOv5（与 hwcompt/rstest.py 相同的加载路径）。"""
    vendor_dir = os.path.join(get_package_share_directory('armpi_lite'), 'yolov5_vendor')
    if vendor_dir not in sys.path:
        sys.path.insert(0, vendor_dir)

    import torch
    from models.experimental import attempt_load
    from utils.general import check_img_size, non_max_suppression, scale_coords
    from utils.torch_utils import select_device

    device = select_device(device_name)
    model = attempt_load(weights_path, map_location=device)
    stride = int(model.stride.max())
    img_size = check_img_size(input_size, s=stride)
    logger.info(f'YOLOv5 已加载: {weights_path} (输入 {img_size}, 设备 {device})')

    def detect(bgr_image):
        """返回 [(x1, y1, x2, y2, conf), ...]，坐标为原图像素。"""
        from utils.datasets import letterbox

        img = letterbox(bgr_image, new_shape=(img_size, img_size), auto=False)[0]
        img = img[:, :, ::-1].transpose(2, 0, 1)  # BGR HWC -> RGB CHW
        img = np.ascontiguousarray(img)

        with torch.no_grad():
            tensor = torch.from_numpy(img).to(device).float() / 255.0
            if tensor.ndimension() == 3:
                tensor = tensor.unsqueeze(0)
            pred = model(tensor, augment=False)[0]
            pred = non_max_suppression(pred, conf_thres, iou_thres, classes=None, agnostic=False)

        results = []
        det = pred[0]
        if det is not None and len(det):
            det[:, :4] = scale_coords(tensor.shape[2:], det[:, :4], bgr_image.shape).round()
            for *xyxy, conf, _cls in det.cpu().tolist():
                results.append((xyxy[0], xyxy[1], xyxy[2], xyxy[3], conf))
        return results

    return detect


class DetectNode(Node):
    def __init__(self):
        super().__init__('arm_detect')

        self.declare_parameter('weights', os.path.join(
            get_package_share_directory('armpi_lite'), 'yolov5_vendor', 'weights', 'best.pt'))
        self.declare_parameter('conf_threshold', 0.6)
        self.declare_parameter('iou_threshold', 0.45)
        self.declare_parameter('input_size', 512)
        self.declare_parameter('device', 'cpu')
        # 相机相对机械臂基座的安装偏移（厘米），与 hwcompt 原程序一致
        self.declare_parameter('x_offset_cm', 18.0)
        self.declare_parameter('y_offset_cm', 1.5)
        self.declare_parameter('z_offset_cm', 16.0)
        # 深度取值：以检测框中心为圆心取 ROI 内有效深度的中位数，比单点更稳
        self.declare_parameter('depth_roi_radius', 3)
        self.declare_parameter('min_depth_m', 0.05)
        self.declare_parameter('max_depth_m', 1.5)
        # 抓取触发：目标连续 stable_frames 帧位置稳定才发布一次 /armpi_lite/target
        self.declare_parameter('stable_frames', 5)
        self.declare_parameter('stable_tolerance_cm', 3.0)
        self.declare_parameter('lost_frames_reset', 15)
        self.declare_parameter('auto_trigger', True)
        self.declare_parameter('publish_debug_image', True)

        p = lambda name: self.get_parameter(name).value
        self.x_offset = p('x_offset_cm')
        self.y_offset = p('y_offset_cm')
        self.z_offset = p('z_offset_cm')
        self.roi_radius = p('depth_roi_radius')
        self.min_depth = p('min_depth_m')
        self.max_depth = p('max_depth_m')
        self.stable_frames = p('stable_frames')
        self.stable_tol = p('stable_tolerance_cm')
        self.lost_reset = p('lost_frames_reset')
        self.auto_trigger = p('auto_trigger')
        self.publish_debug = p('publish_debug_image')

        self.detect = load_yolov5(
            p('weights'), p('conf_threshold'), p('iou_threshold'),
            p('input_size'), p('device'), self.get_logger())

        self.target_pub = self.create_publisher(PointStamped, '/armpi_lite/target', 10)
        self.target_stable_pub = self.create_publisher(PointStamped, '/armpi_lite/target_stable', 10)
        self.target_cam_pub = self.create_publisher(PointStamped, '/armpi_lite/target_camera', 10)
        self.debug_img_pub = self.create_publisher(Image, '/armpi_lite/detection_image', 2)

        color_sub = Subscriber(self, Image, '/camera/color/image_raw')
        depth_sub = Subscriber(self, Image, '/camera/aligned_depth/image_raw')
        info_sub = Subscriber(self, CameraInfo, '/camera/color/camera_info')
        self.sync = ApproximateTimeSynchronizer(
            [color_sub, depth_sub, info_sub], queue_size=10, slop=0.1)
        self.sync.registerCallback(self.on_frame)

        self.recent_targets = deque(maxlen=self.stable_frames)
        self.sent_target = None   # 已发送的目标坐标(厘米)，None 表示待触发
        self.lost_count = 0

        self.get_logger().info('检测节点就绪，等待相机图像...')

    @staticmethod
    def image_to_numpy(msg):
        """sensor_msgs/Image (bgr8) → numpy 数组 (H, W, 3)。"""
        img = np.frombuffer(msg.data, dtype=np.uint8).reshape(msg.height, msg.width, 3)
        return img

    @staticmethod
    def depth_to_numpy(msg):
        """sensor_msgs/Image (16UC1, 毫米) → numpy 数组 (H, W)。"""
        return np.frombuffer(msg.data, dtype=np.uint16).reshape(msg.height, msg.width)

    def roi_depth_m(self, depth_image, ux, uy):
        """取检测框中心 ROI 内有效深度的中位数（米），无有效深度返回 None。"""
        r = self.roi_radius
        x1, x2 = max(0, ux - r), min(depth_image.shape[1], ux + r + 1)
        y1, y2 = max(0, uy - r), min(depth_image.shape[0], uy + r + 1)
        roi = depth_image[y1:y2, x1:x2].astype(np.float32) / 1000.0  # mm -> m
        valid = roi[(roi > self.min_depth) & (roi < self.max_depth)]
        if valid.size == 0:
            return None
        return float(np.median(valid))

    def on_frame(self, color_msg, depth_msg, info_msg):
        """同步帧回调：YOLO 检测 → 深度反投影 → 坐标系转换 → 触发判断 → 调试图像。"""
        color_image = self.image_to_numpy(color_msg)
        depth_image = self.depth_to_numpy(depth_msg)

        detections = self.detect(color_image)

        best = None
        for x1, y1, x2, y2, conf in detections:
            if best is None or conf > best[4]:
                best = (x1, y1, x2, y2, conf)

        arm_xyz = None
        camera_xyz = None
        ux = uy = None
        if best is not None:
            ux = int((best[0] + best[2]) / 2)
            uy = int((best[1] + best[3]) / 2)
            depth_m = self.roi_depth_m(depth_image, ux, uy)
            if depth_m is not None:
                fx, fy = info_msg.k[0], info_msg.k[4]
                cx, cy = info_msg.k[2], info_msg.k[5]
                camera_xyz = [(ux - cx) * depth_m / fx,
                              (uy - cy) * depth_m / fy,
                              depth_m]
                arm_xyz = camera_to_arm_xyz_cm(
                    camera_xyz, self.x_offset, self.y_offset, self.z_offset)

                cam_msg = PointStamped()
                cam_msg.header = color_msg.header
                cam_msg.point.x, cam_msg.point.y, cam_msg.point.z = camera_xyz
                self.target_cam_pub.publish(cam_msg)

        self.update_trigger(arm_xyz, color_msg.header)

        if self.publish_debug:
            self.publish_debug_image_msg(color_image, detections, best, ux, uy,
                                         camera_xyz, arm_xyz, color_msg.header)

    def update_trigger(self, arm_xyz, header):
        """抓取触发状态机：目标连续 stable_frames 帧位置稳定才触发一次，丢失后重置。"""
        if arm_xyz is None or arm_xyz == [0.0, 0.0, 0.0]:
            self.lost_count += 1
            if self.lost_count >= self.lost_reset:
                if self.sent_target is not None:
                    self.get_logger().info('目标已丢失，重新等待抓取触发')
                self.sent_target = None
                self.recent_targets.clear()
            return

        self.lost_count = 0
        self.recent_targets.append(arm_xyz)

        if self.sent_target is not None:
            # 已抓过的目标明显移动后允许再次触发
            dist = np.linalg.norm(np.array(arm_xyz) - np.array(self.sent_target))
            if dist > 2 * self.stable_tol:
                self.get_logger().info('检测到目标位置变化，重新等待稳定')
                self.sent_target = None
                self.recent_targets.clear()
            return

        if len(self.recent_targets) < self.stable_frames:
            return

        arr = np.array(self.recent_targets)
        mean = arr.mean(axis=0)
        spread = np.linalg.norm(arr - mean, axis=1).max()
        if spread > self.stable_tol:
            return

        stable_xyz = [round(float(v), 2) for v in mean]

        msg = PointStamped()
        msg.header.stamp = header.stamp
        msg.header.frame_id = 'arm_base_cm'
        msg.point.x, msg.point.y, msg.point.z = stable_xyz
        self.target_stable_pub.publish(msg)

        if self.auto_trigger:
            self.target_pub.publish(msg)
            self.get_logger().info(f'目标稳定，发布抓取坐标(厘米): {stable_xyz}')
        else:
            self.get_logger().info(f'目标稳定: {stable_xyz}（auto_trigger=false，等待界面确认执行）')
        self.sent_target = stable_xyz

    def publish_debug_image_msg(self, color_image, detections, best, ux, uy,
                                camera_xyz, arm_xyz, header):
        """在画面上标注检测框/中心点/坐标并发布调试图像话题。"""
        import cv2
        canvas = color_image.copy()
        for x1, y1, x2, y2, conf in detections:
            cv2.rectangle(canvas, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
            cv2.putText(canvas, f'{conf:.2f}', (int(x1), int(y1) - 5),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
        if ux is not None:
            cv2.circle(canvas, (ux, uy), 4, (255, 255, 255), 5)
        if arm_xyz is not None:
            cv2.putText(canvas, f'cam(m): {[round(v, 3) for v in camera_xyz]}', (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
            cv2.putText(canvas, f'arm(cm): {arm_xyz}', (10, 60),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        if self.sent_target is not None:
            cv2.putText(canvas, f'SENT: {self.sent_target}', (10, 90),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)

        msg = Image()
        msg.header = header
        msg.height, msg.width = canvas.shape[:2]
        msg.encoding = 'bgr8'
        msg.step = msg.width * 3
        msg.data = canvas.tobytes()
        self.debug_img_pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = DetectNode()
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

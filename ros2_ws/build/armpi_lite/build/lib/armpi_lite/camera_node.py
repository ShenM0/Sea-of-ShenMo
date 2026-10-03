"""D435i 深度相机节点。

用 pyrealsense2 采集彩色流和深度流，将深度对齐到彩色图后发布：
  /camera/color/image_raw          sensor_msgs/Image  (bgr8)
  /camera/aligned_depth/image_raw  sensor_msgs/Image  (16UC1, 单位毫米)
  /camera/color/camera_info        sensor_msgs/CameraInfo (彩色相机内参)

无相机时节点不退出，周期性重试连接（虚拟机需先做 USB 透传）。
"""

import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import CameraInfo, Image

try:
    import pyrealsense2 as rs
except ImportError:
    rs = None


class CameraNode(Node):
    def __init__(self):
        super().__init__('d435i_camera')

        self.declare_parameter('width', 640)
        self.declare_parameter('height', 480)
        self.declare_parameter('fps', 30)
        self.declare_parameter('serial_no', '')
        self.declare_parameter('color_frame_id', 'camera_color_frame')
        self.declare_parameter('depth_frame_id', 'camera_depth_frame')
        self.declare_parameter('retry_interval_s', 3.0)

        self.width = self.get_parameter('width').value
        self.height = self.get_parameter('height').value
        self.fps = self.get_parameter('fps').value
        self.serial_no = self.get_parameter('serial_no').value
        self.color_frame_id = self.get_parameter('color_frame_id').value
        self.depth_frame_id = self.get_parameter('depth_frame_id').value

        self.color_pub = self.create_publisher(Image, '/camera/color/image_raw', 10)
        self.depth_pub = self.create_publisher(Image, '/camera/aligned_depth/image_raw', 10)
        self.info_pub = self.create_publisher(CameraInfo, '/camera/color/camera_info', 10)

        self.pipeline = None
        self.align = None
        self.started = False

        if rs is None:
            self.get_logger().error(
                'pyrealsense2 未安装，请执行: pip3 install --user pyrealsense2')
            return

        # 用定时器驱动取帧，启动失败时按 retry_interval_s 周期重试
        self.timer = self.create_timer(0.001, self.poll_once)

    def try_start(self):
        self.pipeline = rs.pipeline()
        config = rs.config()
        if self.serial_no:
            config.enable_device(self.serial_no)
        config.enable_stream(rs.stream.depth, self.width, self.height, rs.format.z16, self.fps)
        config.enable_stream(rs.stream.color, self.width, self.height, rs.format.bgr8, self.fps)
        self.pipeline.start(config)
        self.align = rs.align(rs.stream.color)
        self.started = True
        self.get_logger().info(
            f'RealSense 已启动: {self.width}x{self.height}@{self.fps}fps，深度已对齐到彩色图')

    def poll_once(self):
        if not self.started:
            try:
                self.try_start()
            except Exception as e:
                self.get_logger().warn(
                    f'相机连接失败（请检查 USB 透传/接线），稍后重试: {e}',
                    throttle_duration_sec=5.0)
                self.pipeline = None
                import time
                time.sleep(self.get_parameter('retry_interval_s').value)
            return

        try:
            frames = self.pipeline.wait_for_frames(timeout_ms=5000)
        except RuntimeError as e:
            self.get_logger().warn(f'取帧失败，尝试重启相机管线: {e}')
            try:
                self.pipeline.stop()
            except Exception:
                pass
            self.started = False
            return

        aligned = self.align.process(frames)
        depth_frame = aligned.get_depth_frame()
        color_frame = aligned.get_color_frame()
        if not depth_frame or not color_frame:
            return

        stamp = self.get_clock().now().to_msg()

        color_image = np.asanyarray(color_frame.get_data())
        depth_image = np.asanyarray(depth_frame.get_data())

        color_msg = Image()
        color_msg.header.stamp = stamp
        color_msg.header.frame_id = self.color_frame_id
        color_msg.height = color_image.shape[0]
        color_msg.width = color_image.shape[1]
        color_msg.encoding = 'bgr8'
        color_msg.step = color_image.shape[1] * 3
        color_msg.data = color_image.tobytes()

        depth_msg = Image()
        depth_msg.header.stamp = stamp
        depth_msg.header.frame_id = self.depth_frame_id
        depth_msg.height = depth_image.shape[0]
        depth_msg.width = depth_image.shape[1]
        depth_msg.encoding = '16UC1'
        depth_msg.step = depth_image.shape[1] * 2
        depth_msg.data = depth_image.tobytes()

        intr = color_frame.profile.as_video_stream_profile().intrinsics
        info_msg = CameraInfo()
        info_msg.header.stamp = stamp
        info_msg.header.frame_id = self.color_frame_id
        info_msg.height = intr.height
        info_msg.width = intr.width
        info_msg.distortion_model = 'plumb_bob'
        info_msg.d = [float(c) for c in intr.coeffs]
        info_msg.k = [intr.fx, 0.0, intr.ppx,
                      0.0, intr.fy, intr.ppy,
                      0.0, 0.0, 1.0]
        info_msg.r = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]
        info_msg.p = [intr.fx, 0.0, intr.ppx, 0.0,
                      0.0, intr.fy, intr.ppy, 0.0,
                      0.0, 0.0, 1.0, 0.0]

        self.color_pub.publish(color_msg)
        self.depth_pub.publish(depth_msg)
        self.info_pub.publish(info_msg)

    def destroy_node(self):
        if self.started and self.pipeline is not None:
            try:
                self.pipeline.stop()
            except Exception:
                pass
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = CameraNode()
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

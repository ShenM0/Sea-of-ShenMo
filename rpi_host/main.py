"""
睿抗国赛上位机入口 (树莓派5 + PySide6)

用法:
  python main.py                # 正常运行
  python main.py --mock         # 无相机调试 (合成图像)
  python main.py --no-arm       # 不连接机械臂
  python main.py --no-model     # 不加载模型 (纯画面调试)
"""
import argparse
import os
import sys

import yaml
from PySide6.QtWidgets import QApplication

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))


def load_settings():
    with open(os.path.join(PROJECT_ROOT, 'config', 'settings.yaml'), encoding='utf-8') as f:
        return yaml.safe_load(f)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mock', action='store_true', help='使用合成图像代替 D435i')
    parser.add_argument('--no-arm', action='store_true', help='不连接机械臂串口')
    parser.add_argument('--no-model', action='store_true', help='不加载 YOLOv5 模型')
    args = parser.parse_args()

    cfg = load_settings()
    if args.mock:
        cfg['camera']['mock'] = True
    if args.no_arm:
        cfg['serial']['enabled'] = False

    app = QApplication(sys.argv)
    app.setApplicationName("RAICOM-AI-Vision-Host")

    # ---- 相机 ----
    from core.camera import create_camera
    camera = create_camera(cfg['camera'])

    # ---- 坐标转换 ----
    from core.coord_transform import CoordTransformer
    transformer = CoordTransformer()
    calib = os.path.join(PROJECT_ROOT, cfg['calib_params'])
    if os.path.exists(calib):
        transformer.load_params(calib)
    else:
        print(f"[WARN] 未找到标定参数: {calib}")

    # ---- 模型 ----
    detector = None
    if not args.no_model:
        try:
            from core.detector import YoloV5Detector
            print("[INFO] 加载 YOLOv5 模型...")
            detector = YoloV5Detector(cfg['model'], PROJECT_ROOT)
            print("[INFO] 模型加载完成")
        except Exception as e:
            print(f"[ERROR] 模型加载失败, 将以纯画面模式运行: {e}")

    # ---- 机械臂 ----
    from core.arm_controller import ArmController
    arm = ArmController(cfg['arm'], cfg['serial']['port'], cfg['serial']['baudrate'],
                        enabled=cfg['serial']['enabled'])
    if cfg['serial']['enabled']:
        arm.connect()  # 失败不致命, GUI 里可重试

    # ---- 流水线 + 会话 + GUI ----
    from core.pipeline import PipelineWorker
    from core.session import CompetitionSession
    from ui.main_window import MainWindow

    pipeline = PipelineWorker(camera, detector, transformer)
    session = CompetitionSession(cfg['competition'], PROJECT_ROOT)
    window = MainWindow(pipeline, arm, session, cfg, transformer)
    window.set_system_status(camera_ok=not cfg['camera']['mock'], model_ok=detector is not None)
    window.show()

    pipeline.start()
    ret = app.exec()

    pipeline.stop()
    pipeline.wait(2000)
    camera.stop()
    sys.exit(ret)


if __name__ == '__main__':
    main()

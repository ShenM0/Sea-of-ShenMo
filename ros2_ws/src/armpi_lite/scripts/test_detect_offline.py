#!/usr/bin/env python3
"""离线测试：不启动 ROS，直接用 best.pt 对示例图片做检测并打印坐标。

用法:
  python3 scripts/test_detect_offline.py [图片路径]
  （不带参数时默认使用 hwcompt 的 captured_images 里的第一张图）
"""

import glob
import os
import sys

# 直接使用源码树里的 vendor 代码，无需先 colcon build
VENDOR_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'yolov5_vendor')
sys.path.insert(0, os.path.abspath(VENDOR_DIR))

import cv2
import numpy as np
import torch
from models.experimental import attempt_load
from utils.datasets import letterbox
from utils.general import check_img_size, non_max_suppression, scale_coords
from utils.torch_utils import select_device

WEIGHTS = os.path.join(os.path.abspath(VENDOR_DIR), 'weights', 'best.pt')
INPUT_SIZE = 512
CONF_THRES = 0.6
IOU_THRES = 0.45


def detect(image, model, device):
    img = letterbox(image, new_shape=(INPUT_SIZE, INPUT_SIZE), auto=False)[0]
    img = img[:, :, ::-1].transpose(2, 0, 1)
    img = np.ascontiguousarray(img)
    tensor = torch.from_numpy(img).to(device).float() / 255.0
    tensor = tensor.unsqueeze(0)
    with torch.no_grad():
        pred = model(tensor, augment=False)[0]
        pred = non_max_suppression(pred, CONF_THRES, IOU_THRES, classes=None, agnostic=False)
    results = []
    det = pred[0]
    if det is not None and len(det):
        det[:, :4] = scale_coords(tensor.shape[2:], det[:, :4], image.shape).round()
        for *xyxy, conf, cls in det.cpu().tolist():
            results.append((*[int(v) for v in xyxy], conf, int(cls)))
    return results


def main():
    if len(sys.argv) > 1:
        image_path = sys.argv[1]
    else:
        candidates = sorted(glob.glob(
            '/home/shenmo/hwcompt/yolov5_d435i_detection-main/captured_images/*.jpg'))
        if not candidates:
            print('未找到示例图片，请手动传入图片路径')
            sys.exit(1)
        image_path = candidates[0]

    image = cv2.imread(image_path)
    if image is None:
        print(f'图片读取失败: {image_path}')
        sys.exit(1)

    print(f'加载模型: {WEIGHTS}')
    device = select_device('cpu')
    model = attempt_load(WEIGHTS, map_location=device)
    check_img_size(INPUT_SIZE, s=int(model.stride.max()))

    print(f'检测图片: {image_path} ({image.shape[1]}x{image.shape[0]})')
    results = detect(image, model, device)

    if not results:
        print('未检测到目标')
        sys.exit(0)

    for x1, y1, x2, y2, conf, cls in results:
        ux, uy = (x1 + x2) // 2, (y1 + y2) // 2
        print(f'类别={cls} 置信度={conf:.3f} 框=({x1},{y1})-({x2},{y2}) 中心像素=({ux},{uy})')

    print('\nOK：模型加载与检测正常。实际坐标还需结合深度图与相机内参（由 detect_node 完成）。')


if __name__ == '__main__':
    main()

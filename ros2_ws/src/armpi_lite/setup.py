import os
from setuptools import setup

package_name = 'armpi_lite'


def directory_data_files(source_dir, install_base):
    """递归收集目录下所有文件，保持目录结构安装到 share 目录。"""
    data_files = []
    for root, _, files in os.walk(source_dir):
        if not files:
            continue
        rel = os.path.relpath(root, source_dir)
        dest = os.path.join(install_base, source_dir) if rel == '.' \
            else os.path.join(install_base, source_dir, rel)
        data_files.append((dest, [os.path.join(root, f) for f in files]))
    return data_files


share_dir = os.path.join('share', package_name)

data_files = [
    ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
    (share_dir, ['package.xml']),
    (os.path.join(share_dir, 'launch'), ['launch/grasp.launch.py']),
    (os.path.join(share_dir, 'config'), ['config/armpi_lite.yaml']),
]
data_files += directory_data_files('yolov5_vendor', share_dir)

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=data_files,
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='shenmo',
    maintainer_email='shenmo@todo.todo',
    description='D435i + YOLOv5 + STM32 串口的机械臂视觉抓取',
    license='MIT',
    entry_points={
        'console_scripts': [
            'camera_node = armpi_lite.camera_node:main',
            'detect_node = armpi_lite.detect_node:main',
            'arm_node = armpi_lite.arm_node:main',
            'gui_node = armpi_lite.gui_node:main',
        ],
    },
)

# 睿抗国赛上位机 (树莓派5 + PySide6)

D435i 深度相机 + YOLOv5(CPU) + ArmPi 机械臂抓放, 带可视化交互界面与评分模式。

## 功能

- 实时画面: 检测框 / 相机系与机械臂系坐标 / FPS / 机械臂状态
- 识别开关: 界面上一键开始/停止识别
- 机械臂: 手动抓取 / 自动抓取 / 回原位 / 软件急停
- 指定位置放置: 输入放置点坐标(工作空间内), 抓取→放置点松爪→回默认姿态 (需配套固件 cmd=0x03)
- 比赛模式: 20 样本采集、180s 计时(超时按 0.5 分/秒扣)、历史回看(缩略图)、
  专家评审模式(逐张判对/错, 自动汇总准确度分 + 时效分)、成绩保存 summary.json

## 目录

```
rpi_host/
├── main.py                 # 入口
├── config/settings.yaml    # 相机/模型/串口/机械臂/评分规则配置
├── config/calib_params.json# 手眼标定参数
├── core/                   # 相机、检测、坐标转换、串口协议、流水线、比赛会话
├── ui/main_window.py       # PySide6 主窗口
├── vendor/                 # yolov5 models/utils 
├── weights/best.pt         # 模型权重
├── records/                # 比赛样本截图与成绩 (运行时生成)
└── deploy/raicom-host.service  # systemd 开机自启
```

## 树莓派5 部署

```bash
# 1. 系统准备 (Raspberry Pi OS 64-bit Bookworm)
sudo apt update && sudo apt install -y python3-venv libgl1 libegl1 libxkbcommon0 libdbus-1-3 \
    fonts-noto-cjk        # 中文字体, 否则界面文字显示为方块

# 2. 虚拟环境
cd ~/raicomf/rpi_host
python3 -m venv --system-site-packages .venv
source .venv/bin/activate

# 3. 依赖 (torch 用官方 CPU wheel)
pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
pip install -r requirements-pi.txt
pip install pyrealsense2        # 若无 aarch64 wheel, 需源码编译 librealsense

# 4. USB 串口权限
sudo usermod -aG dialout $USER  # 重新登录生效

# 5. 运行
python main.py                  # 正常
python main.py --mock --no-arm  # 无硬件调试
```

## 开机自启

```bash
# 修改 service 中的 User / 路径为实际值后:
sudo cp deploy/raicom-host.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable raicom-host.service
sudo systemctl start raicom-host.service
journalctl -u raicom-host.service -f   # 查看日志
```

## 固件配套修改 (testv1ArmPiUltra)

`Hiwonder/System/packet_handle.c` 新增 `cmd=0x03`(抓放一体), 原文件备份为 `packet_handle.c.orig`。
修改后需用 Keil 重新编译烧录。帧格式见 `core/arm_protocol.py`。

## 比赛现场流程

1. 开机自启进入界面 → 确认相机/机械臂状态
2. 点"开始比赛会话" → 每放好一个样本点"采集样本" → 系统自动截图计时
3. 采满 20 张(或手动结束) → 进入"专家评审模式" → 评委逐张点"对/错"
4. 界面自动显示准确度分 + 时效分 + 总分, 结果存于 `records/session_*/summary.json`

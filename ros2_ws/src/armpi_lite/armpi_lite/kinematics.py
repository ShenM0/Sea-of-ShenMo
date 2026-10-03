"""ArmPi Ultra 4 自由度机械臂运动学（上位机版）。

几何参数与映射关系移植自 STM32 固件（闭源 LeArm.lib 按相同几何关系重新实现）：
- 连杆长度: 固件 Hiwonder/Arm/Inc/kinematics.h（单位厘米）
- 关节角→舵机脉宽: 固件 Hiwonder/Arm/src/robot_arm.c theta2servo()
  ID6=底座(knot0), ID5=大臂(knot1), ID4=小臂(knot2), ID3=手腕(knot3)

运动学角度约定：theta1 为大臂相对水平面的仰角，theta2 为大小臂夹角变化量，
theta3 为手腕相对角，末端俯仰角 alpha = theta1 + theta2 + theta3（度）。
"""

import math

LINKAGE_1 = 2.89   # 基座到肩关节高度
LINKAGE_2 = 9.5    # 大臂
LINKAGE_3 = 9.5    # 小臂
LINKAGE_4 = 16.9   # 手腕+夹爪

SERIAL_ANGLE_FACTOR = 4.166666666666667  # 1000/240，脉宽/度
SERVO_POS_MIN = 0
SERVO_POS_MAX = 1000

# 工作原位（固件 robot_arm_go_home：robot_arm.h DEFAULT_X/Y/Z + pitch=-5）
HOME_X, HOME_Y, HOME_Z = 10.0, 0.0, 15.0
HOME_PITCH = -5.0

# 工作范围（固件 robot_arm.h MIN/MAX_X/Y/Z，固件侧也会 clamp）
ARM_RANGE = {'x': (10.0, 20.0), 'y': (-10.0, 10.0), 'z': (0.0, 25.0)}


def thetas_to_servo_positions(thetas):
    """运动学关节角(度) → {舵机ID: 脉宽}，映射同固件 theta2servo()。"""
    t0, t1, t2, t3 = thetas
    servo_angles = (t0, -(90.0 - t1), -t2, t3)
    positions = {}
    for i, angle in enumerate(servo_angles):
        positions[6 - i] = int(round(500 + SERIAL_ANGLE_FACTOR * angle))
    return positions


def fk(thetas):
    """正运动学：(x, y, z, alpha)，厘米/度。用于 IK 自检。"""
    t0, t1, t2, t3 = (math.radians(t) for t in thetas)
    alpha = t1 + t2 + t3
    r = (LINKAGE_2 * math.cos(t1)
         + LINKAGE_3 * math.cos(t1 + t2)
         + LINKAGE_4 * math.cos(alpha))
    z = (LINKAGE_1 + LINKAGE_2 * math.sin(t1)
         + LINKAGE_3 * math.sin(t1 + t2)
         + LINKAGE_4 * math.sin(alpha))
    return r * math.cos(t0), r * math.sin(t0), z, math.degrees(alpha)


def _solve_for_alpha(x, y, z, alpha_deg):
    """给定俯仰角求关节角，返回候选分支列表（肘部负分支优先，与固件常态姿态一致）。"""
    alpha = math.radians(alpha_deg)
    t0 = math.atan2(y, x)
    r = math.hypot(x, y)
    wr = r - LINKAGE_4 * math.cos(alpha)
    wz = z - LINKAGE_1 - LINKAGE_4 * math.sin(alpha)

    d = (wr * wr + wz * wz - LINKAGE_2 ** 2 - LINKAGE_3 ** 2) / (2 * LINKAGE_2 * LINKAGE_3)
    if abs(d) > 1.0:
        return []

    solutions = []
    for sign in (-1.0, 1.0):
        t2 = sign * math.acos(d)
        t1 = math.atan2(wz, wr) - math.atan2(
            LINKAGE_3 * math.sin(t2), LINKAGE_2 + LINKAGE_3 * math.cos(t2))
        t3 = alpha - t1 - t2
        solutions.append([math.degrees(t0), math.degrees(t1),
                          math.degrees(t2), math.degrees(t3)])
    return solutions


def solve_ik(x, y, z, pitch, min_pitch=-90.0, max_pitch=90.0):
    """逆运动学。成功返回 (thetas[4], {舵机ID: 脉宽}, 实际pitch)，无解返回 None。

    先尝试目标 pitch，失败则以 1° 步长向 min/max 两侧搜索，
    取离目标 pitch 最近且所有舵机脉宽在 [0, 1000] 内的解
    （择优语义同固件 robot_arm_coordinate_set）。
    """
    lo, hi = min(min_pitch, pitch), max(max_pitch, pitch)
    span = int(math.ceil(max(pitch - lo, hi - pitch)))

    candidates = [pitch]
    for delta in range(1, span + 1):
        if pitch - delta >= lo:
            candidates.append(pitch - delta)
        if pitch + delta <= hi:
            candidates.append(pitch + delta)

    for alpha in candidates:
        for thetas in _solve_for_alpha(x, y, z, alpha):
            positions = thetas_to_servo_positions(thetas)
            if all(SERVO_POS_MIN <= p <= SERVO_POS_MAX for p in positions.values()):
                return thetas, positions, alpha
    return None

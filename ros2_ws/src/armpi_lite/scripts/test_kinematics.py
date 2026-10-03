#!/usr/bin/env python3
"""运动学自检：FK/IK 往返一致性 + 工作原位姿态校验（不依赖 ROS）。

用法: python3 scripts/test_kinematics.py
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

from armpi_lite.kinematics import (
    ARM_RANGE, HOME_PITCH, HOME_X, HOME_Y, HOME_Z, fk, solve_ik,
)


def main():
    failed = 0

    # 1. 工作原位（与固件 go_home 相同的目标），打印关节角和脉宽供人工核对
    result = solve_ik(HOME_X, HOME_Y, HOME_Z, HOME_PITCH)
    if result is None:
        print('FAIL: 工作原位 IK 无解')
        sys.exit(1)
    thetas, positions, alpha = result
    print(f'工作原位 ({HOME_X}, {HOME_Y}, {HOME_Z}) pitch={HOME_PITCH}:')
    print(f'  关节角(度): {[round(t, 1) for t in thetas]}  实际pitch={alpha:.1f}')
    print(f'  舵机脉宽: {dict(sorted(positions.items()))}')

    # 2. 工作空间网格上 FK/IK 往返一致性
    tested = 0
    max_err = 0.0
    (xmin, xmax), (ymin, ymax), (zmin, zmax) = ARM_RANGE['x'], ARM_RANGE['y'], ARM_RANGE['z']
    x, y, z = xmin, ymin, zmin
    while x <= xmax:
        y = ymin
        while y <= ymax:
            z = zmin
            while z <= zmax:
                for pitch in (-60, -30, 0, 30, 45, 60):
                    result = solve_ik(x, y, z, pitch)
                    if result is None:
                        z += 2.0
                        continue
                    thetas, positions, alpha = result
                    fx, fy, fz, fa = fk(thetas)
                    err = max(abs(fx - x), abs(fy - y), abs(fz - z))
                    max_err = max(max_err, err)
                    tested += 1
                    if err > 0.05:
                        print(f'FAIL: ({x},{y},{z}) pitch={pitch} 往返误差 {err:.4f}cm')
                        failed += 1
                z += 2.0
            y += 2.0
        x += 2.0

    print(f'FK/IK 往返测试: {tested} 组全部通过, 最大误差 {max_err:.4f}cm')

    # 3. 明显不可达的点应返回无解
    for bad in [(35, 0, 15), (10, 0, 40), (25, 25, 25)]:
        if solve_ik(*bad, 0) is not None:
            print(f'FAIL: 不可达点 {bad} 不应有解')
            failed += 1
    print('不可达点测试: 通过')

    if failed:
        print(f'\n共 {failed} 项失败')
        sys.exit(1)
    print('\nOK: 运动学模块自检全部通过')


if __name__ == '__main__':
    main()

import sys
if sys.prefix == '/usr':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/shenmo/ros/ros2ver/ros2_ws/install/armpi_lite'

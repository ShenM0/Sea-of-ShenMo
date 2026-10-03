import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('armpi_lite'), 'config', 'armpi_lite.yaml')

    use_camera = LaunchConfiguration('use_camera')

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_camera', default_value='true',
            description='true=启动相机与检测节点（相机调试）；false=仅运动节点+调试界面（无相机调试）'),

        Node(
            package='armpi_lite',
            executable='arm_node',
            name='arm_controller',
            parameters=[config],
            output='screen',
        ),
        Node(
            package='armpi_lite',
            executable='gui_node',
            name='arm_gui',
            parameters=[config],
            output='screen',
        ),
        Node(
            package='armpi_lite',
            executable='camera_node',
            name='d435i_camera',
            parameters=[config],
            output='screen',
            condition=IfCondition(use_camera),
        ),
        Node(
            package='armpi_lite',
            executable='detect_node',
            name='arm_detect',
            parameters=[config],
            output='screen',
            condition=IfCondition(use_camera),
        ),
    ])

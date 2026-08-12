/**
  ******************************************************************************
  * @file           : robot_arm.c
  * @brief          : 机械臂高级功能控制
  * @author         : (liang)
  * @date           : 2025-07-01
  * @version        : 1.0
  * @note           : 该文件实现了机械臂的所有高级应用逻辑，包括：
  * - 逆运动学解算，通过坐标控制末端执行器
  * - 动作组的录制、保存、读取和执行
  * - 与外部Flash芯片(W25Q64)的交互
  * - 舵机偏差校准和参数设置
  * - 单个关节的精细控制
  ******************************************************************************
  */

#include "global.h"
#include "Kinematics.h"
#include "robot_arm.h"
#include "w25q64.h"
#include "stdlib.h"
#include "math.h"
#include "serial_servo.h" 

/* 全局变量定义 */
KinematicsObjectTypeDef  kinematics;           // 运动学对象
RobotArmHandleTypeDef robot_arm;               // 机械臂句柄，存储机械臂所有状态
extern SerialServoControllerTypeDef serial_servo_controller;  // 外部串行舵机控制器实例

/* 内部(静态)函数声明 */
static void serial_servos_delay(uint32_t ms);
static void theta2servo(KinematicsObjectTypeDef* self, float time);
static bool robot_arm_flash_init(void);
static uint8_t action_frame_run(uint8_t action_group_index, uint8_t frame_index);

/**
 * @brief 毫秒级延时函数
 * @param ms 延时毫秒数
 * @note 内部函数，作为舵机指令之间的短延时，防止总线通信冲突。
 */
static void serial_servos_delay(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @brief 将运动学解算出的关节角度转换为舵机脉宽并发送控制指令
 * @param self 包含解算结果的运动学对象指针
 * @param time 舵机从当前位置运动到目标位置所需的时间(毫秒)
 * @note 内部函数，是连接运动学算法和物理舵机控制的桥梁。
 */
static void theta2servo(KinematicsObjectTypeDef* self, float time)
{
    float target_angle[4] = {0};

    /* 运动学理论角度到舵机物理安装角度的转换 */
    target_angle[0] = self->knot[0].theta;           // 0号关节：底座旋转角度 (直接使用)
    target_angle[1] = -(90.0f - self->knot[1].theta);   // 1号关节：大臂角度 (根据机械结构补偿90度)
    target_angle[2] = -self->knot[2].theta;           // 2号关节：小臂角度 (直接使用)
    target_angle[3] = self->knot[3].theta;           // 3号关节：手腕角度 (直接使用)
    
    /* 依次控制4个主关节舵机 (ID: 6, 5, 4, 3) */
    for (uint8_t i = 0; i < 4; i++)
    {   
    
        uint16_t target_position = 500 + (int)(SERIAL_ANGLE_FACTOR * target_angle[i]);
        serial_servo_set_position(&serial_servo_controller, 6 - i, target_position, time);
        serial_servos_delay(1); 
    }
}

/**
 * @brief 数值映射函数
 * @param x 待映射的值
 * @param in_min 输入范围的最小值
 * @param in_max 输入范围的最大值  
 * @param out_min 输出范围的最小值
 * @param out_max 输出范围的最大值
 * @return 映射后的值
 * @note 这是一个通用的数学工具函数。
 */
float map(float x, float in_min, float in_max, float out_min, float out_max)
{
    // 边界检查，防止除以零
    if (in_max == in_min) return out_min;
    
    return out_min + (x - in_min) * ((out_max - out_min) / (in_max - in_min));
}

/**
 * @brief 通过笛卡尔坐标设置机械臂末端执行器的位置和姿态（逆运动学）
 * @param target_x 目标位置的X坐标(单位:mm)
 * @param target_y 目标位置的Y坐标(单位:mm)
 * @param target_z 目标位置的Z坐标(单位:mm)
 * @param pitch 目标姿态的俯仰角(单位:度)
 * @param min_pitch 俯仰角最小限制(用于运动学求解)
 * @param max_pitch 俯仰角最大限制(用于运动学求解)
 * @param time 运动到目标位置的时间(单位:毫秒)
 * @return 1 (true) - 设置成功, 0 (false) - 逆运动学无解，无法到达该位置
 */
uint8_t robot_arm_coordinate_set(float target_x,
                                 float target_y,
                                 float target_z,
                                 float pitch,
                                 float min_pitch,
                                 float max_pitch,
                                 uint32_t time)
{
    bool result1_state, result2_state;
    KinematicsObjectTypeDef kinematics_result1, kinematics_result2;   
    VectorObjectTypeDef vector;

    /* 设置目标位置向量 */
    vector.x = target_x;
    vector.y = target_y;
    vector.z = target_z;
    
    /* 运动学库可能会针对不同约束条件给出不同解，这里尝试两种约束并择优 */
    result1_state = set_pitch_range(&kinematics_result1, &vector, pitch, min_pitch);
    result2_state = set_pitch_range(&kinematics_result2, &vector, pitch, max_pitch);
    
    /* 选择最优解的逻辑 */
    if (result1_state)
    {
        // 默认先采纳第一个解
        kinematics = kinematics_result1;
        
        // 如果第二个解也有效，则比较哪个解的最终姿态更接近目标俯仰角
        if (result2_state)
        {
            if (fabs(kinematics_result2.alpha - pitch) < fabs(kinematics_result1.alpha - pitch))
            {
                kinematics = kinematics_result2; // 第二个解更优
            }
        }
    }
    else if (result2_state)
    {
        // 第一个解无效，只能采纳第二个解
        kinematics = kinematics_result2;
    }
    else
    {
        // 两个解都无效，说明目标点无法到达
        return false;
    }

    /* 如果找到了可行的解，则控制舵机执行 */
    theta2servo(&kinematics, time);
    return true;
}

/**
 * @brief 从所有串行舵机中读取偏差校准值
 * @param value 指向一个大小为6的int8_t数组，用于存储读取到的偏差值
 * @note 会同步更新 robot_arm.servo_offset 数组缓存。
 */
void robot_arm_offset_read(int8_t* value)
{
    for(uint8_t i = 0; i < 6; i++)
    {
        /* 循环读取直到成功为止，防止通信失败 */
        while(serial_servo_read_deviation(&serial_servo_controller, i + 1, &value[i]) != 0)
        {
            serial_servos_delay(2);
        }
        robot_arm.servo_offset[i] = value[i]; // 更新句柄中的缓存
        serial_servos_delay(2);
    }
}

/**
 * @brief 设置指定ID舵机的偏差校准值（仅在当前生效，掉电丢失）
 * @param id 舵机ID (范围 1-6)
 * @param value 偏差值 (范围 -100 ~ 100)
 */
void robot_arm_offset_set(uint8_t id, int8_t value)
{
    /* 参数范围检查与限制，防止非法值 */
    id = (id > 6) ? 6 : ((id < 1) ? 1 : id);
    value = (value > 100) ? 100 : ((value < -100) ? -100 : value);
    
    robot_arm.servo_offset[id - 1] = value; // 更新句柄中的缓存
    
    serial_servo_set_deviation(&serial_servo_controller, id, (int)value);
    serial_servos_delay(1);
}

/**
 * @brief 将所有舵机的当前偏差校准值永久保存到舵机内部的EEPROM中
 * @note 此操作写入EEPROM，有次数限制，建议调试完成后再执行保存。
 */
void robot_arm_offset_save(void)
{
    for(uint8_t i = 0; i < 6; i++)
    {
        serial_servo_save_deviation(&serial_servo_controller, i + 1);
        serial_servos_delay(50); /* 保存到EEPROM需要较长时间，必须延时等待 */
    } 
}

/**
 * @brief 设置机械爪的开合角度
 * @param open_angle 开合角度 (范围 MIN_OPEN_ANGLE ~ MAX_OPEN_ANGLE)
 * @param open_angle_time 完成开合动作的时间(毫秒)
 */
void robot_arm_claw_set(float open_angle, uint32_t open_angle_time)
{
    /* 角度范围限制 */
    open_angle = (open_angle > MAX_OPEN_ANGLE) ? MAX_OPEN_ANGLE : 
                 ((open_angle < MIN_OPEN_ANGLE) ? MIN_OPEN_ANGLE : open_angle);

    /* 1号舵机控制爪子，将角度值转换为脉宽。根据机械结构，闭合时脉宽增大 */
    uint16_t target_position = 700 - (int)(5.555555555555556f * open_angle);
    serial_servo_set_position(&serial_servo_controller, 1, target_position, open_angle_time);
} 

/**
 * @brief 设置手腕的旋转角度
 * @param rotation_angle 旋转角度 (范围 MIN_ROTATION_ANGLE ~ MAX_ROTATION_ANGLE)
 * @param rotation_angle_time 完成旋转动作的时间(毫秒)
 */
void robot_arm_roll_set(float rotation_angle, uint32_t rotation_angle_time)
{
    /* 角度范围限制 */
    rotation_angle = (rotation_angle > MAX_ROTATION_ANGLE) ? MAX_ROTATION_ANGLE : 
                     ((rotation_angle < MIN_ROTATION_ANGLE) ? MIN_ROTATION_ANGLE : rotation_angle);
    
    /* 2号舵机控制手腕旋转，将角度值转换为脉宽。根据机械结构，左转脉宽增大 */
    uint16_t target_position = 500 - (int)(SERIAL_ANGLE_FACTOR * rotation_angle);
    serial_servo_set_position(&serial_servo_controller, 2, target_position, rotation_angle_time);
}

/**
 * @brief 控制单个舵机运动到指定的原始脉宽值
 * @param id 舵机ID (1-6)
 * @param target_duty 目标脉宽值 (通常为 0-1000)
 * @param time 运动时间(毫秒)
 */
void robot_arm_knot_run(uint8_t id, int target_duty, uint32_t time)
{
    /* ID范围检查 */
    id = (id > 6) ? 6 : ((id < 1) ? 1 : id);
    
    /* 对爪子舵机(ID=1)进行特殊保护，限制最大脉宽，防止堵转烧毁电机 */
    if(id == 1)
    {
        target_duty = (target_duty > 700) ? 700 : target_duty;    
    }
    
    serial_servo_set_position(&serial_servo_controller, id, target_duty, time);
    serial_servos_delay(1);
}

/**
 * @brief 停止指定ID的舵机运动
 * @param id 舵机ID (1-6)
 */
void robot_arm_knot_stop(uint8_t id)
{
    /* ID范围检查 */
    id = (id > 6) ? 6 : ((id < 1) ? 1 : id);
    
    serial_servo_stop(&serial_servo_controller, id);
    serial_servos_delay(1);
}

/**
 * @brief 检查指定舵机是否已经到达目标位置
 * @param id 舵机ID (1-6)
 * @param target_duty 目标脉宽值
 * @return true - 已到达, false - 未到达
 */
bool robot_arm_knot_is_finish(uint8_t id, int target_duty)
{
    int16_t current_position;
    
    id = (id > 6) ? 6 : ((id < 1) ? 1 : id);
    
    /* 循环读取当前位置直到成功 */
    while(serial_servo_read_position(&serial_servo_controller, id, &current_position) != 0)
    {
        serial_servos_delay(1);
    }
    serial_servos_delay(1);
    
    /* 判断当前位置与目标位置的差的绝对值是否在允许的误差范围内(10个脉宽单位) */
    return (abs(target_duty - current_position) < 10);
}

/**
 * @brief 获取舵机当前的脉宽位置
 * @param id 舵机ID (1-6)
 * @return 当前脉宽值
 */
int robot_arm_get_knot_current_duty(uint8_t id)
{
    int16_t current_position;
    
    id = (id > 6) ? 6 : ((id < 1) ? 1 : id);
    
    /* 循环读取直到成功 */
    while(serial_servo_read_position(&serial_servo_controller, id, &current_position) != 0)
    {
        serial_servos_delay(1);
    }
    serial_servos_delay(1);
    
    return current_position;
}

/**
 * @brief 擦除Flash中存储的所有动作组数据
 * @note 这是一个危险操作，它会将所有动作组的帧数记录清零。
 */
void action_group_erase(void)
{
    /* 将内存中的帧数统计数组清零 */
    memset(robot_arm.action_group._sum, 0, sizeof(robot_arm.action_group._sum));
    
    /* 擦除Flash中对应的扇区，然后将清零后的数组写回 */
    w25q64_erase_sector(ACTION_FRAME_SUM_BASE_ADDRESS);
    w25q64_write(ACTION_FRAME_SUM_BASE_ADDRESS,
                 (const uint8_t*)robot_arm.action_group._sum,
                 sizeof(robot_arm.action_group._sum));
}

/**
 * @brief 初始化Flash存储，如果首次使用则进行格式化
 * @return true - 初始化成功, false - Flash读写失败
 */
static bool robot_arm_flash_init(void)
{
    uint8_t read_logo[9] = {0};
    const uint8_t logo[] = "Hiwonder"; // 预设的标识符
    
    w25q64_init(); // 初始化Flash芯片驱动
    
    /* 读取Flash中预设地址的LOGO，检查是否已经初始化过 */
    w25q64_read(LOGO_BASE_ADDRESS, read_logo, sizeof(read_logo));

    /* 逐字节比较LOGO */
    if (memcmp(read_logo, logo, sizeof(logo)) != 0)
    {
        // 标识不匹配，判定为首次使用或数据损坏，执行格式化
        uint8_t offset_val[6] = {0}; // 用于初始化的空偏移值
        
        // 1. 擦除并写入LOGO标识
        w25q64_erase_sector(LOGO_BASE_ADDRESS);                
        w25q64_write(LOGO_BASE_ADDRESS, logo, sizeof(logo));
        
        // 2. 擦除并写入空的舵机偏移数据
        w25q64_erase_sector(SERVOS_OFFSET_BASE_ADDRESS);   
        w25q64_write(SERVOS_OFFSET_BASE_ADDRESS, offset_val, sizeof(offset_val));
                    
        // 3. 擦除所有动作组
        action_group_erase();
    }
    
    /* 再次读取并校验LOGO，确保Flash读写正常 */
    w25q64_read(LOGO_BASE_ADDRESS, read_logo, sizeof(read_logo));
    if (memcmp(read_logo, logo, sizeof(logo)) != 0)
    {
        return false; // Flash读写验证失败
    }
    
    return true;
}

/**
 * @brief 执行动作序列中的单个动作帧
 * @param action_group_index 动作组索引
 * @param frame_index 帧索引
 * @return 当前帧的执行状态 (START, RUNNING, IDLE)
 */
static uint8_t action_frame_run(uint8_t action_group_index, uint8_t frame_index)
{
    static uint8_t set_id[MAX_SERVOS_NUM];      // 存储本帧要控制的舵机ID
    static uint16_t set_duty[MAX_SERVOS_NUM];   // 存储本帧要控制的舵机脉宽
    uint8_t control_servos_sum;                 // 本帧实际控制的舵机数量
    uint32_t ag_addr_offset;                    // 动作组在Flash中的地址偏移
    uint32_t af_addr_offset;                    // 动作帧在Flash中的地址偏移
    uint8_t frame[ACTION_FRAME_SIZE];           // 存储从Flash读出的帧数据
    
    switch(robot_arm.action_group.frame.status)
    {
        case ACTION_FRAME_START:
            /* 计算Flash地址并读取一帧数据 */
            ag_addr_offset = action_group_index * ACTION_GROUP_SIZE;
            af_addr_offset = frame_index * ACTION_FRAME_SIZE;
            w25q64_read(ACTION_GROUP_BASE_ADDRESS + ag_addr_offset + af_addr_offset,
                        frame, sizeof(frame));
            
            control_servos_sum = frame[0];  // 帧数据的第一个字节是舵机数量

            if (control_servos_sum > MAX_SERVOS_NUM) return ACTION_FRAME_START; // 数据错误则重新开始
            
            // 解析运动时间（小端模式，高字节在后）
            robot_arm.action_group.frame.time = MERGE_HL(frame[2], frame[1]);

            // 解析每个舵机的控制参数并发送指令
            for (uint8_t i = 0; i < control_servos_sum; i++)
            {
                set_id[i] = frame[3 + i * 3];     // 舵机ID
                set_duty[i] = (uint16_t)MERGE_HL(frame[5 + i * 3], frame[4 + i * 3]); // 目标位置
                robot_arm_knot_run(set_id[i], (int)set_duty[i], robot_arm.action_group.frame.time);
            }
            // 状态转移到RUNNING
            robot_arm.action_group.frame.status = ACTION_FRAME_RUNNING;
            break;
        
        case ACTION_FRAME_RUNNING:
            // 延时等待动作执行完毕
            HAL_Delay(robot_arm.action_group.frame.time);
            robot_arm.action_group.frame.index++; // 帧索引自增，准备执行下一帧
            robot_arm.action_group.frame.status = ACTION_FRAME_IDLE; // 状态转移到IDLE
            break;
        
        case ACTION_FRAME_IDLE:
            // 帧执行完毕，处于空闲等待状态，等待上层逻辑处理
            break;
        
        default:
            break;
    }
    
    return robot_arm.action_group.frame.status;
}

/**
 * @brief 运行指定的动作组
 * @param action_group_index 要运行的动作组的索引
 * @param running_times 运行次数 (如果为0，表示无限循环)
 * @return true - 整个动作组执行完毕, false - 正在执行中
 */
bool action_group_run(uint8_t action_group_index, uint8_t running_times)
{
    bool is_finished = false;
    
    switch (robot_arm.action_group.status)
    {
        case ACTION_GROUP_START:
            /* 初始化动作组执行参数 */
            robot_arm.action_group.running_times = running_times;
            robot_arm.action_group.index = action_group_index;
            robot_arm.action_group.frame.index = 0; // 从第0帧开始
            
            /* 从Flash读取该动作组的总帧数 */
            w25q64_read(ACTION_FRAME_SUM_BASE_ADDRESS + action_group_index,
                        &robot_arm.action_group.sum, 1);
            
            /* 如果总帧数大于0，则开始运行，否则直接变为空闲 */
            robot_arm.action_group.status = (robot_arm.action_group.sum > 0) ? 
                                            ACTION_GROUP_RUNNING : ACTION_GROUP_IDLE;
            break;

        case ACTION_GROUP_RUNNING:
            /* 调用帧执行函数，并判断其返回值 */
            if(action_frame_run(robot_arm.action_group.index, 
                               robot_arm.action_group.frame.index) == ACTION_FRAME_IDLE)   
            {
                // 如果当前帧执行完成
                if (robot_arm.action_group.frame.index == robot_arm.action_group.sum)
                {
                    // 如果所有帧都已执行完，进入周期结束状态
                    robot_arm.action_group.status = ACTION_GROUP_END_PERIOD;
                    robot_arm.action_group.frame.index = 0;  // 重置帧索引，为下一轮循环做准备
                }
                else
                {
                    // 否则，重置帧状态，准备开始下一帧
                    robot_arm.action_group.frame.status = ACTION_FRAME_START;
                }
            }   
            break;

        case ACTION_GROUP_END_PERIOD:
            /* 一轮动作组执行完毕，处理循环次数 */
            if(robot_arm.action_group.running_times == 1)
            {
                // 这是最后一次循环，执行完毕后变为空闲
                robot_arm.action_group.status = ACTION_GROUP_IDLE;
            }
            else
            {
                if(robot_arm.action_group.running_times != 0)
                {
                    robot_arm.action_group.running_times--; // 剩余次数减1
                }
                robot_arm.action_group.status = ACTION_GROUP_RUNNING; // 继续下一轮循环
            }
            break;

        case ACTION_GROUP_IDLE:
            /* 动作组完全执行完毕，设置完成标志 */
            is_finished = true;
            break;
        
        default:
            break;
    }
    
    return is_finished;
}

/**
 * @brief 重置动作组的执行状态机
 * @note 当需要从外部中断一个动作并开始新动作时调用。
 */
void action_group_reset(void)
{
    robot_arm.action_group.status = ACTION_GROUP_START;
    robot_arm.action_group.frame.status = ACTION_FRAME_START;
}

/**
 * @brief 立即停止当前正在执行的动作组
 */
void action_group_stop(void)
{
    /* 停止所有舵机运动 */
    for (uint8_t i = 0; i < MAX_SERVOS_NUM; i++)
    {
        robot_arm_knot_stop(i + 1);
    }
    
    /* 将状态机强制设置为空闲状态 */
    robot_arm.action_group.status = ACTION_GROUP_IDLE;
    robot_arm.action_group.frame.status = ACTION_FRAME_START;
}

/**
 * @brief 保存一帧动作数据到Flash中
 * @param action_group_index 要保存到的动作组索引
 * @param frame_num 该动作组的总帧数
 * @param frame_index 当前正在保存的帧的索引
 * @param pdata 指向帧数据的指针
 * @param size 数据大小 (应等于ACTION_FRAME_SIZE)
 */
void action_group_save(uint8_t action_group_index, 
                       uint8_t frame_num,
                       uint8_t frame_index,
                       uint8_t* pdata,
                       uint16_t size)
{
    uint32_t ag_addr_offset, af_addr_offset, page_offset, write_addr;
    uint16_t remaining_space;
    
    /* 更新机械臂句柄中的状态 */
    robot_arm.action_group.index = action_group_index;
    robot_arm.action_group.frame.index = frame_index;
    
    /* 计算在Flash中要写入的绝对地址 */
    ag_addr_offset = action_group_index * ACTION_GROUP_SIZE;
    af_addr_offset = frame_index * ACTION_FRAME_SIZE;
    write_addr = ACTION_GROUP_BASE_ADDRESS + ag_addr_offset + af_addr_offset;
    
    /* 如果是第0帧，表示开始保存一个新的动作组，需要先擦除对应的Flash扇区 */
    if (frame_index == 0) 
    {
        // 一个动作组可能跨越多个扇区，这里假设最多跨越2个(8192字节 > 255*21)
        for (uint8_t i = 0; i < 2; i++) 
        {
            w25q64_erase_sector(ACTION_GROUP_BASE_ADDRESS + ag_addr_offset + (i * 4096));
        }
    }

    /* 处理跨页写入问题 */
    page_offset = write_addr % 256;  // Flash页大小为256字节
    remaining_space = 256 - page_offset;
    if (remaining_space < size) 
    {
        // 如果剩余空间不足以写入一帧，则分两次写入
        w25q64_write(write_addr, pdata, remaining_space);
        w25q64_write(write_addr + remaining_space, pdata + remaining_space, size - remaining_space);
    } 
    else 
    {
        // 否则一次性写入
        w25q64_write(write_addr, pdata, size);
    }
    
    /* 如果这是最后一帧，需要更新Flash中存储的总帧数信息 */
    if ((robot_arm.action_group.frame.index + 1) == frame_num)
    {
        w25q64_read(ACTION_FRAME_SUM_BASE_ADDRESS, 
                    robot_arm.action_group._sum, 
                    sizeof(robot_arm.action_group._sum));
        
        robot_arm.action_group._sum[robot_arm.action_group.index] = frame_num;
        
        w25q64_erase_sector(ACTION_FRAME_SUM_BASE_ADDRESS);
        w25q64_write(ACTION_FRAME_SUM_BASE_ADDRESS, 
                     (const uint8_t*)robot_arm.action_group._sum, 
                     sizeof(robot_arm.action_group._sum));
    }
}

/**
 * @brief 机械臂复位到初始姿态
 * @param time 完成复位动作的时间(毫秒)
 */
void robot_arm_reset(uint32_t time)
{
    /* 依次将1-6号舵机设置到预定义的复位脉宽值 */
    serial_servo_set_position(&serial_servo_controller, 1, SERIAL_SERVO1_RESET_DUTY, time);
    serial_servos_delay(1);
    serial_servo_set_position(&serial_servo_controller, 2, SERIAL_SERVO2_RESET_DUTY, time);
    serial_servos_delay(1);
    serial_servo_set_position(&serial_servo_controller, 3, SERIAL_SERVO3_RESET_DUTY, time);
    serial_servos_delay(1);
    serial_servo_set_position(&serial_servo_controller, 4, SERIAL_SERVO4_RESET_DUTY, time);
    serial_servos_delay(1);
    serial_servo_set_position(&serial_servo_controller, 5, SERIAL_SERVO5_RESET_DUTY, time);
    serial_servos_delay(1);
    serial_servo_set_position(&serial_servo_controller, 6, SERIAL_SERVO6_RESET_DUTY, time);
    serial_servos_delay(1);
}

/**
 * @brief 机械臂回到统一的工作原位姿态
 * @param time 完成动作的时间(毫秒)
 */
void robot_arm_go_home(uint32_t time)
{
    (void)robot_arm_coordinate_set(DEFAULT_X,
                                   DEFAULT_Y,
                                   DEFAULT_Z,
                                   -5.0f,
                                   -90.0f,
                                   90.0f,
                                   time);
}

/**
 * @brief 初始化机械臂整个系统
 * @return true - 初始化成功, false - 初始化失败
 * @note 这是上电后需要调用的主要初始化函数。
 */
bool robot_arm_init(void)
{
    int8_t read_offset[6]; // 临时缓冲区
    
    /* 初始化底层依赖 */
	serial_servo_init(); 
    kinematics_init(&kinematics);
    
    /* 清空机械臂状态句柄 */
    memset(&robot_arm, 0, sizeof(RobotArmHandleTypeDef));
    
    /* 初始化Flash，如果失败则系统无法正常工作 */
    if(robot_arm_flash_init() == false)
    {
        return false;
    }

    /* 等待系统稳定 */
    HAL_Delay(200);
    
    /* 首次上电或调试时，可以取消注释下面的代码 */
    // robot_arm_reset(2000); // 缓慢复位到初始姿态
    // robot_arm_offset_read(read_offset); // 读取已保存的舵机偏差值
    
    return true;
}

/**
 * @brief 获取机械臂的句柄指针
 * @return 指向全局机械臂状态句柄的指针
 * @note 通过此函数可以从外部文件访问机械臂的内部状态。
 */
RobotArmHandleTypeDef* robot_arm_get_handle(void)
{
    return &robot_arm;
}

/**
 * @brief 检查机械臂当前是否空闲（即没有在执行动作组）
 * @return true - 空闲, false - 忙碌
 */
bool robot_arm_is_idle(void)
{
    return (robot_arm.action_group.status == ACTION_GROUP_IDLE);
}

/**
 * @brief 设置所有舵机的角度限制（写入舵机RAM，掉电丢失）
 * @param servo_limits 一个 6x2 的二维数组，每行包含[最小角度, 最大角度]
 */
void robot_arm_set_angle_limits(uint16_t servo_limits[][2])
{
    for(uint8_t i = 0; i < 6; i++)
    {
        serial_servo_set_angle_limit(&serial_servo_controller, 
                                   i + 1, 
                                   servo_limits[i][0], 
                                   servo_limits[i][1]);
        serial_servos_delay(2);
    }
}

/**
 * @brief 紧急停止所有舵机
 * @note 用于触发紧急情况下的安全保护，会立即停止所有电机并终止动作组。
 */
void robot_arm_emergency_stop(void)
{
    for(uint8_t i = 1; i <= 6; i++)
    {
        robot_arm_knot_stop(i);
    }
    
    // 强制停止动作组执行
    action_group_stop();
}

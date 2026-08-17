#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>
#include "ti_msp_dl_config.h"

// 视觉串口 (UART0, 115200, TX=PB0 / RX=PB1), 收发全部非阻塞:
//   RX — 中断按行缓存, 主循环随时可取
//   TX — 环形队列缓存, 由 UART_tx_poll() 在主循环中非阻塞排空

void UART_vision_init(void);

// 字符串写入 TX 环形队列; 队列空间不足返回 false (不阻塞, 可稍后重试)
bool UART_send_string(const char *str);

// 非阻塞排空 TX 队列, 主循环每次迭代调用一次
void UART_tx_poll(void);

// 取走最近一个完整行 (以 '\n' 结尾), 无完整行返回 false
bool UART_try_read_line(char *buf, uint32_t max_len);

// 通用视觉反馈: 从最近一行中解析单个带符号浮点值 (如 "-3.5"),
// 兼容数值前带其他文本的行; 未取得有效数值返回 false
bool UART_try_get_feedback(float *value);

void UART_SendString(char *str);

#endif /* UART_H */

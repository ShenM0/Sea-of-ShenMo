#include "uart.h"

#include <stdio.h>
#include <stdlib.h>

#define UART_LINE_BUFFER_SIZE 64
#define UART_RX_BUFFER_SIZE   128   // 必须为 2 的幂
#define UART_TX_BUFFER_SIZE   64    // 必须为 2 的幂

// RX 字节级环形缓冲: ISR 只存字节, 行拼装在主循环完成
// 溢出时丢最旧字节 (最新值语义), 乱码流不会把接收永久卡死
static volatile char uart_rx_buf[UART_RX_BUFFER_SIZE];
static volatile uint8_t uart_rx_head = 0;   // ISR 写入位置
static volatile uint8_t uart_rx_tail = 0;   // 主循环读取位置

// TX 环形队列, 仅在主循环上下文访问 (UART_send_string / UART_tx_poll)
static volatile char uart_tx_buf[UART_TX_BUFFER_SIZE];
static volatile uint8_t uart_tx_head = 0;   // 写入位置
static volatile uint8_t uart_tx_tail = 0;   // 待发送位置

void UART_vision_init(void)
{
    DL_UART_Main_enableInterrupt(UART_0_INST, DL_UART_MAIN_INTERRUPT_RX);
    NVIC_EnableIRQ(UART_0_INST_INT_IRQN);
}

bool UART_send_string(const char *str)
{
    uint32_t len = 0;
    uint32_t i;
    uint8_t head = uart_tx_head;
    uint8_t count = (uint8_t)((head - uart_tx_tail) & (UART_TX_BUFFER_SIZE - 1U));
    uint8_t free_space = (uint8_t)((UART_TX_BUFFER_SIZE - 1U) - count);

    if (str == NULL) {
        return false;
    }
    while (str[len] != '\0') {
        len++;
    }
    if ((len == 0U) || (len > free_space)) {
        return false;   // 空间不足整体放弃, 避免发出半条消息
    }

    for (i = 0; i < len; i++) {
        uart_tx_buf[head] = str[i];
        head = (uint8_t)((head + 1U) & (UART_TX_BUFFER_SIZE - 1U));
    }
    uart_tx_head = head;
    return true;
}

void UART_tx_poll(void)
{
    uint8_t tail = uart_tx_tail;

    while (tail != uart_tx_head) {
        if (!DL_UART_transmitDataCheck(UART_0_INST, (uint8_t) uart_tx_buf[tail])) {
            break;   // TX FIFO 已满, 下次主循环再继续, 不等待
        }
        tail = (uint8_t)((tail + 1U) & (UART_TX_BUFFER_SIZE - 1U));
    }
    uart_tx_tail = tail;
}

bool UART_try_read_line(char *buf, uint32_t max_len)
{
    uint8_t head_snapshot = uart_rx_head;   // ISR 可能继续写入, 快照即可
    uint8_t p = uart_rx_tail;
    uint32_t i = 0;

    if ((buf == NULL) || (max_len == 0U)) {
        return false;
    }

    // 在环形缓冲中找行尾 '\n', 同时拷贝内容
    while (p != head_snapshot) {
        char c = uart_rx_buf[p];

        if (c == '\n') {
            uart_rx_tail = (uint8_t)((p + 1U) & (UART_RX_BUFFER_SIZE - 1U));
            buf[i] = '\0';
            return (i > 0U);
        }
        if ((c != '\r') && (i < (max_len - 1U))) {
            buf[i++] = c;
        }
        p = (uint8_t)((p + 1U) & (UART_RX_BUFFER_SIZE - 1U));
    }
    return false;   // 没有完整行
}

// 解析单行视觉反馈:
//   新帧协议 "$data,checksum" — checksum 为 data 各字节和 % 256, 校验不过拒收
//   兼容旧格式 — 数值前可带其他文本 (如 "err=-3.5"), 无校验直接解析
// 成功写入 value 返回 true, 畸形/校验失败帧返回 false
static bool parse_vision_frame(const char *line, float *value)
{
    if (line[0] == '$') {
        const char *p = line + 1;
        unsigned int sum = 0U;
        long checksum_field;
        char *end;

        // 累加 '$' 与 ',' 之间的数据字节, 与相机端 sum(raw.encode()) % 256 对应
        while ((*p != '\0') && (*p != ',')) {
            sum += (unsigned int)(uint8_t)(*p);
            p++;
        }
        if (*p != ',') {
            return false;   // 缺校验和字段, 畸形帧
        }
        checksum_field = strtol(p + 1, &end, 10);
        if ((end == (p + 1)) || (*end != '\0')) {
            return false;   // 校验和不是纯十进制数
        }
        if ((sum % 256U) != ((unsigned int) checksum_field % 256U)) {
            return false;   // 校验失败: 丢字节/错位拼出的帧
        }
        return (sscanf(line + 1, "%f", value) == 1);
    }

    // 旧格式兼容: 跳过数值前的非数字字符
    {
        const char *p = line;

        while ((*p != '\0') &&
               !(((*p >= '0') && (*p <= '9')) || (*p == '-') || (*p == '+'))) {
            p++;
        }
        return ((*p != '\0') && (sscanf(p, "%f", value) == 1));
    }
}

bool UART_try_get_feedback(float *value)
{
    char line[UART_LINE_BUFFER_SIZE];
    bool got_value = false;

    if (value == NULL) {
        return false;
    }

    // 排空所有完整行, 按视觉反馈解析 (旧帧被新帧覆盖是最新值语义)
    while (UART_try_read_line(line, sizeof(line))) {
        if (parse_vision_frame(line, value)) {
            got_value = true;
        }
    }
    return got_value;
}

void UART_0_INST_IRQHandler()
{
    switch (DL_UART_getPendingInterrupt(UART_0_INST))
    {
    case DL_UART_IIDX_RX:
        {
            uint8_t next = (uint8_t)((uart_rx_head + 1U) & (UART_RX_BUFFER_SIZE - 1U));

            if (next == uart_rx_tail) {
                // 缓冲已满: 丢弃最旧字节为新字节腾位 (最新值语义)
                // 若丢新字节, 乱码流(无'\n')填满缓冲后 tail 永远卡住,
                // 后续合法数据全被丢弃, 接收永久失效; 丢旧字节则总能自愈
                uart_rx_tail = (uint8_t)((uart_rx_tail + 1U) & (UART_RX_BUFFER_SIZE - 1U));
            }
            uart_rx_buf[uart_rx_head] = (char) DL_UART_receiveData(UART_0_INST);
            uart_rx_head = next;
            break;
        }

    default:
        break;
    }
}

void UART_SendString(char *str)//手动组合字符串，m0没有直接发送字符串的代码
{
    while(*str != 0)
    {  
       DL_UART_transmitDataBlocking(UART_1_INST, *str);
       str++;
    }
}
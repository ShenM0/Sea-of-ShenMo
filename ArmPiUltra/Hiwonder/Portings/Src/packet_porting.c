/**
 * @file packet_portting.c
 * @author liang
 * @brief 串口协议接口实现 
 * @version 0.2
 * @date 2025-06-19
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "main.h"

#include "usart.h"           
#include "lwrb.h"           

#define PACKET_RX_FIFO_BUFFER_SIZE  4096 /* FIFO缓存长度 */
#define PACKET_TX_FIFO_BUFFER_SIZE  2096 /* 发送FIFO缓存长度 */
#define PACKET_RX_DMA_BUFFER_SIZE   1024 /* 单个DMA缓存长度 */
#define PACKET_TX_DMA_BUFFER_SIZE   512  /* 单个DMA缓存长度 */


struct PacketController packet_controller; /* 协议控制器实例 */

/* 内部函数声明 */
static void transmit_trigger(struct PacketController *self);

/**
 * @brief 初始化packet 控制器对象
 * @retval void
 */
void packet_init(void) {
    memset(&packet_controller, 0, sizeof(packet_controller));
    packet_controller_new(&packet_controller);
    packet_controller.state = PACKET_CONTROLLER_STATE_STARTBYTE1;
    packet_controller.data_index = 0;
    packet_controller.transmit_trigger = transmit_trigger;

    /* DMA 缓存初始化 (使用 Ping-Pong 缓存) */
    static uint8_t rx_dma_buffer1[PACKET_RX_DMA_BUFFER_SIZE];
    static uint8_t rx_dma_buffer2[PACKET_RX_DMA_BUFFER_SIZE];
    static uint8_t rx_fifo_buffer[PACKET_RX_FIFO_BUFFER_SIZE];
    static uint8_t tx_fifo_buffer[PACKET_TX_FIFO_BUFFER_SIZE];
    static uint8_t tx_dma_buffer[PACKET_TX_DMA_BUFFER_SIZE];
    static lwrb_t rx_fifo;
    static lwrb_t tx_fifo;

    packet_controller.rx_dma_buffers[0] = rx_dma_buffer1;
    packet_controller.rx_dma_buffers[1] = rx_dma_buffer2;
    packet_controller.rx_dma_buffer_size = PACKET_RX_DMA_BUFFER_SIZE;
    packet_controller.rx_dma_buffer_index = 0;

    packet_controller.tx_dma_buffer = tx_dma_buffer;
    packet_controller.tx_dma_buffer_size = PACKET_TX_DMA_BUFFER_SIZE;

    /* 接收/发送 FIFO 初始化 */
    packet_controller.rx_fifo = &rx_fifo;
    packet_controller.tx_fifo = &tx_fifo;
    lwrb_init(packet_controller.rx_fifo, rx_fifo_buffer, PACKET_RX_FIFO_BUFFER_SIZE);
    lwrb_init(packet_controller.tx_fifo, tx_fifo_buffer, PACKET_TX_FIFO_BUFFER_SIZE);
}

/**
 * @brief 启动第一次串口DMA接收
 * @retval void
 * @note  使用HAL库函数替代寄存器操作
 */
void start_recv(void) {
    HAL_UART_Receive_DMA(&huart1, packet_controller.rx_dma_buffers[packet_controller.rx_dma_buffer_index], packet_controller.rx_dma_buffer_size);
}

/**
 * @brief 轮询任务
 * @retval void
 * @note  通过轮询DMA剩余传输长度和时间戳来判断一帧数据是否结束
 */
void recv_task(void) {
    static uint32_t last_dma_len = 0;
    static uint32_t last_ticks = 0;
    uint32_t current_dma_len = __HAL_DMA_GET_COUNTER(huart1.hdmarx);
    uint32_t current_ticks = HAL_GetTick(); 

    // 如果DMA剩余长度在一段时间内没有变化，并且DMA没有在全速运转（说明不是正在连续接收），则认为一帧结束了
    if (current_dma_len == last_dma_len && current_dma_len != packet_controller.rx_dma_buffer_size) {
        if (current_ticks - last_ticks >= 1) { 
            HAL_UART_DMAStop(&huart1);

            int cur_index = packet_controller.rx_dma_buffer_index;
            uint32_t received_len = packet_controller.rx_dma_buffer_size - current_dma_len;

            if (received_len > 0) {
                lwrb_write(packet_controller.rx_fifo, packet_controller.rx_dma_buffers[cur_index], received_len);
            }
            
            packet_controller.rx_dma_buffer_index = (packet_controller.rx_dma_buffer_index + 1) % 2;
            HAL_UART_Receive_DMA(&huart1, packet_controller.rx_dma_buffers[packet_controller.rx_dma_buffer_index], packet_controller.rx_dma_buffer_size);
            last_dma_len = __HAL_DMA_GET_COUNTER(huart1.hdmarx);
            last_ticks = current_ticks;
        }
    } else {
        // 如果DMA长度有变化，说明正在接收数据，只需更新状态即可
        last_dma_len = current_dma_len;
        last_ticks = current_ticks;
    }
    
    // 调用上层协议解析函数，处理FIFO中的数据
    packet_controller.receive_handle(&packet_controller);
}

/**
 * @brief 触发DMA发送
 * @param self 控制器指针
 * @retval void
 * @note  使用HAL库函数替代寄存器操作
 */
static void transmit_trigger(struct PacketController *self) {
    // 检查发送标志位，防止DMA冲突
    if (!self->sending) {
        size_t size = lwrb_get_full(self->tx_fifo);
        if (size > 0) {
            self->sending = true; 
            size = size > self->tx_dma_buffer_size ? self->tx_dma_buffer_size : size;
            lwrb_read(self->tx_fifo, self->tx_dma_buffer, size);
            if (HAL_UART_Transmit_DMA(&huart1, self->tx_dma_buffer, size) != HAL_OK) {
                self->sending = false;
            }
        }
    }
}

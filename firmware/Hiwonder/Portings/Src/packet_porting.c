/**
 * @file packet_portting.c
 * @author liang
 * @brief ����Э��ӿ�ʵ�� 
 * @version 0.2
 * @date 2025-06-19
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "main.h"

#include "usart.h"           
#include "lwrb.h"           

#define PACKET_RX_FIFO_BUFFER_SIZE  4096 /* FIFO���泤�� */
#define PACKET_TX_FIFO_BUFFER_SIZE  2096 /* ����FIFO���泤�� */
#define PACKET_RX_DMA_BUFFER_SIZE   1024 /* ����DMA���泤�� */
#define PACKET_TX_DMA_BUFFER_SIZE   512  /* ����DMA���泤�� */


struct PacketController packet_controller; /* Э�������ʵ�� */

/* �ڲ��������� */
static void transmit_trigger(struct PacketController *self);

/**
 * @brief ��ʼ��packet ����������
 * @retval void
 */
void packet_init(void) {
    memset(&packet_controller, 0, sizeof(packet_controller));
    packet_controller_new(&packet_controller);
    packet_controller.state = PACKET_CONTROLLER_STATE_STARTBYTE1;
    packet_controller.data_index = 0;
    packet_controller.transmit_trigger = transmit_trigger;

    /* DMA �����ʼ�� (ʹ�� Ping-Pong ����) */
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

    /* ����/���� FIFO ��ʼ�� */
    packet_controller.rx_fifo = &rx_fifo;
    packet_controller.tx_fifo = &tx_fifo;
    lwrb_init(packet_controller.rx_fifo, rx_fifo_buffer, PACKET_RX_FIFO_BUFFER_SIZE);
    lwrb_init(packet_controller.tx_fifo, tx_fifo_buffer, PACKET_TX_FIFO_BUFFER_SIZE);
}

/**
 * @brief ������һ�δ���DMA����
 * @retval void
 * @note  ʹ��HAL�⺯������Ĵ�������
 */
void start_recv(void) {
    HAL_UART_Receive_DMA(&huart1, packet_controller.rx_dma_buffers[packet_controller.rx_dma_buffer_index], packet_controller.rx_dma_buffer_size);
}

/**
 * @brief ��ѯ����
 * @retval void
 * @note  ͨ����ѯDMAʣ�ഫ�䳤�Ⱥ�ʱ������ж�һ֡�����Ƿ����
 */
void recv_task(void) {
    static uint32_t last_dma_len = 0;
    static uint32_t last_ticks = 0;
    uint32_t current_dma_len = __HAL_DMA_GET_COUNTER(huart1.hdmarx);
    uint32_t current_ticks = HAL_GetTick(); 

    // ���DMAʣ�೤����һ��ʱ����û�б仯������DMAû����ȫ����ת��˵�����������������գ�������Ϊһ֡������
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
        // ���DMA�����б仯��˵�����ڽ������ݣ�ֻ�����״̬����
        last_dma_len = current_dma_len;
        last_ticks = current_ticks;
    }
    
    // �����ϲ�Э���������������FIFO�е�����
    packet_controller.receive_handle(&packet_controller);
}

/**
 * @brief ����DMA����
 * @param self ������ָ��
 * @retval void
 * @note  ʹ��HAL�⺯������Ĵ�������
 */
static void transmit_trigger(struct PacketController *self) {
    // 修复丢帧竞态：sending 标志的“读-判断-置位”必须原子化。
    // 原实现无临界区保护，主循环(心跳)与 UART 发送完成中断可并发进入，
    // 导致 sending 卡死在 true，tx_fifo 无法清空，后续舵机命令帧被 transmit() 静默丢弃
    // （表现为舵机保持上一位置有扭矩但不响应新命令）。
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (self->sending) {
        __set_PRIMASK(primask);   // 恢复中断状态后返回
        return;
    }
    self->sending = true;         // 临界区内先占位，防止并发重入
    __set_PRIMASK(primask);       // 恢复中断（若原本未关中断则重新使能）

    size_t size = lwrb_get_full(self->tx_fifo);
    if (size > 0) {
        size = size > self->tx_dma_buffer_size ? self->tx_dma_buffer_size : size;
        lwrb_read(self->tx_fifo, self->tx_dma_buffer, size);
        if (HAL_UART_Transmit_DMA(&huart1, self->tx_dma_buffer, size) != HAL_OK) {
            self->sending = false;  // DMA 启动失败，释放占位以便重试
        }
    } else {
        self->sending = false;      // FIFO 已空，释放占位
    }
}

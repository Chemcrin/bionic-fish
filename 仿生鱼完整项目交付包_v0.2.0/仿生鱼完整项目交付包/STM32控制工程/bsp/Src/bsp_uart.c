#include "bsp_uart.h"

#include <string.h>

#include "app_config.h"
#include "main.h"
#include "ring_buffer.h"
#include "usart.h"

typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t data[CFG_UART_TX_RING_BYTES];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t in_flight;
    volatile bool busy;
    volatile uint32_t drop_count;
} UartTxQueue;

static uint8_t s_esp_rx_storage[CFG_ESP_RX_RING_BYTES];
static RingBuffer s_esp_rx;
static uint8_t s_esp_rx_byte;
static UartTxQueue s_esp_tx;
static UartTxQueue s_debug_tx;

static uint16_t QueueMask(void)
{
    return (uint16_t)(CFG_UART_TX_RING_BYTES - 1U);
}

static uint16_t QueueUsed(const UartTxQueue *queue)
{
    return (uint16_t)((queue->head - queue->tail) & QueueMask());
}

static uint16_t QueueFree(const UartTxQueue *queue)
{
    return (uint16_t)(QueueMask() - QueueUsed(queue));
}

static void QueueInit(UartTxQueue *queue, UART_HandleTypeDef *huart)
{
    memset(queue, 0, sizeof(*queue));
    queue->huart = huart;
}

static bool QueueEnqueue(UartTxQueue *queue, const char *data, size_t length)
{
    size_t index;
    if ((queue == 0) || (data == 0) || (length == 0U) || (length > QueueFree(queue))) {
        if (queue != 0) {
            queue->drop_count++;
        }
        return false;
    }
    for (index = 0U; index < length; index++) {
        queue->data[queue->head] = (uint8_t)data[index];
        queue->head = (uint16_t)((queue->head + 1U) & QueueMask());
    }
    return true;
}

static void QueueKick(UartTxQueue *queue)
{
    uint16_t contiguous;
    HAL_StatusTypeDef status;
    if ((queue == 0) || queue->busy || (queue->tail == queue->head)) {
        return;
    }
    contiguous = (queue->head > queue->tail) ? (uint16_t)(queue->head - queue->tail)
                                               : (uint16_t)(CFG_UART_TX_RING_BYTES - queue->tail);
    queue->in_flight = contiguous;
    queue->busy = true;
    status = HAL_UART_Transmit_IT(queue->huart, &queue->data[queue->tail], contiguous);
    if (status != HAL_OK) {
        /* HAL_BUSY 时不丢队列，下一个主循环再尝试；其余错误也不阻塞控制。 */
        queue->busy = false;
        queue->in_flight = 0U;
    }
}

static void QueueOnComplete(UartTxQueue *queue)
{
    if ((queue == 0) || !queue->busy) {
        return;
    }
    queue->tail = (uint16_t)((queue->tail + queue->in_flight) & QueueMask());
    queue->in_flight = 0U;
    queue->busy = false;
}

void BSP_Uart_Init(void)
{
    if (!RingBuffer_Init(&s_esp_rx, s_esp_rx_storage, CFG_ESP_RX_RING_BYTES)) {
        Error_Handler();
    }
    QueueInit(&s_esp_tx, &huart2);
    QueueInit(&s_debug_tx, &huart3);
    if (HAL_UART_Receive_IT(&huart2, &s_esp_rx_byte, 1U) != HAL_OK) {
        Error_Handler();
    }
}

void BSP_Uart_Service(void)
{
    QueueKick(&s_esp_tx);
    QueueKick(&s_debug_tx);
}

void BSP_Uart_OnRxComplete(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        (void)RingBuffer_PushFromIsr(&s_esp_rx, s_esp_rx_byte);
        /* 回调只入队并重挂接 1 字节接收，绝不在中断解析字符串。 */
        if (HAL_UART_Receive_IT(&huart2, &s_esp_rx_byte, 1U) != HAL_OK) {
            s_esp_rx.data_lost = true;
        }
    }
}

void BSP_Uart_OnTxComplete(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        QueueOnComplete(&s_esp_tx);
    } else if (huart == &huart3) {
        QueueOnComplete(&s_debug_tx);
    }
}

void BSP_Uart_OnError(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        /* ORE 等错误意味着某个字节已丢；主循环必须重置协议帧状态机。 */
        s_esp_rx.data_lost = true;
        (void)HAL_UART_Receive_IT(&huart2, &s_esp_rx_byte, 1U);
    }
}

bool BSP_Uart_ReadEsp(uint8_t *byte)
{
    return RingBuffer_Pop(&s_esp_rx, byte);
}

bool BSP_Uart_EspRxLost(void)
{
    return s_esp_rx.data_lost;
}

void BSP_Uart_ClearEspRxLost(void)
{
    s_esp_rx.data_lost = false;
}

uint32_t BSP_Uart_EspRxOverflowCount(void)
{
    return s_esp_rx.overflow_count;
}

bool BSP_Uart_SendEsp(const char *data, size_t length)
{
    return QueueEnqueue(&s_esp_tx, data, length);
}

bool BSP_Uart_SendDebug(const char *data, size_t length)
{
    return QueueEnqueue(&s_debug_tx, data, length);
}

uint16_t BSP_Uart_EspTxFree(void)
{
    return QueueFree(&s_esp_tx);
}

uint16_t BSP_Uart_DebugTxFree(void)
{
    return QueueFree(&s_debug_tx);
}

uint32_t BSP_Uart_TxDropCount(void)
{
    return s_esp_tx.drop_count + s_debug_tx.drop_count;
}

/** @file ring_buffer.h @brief ISR 单生产者 / 主循环单消费者的无锁环形缓冲。 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    uint16_t mask;                   /* capacity - 1，capacity 必须为 2 的幂 */
    volatile uint16_t head;          /* 仅 ISR 写入 */
    volatile uint16_t tail;          /* 仅主循环写入 */
    volatile uint32_t overflow_count;
    volatile bool data_lost;
} RingBuffer;

bool RingBuffer_Init(RingBuffer *rb, uint8_t *storage, uint16_t capacity);
bool RingBuffer_PushFromIsr(RingBuffer *rb, uint8_t byte);
bool RingBuffer_Pop(RingBuffer *rb, uint8_t *byte);
uint16_t RingBuffer_Used(const RingBuffer *rb);
uint16_t RingBuffer_Free(const RingBuffer *rb);
void RingBuffer_Reset(RingBuffer *rb);

#endif /* RING_BUFFER_H */

#include "ring_buffer.h"

static bool IsPowerOfTwo(uint16_t value)
{
    return (value >= 2U) && ((value & (uint16_t)(value - 1U)) == 0U);
}

bool RingBuffer_Init(RingBuffer *rb, uint8_t *storage, uint16_t capacity)
{
    if ((rb == 0) || (storage == 0) || !IsPowerOfTwo(capacity)) {
        return false;
    }

    rb->data = storage;
    rb->mask = (uint16_t)(capacity - 1U);
    rb->head = 0U;
    rb->tail = 0U;
    rb->overflow_count = 0U;
    rb->data_lost = false;
    return true;
}

bool RingBuffer_PushFromIsr(RingBuffer *rb, uint8_t byte)
{
    uint16_t next;

    if ((rb == 0) || (rb->data == 0)) {
        return false;
    }

    next = (uint16_t)((rb->head + 1U) & rb->mask);
    if (next == rb->tail) {
        /* 满时丢弃最新字节，绝不覆盖仍在被主循环消费的数据。 */
        rb->overflow_count++;
        rb->data_lost = true;
        return false;
    }

    rb->data[rb->head] = byte;
    rb->head = next;
    return true;
}

bool RingBuffer_Pop(RingBuffer *rb, uint8_t *byte)
{
    if ((rb == 0) || (byte == 0) || (rb->data == 0) || (rb->tail == rb->head)) {
        return false;
    }

    *byte = rb->data[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1U) & rb->mask);
    return true;
}

uint16_t RingBuffer_Used(const RingBuffer *rb)
{
    if (rb == 0) {
        return 0U;
    }
    return (uint16_t)((rb->head - rb->tail) & rb->mask);
}

uint16_t RingBuffer_Free(const RingBuffer *rb)
{
    if (rb == 0) {
        return 0U;
    }
    /* 环形队列留一个槽位来区分“空”和“满”。 */
    return (uint16_t)(rb->mask - RingBuffer_Used(rb));
}

void RingBuffer_Reset(RingBuffer *rb)
{
    if (rb == 0) {
        return;
    }
    /* 调用方须只在主循环、且已丢弃当前协议帧时调用。 */
    rb->tail = rb->head;
    rb->data_lost = false;
}

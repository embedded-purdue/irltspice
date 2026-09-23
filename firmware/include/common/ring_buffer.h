#ifndef __COMMON_RING_BUFFER_H__
#define __COMMON_RING_BUFFER_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#extern "C" {
#endif

// Ring buffer library intended for use with hardware communication protocols (uart, spi, usb).
// Volatile buffers, access to internals for drivers.
// Requires power of 2 buffer size for use with RP2350 DMA + some optimization.

typedef struct {
    volatile uint8_t *buffer;
    uint32_t buffer_size;
    volatile uint8_t *head;
    uint32_t fill;
} RingBuffer;

void ring_buffer_init(RingBuffer *rb, volatile uint8_t *buffer, uint32_t buffer_size);

void ring_buffer_push(RingBuffer *rb, uint8_t data);
uint8_t ring_buffer_pop(RingBuffer *rb);

static inline void ring_buffer_set_head(RingBuffer *rb, uint8_t *head) { rb->head = head; }

static inline bool ring_buffer_empty(RingBuffer *rb) { return rb->fill == 0; }

static inline uint32_t ring_buffer_get_fill(RingBuffer *rb) { return rb->fill; }

static inline void ring_buffer_set_fill(RingBuffer *rb, uint32_t fill) { rb->fill = fill; }

static inline uint32_t ring_buffer_get_size(RingBuffer *rb) { return rb->buffer_size; }

#ifdef __cplusplus
}
#endif

#endif // __COMMON_RING_BUFFER_H__

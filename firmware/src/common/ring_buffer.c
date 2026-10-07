#include "common/ring_buffer.h"

#include "assert.h"
#include <stdalign.h>
#include <stddef.h>

// ================================================================
// Static Helpers
// ================================================================

static uint8_t *_get_wrapped_addr(RingBuffer *rb, uintptr_t addr) {
    uintptr_t mask = rb->buffer_size - 1;
    return (uint8_t *)((addr & mask) | ((uintptr_t)rb->buffer & ~mask));
}

// ================================================================
// Actually Useful Baloney
// ================================================================

void ring_buffer_init(RingBuffer *rb, volatile uint8_t *buffer, uint32_t buffer_size) {
    assert(rb != NULL);
    assert(buffer != NULL);

    // buffer_size must be power of 2
    assert((buffer_size & (buffer_size - 1)) == 0);

    // buffer must be aligned to proper power of two
    assert((uintptr_t)buffer % buffer_size == 0);

    rb->buffer = buffer;
    rb->buffer_size = buffer_size;
    rb->head = buffer;
    rb->fill = 0;
    rb->overflow_cnt = 0;
}

void ring_buffer_push(RingBuffer *rb, uint8_t data) {
    uint8_t *tail = _get_wrapped_addr(rb, (uintptr_t)rb->head + rb->fill);
    *tail = data;

    rb->fill = (rb->fill == rb->buffer_size) ? (1) : (rb->fill + 1);
}

uint8_t ring_buffer_pop(RingBuffer *rb) {
    if (ring_buffer_empty(rb)) {
        return 0;
    }

    --rb->fill;
    uint8_t data = *rb->head;
    rb->head = _get_wrapped_addr(rb, (uintptr_t)rb->head + 1);

    return data;
}

bool ring_buffer_update_tail(RingBuffer *rb, uint8_t *new_tail) {

    uintptr_t head = (uintptr_t)rb->head;
    uintptr_t tail = (uintptr_t)new_tail;

    uint32_t fill = 0;
    if (tail >= head) {
        fill = tail - head;
    } else {
        fill = ring_buffer_get_size(rb) - (head - tail);
    }

    if (fill < rb->fill) {
        // overflow detected
        ++rb->overflow_cnt;
        return false;
    }
    rb->fill = fill;

    return true;
}

#include "common/framing.h"
#include "pb.h"

#include <assert.h>
#include <stddef.h>

#ifndef __cplusplus
#include <pb_decode.h>
#include <pb_encode.h>
#endif

void frame_parser_init(FrameParser *fp, RingBuffer *rb) {
    assert(fp != NULL);
    assert(rb != NULL);

    fp->rb = rb;
    fp->state = (FrameParseState){
        .phase = AWAITING_START,
        .cnt = 0,
        .length = 0,
        .length_check = 0,
        .crc = 0,
        .data_valid = false,
        .data_start = NULL,
    };
}

static void _process_byte(FrameParser *fp, uint8_t data) {
    switch (fp->state.phase) {

    case AWAITING_START:
        if (data == START_BYTE) {
            fp->state.phase = AWAITING_LENGTH;
            fp->state.length = 0;
            fp->state.cnt = 2;
        }
        break;

    case AWAITING_LENGTH:
        fp->state.length <<= 8;
        fp->state.length |= data;
        --fp->state.cnt;
        if (fp->state.cnt == 0) {
            fp->state.phase = AWAITING_LENGTH_CHECK;
            fp->state.length_check = 0;
            fp->state.cnt = 2;
        }
        break;

    case AWAITING_LENGTH_CHECK:
        fp->state.length_check <<= 8;
        fp->state.length_check |= data;
        --fp->state.cnt;
        if (fp->state.cnt == 0) {
            if (fp->state.length != fp->state.length_check) {
                fp->state.phase = AWAITING_START;
                break;
            }
            fp->state.phase = RECEIVING_DATA;
            fp->state.cnt = fp->state.length;
            fp->state.data_start = (uint8_t *)fp->rb->head;
        }
        break;

    case RECEIVING_DATA:
        --fp->state.cnt;
        if (fp->state.cnt == 0) {
            fp->state.phase = RECEIVING_CRC;
            fp->state.cnt = 2;
            fp->state.crc = 0;
        }
        break;

    case RECEIVING_CRC:
        --fp->state.cnt;
        fp->state.crc <<= 8;
        fp->state.crc |= data;
        if (fp->state.cnt == 0) {
            // TODO: check crc - for now 0 is placeholder value
            if (fp->state.crc == 0) {
                fp->state.data_valid = true;
            }
            fp->state.phase = AWAITING_START;
        }
        break;

    default:
        // invalid state
        break;
    }
}

// Parses all data in ring buffer
// Returns true early if a packet is parsed
bool frame_parser_parse(FrameParser *fp) {
    if (fp->state.data_valid) {
        return true;
    }

    while (!ring_buffer_empty(fp->rb)) {
        uint8_t data = ring_buffer_pop(fp->rb);

        _process_byte(fp, data);

        if (fp->state.data_valid) {
            return true;
        }
    }

    return false;
}

static void _frame_header(RingBuffer *rb, size_t len) {
    ring_buffer_push(rb, START_BYTE);
    ring_buffer_push(rb, (len & 0xFF00) >> 8);
    ring_buffer_push(rb, len & 0xFF);
    ring_buffer_push(rb, (len & 0xFF00) >> 8);
    ring_buffer_push(rb, len & 0xFF);
}

static void _frame_crc(RingBuffer *rb, uint16_t crc) {
    // crc - not yet implemented
    ring_buffer_push(rb, (crc & 0xFF00) >> 8);
    ring_buffer_push(rb, crc & 0xFF);
}

void frame_payload(RingBuffer *rb, const uint8_t *buf, size_t len) {
    _frame_header(rb, len);
    for (size_t i = 0; i < len; i++) {
        ring_buffer_push(rb, buf[i]);
    }
    _frame_crc(rb, 0);
}

#ifndef __cplusplus

// Nanopb specific functions
static bool _ring_buffer_pb_pop_callback(pb_istream_t *stream, pb_byte_t *buf, size_t count) {
    RingBuffer *rb = (RingBuffer *)stream->state;

    if (buf == NULL) {
        for (size_t i = 0; i < count; i++) {
            ring_buffer_pop(rb);
        }
        return true;
    }

    for (size_t i = 0; i < count; i++) {
        buf[i] = ring_buffer_pop(rb);
    }
    return true;
}

bool frame_parser_parse_for_proto(FrameParser *fp, const pb_msgdesc_t *fields, void *dest_struct) {
    if (frame_parser_parse(fp)) {
        RingBuffer temp_buffer = {
            .buffer = fp->rb->buffer,
            .buffer_size = fp->rb->buffer_size,
            .head = frame_parser_get_data_start(fp),
            .fill = frame_parser_get_data_length(fp),
        };

        pb_istream_t stream = {
            .bytes_left = temp_buffer.fill,
            .state = &temp_buffer,
            .callback = _ring_buffer_pb_pop_callback,
        };

        pb_decode(&stream, fields, dest_struct);

        frame_parser_mark_data_used(fp);
        return true;
    }

    return false;
}

static bool _ring_buffer_pb_push_callback(pb_ostream_t *stream, const pb_byte_t *buf,
                                          size_t count) {
    RingBuffer *rb = stream->state;

    for (size_t i = 0; i < count; i++) {
        ring_buffer_push(rb, buf[i]);
    }

    return true;
}

uint16_t frame_proto(RingBuffer *rb, const pb_msgdesc_t *fields, const void *proto_struct) {
    // Create temp ring buffer for placing payload data
    RingBuffer temp_buffer = {
        .buffer = rb->buffer,
        .buffer_size = rb->buffer_size,
        .fill = 0,
        .head = rb->head,
    };

    // Advance head past framing header
    for (size_t i = 0; i < 5; i++) {
        ring_buffer_push(&temp_buffer, 0);
    }

    // Encode payload into ring buffer
    pb_ostream_t stream = {
        .max_size = ring_buffer_get_size(rb),
        .bytes_written = 0,
        .state = &temp_buffer,
        .callback = _ring_buffer_pb_push_callback,
    };

    pb_encode(&stream, fields, proto_struct);

    size_t len = stream.bytes_written;

    // Setup protobuf frame
    _frame_header(rb, len);
    rb->fill += len;
    _frame_crc(rb, 0);

    return len;
}

#endif

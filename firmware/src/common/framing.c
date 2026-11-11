#ifdef FRAMING_USE_VECTOR
// When built as part of the host driver this file is symlinked next to its
// header, so resolve the header relative to this source instead.
#include "framing.h"
#else
#include "common/framing.h"
#endif

#include <assert.h>
#include <stddef.h>

#ifndef __cplusplus
#include "pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#define STRUCT(NAME) (NAME)
#else
// MSVC very picky abt this
#define STRUCT(NAME) NAME
#endif

// ================================================================
// Buffer abstraction
// ================================================================
//
// The firmware path operates directly on a RingBuffer, which is DMA friendly
// and allocation free. The host-side driver defines FRAMING_USE_VECTOR and
// operates on a std::vector<uint8_t> instead. Pushes append to the back; the
// parser walks the buffer forward with its own read cursor and never erases
// mid-frame (see frame_parser_mark_data_used).

#ifdef FRAMING_USE_VECTOR

static inline void _frame_buf_push(FrameBuffer *buf, uint8_t data) { buf->push_back(data); }

static inline bool _frame_buf_empty(const FrameParser *fp) {
    return fp->state.read_pos >= fp->rb->size();
}

static inline uint8_t _frame_buf_pop(FrameParser *fp) { return (*fp->rb)[fp->state.read_pos++]; }

#else

static inline void _frame_buf_push(FrameBuffer *buf, uint8_t data) { ring_buffer_push(buf, data); }

static inline bool _frame_buf_empty(const FrameParser *fp) { return ring_buffer_empty(fp->rb); }

static inline uint8_t _frame_buf_pop(FrameParser *fp) { return ring_buffer_pop(fp->rb); }

#endif

// ================================================================
// Parsing Logic
// ================================================================

void frame_parser_init(FrameParser *fp, FrameBuffer *rb) {
    assert(fp != NULL);
    assert(rb != NULL);

    fp->rb = rb;
    fp->state = STRUCT(FrameParseState){
        .phase = AWAITING_START,
        .cnt = 0,
        .length = 0,
        .length_check = 0,
        .crc = 0,
        .data_valid = false,
        .data_start = NULL,
    };
#ifdef FRAMING_USE_VECTOR
    fp->state.read_pos = 0;
#endif
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
#ifdef FRAMING_USE_VECTOR
            fp->state.data_start = fp->rb->data() + fp->state.read_pos;
#else
            fp->state.data_start = (uint8_t *)fp->rb->head;
#endif
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

    while (!_frame_buf_empty(fp)) {
        uint8_t data = _frame_buf_pop(fp);

        _process_byte(fp, data);

        if (fp->state.data_valid) {
            return true;
        }
    }

    return false;
}

// ================================================================
// Framing Logic
// ================================================================

static void _frame_header(FrameBuffer *rb, size_t len) {
    _frame_buf_push(rb, START_BYTE);
    _frame_buf_push(rb, (len & 0xFF00) >> 8);
    _frame_buf_push(rb, len & 0xFF);
    _frame_buf_push(rb, (len & 0xFF00) >> 8);
    _frame_buf_push(rb, len & 0xFF);
}

static void _frame_crc(FrameBuffer *rb, uint16_t crc) {
    // crc - not yet implemented
    _frame_buf_push(rb, (crc & 0xFF00) >> 8);
    _frame_buf_push(rb, crc & 0xFF);
}

void frame_payload(FrameBuffer *rb, const uint8_t *buf, size_t len) {
    _frame_header(rb, len);
    for (size_t i = 0; i < len; i++) {
        _frame_buf_push(rb, buf[i]);
    }
    _frame_crc(rb, 0);
}

// Nanopb specific functions
#ifndef __cplusplus

static bool _ring_buffer_pb_pop_callback(pb_istream_t *stream, pb_byte_t *buf, size_t count) {
    RingBuffer *rb = (RingBuffer *)stream->state;

    // Buffer of NULL means nanopb is ignoring these bytes
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

    // header length + payload length + crc length
    return 5 + len + 2;
}

#endif

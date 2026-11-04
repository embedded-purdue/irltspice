#ifndef __COMMON_FRAMING_H__
#define __COMMON_FRAMING_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Tools for parsing our framing format for all internode comms
//
// The firmware runs on a RingBuffer (DMA friendly, fixed size, allocation
// free); the host-side driver has no such constraint and just wants a growable
// byte queue. Define FRAMING_USE_VECTOR when building the driver to swap the
// RingBuffer for a std::vector<uint8_t>. The rest of the API is unchanged.

#ifdef FRAMING_USE_VECTOR
#include <cstddef>
#include <vector>
typedef std::vector<uint8_t> FrameBuffer;
#else
#include "common/ring_buffer.h"
typedef RingBuffer FrameBuffer;
#endif

#ifndef __cplusplus
#include <pb_common.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// FRAMING FORMAT
// - Start byte (0xAB)
// - Length (2 bytes)
// - Length again (2 bytes), check match
// - Payload (n bytes)
// - crc16 (2 bytes), 0 for placeholder value for now

#define START_BYTE 0xAB

typedef enum {
    AWAITING_START,
    AWAITING_LENGTH,
    AWAITING_LENGTH_CHECK,
    RECEIVING_DATA,
    RECEIVING_CRC,
} FrameParsePhase;

typedef struct {
    FrameParsePhase phase;
    uint16_t cnt;
    uint16_t length;
    uint16_t length_check;
    uint16_t crc;
    bool data_valid;
    uint8_t *data_start;
#ifdef FRAMING_USE_VECTOR
    // A std::vector has no head pointer, so the parser owns the read cursor
    // into the caller's buffer. It is the index of the next unparsed byte.
    size_t read_pos;
#endif
} FrameParseState;

typedef struct {
    FrameBuffer *rb;
    FrameParseState state;
} FrameParser;

void frame_parser_init(FrameParser *fp, FrameBuffer *rb);

// Parses all data in ring buffer
// Returns true early if a packet is parsed
bool frame_parser_parse(FrameParser *fp);

static inline uint8_t *frame_parser_get_data_start(FrameParser *fp) { return fp->state.data_start; }

static inline uint16_t frame_parser_get_data_length(FrameParser *fp) { return fp->state.length; }

static inline void frame_parser_mark_data_used(FrameParser *fp) {
    fp->state.data_valid = false;
#ifdef FRAMING_USE_VECTOR
    // The caller is done with the payload: drop every byte consumed so far and
    // rewind the cursor. Any bytes past the frame are preserved.
    fp->rb->erase(fp->rb->begin(), fp->rb->begin() + fp->state.read_pos);
    fp->state.read_pos = 0;
    fp->state.data_start = NULL;
#endif
}

// Nanopb (c) specific functions
#ifndef __cplusplus
bool frame_parser_parse_for_proto(FrameParser *fp, const pb_msgdesc_t *fields, void *proto_struct);

uint16_t frame_proto(FrameBuffer *rb, const pb_msgdesc_t *fields, const void *proto_struct);
#endif

void frame_payload(FrameBuffer *rb, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __COMMON_FRAMING_H__

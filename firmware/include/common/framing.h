#ifndef __COMMON_FRAMING_H__
#define __COMMON_FRAMING_H__

#include "common/ring_buffer.h"
#include <stdint.h>

#ifndef __cplusplus
#include <pb_common.h>
#endif

#ifdef __cplusplus
#extern "C" {
#endif

// Tools for parsing our framing format for all internode comms

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
} FrameParseState;

typedef struct {
    RingBuffer *rb;
    FrameParseState state;
} FrameParser;

void frame_parser_init(FrameParser *fp, RingBuffer *rb);

// Parses all data in ring buffer
// Returns true early if a packet is parsed
bool frame_parser_parse(FrameParser *fp);

static inline uint8_t *frame_parser_get_data_start(FrameParser *fp) { return fp->state.data_start; }

static inline uint16_t frame_parser_get_data_length(FrameParser *fp) { return fp->state.length; }

static inline void frame_parser_mark_data_used(FrameParser *fp) { fp->state.data_valid = false; }

// Nanopb (c) specific functions
#ifndef __cplusplus
bool frame_parser_parse_for_proto(FrameParser *fp, const pb_msgdesc_t *fields, void *proto_struct);

uint16_t frame_proto(RingBuffer *rb, const pb_msgdesc_t *fields, const void *proto_struct);
#endif

void frame_payload(RingBuffer *rb, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __COMMON_FRAMING_H__

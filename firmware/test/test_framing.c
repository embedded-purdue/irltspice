#include <unity.h>

#include "command.pb.h"
#include "common/framing.h"

#include <pb_decode.h>
#include <pb_encode.h>

#define BUFFER_SIZE_BITS (12)
#define BUFFER_SIZE (1 << BUFFER_SIZE_BITS)
__attribute__((aligned(BUFFER_SIZE))) volatile uint8_t buffer[BUFFER_SIZE];

RingBuffer rb;
FrameParser parser;

void setUp() {
    ring_buffer_init(&rb, buffer, BUFFER_SIZE);
    frame_parser_init(&parser, &rb);
}

void test_init() {
    TEST_ASSERT_EQUAL(rb.buffer_size, BUFFER_SIZE);
    TEST_ASSERT_EQUAL(rb.buffer, buffer);
    TEST_ASSERT_EQUAL(rb.fill, 0);
    TEST_ASSERT_EQUAL(rb.head, buffer);

    TEST_ASSERT_EQUAL(parser.rb, &rb);
    TEST_ASSERT_EQUAL(parser.state.phase, AWAITING_START);
    TEST_ASSERT_EQUAL(parser.state.cnt, 0);
    TEST_ASSERT_EQUAL(parser.state.length, 0);
    TEST_ASSERT_EQUAL(parser.state.length_check, 0);
    TEST_ASSERT_EQUAL(parser.state.crc, 0);
    TEST_ASSERT_EQUAL(parser.state.data_valid, false);
    TEST_ASSERT_EQUAL(parser.state.data_start, NULL);
}

void test_parse_byte() {
    // Send single byte

    ring_buffer_push(&rb, START_BYTE);
    ring_buffer_push(&rb, 0); // length
    ring_buffer_push(&rb, 1); // length

    ring_buffer_push(&rb, 0); // length check
    ring_buffer_push(&rb, 1); // length check

    ring_buffer_push(&rb, 5); // data

    ring_buffer_push(&rb, 0); // crc (not yet implemented)
    ring_buffer_push(&rb, 0); // crc (not yet implemented)

    TEST_ASSERT_TRUE(frame_parser_parse(&parser));
    TEST_ASSERT_EQUAL(frame_parser_get_data_length(&parser), 1);
    TEST_ASSERT_EQUAL(frame_parser_get_data_start(&parser), &buffer[5]);
    TEST_ASSERT_EQUAL(*frame_parser_get_data_start(&parser), 5);

    frame_parser_mark_data_used(&parser);

    TEST_ASSERT_FALSE(frame_parser_parse(&parser));
}

// Setup proto payload
const MotherCommand proto_payload = {
    .which_cmd = MotherCommand_dummy_command_tag,
    .cmd.dummy_command = {.motherboard_id = 0,
                          .daughterboard_id = 3,
                          .has_payload = true,
                          .payload = {.which_data = ProgrammingPayload_xbar_tag,
                                      .data.xbar = {
                                          .crossbar_id = 0,
                                          .row = 0,
                                          .column = 0,
                                      }}}};

void test_parse_proto_manual() {
    size_t len = frame_proto(&rb, MotherCommand_fields, &proto_payload);

    // Parse frame
    TEST_ASSERT_TRUE(frame_parser_parse(&parser));
    TEST_ASSERT_EQUAL(frame_parser_get_data_length(&parser), len);

    // Decode proto
    pb_istream_t istream = pb_istream_from_buffer(frame_parser_get_data_start(&parser),
                                                  frame_parser_get_data_length(&parser));

    MotherCommand command = MotherCommand_init_default;
    pb_decode(&istream, MotherCommand_fields, &command);

    TEST_ASSERT_EQUAL(command.cmd.dummy_command.daughterboard_id, 3);
}

void test_parse_proto_helper() {
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        frame_proto(&rb, MotherCommand_fields, &proto_payload);

        MotherCommand command = MotherCommand_init_default;
        TEST_ASSERT_TRUE(frame_parser_parse_for_proto(&parser, MotherCommand_fields, &command));

        TEST_ASSERT_EQUAL(command.cmd.dummy_command.daughterboard_id, 3);
    }
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init);
    RUN_TEST(test_parse_byte);
    RUN_TEST(test_parse_proto_manual);
    RUN_TEST(test_parse_proto_helper);

    UNITY_END();
}

#include "daughterboard/recv_commands.h"

#include "spi.h"
#include "stm32f4xx_hal_spi.h"

#include "common/framing.h"
#include "common/ring_buffer.h"

#define RX_BUFFER_SIZE (4096)
__attribute__((aligned(RX_BUFFER_SIZE))) static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];

static RingBuffer g_rb;
static FrameParser g_fp;

void recv_command_init() {
    ring_buffer_init(&g_rb, (uint8_t *)rx_buffer, RX_BUFFER_SIZE);
    frame_parser_init(&g_fp, &g_rb);

    HAL_SPI_Receive_DMA(&hspi1, (uint8_t *)rx_buffer, RX_BUFFER_SIZE);
}

bool recv_command(DaughterCommand *command) {
    // Update ring buffer to reflect DMA state
    uint32_t remaining_items = __HAL_DMA_GET_COUNTER(hspi1.hdmarx);
    uint32_t current_index = (RX_BUFFER_SIZE - remaining_items) % RX_BUFFER_SIZE;
    uint8_t *current_address = (uint8_t *)&rx_buffer[current_index];
    bool res = ring_buffer_update_tail(&g_rb, current_address);

    if (!res) {
        // overflow detected
    }

    return frame_parser_parse_for_proto(&g_fp, DaughterCommand_fields, command);
}

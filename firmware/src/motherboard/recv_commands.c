#include "motherboard/recv_commands.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#include "common/framing.h"
#include "common/ring_buffer.h"

#define COMMS_DMA_BUFFER_SIZE_BITS (16)
#define COMMS_DMA_BUFFER_SIZE (1 << COMMS_DMA_BUFFER_SIZE_BITS)

__attribute__((
    __aligned__(COMMS_DMA_BUFFER_SIZE))) static volatile uint8_t g_buffer[COMMS_DMA_BUFFER_SIZE];

static int g_dma_ch;

static RingBuffer g_rb;
static FrameParser g_fp;

void recv_command_init() {
    ring_buffer_init(&g_rb, g_buffer, COMMS_DMA_BUFFER_SIZE);
    frame_parser_init(&g_fp, &g_rb);

    // UART DMA Setup
    g_dma_ch = dma_claim_unused_channel(false);
    assert(g_dma_ch != -1); // no channel available

    dma_channel_config_t cfg = dma_channel_get_default_config(g_dma_ch);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, uart_get_dreq_num(uart0, false));
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_ring(&cfg, true, COMMS_DMA_BUFFER_SIZE_BITS);
    channel_config_set_high_priority(&cfg, true);

    dma_channel_configure(g_dma_ch, &cfg, g_buffer, &uart0_hw->dr,
                          dma_encode_endless_transfer_count(), true);

    // UART setup
    uart_init(uart0, 115200);
    gpio_set_function(0, UART_FUNCSEL_NUM(uart0, 0));
    gpio_set_function(1, UART_FUNCSEL_NUM(uart0, 1));
}

bool recv_command(MotherCommand *command) {
    // Update ring buffer state to reflect what DMA has done
    uintptr_t head = (uintptr_t)g_rb.head;
    uintptr_t tail = dma_hw->ch[g_dma_ch].write_addr;

    uint32_t fill = 0;
    if (tail > head) {
        fill = tail - head;
    } else {
        fill = ring_buffer_get_size(&g_rb) - (head - tail);
    }

    if (fill < g_rb.fill) {
        // overflow detected
    }
    g_rb.fill = fill;

    // Parse buffer for proto
    return frame_parser_parse_for_proto(&g_fp, MotherCommand_fields, command);
}

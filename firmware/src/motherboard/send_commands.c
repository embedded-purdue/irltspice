#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "command.pb.h"
#include "common/framing.h"
#include "common/ring_buffer.h"
#include "pico/platform/sections.h"

// SENDING DATA TO DAUGHTERBOARDS

// Protocol:
// - spi
// - motherboard is master, daughters are slaves
// - daughters indicate they are about to start responding with data by setting a gpio high
// - if a daughter has its gpio high, the mother will send dummy data to suck the data out
// - if a mother is sending to daughter already and the daughter puts its pin high, mother starts
// parsing daughter input as a command

// Implementation (mother side):
// - Push to ring buffers, have dma sending spi
// - One spi output, different cs
// - When switching daughterboard, update cs pins and change dma channel

// Message queue?
// - place into queue with start addr, length
// - dma complete interrupt
// - how does this interact with receiving commands?

// Different output queues for each daughterboard?
//     - probably yeah, but then need different dma channel for each
//

// 16 bit spi transfers - with framing, don't need to worry about extra bytes
// Slave raises pin high for ready, need to wait for low and then high again for ready.

// place messages into ring buffer in order
// set dma transfer count

// 4 dma channels - enable the appropriate one when sending to it and it signals that it has
// data

#define NUM_DAUGHTERS (4)
#define SEND_BUFFER_SIZE_BITS (12)
#define SEND_BUFFER_SIZE (1 << SEND_BUFFER_SIZE_BITS)

#define RECV_BUFFER_SIZE_BITS (12)
#define RECV_BUFFER_SIZE (1 << RECV_BUFFER_SIZE_BITS)

static const uint8_t g_cs_pins[4] = {10, 11, 12, 13};
__attribute__((__aligned__(
    SEND_BUFFER_SIZE))) static volatile uint8_t g_send_buffers[NUM_DAUGHTERS][SEND_BUFFER_SIZE];
__attribute__((__aligned__(
    RECV_BUFFER_SIZE))) static volatile uint8_t g_recv_buffers[NUM_DAUGHTERS][RECV_BUFFER_SIZE];

typedef struct {
    RingBuffer send_buffer;
    RingBuffer recv_buffer;
    uint8_t cs_pin;
} DaughterBoard;

static DaughterBoard g_daughters[NUM_DAUGHTERS];
static uint8_t g_send_dma_ch;
__attribute__((used)) static uint8_t g_recv_dma_ch;

// spi transfer complete interrupt handler
// placed "not in flash" to prevent needing to load from flash during interrupt - best practice for
// all interrupt handlers
void __not_in_flash_func(spi_transfer_complete_handler)() {
    // check if dma transfer is also done
    // This is in case dma is slow because of bus traffic and spi completes transfers before all dma
    // transfers have happened
}

void send_command_init() {
    // setup spi
    // this enables dma requests by default
    spi_init(spi0, 2000000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // PINS
    //  - TX: 3
    //  - RX: 4
    //  - SCK: 6
    //  - CSn: handle ourselves
    //      - could hypothetically have a dma channel with buffer for csn output that chains to the
    //      spi transfer but def overkill
    gpio_set_function(3, GPIO_FUNC_SPI);
    gpio_set_function(4, GPIO_FUNC_SPI);
    gpio_set_function(6, GPIO_FUNC_SPI);

    // setup send dma
    g_send_dma_ch = dma_claim_unused_channel(true);

    dma_channel_config_t cfg = dma_channel_get_default_config(g_send_dma_ch);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(spi0, true));
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_ring(&cfg, false, SEND_BUFFER_SIZE_BITS);

    dma_channel_configure(g_send_dma_ch, &cfg, &spi0_hw->dr, NULL, 0, false);

    // TODO: setup recv dma

    for (uint8_t i = 0; i < NUM_DAUGHTERS; i++) {
        // setup gpio
        uint8_t cs_pin = g_cs_pins[i];
        gpio_init(cs_pin);
        gpio_set_dir(cs_pin, true);
        gpio_put(cs_pin, true);
        g_daughters[i].cs_pin = cs_pin;

        // setup buffer
        ring_buffer_init(&g_daughters[i].send_buffer, g_send_buffers[i], SEND_BUFFER_SIZE);
        ring_buffer_init(&g_daughters[i].recv_buffer, g_recv_buffers[i], RECV_BUFFER_SIZE);
    }
}

bool send_command(uint8_t daughter_id, DaughterCommand *command) {
    if (dma_channel_is_busy(g_send_dma_ch)) {
        return false;
    }

    RingBuffer *rb = &g_daughters[daughter_id].send_buffer;

    uint16_t len = frame_proto(rb, DaughterCommand_fields, command);

    gpio_put(g_daughters[daughter_id].cs_pin, false);

    // TODO: place into command queue that is serviced by dma complete interrupt
    dma_channel_set_read_addr(g_send_dma_ch, rb->head, false);
    dma_channel_set_trans_count(g_send_dma_ch, len, true);

    // TODO: make asynchronous using dma complete interrupts
    // actually dma complete just means everything is in the fifo, and there is no spi complete
    // interrupt. Not a great way to make this async. Could sleep until dma empty interrupt, then
    // poll spi busy.
    while (dma_channel_is_busy(g_send_dma_ch))
        ;
    while (spi_is_busy(spi0))
        ;

    gpio_put(g_daughters[daughter_id].cs_pin, true);

    // TODO: make the fill set to something appropriate based on size of message sent
    ring_buffer_set_head(rb, (uint8_t *)dma_hw->ch[g_send_dma_ch].read_addr);
    ring_buffer_set_fill(rb, 0);

    return true;
}

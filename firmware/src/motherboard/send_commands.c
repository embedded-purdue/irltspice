#include "FreeRTOS.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "queue.h"
#include "task.h"

#include "command.pb.h"
#include "common/framing.h"
#include "common/ring_buffer.h"
#include "pico/platform/sections.h"

#include "command.pb.h"

// SENDING DATA TO / RECEIVING DATA FROM DAUGHTERBOARDS

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

// Message queue
// - place into queue with start addr, length
// - dma complete interrupt + poll on spi complete

// command queue - send command function adds this to queue
// main function - checks for stuff in queue, checks for active data ready

// ===============================================================================================
// Config
// ===============================================================================================

#define NUM_DAUGHTERS (4)
static const uint8_t g_cs_pins[NUM_DAUGHTERS] = {10, 14, 18, 22};
static const uint8_t g_reset_pins[NUM_DAUGHTERS] = {11, 15, 19, 23};
static const uint8_t g_data_ready_pins[NUM_DAUGHTERS] = {12, 16, 20, 24};
static const uint8_t g_status_pins[NUM_DAUGHTERS] = {13, 17, 21, 25};

#define MOSI_PIN (3)
#define MISO_PIN (4)
#define SCK_PIN (6)

// Size of buffer will be 1 << x_BUFFER_SIZE_BITS
// Needs to be power of 2 for DMA purposes
#define SEND_BUFFER_SIZE_BITS (12)
#define RECV_BUFFER_SIZE_BITS (12)

#define COMMAND_QUEUE_LENGTH (NUM_DAUGHTERS * 10)

// ===============================================================================================
// Buffer Setup
// ===============================================================================================

/* SPI DMA BUFFERS */
#define SEND_BUFFER_SIZE (1 << SEND_BUFFER_SIZE_BITS)
#define RECV_BUFFER_SIZE (1 << RECV_BUFFER_SIZE_BITS)

__attribute__((__aligned__(
    SEND_BUFFER_SIZE))) static volatile uint8_t g_send_buffers[NUM_DAUGHTERS][SEND_BUFFER_SIZE];
__attribute__((__aligned__(
    RECV_BUFFER_SIZE))) static volatile uint8_t g_recv_buffers[NUM_DAUGHTERS][RECV_BUFFER_SIZE];

/* Command Queue */
typedef struct {
    uint8_t daughter_id;
    uint16_t len;
    uint8_t type;
} CommandQueueEntry;

static uint8_t g_command_queue_storage_buffer[COMMAND_QUEUE_LENGTH * sizeof(CommandQueueEntry)];
static StaticQueue_t g_command_queue_structure;
static QueueHandle_t g_command_queue_handle;

// ================================================================================================
// Global State
// ================================================================================================

typedef enum {
    NOT_PRESENT,
    READY,
    TOPO_REQUESTED,
    RECEIVING,
    STOP_REQUESTED,
    ERROR,
} DaughterState;

typedef struct {
    RingBuffer send_buffer;
    RingBuffer recv_buffer;
    FrameParser recv_parser;
    uint8_t cs_pin;
    uint8_t reset_pin;
    uint8_t data_ready_pin;
    uint8_t status_pin;
    DaughterboardTopology info;
    DaughterState state;
    DaughterResponse last_response;
    uint16_t cmd_cnt; // number of programming commands sent in current cycle. Compared against
                      // daughter's stop response.
} DaughterBoard;

static DaughterBoard g_daughters[NUM_DAUGHTERS];

static uint8_t g_send_dma_ch;
static uint8_t g_recv_dma_ch;

// ================================================================================================
// Interrupt Handlers
// ================================================================================================

// dma transfer complete interrupt handler
// placed "not in flash" to prevent needing to load from flash during interrupt - best practice for
// all interrupt handlers
void __not_in_flash_func(dma_transfer_complete_handler)() {
    // signal that dma transfer is completed
    // main thread will sleep until this happens, then poll until spi is complete
}

void __not_in_flash_func(data_ready_pin_high_handler)() {
    // signal that daughter has data ready
}

void __not_in_flash_func(status_pin_handler)() {
    // update global state of daughter
}

// ================================================================================================
// Initialization
// ================================================================================================

void send_command_init() {

    /* SETUP SPI PERIPHERAL */

    // this enables dma requests by default
    spi_init(spi0, 2000000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SCK_PIN, GPIO_FUNC_SPI);

    /* SETUP SEND DMA */

    g_send_dma_ch = dma_claim_unused_channel(true);

    dma_channel_config_t cfg = dma_channel_get_default_config(g_send_dma_ch);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(spi0, true));
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_ring(&cfg, false, SEND_BUFFER_SIZE_BITS);

    dma_channel_configure(g_send_dma_ch, &cfg, &spi0_hw->dr, NULL, 0, false);

    g_recv_dma_ch = dma_claim_unused_channel(true);

    cfg = dma_channel_get_default_config(g_recv_dma_ch);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, spi_get_dreq(spi0, false));
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_ring(&cfg, false, RECV_BUFFER_SIZE_BITS);

    dma_channel_configure(g_recv_dma_ch, &cfg, NULL, &spi0_hw->dr, 0, false);

    /* SETUP GPIO AND INIT DAUGHTERBOARD STRUCTS */

    for (uint8_t i = 0; i < NUM_DAUGHTERS; i++) {
        // setup gpio
        uint8_t cs_pin = g_cs_pins[i];
        gpio_init(cs_pin);
        gpio_set_dir(cs_pin, true);
        gpio_put(cs_pin, true);
        g_daughters[i].cs_pin = cs_pin;

        uint8_t reset_pin = g_reset_pins[i];
        gpio_init(reset_pin);
        gpio_set_dir(reset_pin, true);
        gpio_put(reset_pin, true);
        g_daughters[i].reset_pin = reset_pin;

        uint8_t data_ready_pin = g_data_ready_pins[i];
        gpio_init(data_ready_pin);
        g_daughters[i].data_ready_pin = data_ready_pin;

        uint8_t status_pin = g_status_pins[i];
        gpio_init(status_pin);
        g_daughters[i].status_pin = status_pin;

        // setup buffer
        ring_buffer_init(&g_daughters[i].send_buffer, g_send_buffers[i], SEND_BUFFER_SIZE);
        ring_buffer_init(&g_daughters[i].recv_buffer, g_recv_buffers[i], RECV_BUFFER_SIZE);
        frame_parser_init(&g_daughters[i].recv_parser, &g_daughters[i].recv_buffer);

        g_daughters[i].info = (DaughterboardTopology)DaughterboardTopology_init_default;

        g_daughters[i].state = NOT_PRESENT;
    }

    /* SETUP COMMAND QUEUE */

    g_command_queue_handle =
        xQueueCreateStatic(COMMAND_QUEUE_LENGTH, sizeof(CommandQueueEntry),
                           g_command_queue_storage_buffer, &g_command_queue_structure);
}

// ================================================================================================
// Public API
// ================================================================================================

bool send_command(uint8_t daughter_id, DaughterCommand *command) {
    // TODO: check if command is valid based on daughter state

    RingBuffer *rb = &g_daughters[daughter_id].send_buffer;

    // TODO: add length check before framing proto. Do not overflow send buffer.
    uint16_t len = frame_proto(rb, DaughterCommand_fields, command);

    CommandQueueEntry entry = {.daughter_id = daughter_id, .len = len, .type = command->which_cmd};
    return xQueueSend(g_command_queue_handle, &entry, portMAX_DELAY);
}

// ================================================================================================
// Business Logic - Task Implementation
// ================================================================================================

void daughter_comms_task(void *pvParameters) {
    (void)pvParameters;

    /* RESET EACH DAUGHTER */
    for (uint8_t id = 0; id < NUM_DAUGHTERS; id++) {
        gpio_put(g_reset_pins[id], 0);
    }
    vTaskDelay(100);
    for (uint8_t id = 0; id < NUM_DAUGHTERS; id++) {
        gpio_put(g_reset_pins[id], 1);
    }
    vTaskDelay(100);

    for (;;) {

        /* POP COMMAND FROM QUEUE + CHECK FOR DATA READY SIGNALS */

        CommandQueueEntry queue_entry;
        bool is_suck = false;
        if (!xQueueReceive(g_command_queue_handle, &queue_entry, 0)) {
            for (uint8_t id = 0; id < NUM_DAUGHTERS; ++id) {
                if (gpio_get(g_data_ready_pins[id])) {
                    is_suck = true;
                    queue_entry.daughter_id = id;
                    queue_entry.len = DaughterResponse_size;
                    break;
                }
            }

            // nothing in queue or daughter data ready - don't send any spi
            if (!is_suck) {
                continue;
            }
        }

        /* SEND COMMAND */

        DaughterBoard *daughter = &g_daughters[queue_entry.daughter_id];
        uint16_t len = queue_entry.len;
        RingBuffer *send_rb = &daughter->send_buffer;
        RingBuffer *recv_rb = &daughter->recv_buffer;
        gpio_put(daughter->cs_pin, false);

        // Setup spi buffer to send from - if pulling data out, just send 0s
        dma_channel_config_t cfg = dma_get_channel_config(g_send_dma_ch);
        if (is_suck) {
            static uint8_t dummy = 0;
            channel_config_set_read_increment(&cfg, false);
            dma_channel_set_config(g_send_dma_ch, &cfg, false);
            dma_channel_set_read_addr(g_send_dma_ch, &dummy, false);
        } else {
            channel_config_set_read_increment(&cfg, true);
            dma_channel_set_config(g_send_dma_ch, &cfg, false);
            dma_channel_set_read_addr(g_send_dma_ch, send_rb->head, false);
        }
        dma_channel_set_write_addr(g_recv_dma_ch, ring_buffer_get_tail(recv_rb), false);
        dma_channel_set_trans_count(g_recv_dma_ch, len, true);
        dma_channel_set_trans_count(g_send_dma_ch, len, true);

        /* UPDATE DAUGHTER STATE */

        if (!is_suck) {
            switch (daughter->state) {
            case READY:
                if (queue_entry.type == DaughterCommand_topo_tag) {
                    daughter->state = TOPO_REQUESTED;
                } else if (queue_entry.type == DaughterCommand_prog_tag) {
                    daughter->state = RECEIVING;
                    daughter->cmd_cnt = 1;
                } else {
                    daughter->state = ERROR;
                }
                break;
            case RECEIVING:
                if (queue_entry.type == DaughterCommand_prog_tag) {
                    daughter->cmd_cnt++;
                } else {
                    daughter->state = ERROR;
                }
                break;
            case TOPO_REQUESTED:
            case STOP_REQUESTED:
            case NOT_PRESENT:
            case ERROR:
                daughter->state = ERROR;
                break;
            }
        }

        /* COMPLETE SPI TRANSFER */

        // TODO: make asynchronous using dma complete interrupts
        // No direct spi complete interrupt. Wait on dma complete interrupt, then poll spi.
        while (dma_channel_is_busy(g_send_dma_ch))
            ;
        while (spi_is_busy(spi0))
            ;

        gpio_put(daughter->cs_pin, true);

        /* PARSE DAUGHTER RESPONSE */

        ring_buffer_set_fill(recv_rb, ring_buffer_get_fill(recv_rb) + len);

        DaughterResponse *response = &daughter->last_response;
        if (frame_parser_parse_for_proto(&daughter->recv_parser, DaughterResponse_fields,
                                         response)) {
            switch (daughter->state) {
            case TOPO_REQUESTED:
                if (response->which_data == DaughterResponse_topo_tag) {
                    daughter->state = READY;
                } else {
                    daughter->state = ERROR;
                }
                break;
            case STOP_REQUESTED:
                if (response->which_data == DaughterResponse_stop_tag) {
                    // TODO: compare cmd_cnt
                    daughter->state = READY;
                } else {
                    daughter->state = ERROR;
                }
                break;
            case NOT_PRESENT:
            case READY:
            case RECEIVING:
            case ERROR:
                daughter->state = ERROR;
                break;
            }
        }

        /* UPDATE SEND BUFFER STATE */

        if (!is_suck) {
            ring_buffer_update_head(send_rb, (uint8_t *)dma_hw->ch[g_send_dma_ch].read_addr);
        }
    }
}

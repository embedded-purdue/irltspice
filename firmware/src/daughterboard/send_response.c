#include "daughterboard/send_response.h"
#include "common/queue.h"

/*
 * This is the logic for sending a response over SPI from daughter->mother.
 *
 * We are kind of using SPI in a weird way here because we want the slave to be able to trigger
 * transfers to send messages whenever it wants.
 *
 * We are using SPI instead of I2C because SPI can be clocked much faster with no bus conflicts and
 * the master will be sending much more data than the slave (slave sending data is where things are
 * problematic for us). I2C would probably work fine as well for our purposes, but would have its
 * own engineering challenges for setting addresses etc.
 *
 * In SPI, the master (motherboard) must initiate transfers. There will be times when the slave
 * (daughter) has data to send, but the mother has no reason to initiate transfers. To resolve this,
 * we give the daughter a gpio that it can use to indicate that it has data ready to send. To send a
 * message, the daughter prepares data to be sent then sets this gpio high.
 *
 * If the mother has no transfers to send, it checks if any of the daughters want to send data back.
 * It will continue sending dummy data to the daughter until the gpio goes low.
 *
 *
 *
 * To implement this on the daughter side, we place all of the data we want to send in a ring
 * buffer. Then, we keep a queue of the length of each message we want to send.
 *
 * This way, we can set up a dma transfer to send all of our first message. Once this transfer is
 * complete (interrupt fires), we check if there is another message in the queue. If there is, we
 * set up another dma transfer.
 *
 * This creates a chain of interrupts. The first dma transfer causes the first interrupt, which then
 * starts another dma transfer for the next item in the queue, which triggers another interrupt.
 *
 * When we are adding responses to the queue, we need to be aware of whether this chain is currently
 * active. If it is not, we start the first transfer. If it is, we just add the message to the
 * queue and let the interrupts get to it.
 *
 *
 * We also need to handle wrapping around our ring buffer. Unlike the RP2350, the STM DMA is not
 * aware of the size of our ring buffer. If we have a transfer that wraps from the end to the start
 * of the buffer, we need to split that into two dma transfers.
 *
 * */

#include "boards/stm32f4_nucleo/main.h"
#include "common/framing.h"
#include "common/ring_buffer.h"
#include "main.h"
#include "spi.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_spi.h"

typedef struct {
    size_t len;
    QueueNode queue_node;
} ListNode;

#define SEND_BUFFER_SIZE (4096)
__attribute__((aligned(SEND_BUFFER_SIZE))) static uint8_t send_buffer[SEND_BUFFER_SIZE];

static RingBuffer g_rb;

#define NODE_BUFFER_SIZE (256)
static ListNode g_node_buffer[NODE_BUFFER_SIZE];
static Queue g_message_queue;
static Queue g_free_node_list;

static volatile bool g_is_sending;

// priority - if true, place at start of list
static bool _enqueue_response(uint16_t len, bool priority) {
    // enter critical section
    // TODO:: make util for entering/exiting critical section
    uint32_t original_irq_state = __get_PRIMASK();
    __disable_irq();

    if (queue_is_empty(&g_free_node_list)) {
        __set_PRIMASK(original_irq_state);
        return false;
    }

    ListNode *new_node = pop(&g_free_node_list, ListNode);
    new_node->len = len;

    enqueue(&g_message_queue, &new_node->queue_node, priority);

    // exit critical section
    __set_PRIMASK(original_irq_state);

    return true;
}

// returns false if command queue is empty
static bool _trigger_spi_transmission() {
    // disable interrupts so we don't write HAL_SPI_STATE_BUSY_TX after the transfer has already
    // completed and been processed by HAL interrupt
    uint32_t original_irq_state = __get_PRIMASK();
    __disable_irq();

    // pop command from buffer
    ListNode *command_node = pop(&g_message_queue, ListNode);
    if (command_node == NULL) {
        return false;
    }

    enqueue(&g_free_node_list, &command_node->queue_node, true);

    // if len > space left in rb, need to split into two transfers
    // for proper wrapping.
    uint16_t len = command_node->len;
    uintptr_t diff = ((uintptr_t)g_rb.buffer + g_rb.buffer_size) - ((uintptr_t)g_rb.head);

    if (len > diff) {
        _enqueue_response(len - diff, true);
        len = diff;
    }

    HAL_SPI_StateTypeDef original_state = hspi1.State;

    hspi1.State =
        HAL_SPI_STATE_READY; // We overwrite the normal state behavior so we can trigger/complete
                             // tx dma transfers while keeping our rx dma active.

    HAL_StatusTypeDef res = HAL_SPI_Transmit_DMA(&hspi1, (const uint8_t *)g_rb.head, len);

    if (res) {
        hspi1.State = original_state | HAL_SPI_STATE_BUSY_TX;
    } else {
        hspi1.State = original_state;
    }

    // Re-enable interrupts (if they were enabled when function was called)
    __set_PRIMASK(original_irq_state);

    return true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi != &hspi1) {
        return;
    }

    // enter critical section
    uint32_t original_irq_state = __get_PRIMASK();
    __disable_irq();

    // update rb based on dma state
    ring_buffer_update_head(&g_rb, (uint8_t *)hspi1.hdmatx->Instance->M0AR);

    g_is_sending = _trigger_spi_transmission();
    if (!g_is_sending) {
        HAL_GPIO_WritePin(DATA_READY_GPIO_Port, DATA_READY_Pin, GPIO_PIN_RESET);
    }

    // exit critical section
    __set_PRIMASK(original_irq_state);
}

void send_response_init() {
    ring_buffer_init(&g_rb, send_buffer, SEND_BUFFER_SIZE);

    queue_init(g_message_queue, g_free_node_list, g_node_buffer, NODE_BUFFER_SIZE);

    g_is_sending = false;

    // set up gpio
    HAL_GPIO_WritePin(DATA_READY_GPIO_Port, DATA_READY_Pin, GPIO_PIN_RESET);
}

bool send_response(DaughterResponse *command) {
    uint16_t len = frame_proto(&g_rb, DaughterResponse_fields, command);
    if (!_enqueue_response(len, false)) {
        return false;
    }

    // if we are currently sending, nothing to do. Tx cplt callback will trigger subsequent
    // transfers. if we are not currently sending, trigger first spi dma transfer

    if (!g_is_sending) {
        // this is the first transfer in the chain
        g_is_sending = true;
        HAL_GPIO_WritePin(DATA_READY_GPIO_Port, DATA_READY_Pin, GPIO_PIN_SET);
        _trigger_spi_transmission();
    }

    return true;
}

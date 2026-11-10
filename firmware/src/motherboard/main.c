#include "command.pb.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "motherboard/recv_commands.h"
#include "motherboard/send_commands.h"

#include "FreeRTOS.h"
#include "motherboard/FreeRTOSConfig.h"
#include "portmacrocommon.h"
#include "task.h"

// ----------------------------------------
// FreeRTOS Configs
// ----------------------------------------

#define DAUGHTER_COMMS_TASK_SIZE configMINIMAL_STACK_SIZE * 10

StaticTask_t daughter_comms_tcb;
StackType_t daughter_comms_stack[DAUGHTER_COMMS_TASK_SIZE];

/* Supply static memory blocks for the background Idle Task */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* Supply static memory blocks for the Software Timer Service Task */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/* Manual override to resolve the FreeRTOS RP2350 port's vector setup */
void exception_set_exclusive_handler(int num, void (*handler)(void)) {
    // Write handler directly to the ARM Cortex-M33 VTOR table offset
    // 0xE000ED08 is the standard ARM VTOR address; index 16 skips core exceptions
    volatile uint32_t *vtor = (volatile uint32_t *)0xE000ED08;
    vtor[16 + num] = (uint32_t)handler;
}

/* Catch stack overflow */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    while (1)
        ;
}

// ----------------------------------------
// Command Processing Logic
// ----------------------------------------

static void _process_command(MotherCommand *command) {
    (void)command; // nop
}

// ----------------------------------------
// Main
// ----------------------------------------

int main(void) {
    // Setup FPU
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88;
    *cpacr |= ((3UL << 20) | (3UL << 22));
    __asm volatile("isb");

    // Setup pin 25 as gpio
    gpio_init(25);

    // Set pin 25 as output
    gpio_set_dir(25, GPIO_OUT);

    DaughterCommand daughter_command = {
        .which_cmd = DaughterCommand_topo_tag,
    };
    send_command_init();
    send_command(0, &daughter_command);

    xTaskCreateStatic(daughter_comms_task, "daughter_comms", DAUGHTER_COMMS_TASK_SIZE, NULL, 1,
                      daughter_comms_stack, &daughter_comms_tcb);

    vTaskStartScheduler();

    // Setup uart + dma from host
    recv_command_init();
    MotherCommand mother_command;
    while (1) {
        while (recv_command(&mother_command)) {
            _process_command(&mother_command);
        }

        // Toggle pin 25
        sio_hw->gpio_togl |= 1 << 25;
        sleep_ms(1000);
    }
}

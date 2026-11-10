#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Ensure C specifications are only visible to the compiler, not the assembler */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

/* Core Scheduler Settings */
#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ (150000000UL) /* Default RP2350 Clock is 150MHz */
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES (5)
#define configMINIMAL_STACK_SIZE ((unsigned short)128)
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_QUEUE_SETS 0
#define configUSE_TIME_SLICING 1
#define configUSE_NEWLIB_REENTRANT 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5

/* Memory Allocation */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE ((size_t)(128 * 1024)) /* RP2350 has 520KB SRAM */

/* Hook Functions */
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 1
#define configUSE_MALLOC_FAILED_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* Run Time and Task Stats Gathering */
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* Co-routine Definitions */
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES (2)

/* Software Timer Definitions */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (2)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* ARM Cortex-M33 Architecture Specific Settings */
#define configENABLE_MPU 0
#define configENABLE_TRUSTZONE 0
#define configRUN_FREERTOS_SECURE_ONLY 1
#define configENABLE_FPU 1

/* Interrupt Nesting Behavior */
#define configKERNEL_INTERRUPT_PRIORITY (7 << 5) /* Priority 7 (lowest) */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (16)

/* Define Optional API Functions */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskCleanUpResources 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

/* Map FreeRTOS handlers to standard Cortex-M vectors */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#define configASSERT(x)                                                                            \
    if ((x) == 0) {                                                                                \
        portDISABLE_INTERRUPTS();                                                                  \
        for (;;)                                                                                   \
            ;                                                                                      \
    }

#define configNUMBER_OF_CORES 1

#endif /* FREERTOS_CONFIG_H */

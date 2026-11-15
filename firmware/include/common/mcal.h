#ifndef __COMMON_MCAL_H__
#define __COMMON_MCAL_H__

#include <stdint.h>
#include <stdbool.h>

// whatever hardware functions we need in drivers

// #ifdef __STM32__

// typedef uint8_t McalGpioPin;
// typedef SPIHandle_Typedef McalSpi;

// #else

// typedef int McalGpioPin;

// #endif

#ifdef MCAL_STM32F4
    typedef struct {
        GPIO_TypeDef *port;
        uint16_t pin;
    } McalGpioPin;
#elif defined(MCAL_RP2350)
    typedef int McalGpioPin;
#else
    #error "define mcal_stm32f4 or mcal_rp2350"
#endif

typedef enum {
    MCAL_GPIO_OUT,
    MCAL_GPIO_IN,
} McalGpioDir;

void mcal_gpio_init(McalGpioPin pin);
void mcal_gpio_set_dir(McalGpioPin pin, McalGpioDir dir);
void mcal_gpio_put(McalGpioPin pin, bool value);
void mcal_sleep_ms(uint32_t ms);

#endif

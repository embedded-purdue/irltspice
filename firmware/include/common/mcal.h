#ifndef __COMMON_MCAL_H__
#define __COMMON_MCAL_H__

#include <stdint.h>
#include <stdbool.h>

// whatever hardware functions we need in drivers

#ifdef __STM32__

typedef uint8_t McalGpioPin;
typedef SPIHandle_Typedef McalSpi;

#else

typedef int McalGpioPin;

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

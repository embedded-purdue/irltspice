#ifndef __COMMON_MCAL_H__
#define __COMMON_MCAL_H__

#include <stdint.h>

// whatever hardware functions we need in drivers

#ifdef __STM32__

typedef uint8_t McalGpioPin;
typedef SPIHandle_Typedef McalSpi;

#else

typedef struct {
    uint8_t port;
    uint8_t pin;
} GpioPin;

#endif

void mcal_gpio_init(McalGpioPin pin);

#endif

#include "common/cd22m.h"
#include <stm32f4xx_hal.h>

// Define this as the GPIO bus that the
// daughterboard uses to talk to the CD22M.
#define GPIOx GPIOA

// Define this if the ax and ay pins are contiguous
// so that `GPIOx->BSRR` can be used.
// #define CONTIGUOUS_PINS

const int cd22m_pin_ax[4] = {};
const int cd22m_pin_ay[3] = {};
const int cd22m_pin_data = 0;
const int cd22m_pin_cs = 0;
const int cd22m_pin_strobe = 0;
const int cd22m_pin_reset = 0;

void cd22m_init(void) {
    // we don't need to do MX_GPIO_Init, because it's done in mx_init
}

void cd22m_set_switch(unsigned char ax, unsigned char ay, bool closed) {
#ifdef CONTIGUOUS_PINS
    // Bits 0-15 of BSRR are for "setting".
    // Bits 16-31 of BSRR are for "resetting".
    // Setting and resetting are both done with setting a bit to 1.
    uint32_t not_ax = (~ax & 0b1111), not_ay = (~ay & 0b111);
    uint32_t set = (ax << cd22m_pin_ax[0]) | (ay << cd22m_pin_ay[0]);
    uint32_t reset = (not_ax << cd22m_pin_ax[0]) | (not_ay << cd22m_pin_ay[0]);
    uint32_t bsrr = (reset << 16) | set;
    GPIOx->BSRR = bsrr;
#else
    for (int i = 0; i < 4; i++) {
        HAL_GPIO_WritePin(GPIOx, cd22m_pin_ax[i], (ax >> i) & 1);
    }
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_WritePin(GPIOx, cd22m_pin_ay[i], (ay >> i) & 1);
    }
    HAL_GPIO_WritePin(GPIOx, cd22m_pin_data, closed);
#endif

    HAL_GPIO_WritePin(GPIOx, cd22m_pin_strobe, 0);
    HAL_Delay(CD22M_PRESTROBE_DELAY_MS);
    HAL_GPIO_WritePin(GPIOx, cd22m_pin_strobe, 1);
    HAL_Delay(CD22M_POSTSTROBE_DELAY_MS);
    HAL_GPIO_WritePin(GPIOx, cd22m_pin_strobe, 0);
}

void cd22m_reset(void) {
    HAL_GPIO_WritePin(GPIOx, cd22m_pin_reset, 1);
    HAL_Delay(CD22M_RESET_DELAY_MS);
    HAL_GPIO_WritePin(GPIOx, cd22m_pin_reset, 0);
}

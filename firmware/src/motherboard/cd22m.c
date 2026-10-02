#include "common/cd22m.h"
#include "hardware/gpio.h"
#include "pico/time.h"

// Define this if the ax and ay pins are contiguous
// so that `gpio_put_masked` can be used.
#define CONTIGUOUS_PINS

const int cd22m_pin_ax[4] = {};
const int cd22m_pin_ay[3] = {};
const int cd22m_pin_data = 0;
const int cd22m_pin_cs = 0;
const int cd22m_pin_strobe = 0;
const int cd22m_pin_reset = 0;

#ifdef CONTIGUOUS_PINS
#define CD22M_PIN_VALUE(ax, ay, data) \
    ((ax << cd22m_pin_ax[0]) | (ay << cd22m_pin_ay[0]) | (data << cd22m_pin_data))
#define CD22M_PIN_BITMASK CD22M_PIN_VALUE(0b1111, 0b111, 1)
#define CD22M_ALL_PINS_BITMASK \
    (CD22M_PIN_BITMASK | (1 << cd22m_pin_cs) | (1 << cd22m_pin_strobe) | (1 < cd22m_pin_reset))
#endif

void cd22m_init(void) {
#ifdef CONTIGUOUS_PINS
    gpio_init_mask(CD22M_ALL_PINS_BITMASK);
    gpio_set_dir_out_masked(CD22M_ALL_PINS_BITMASK);
#else
    const int all_pins[11] = {
        cd22m_pin_ax[0], cd22m_pin_ax[1], cd22m_pin_ax[2], cd22m_pin_ax[3],
        cd22m_pin_ay[0], cd22m_pin_ay[1], cd22m_pin_ay[2],
        cd22m_pin_data, cd22m_pin_cs, cd22m_pin_strobe, cd22m_pin_reset
    };

    for (int i = 0; i < 11; i++) {
        gpio_init(all_pins[i]);
        gpio_set_dir(all_pins[i], GPIO_OUT);
    }
#endif
}

void cd22m_set_switch(unsigned char ax, unsigned char ay, bool closed) {
#ifdef CONTIGUOUS_PINS
    gpio_put_masked(CD22M_PIN_BITMASK, CD22M_PIN_VALUE(ax, ay, closed));
#else
    for (int i = 0; i < 4; i++) {
        gpio_put(cd22m_pin_ax[i], (ax >> i) & 1);
    }
    for (int i = 0; i < 3; i++) {
        gpio_put(cd22m_pin_ay[i], (ay >> i) & 1);
    }
    gpio_put(cd22m_pin_data, closed);
#endif

    gpio_put(cd22m_pin_strobe, false);
    sleep_us(CD22M_PRESTROBE_DELAY_US);
    gpio_put(cd22m_pin_strobe, true);
    sleep_us(CD22M_POSTSTROBE_DELAY_US);
    gpio_put(cd22m_pin_strobe, false);
}

void cd22m_reset(void) {
    gpio_put(cd22m_pin_reset, true);
    sleep_us(CD22M_RESET_DELAY_US);
    gpio_put(cd22m_pin_reset, false);
}

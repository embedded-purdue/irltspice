#include "common/cd22m.h"
#include "hardware/gpio.h"
#include "pico/time.h"

const int cd22m_pin_ax[4] = {};
const int cd22m_pin_ay[3] = {};
const int cd22m_pin_data = -1;
const int cd22m_pin_cs = -1;
const int cd22m_pin_strobe = -1;
const int cd22m_pin_reset = -1;

void cd22m_init(void) {
    const int all_pins[10] = {
        cd22m_pin_ax[0], cd22m_pin_ax[1], cd22m_pin_ax[2], cd22m_pin_ax[3],
        cd22m_pin_ay[0], cd22m_pin_ay[1], cd22m_pin_ay[2],
        cd22m_pin_data, cd22m_pin_cs, cd22m_pin_strobe
    };

    for (int i = 0; i < 10; i++) {
        gpio_init(all_pins[i]);
        gpio_set_dir(all_pins[i], GPIO_OUT);
    }
}

void cd22m_set_switch(unsigned char ax, unsigned char ay, bool closed) {
    for (int i = 0; i < 4; i++) {
        gpio_put(cd22m_pin_ax[i], (ax >> i) & 1);
    }
    for (int i = 0; i < 3; i++) {
        gpio_put(cd22m_pin_ay[i], (ay >> i) & 1);
    }
    gpio_put(cd22m_pin_data, closed);

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

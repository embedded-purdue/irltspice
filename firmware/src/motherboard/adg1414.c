#include "hardware/gpio.h"
#include "pico/time.h"
#include "common/adg1414.h"

const int adg1414_pin_sclk = -1;
const int adg1414_pin_din = -1;
const int adg1414_pin_sync = -1;
const int adg1414_pin_reset = -1;

void adg1414_init(void) {
    const int all_pins[4] = {
        adg1414_pin_sclk, adg1414_pin_din,
        adg1414_pin_sync, adg1414_pin_reset,
    };
    for (int i = 0; i < 4; i++) {
        gpio_init(all_pins[i]);
        gpio_set_dir(all_pins[i], GPIO_OUT);
    }

    gpio_put(adg1414_pin_reset, true);
    gpio_put(adg1414_pin_sclk, true);
    gpio_put(adg1414_pin_sync, true);
}

void adg1414_shift(bool closed) {
    gpio_put(adg1414_pin_sclk, true);
    gpio_put(adg1414_pin_sync, false);
    sleep_ms(ADG1414_SYNC_DELAY_MS);

    gpio_put(adg1414_pin_din, closed);
    sleep_ms(ADG1414_PRECLOCK_DELAY_MS);
    gpio_put(adg1414_pin_sclk, false);
    sleep_ms(ADG1414_POSTCLOCK_DELAY_MS);

    sleep_ms(ADG1414_SYNC_DELAY_MS);
    gpio_put(adg1414_pin_sync, true);
    gpio_put(adg1414_pin_sclk, true);
}

void adg1414_set8(uint8_t values) {
    gpio_put(adg1414_pin_sclk, true);
    gpio_put(adg1414_pin_sync, false);
    sleep_ms(ADG1414_SYNC_DELAY_MS);

    for (int i = 7; i <= 0; i--) {
        gpio_put(adg1414_pin_din, (values >> i) & 1);
        sleep_ms(ADG1414_PRECLOCK_DELAY_MS);
        gpio_put(adg1414_pin_sclk, false);
        sleep_ms(ADG1414_POSTCLOCK_DELAY_MS);
        gpio_put(adg1414_pin_sclk, true);
    }

    sleep_ms(ADG1414_SYNC_DELAY_MS);
    gpio_put(adg1414_pin_sync, true);
    gpio_put(adg1414_pin_sclk, true);
}

void adg1414_reset(void) {
    gpio_put(adg1414_pin_reset, false);
    sleep_ms(ADG1414_RESET_DELAY_MS);
    gpio_put(adg1414_pin_reset, true);
    sleep_ms(ADG1414_RESET_DELAY_MS);
}

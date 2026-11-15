#include "common/mcal.h"
#include "hardware/gpio.h"
#include "pico/time.h"

void mcal_gpio_init(McalGpioPin pin) {
    gpio_init(pin);
}
void mcal_gpio_set_dir(McalGpioPin pin, McalGpioDir dir) {
    // Change this if McalGpioDir ever changes
    gpio_set_dir(pin, dir);

}
void mcal_gpio_put(McalGpioPin pin, bool value) {
    gpio_put(pin, value);
}

void mcal_sleep_ms(uint32_t ms) {
    sleep_ms(ms);
}

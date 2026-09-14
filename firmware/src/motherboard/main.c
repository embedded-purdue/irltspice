#include "hardware/gpio.h"
#include "pico/time.h"

int main(void) {
  // Setup pin 25 as gpio
  gpio_init(24);

  // Set pin 25 as output
  gpio_set_dir(24, GPIO_OUT);

  while (1) {
    // Toggle pin 25
    sio_hw->gpio_togl |= 1 << 24;
    sleep_ms(1000);
  }
}

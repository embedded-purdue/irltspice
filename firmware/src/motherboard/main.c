#include "hardware/gpio.h"
#include "pico/time.h"

#include "command.pb.h"

int main(void) {

  // Example protobuf struct. Type defined in proto/command.proto
  Command command = {.command_type = CommandType_TOPOLOGY_REQUEST};
  (void)command;

  // Setup pin 25 as gpio
  gpio_init(25);

  // Set pin 25 as output
  gpio_set_dir(25, GPIO_OUT);

  while (1) {
    // Toggle pin 25
    sio_hw->gpio_togl |= 1 << 25;
    sleep_ms(1000);
  }
}

#include "hardware/gpio.h"
#include "pico/time.h"

#include "command.pb.h"
#include "motherboard/recv_commands.h"

static void _process_command(MotherCommand *command) {
    (void)command; // nop
}

int main(void) {
    recv_command_init();

    MotherCommand command = MotherCommand_init_default;

    // Example protobuf struct. Type defined in proto/command.proto
    MotherResponse response = {.which_data = MotherResponse_stop_tag,
                               .data.stop = {.complete_count = 5}};
    (void)response;

    // Setup pin 25 as gpio
    gpio_init(25);

    // Set pin 25 as output
    gpio_set_dir(25, GPIO_OUT);

    while (1) {
        while (recv_command(&command)) {
            _process_command(&command);
        }

        // Toggle pin 25
        sio_hw->gpio_togl |= 1 << 25;
        sleep_ms(1000);
    }
}

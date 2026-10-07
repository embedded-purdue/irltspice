#include "command.pb.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "motherboard/recv_commands.h"
#include "motherboard/send_commands.h"

static void _process_command(MotherCommand *command) {
    (void)command; // nop
}

int main(void) {
    // Setup pin 25 as gpio
    gpio_init(25);

    // Set pin 25 as output
    gpio_set_dir(25, GPIO_OUT);

    DaughterCommand daughter_command = {
        .which_cmd = DaughterCommand_topo_tag,
    };
    send_command_init();
    send_command(0, &daughter_command);

    // Setup uart + dma from host
    recv_command_init();
    MotherCommand mother_command;
    while (1) {
        while (recv_command(&mother_command)) {
            _process_command(&mother_command);
        }

        // Toggle pin 25
        sio_hw->gpio_togl |= 1 << 25;
        sleep_ms(1000);
    }
}

#include "main.h"
#include "daughterboard/recv_commands.h"
#include "stm32f4xx_hal_gpio.h"

int main(void) {
    mx_init();

    DaughterCommand command;

    for (;;) {
        while (recv_command(&command)) {
            switch (command.which_cmd) {
            case DaughterCommand_topo_tag:
                HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                break;
            default:
                break;
            }
            HAL_Delay(10);
        }
    }
}

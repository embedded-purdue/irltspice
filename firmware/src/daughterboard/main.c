#include "main.h"
#include "command.pb.h"
#include "daughterboard/recv_commands.h"
#include "daughterboard/send_response.h"
#include "stm32f4xx_hal_gpio.h"

int main(void) {
    mx_init();

    // TODO: initialize / reset all ICs

    recv_command_init();
    send_response_init();
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

    DaughterResponse response;

    DaughterCommand command;
    for (;;) {
        while (recv_command(&command)) {
            switch (command.which_cmd) {
            case DaughterCommand_topo_tag:
                HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                response.which_data = DaughterCommand_topo_tag;
                response.data.topo =
                    (DaughterboardTopology){.unique_id = 5,
                                            .firm_version_major = 0,
                                            .firm_version_minor = 0,
                                            .firm_version_patch = 0,
                                            .which_info = DaughterboardTopology_basic_daughter_tag,
                                            .info.basic_daughter = (BasicDaughterboard){}};
                send_response(&response);
                break;
            default:
                break;
            }
            HAL_Delay(10);
        }
    }
}

#ifndef __MOTHERBOARD_SEND_COMMANDS_H__
#define __MOTHERBOARD_SEND_COMMANDS_H__

#include "command.pb.h"

void send_command_init();
bool send_command(uint8_t daughter_id, DaughterCommand *command);

#endif // __MOTHERBOARD_SEND_COMMANDS_H__

#ifndef __MOTHERBOARD_RECV_COMMANDS_H__
#define __MOTHERBOARD_RECV_COMMANDS_H__

#include "command.pb.h"

void recv_command_init();
bool recv_command(MotherCommand *command);

#endif // __MOTHERBOARD_RECV_COMMANDS_H__

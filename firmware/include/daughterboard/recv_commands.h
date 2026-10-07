#ifndef __DAUGHTERBOARD_RECV_COMMANDS_H__
#define __DAUGHTERBOARD_RECV_COMMANDS_H__

#include "command.pb.h"

void recv_command_init();
bool recv_command(DaughterCommand *command);

#endif // __DAUGHTERBOARD_RECV_COMMANDS_H__

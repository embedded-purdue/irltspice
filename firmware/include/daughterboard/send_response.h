#ifndef __DAUGHTERBOARD_SEND_RESPONSE_H__
#define __DAUGHTERBOARD_SEND_RESPONSE_H__

#include "command.pb.h"

void send_response_init();
bool send_response(DaughterResponse *command);

#endif // __DAUGHTERBOARD_SEND_RESPONSE_H__

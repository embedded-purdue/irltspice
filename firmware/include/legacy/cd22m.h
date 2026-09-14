#ifndef __CD22M_H__
#define __CD22M_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void enable_connection(uint8_t x_pin, uint8_t y_pin);
void disable_connection(uint8_t x_pin, uint8_t y_pin);
void reset_crossbars(void);
void strobe_crossbars(void);

#ifdef __cplusplus
}
#endif

#endif /* __CD22M_H__ */
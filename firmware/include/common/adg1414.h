#ifndef __COMMON_ADG1414_H__
#define __COMMON_ADG1414_H__

#include <stdbool.h>
#include <stdint.h>

// The ADG1414's shift register is MSB-first.
// This means that when a single value is
// shifted in, that becomes the value for Switch 1.
// When 8 values are shifted in,
// the first one becomes the value for Switch 8,
// and the last one becomes the value for Switch 1.

#define ADG1414_RESET_DELAY_MS 1
#define ADG1414_SYNC_DELAY_MS 1
#define ADG1414_PRECLOCK_DELAY_MS 1
#define ADG1414_POSTCLOCK_DELAY_MS 1

extern const int adg1414_pin_sclk;
extern const int adg1414_pin_din;
extern const int adg1414_pin_sync; // active-low
extern const int adg1414_pin_reset; // active-low

void adg1414_init(void);
void adg1414_reset(void);
void adg1414_shift(bool closed); // Shift in a value
// Set all 8 values.
// Bit 7 corresponds to Switch 8, Bit 0 corresponds to Switch 1
void adg1414_set8(uint8_t values);

#endif // __COMMON_ADG1414_H__

#ifndef __COMMON_CD22M_H__
#define __COMMON_CD22M_H__

#include <stdbool.h>

// CD22M is really easy to understand.
// It is controlled by the pins:
// AX0 to AX3 (number from 0 to 15),
// AY0 to AY2 (number from 0 to 7),
// DATA (1 (closed switch) or 0 (open switch)),
// CS (Chip Select), and STROBE.

#define CD22M_PRESTROBE_DELAY_US 1
#define CD22M_POSTSTROBE_DELAY_US 1
#define CD22M_RESET_DELAY_US 1

extern const int cd22m_pin_ax[4];
extern const int cd22m_pin_ay[3];
extern const int cd22m_pin_data, cd22m_pin_cs, cd22m_pin_strobe, cd22m_pin_reset;

void cd22m_init(void);
void cd22m_set_switch(unsigned char ax, unsigned char ay, bool closed);
void cd22m_reset(void); // set all switches to open

#endif // !__COMMON_CD22M_H__ (should we have the exclamation mark or not?)

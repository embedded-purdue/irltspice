#ifndef __SINGLE_CELL_H__
#define __SINGLE_CELL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_spi.h"

void program_single_cell_pot(int bruz_num, int pot_num, uint8_t res, SPI_HandleTypeDef *hspi);
void enable_single_cell_connection(int cd22m_num, uint8_t x_pin, uint8_t y_pin);
void setup_single_cell_gpios(void);
void reset_single_cell_cd22m(void);
void reset_single_cell_bruz(void);

#ifdef __cplusplus
}
#endif

#endif /* __SINGLE_CELL_H__ */
#ifndef __BRUZ200_H__
#define __BRUZ200_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_spi.h"
#include <stdint.h>

void program_bruz(uint8_t bruz_num, uint8_t addr, uint8_t res, SPI_HandleTypeDef *hspi);
void program_pot(uint8_t pot_num, uint8_t res, SPI_HandleTypeDef *hspi);
void set_resistance(uint8_t addr, uint8_t res, SPI_HandleTypeDef *hspi);
void config_pots(uint8_t *pot_resistances, SPI_HandleTypeDef *hspi);
void reset_pots(void);
// uint8_t read_register(MCP4241_addr_t, SPI_HandleTypeDef *hspi);
// void write_register(MCP4241_addr_t, uint16_t, SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif /* __BRUZ200_H__ */
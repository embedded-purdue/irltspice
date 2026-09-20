#include "bruz200.h"

#include "stm32f0xx_hal.h"
#include "shift_registers.h"
#include "stm32f0xx_hal_spi.h"

#define TRANSMIT_DELAY  10
#define ONE_BYTE_CMD    1
#define ADDR_MASK       0b11
#define RES_OFFSET      1

uint8_t pot_order[24] = {
  0, 1, 2, 3,
  12, 13, 14, 15,
  4, 5, 16, 17,
  6, 7, 8, 9,
  18, 19, 20, 21,
  10, 11, 22, 23
};

uint8_t bruz_order[8] = {
  0, 4, 1, 5, 2, 6, 3, 7
};

#define NUM_OF_POTS 24

void program_pot(uint8_t pot_num, uint8_t res, SPI_HandleTypeDef *hspi) {
  uint8_t bruz_num;
  uint8_t addr;

  /*
  0-3   -> 0, 0-3
  4-5   -> 1, 0/2
  6-9   -> 2  0-3
  10-11 -> 3  0/2
  12-15 -> 4  ...
  16-17 -> 5
  18-21 -> 6
  22-23 -> 7
  */

  if ((pot_num % 6) < 4) {
    bruz_num = (pot_num / 6) * 2;
    addr = (pot_num % 6);
  } else {
    bruz_num = ((pot_num / 6) * 2) + 1;
    addr = (((pot_num % 6) - 4) * 2);
  }

  program_bruz(bruz_num, addr, res, hspi);

}

void program_bruz(uint8_t bruz_num, uint8_t addr, uint8_t res, SPI_HandleTypeDef *hspi) {
  sr_reset(BRUZ_SR);
  sr_start(BRUZ_SR);

  for (int i = 0; i < 7; i++ ) {
    if (bruz_order[i] != bruz_num) { sr_clock(BRUZ_SR); }
    else { break; }
  }
  set_resistance(addr, res, hspi);
  sr_shift_en(BRUZ_SR);
}

void set_resistance(uint8_t addr, uint8_t res, SPI_HandleTypeDef *hspi) {
  uint8_t tx[2];

  // {wiper_addr[3:0], 4'b0}
  // {write_data[7:0]}
  tx[0] = (uint8_t)(res << RES_OFFSET);
  tx[1] = (uint8_t)(addr & ADDR_MASK);
  HAL_SPI_Transmit(hspi, tx, 1, TRANSMIT_DELAY);

  // while (hspi->State == HAL_SPI_STATE_BUSY_TX);
}

void reset_pots(void) {
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 0);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 1);
}

void config_pots(uint8_t *pot_resistances, SPI_HandleTypeDef *hspi) {
  for (int i = 0; i < NUM_OF_POTS; i++) {
    if (pot_resistances[i] != 0xFF) {
      program_pot(i, pot_resistances[i], hspi);
    }
  }
}

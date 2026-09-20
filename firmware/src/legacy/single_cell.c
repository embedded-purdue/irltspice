#include "single_cell.h"
#include "bruz200.h"

static uint8_t x_addr_single_cell[16] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 6, 7, 14, 15};

void program_single_cell_pot (int bruz_num, int pot_num, uint8_t res, SPI_HandleTypeDef *hspi) {
  if (bruz_num == 0) HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 0);
  else               HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);

  set_resistance(pot_num, res, hspi);

  if (bruz_num == 0) HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, 1);
  else               HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
}

void reset_single_cell_bruz(void) {
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 0);
  HAL_Delay(10);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 1);
}

void reset_single_cell_cd22m(void) {
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, 1);
  HAL_Delay(10);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, 0);
}

void enable_single_cell_connection(int cd22m_num, uint8_t x_pin, uint8_t y_pin) {
  // turning off all address bits, strobe, and data
  // GPIOB->ODR &= ~(0xFF80);

  if (cd22m_num == 0) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 1);
  else                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, 1);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, 1); // DATA

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, (x_addr_single_cell[x_pin] >> 3) & 0b1); // AX3
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14 , (x_addr_single_cell[x_pin] >> 2) & 0b1); // AX2
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13 , (x_addr_single_cell[x_pin] >> 1) & 0b1); // AX1
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, (x_addr_single_cell[x_pin] >> 0) & 0b1); // AX0

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, (y_pin >> 2) & 0b1); // AY2
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, (y_pin >> 1) & 0b1); // AY1
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9 , (y_pin >> 0) & 0b1); // AY0

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,  1); // STROBE
  HAL_Delay(10);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,  0);

  if (cd22m_num == 0) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, 0);
  else                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, 0);
}

void setup_single_cell_gpios(void) {
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef GPIOB_InitStruct = {0};
  GPIOB_InitStruct.Pin =  GPIO_PIN_3  | GPIO_PIN_15 |
                          GPIO_PIN_6  | GPIO_PIN_7  | GPIO_PIN_8  |
                          GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_11 |
                          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
  GPIOB_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIOB_InitStruct.Pull = GPIO_NOPULL;
  GPIOB_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIOB_InitStruct);

  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIOA_InitStruct = {0};
  GPIOA_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                         GPIO_PIN_10;
  GPIOA_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIOA_InitStruct.Pull = GPIO_NOPULL;
  GPIOA_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIOA_InitStruct);

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 1); // active lows
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, 1);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, 1);

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, 0); // active highs
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 0);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, 0);

}
#include "cd22m.h"
#include "stm32f0xx_hal.h"

uint8_t x_addr[16] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 6, 7, 14, 15};

void enable_connection(uint8_t x_pin, uint8_t y_pin) {
  // turning off all address bits, strobe, and data
  GPIOB->ODR &= ~(0xFF80);

  // turning on correct address bits and data
  // GPIOB->ODR |= ((address << 9) | 0x0100);
  GPIOB->ODR |= ((x_addr[x_pin] & 0b1111) << 12) | ((y_pin & 0b111) << 9) | 0x0100; 

  // STROBE
  strobe_crossbars();
}

void disable_connection(uint8_t x_pin, uint8_t y_pin) {
  // turning off all address bits, strobe, and data
  GPIOB->ODR &= ~(0xFF80);

  // turning on correct address bits
  // GPIOB->ODR |= (address << 9);
  GPIOB->ODR |= ((x_addr[x_pin] & 0b1111) << 12) | ((y_pin & 0b111) << 9); 

  // STROBE
  strobe_crossbars();
}

void reset_crossbars(void) {
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, 1);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, 0);
}

void strobe_crossbars(void) {
  GPIOB->ODR |= 0x0080;
  HAL_Delay(5);
  GPIOB->ODR &= ~(0x0080);
}

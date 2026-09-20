#include "shift_registers.h"
#include "stm32f0xx_hal.h"

void sr_reset(sr_t sr) {
  if (sr == BRUZ_SR) {
    // bruz is ~cs
    sr_set(sr, 1);
    for (int i = 0; i < 8; i++) sr_clock(sr);
  } else {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 0);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 1);
  }
}

void sr_set(sr_t sr, int data) {
  if (sr == BRUZ_SR)  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, data);
  else                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, data);
}

void sr_clock(sr_t sr) {
  if (sr == BRUZ_SR) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, 1);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, 0);
  } else {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, 1);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, 0);
  }
}

void sr_start(sr_t sr) {
  if (sr == BRUZ_SR) {
    sr_set(sr, 0);
    sr_clock(sr);
    sr_set(sr, 1);
  } else {
    sr_set(sr, 1);
    sr_clock(sr);
    sr_set(sr, 0);
  }

}

void sr_shift_en(sr_t sr) {
  sr_clock(sr);
}

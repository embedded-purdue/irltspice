#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_uart.h"
#include "stm32f0xx_hal_spi.h"
#include "init.h"

#define STALL while (1) {}

void setup_spi(SPI_HandleTypeDef *hspi)
{
  __HAL_RCC_SPI1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // gpio config
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin           = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode          = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull          = GPIO_NOPULL;
  GPIO_InitStruct.Speed         = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate     = GPIO_AF0_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // spi config
  hspi->Instance                = SPI1;
  hspi->Init.Mode               = SPI_MODE_MASTER;
  hspi->Init.Direction          = SPI_DIRECTION_2LINES;
  hspi->Init.DataSize           = SPI_DATASIZE_10BIT;
  hspi->Init.CLKPolarity        = SPI_POLARITY_LOW;
  hspi->Init.CLKPhase           = SPI_PHASE_1EDGE;
  hspi->Init.NSS                = SPI_NSS_SOFT;
  hspi->Init.BaudRatePrescaler  = SPI_BAUDRATEPRESCALER_256;
  hspi->Init.FirstBit           = SPI_FIRSTBIT_MSB;
  hspi->Init.TIMode             = SPI_TIMODE_DISABLE;
  hspi->Init.CRCCalculation     = SPI_CRCCALCULATION_DISABLE;

  if (HAL_SPI_Init(hspi) != HAL_OK) { STALL }

  SET_BIT(SPI1->CR2, SPI_CR2_FRXTH);
}

void setup_gpios(void) {
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef GPIOB_InitStruct = {0};
  GPIOB_InitStruct.Pin =  GPIO_PIN_3  |
                          GPIO_PIN_4  | GPIO_PIN_5  | GPIO_PIN_6  |
                          GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  |
                          GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                          GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  GPIOB_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIOB_InitStruct.Pull = GPIO_NOPULL;
  GPIOB_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIOB_InitStruct);

  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIOA_InitStruct = {0};
  GPIOA_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
  GPIOA_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIOA_InitStruct.Pull = GPIO_NOPULL;
  GPIOA_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIOA_InitStruct);

  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef GPIOC_InitStruct = {0};
  GPIOC_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_5;
  GPIOC_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIOC_InitStruct.Pull = GPIO_NOPULL;
  GPIOC_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIOC_InitStruct);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 0);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 1);  // active low resets
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, 1);

  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, 0);

}


void internal_clock()
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL12;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV2;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1);

  while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)RCC_CFGR_SWS_PLL);
  
  // fix hal timing
  SystemCoreClock = 48000000;
  HAL_InitTick(TICK_INT_PRIORITY);
}

void setup_uart(void)
{
  RCC->AHBENR |= 0x00180000;
  GPIOC->MODER |= 0x02000000;
  GPIOC->AFR[1] |= 0x00020000;
  GPIOD->MODER |= 0x00000020;
  GPIOD->AFR[0] |= 0x00000200;
  RCC->APB1ENR |= 0x00100000;
  USART5->CR1 &= ~0x00000001;
  USART5->CR1 |= 0x00008000;
  USART5->BRR = 0x340;
  USART5->CR1 |= 0x0000000c;
  USART5->CR1 |= 0x00000001;
}

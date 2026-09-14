// my libs
#include "main.h"
#include "init.h"
#include "bruz200.h"
#include "cd22m.h"
#include "shift_registers.h"
#include "single_cell.h"

// c libs
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define REAL_BOARD
#define REAL_CONFIG
// #define MEM_DUMP

#define WAIT_FOR_UART_TX while (!(USART5->ISR & USART_ISR_TXE))
#ifdef REAL_BOARD
  #define NUM_OF_CROSSBARS      9
  #define NUM_OF_POTS           24
#endif
#ifdef SINGLE_CELL
  #define NUM_OF_CROSSBARDS     2
  #define NUM_OF_POTS           6
#endif
#define NUM_OF_CROSSBAR_CONS  128
#define MAX_POT_RES           128
#define ERROR_DELAY           2000

uint8_t crossbar_order[9] = {
  0, 3, 6,
  1, 4, 7,
  2, 5, 8
};

int main(void)
{
  SPI_HandleTypeDef hspi;
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);

  HAL_Init();
  internal_clock();
  setup_uart();
  setup_spi(&hspi);
  setup_gpios();

  // var inits for loop
  board_state_t state = IDLE;
  uint8_t crossbar_cons[NUM_OF_CROSSBARS][NUM_OF_CROSSBAR_CONS] = {0};
  uint8_t pot_resistances[NUM_OF_POTS] = {0};
  uint8_t rx;

  uint8_t cur_cb;
  uint8_t cur_pot;

  reset_crossbars();
  reset_pots();

  while (true) {  

    if (state == ERROR_STATE) {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
    }

    if (state == IDLE) {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, 1);
    }

    if ((USART5->ISR & USART_ISR_RXNE)) {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 0);
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, 0);
      rx = USART5->RDR;

      if (rx == RESET_CONFIG) {
        state = IDLE;

        memset(crossbar_cons,   0, sizeof(crossbar_cons));
        memset(pot_resistances, 1, sizeof(pot_resistances));

        reset_crossbars();
        reset_pots();
        sr_reset(BRUZ_SR);
        sr_reset(CD22M_SR);

        WAIT_FOR_UART_TX;
        USART5->TDR = RESET_SUCCESS;
        continue;
      }

      switch (state) {
        case IDLE:
          if (rx == START_CONFIG) { state = STARTING; }
          else                    { state = ERROR_STATE; }
          break;
        
        case ERROR_STATE:
          WAIT_FOR_UART_TX;
          USART5->TDR = UART_ERROR;
          break;
        
        case STARTING:
          if (rx == START_CONFIG) { 
            WAIT_FOR_UART_TX;
            USART5->TDR = READY_TO_START;
            state = UART_CONFIG;
          } else {
            state = ERROR_STATE;
          }
          break;

        case UART_CONFIG:
          if (get_prefix(rx) == START_CB) { 
            cur_cb = get_message(rx);
            state = CHOOSE_CB_CONNS;
          } else if (get_prefix(rx) == START_POT) {
            cur_pot = get_message(rx);
            state = CHOOSE_POT_RES;
          } else if (rx == END_CONFIG) {
            // HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, 1);

            reset_crossbars();
            reset_pots();

            sr_reset(BRUZ_SR);
            sr_reset(CD22M_SR);

            WAIT_FOR_UART_TX;
            USART5->TDR = CONFIG_SUCCESS;

            #ifdef MEM_DUMP
              for (int i = 0; i < NUM_OF_CROSSBARS; i++) {
                WAIT_FOR_UART_TX;
                USART5->TDR = i;
                for (int j = 0; j < NUM_OF_CROSSBAR_CONS; j++) {
                  if (crossbar_cons[i][j]) {
                    WAIT_FOR_UART_TX;
                    USART5->TDR = j;
                  }
                }
                WAIT_FOR_UART_TX;
                USART5->TDR = '\n';
              }

              for (int i = 0; i < NUM_OF_POTS; i++) {
                if (pot_resistances[i]) {
                  WAIT_FOR_UART_TX;
                  USART5->TDR = i;
                  WAIT_FOR_UART_TX;
                  USART5->TDR = pot_resistances[i];
                  WAIT_FOR_UART_TX;
                  USART5->TDR = '\n';
                }
              }
            #endif
            
            #ifdef REAL_CONFIG
            config_pots(pot_resistances, &hspi);

            sr_start(CD22M_SR);
            for (int i = 0; i < NUM_OF_CROSSBARS; i++) {
              for (int j = 0; j < NUM_OF_CROSSBAR_CONS; j++) {
                if (crossbar_cons[crossbar_order[i]][j]) {
                  enable_connection(get_x(j), get_y(j));
                }
              }
              sr_shift_en(CD22M_SR);
            }
            state = IDLE;

            WAIT_FOR_UART_TX;
            #endif

          } else {
            state = ERROR_STATE;
          }
          break;

        case CHOOSE_CB_CONNS:
          if (rx < NUM_OF_CROSSBAR_CONS) {
            crossbar_cons[cur_cb][rx] = 1;
          }
          else if (rx == END_CB) { 
            state = UART_CONFIG;
            WAIT_FOR_UART_TX;
            USART5->TDR = RETURN_TO_CONFIG;
          }
          else { 
            state = ERROR; 
          }
          break;

        case CHOOSE_POT_RES: 
          if (rx < MAX_POT_RES) {
            pot_resistances[cur_pot] = rx;
            USART5->TDR = RETURN_TO_CONFIG;
            state = UART_CONFIG;
          } else { 
            state = ERROR_STATE;
          }
          break;
        default: break;
      }
    }
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

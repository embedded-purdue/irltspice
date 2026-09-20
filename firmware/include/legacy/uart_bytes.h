#ifndef __UART_BYTES_H__
#define __UART_BYTES_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// irlc -> micro
#define START_CONFIG 0b10000000
#define END_CONFIG 0b10000001
#define START_CB 0b110 // last 5 bits are 0-8 to decide which crossbar to config
#define END_CB 0b10000011
#define START_POT 0b101 // last 5 bits are 0-23 to decide which pot to config
#define END_POT 0b10000101
#define RESET_CONFIG 0b10011111
#define RETURN_TO_CONFIG 0b10011100

// micro -> irlc
#define READY_TO_START 0b10000110
#define CONFIG_SUCCESS 0b10001000
#define UART_ERROR 0b11111111
#define RESET_SUCCESS 0b10010101

typedef uint8_t instruction_t;
static inline instruction_t init_instr(uint8_t prefix, uint8_t message) {
  return (prefix << 5) | (message & 0x1F);
}
static inline uint8_t get_prefix(instruction_t instr) { return instr >> 5; }
static inline uint8_t get_message(instruction_t instr) { return instr & 0x1F; }

typedef uint8_t connection_t;
static inline connection_t init_connetion(uint8_t x_pin, uint8_t y_pin) {
  return ((y_pin & 0x7) << 4) | (x_pin & 0xF);
}
static inline uint8_t get_x(connection_t conn) { return conn & 0xF; }
static inline uint8_t get_y(connection_t conn) { return (conn >> 4) & 0x7; }

typedef enum {
  IDLE,
  ERROR_STATE,
  STARTING,
  UART_CONFIG,
  CHOOSE_CB_CONNS,
  CHOOSE_POT_RES,
  POST_POT_CONFIG,
  POST_BOARD_CONFIG,
  ANALOG_CONFIG
} board_state_t;

#ifdef __cplusplus
}
#endif

#endif /* __UART_BYTES_H__ */

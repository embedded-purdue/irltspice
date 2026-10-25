#ifndef MCP_I2C_H
#define MCP_I2C_H

#include "hardware/i2c.h"

typedef struct {
    i2c_inst_t *i2c;
} MCP_I2C;

int address_select(int pot);
void init_mcp_i2c(int sda, int scl, i2c_inst_t* i2c, int baudrate, MCP_I2C* mcp);
void set_resistance(int ohms, MCP_I2C* mcp, int pot);
int address_select(int pot);

#endif
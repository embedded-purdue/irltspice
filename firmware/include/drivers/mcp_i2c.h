#ifndef __DRIVERS_MCP_I2C_H__
#define __DRIVERS_MCP_I2C_H__

#include "hardware/i2c.h"

typedef struct {
    i2c_inst_t *i2c;
} MCP_I2C;

void mcp_i2c_init(int sda, int scl, i2c_inst_t *i2c, int baudrate, MCP_I2C *mcp);
void mcp_i2c_set_resistance(int ohms, MCP_I2C *mcp, int pot);

#endif

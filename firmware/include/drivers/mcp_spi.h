#ifndef MCP_SPI_H
#define MCP_SPI_H

#include "hardware/spi.h"

typedef struct {
    spi_inst_t *spi;
} MCP_SPI;

int address_select(int pot);
void init_mcp_spi(int sck, int sda, int sdo, int cs, int baudrate, spi_inst_t* spi, MCP_SPI* mcp);
void set_resistance(int ohms, MCP_SPI* mcp, int pot);
void reset_resistance(MCP_SPI* mcp, int pot);
int address_select(int pot);

#endif
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "mcp_spi.h"

int main(void) {

    int spi_sck = 2;
    int spi_csn = 1;
    int spi_sda = 0;
    int spi_sdo = 3;
    MCP_SPI mcp_spi;
    init_mcp_spi(spi_sck, spi_sda, spi_sdo, spi_csn, 5000000, spi0, &mcp_spi);

    for (int i = 0; i < 3; i++) {
        set_resistance(25000 * i, &mcp_spi, i);
    }
}
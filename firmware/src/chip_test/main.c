#include "cd22m_test.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "mcp_i2c.h"
#include "mcp_spi.h"

int main(void) {

    int spi_sck = 2;
    int spi_csn = 1;
    int spi_sda = 0;
    int spi_sdo = 3;
    MCP_SPI mcp_spi;
    init_mcp_spi(spi_sck, spi_sda, spi_sdo, spi_csn, 5000000, spi0, &mcp_spi);

    int i2c_sda = 28;
    int i2c_scl = 29;

    MCP_I2C mcp_i2c;
    mcp_i2c_init(i2c_sda, i2c_scl, i2c0, 100000, &mcp_i2c);
    mcp_i2c_set_resistance(40000, &mcp_i2c, 3);

    for (int i = 0; i < 3; i++) {
        set_resistance(25000 * i, &mcp_spi, i);
    }

    cd22m_test();
}

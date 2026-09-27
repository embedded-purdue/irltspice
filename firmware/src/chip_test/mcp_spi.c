#include "hardware/spi.h"
#include "hardware/gpio.h"

void init_mcp_spi(int sck, int sda, int sdo, int cs, int baudrate, spi_inst_t* spi) {
    gpio_set_function(sck, 1);
    gpio_set_function(sda, 1);
    gpio_set_function(cs, 1);
    gpio_set_function(sdo, 1);
    spi_init(spi, baudrate);
    spi_set_format(spi, 16, 0, 0, SPI_MSB_FIRST);
}

void set_resistance(int ohms, spi_inst_t* spi, int pot){
/*
16 bits
15:12 - address for which pot to change
  - 00h: volatile pot 0
  - 01h: volatile pot 1
  - 06h: volatile pot 2
  - 07h: volatile pot 3
11:10 - command
  - 00: write
  - 01: increment
  - 10: decrement
  - 11: read
9:0 - data bits
  - d9 always unused
  - 8:0 used from 0x100 to 0x000, 256 steps
  - 0 to 100k but adds 75 due to internal resistance
*/
    int address = address_select(pot);
    int step = (ohms * 256 + 50000) / 100000;
    uint16_t src = 0;

    src |= address << 12;
    src |= step;

    spi_write16_blocking(spi, &src, 1);
}

void reset_resistance(spi_inst_t* spi, int pot){
    int address = address_select(pot);
    uint16_t src = 0;
    src |= address;
    spi_write16_blocking(spi, &src, 1);
}

int address_select(int pot){
    int address = 0;
    if (pot == 1){
        address = 1;
    }
    else if (pot == 2){
        address = 6;
    }
    else if (pot == 3){
        address = 7;
    }
    return address;
}
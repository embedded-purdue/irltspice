#include "hardware/spi.h"
#include "hardware/gpio.h"

// this is for TMUXS7614DZEMR

void init_tmuxs76_spi(int sck, int sdo, int cs, int sda, spi_inst_t* spi, int baudrate) {
    gpio_set_function(sck, 1);
    gpio_set_function(sda, 1);
    gpio_set_function(cs, 1);
    gpio_set_function(sdo, 1);
    spi_init(spi, baudrate);
    spi_set_format(spi, 16, 0, 0, SPI_MSB_FIRST);
}

/*
d1: 1 <---> s1: 3
d2: 2 <---> s2: 4
d3: 8 <---> s3: 6
d4: 9 <---> s4: 7
d5: 16 <---> s5: 18
d6: 17 <---> s6: 19
d7: 23 <---> s7: 21
d8: 24 <---> s8: 22
*/

/*
enable a single switch based on sw (number)
*/
void set_single(int sw, spi_inst_t* spi){
    int16_t src = 0x0100; // default write address
    src |= (1 << (sw-1));
    spi_write16_blocking(spi, &src, 1);
}

/*
enable up to all switches at once with a sw mask
preferred for behavior because repeated set_single will overwrite
the enables, but more difficult data type to work with
*/
void set_eight(int8_t sw_mask, spi_inst_t* spi){
    int16_t src = 0x0100;
    src |= sw_mask;
    spi_write16_blocking(spi, &src, 1);
}

/*
disables all
*/
void reset_switches(spi_inst_t* spi){
    int16_t src = 0x0100;
    spi_write16_blocking(spi, &src, 1);
}

/*
8 bit mask to read enabled switches
first 8 are junk/alignment 0x25, next 8 are the register enable data
*/
int16_t read_reg(spi_inst_t* spi){
    int16_t src = 0x8100; // 1 0000001 00000000
    // spi_write16_blocking(spi, &src, 1); 
    int16_t dst = 0;
    spi_read16_blocking(spi, src, &dst, 1);
    return dst & 0xff
}
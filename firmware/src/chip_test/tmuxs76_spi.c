#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "tmuxs76_spi.h"

// this is for TMUXS7614DZEMR
/*
pins for switches
d1: 1 <---> s1: 3
d2: 2 <---> s2: 4
d3: 8 <---> s3: 6
d4: 9 <---> s4: 7
d5: 16 <---> s5: 18
d6: 17 <---> s6: 19
d7: 23 <---> s7: 21
d8: 24 <---> s8: 22
*/


void tmuxs7614_init(int sck, int sdo, int cs, int sda, spi_inst_t* spi, int baudrate, TmuxS7614* tmux) {
    gpio_set_function(sck, 1);
    gpio_set_function(sda, 1);
    gpio_set_function(cs, 1);
    gpio_set_function(sdo, 1);
    spi_init(spi, baudrate);
    spi_set_format(spi, 16, 0, 0, SPI_MSB_FIRST);
    tmux->spi = spi;
}

/*
8 bit mask to read enabled switches
first 8 are junk/alignment 0x25, next 8 are the register enable data
*/
uint8_t tmuxs7614_read_switches(TmuxS7614* tmux){
    uint16_t src = 0x8100; // 1 0000001 00000000
    uint16_t dst = 0;
    spi_read16_blocking(tmux->spi, src, &dst, 1);
    return dst & 0xff;
}

/*
read current switch state
enable a single switch based on sw (number)
keep other switches the same
*/
void tmuxs7614_set_single(uint8_t sw, TmuxS7614* tmux){
    uint8_t cur = tmuxs7614_read_switches(tmux);
    uint16_t src = 0x0100; // default write address
    src |= cur;
    src |= (1 << (sw-1));
    spi_write16_blocking(tmux->spi, &src, 1);
}

/*
enable up to all switches at once with a sw mask
*/
void tmuxs7614_set_mask(TmuxS7614* tmux, uint8_t mask){
    uint16_t src = 0x0100;
    src |= mask;
    spi_write16_blocking(tmux->spi, &src, 1);
}

/*
disables all
*/
void tmuxs7614_reset(TmuxS7614* tmux){
    uint16_t src = 0x0100;
    spi_write16_blocking(tmux->spi, &src, 1);
}
#ifndef TMUXS7614_H
#define TMUXS7614_H

#include "hardware/spi.h"

typedef struct {
    spi_inst_t *spi;
} TmuxS7614;

void tmuxs7614_init(int sck, int sdo, int cs, int sda, spi_inst_t* spi, int baudrate, TmuxS7614* tmux);

void tmuxs7614_set_single(uint8_t sw, TmuxS7614 *tmux);

void tmuxs7614_set_mask(TmuxS7614 *tmux, uint8_t mask);

void tmuxs7614_reset(TmuxS7614 *tmux);

uint8_t tmuxs7614_read_switches(TmuxS7614 *tmux);

#endif

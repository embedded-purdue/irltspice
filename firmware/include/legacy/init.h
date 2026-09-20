#ifndef __INIT_H__
#define __INIT_H__

#ifdef __cplusplus
extern "C"
{
#endif

void internal_clock(void);
void setup_uart(void);
void setup_spi(SPI_HandleTypeDef *);
void setup_gpios(void);

#ifdef __cplusplus
}
#endif

#endif /* __INIT_H__ */

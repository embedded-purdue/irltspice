#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "mcp_i2c.h"

// THIS IS FOR mcp4462

int i2c_address_select(int pot);

void init_mcp_i2c(int sda, int scl, i2c_inst_t* i2c, int baudrate, MCP_I2C* mcp) {
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    i2c_init(i2c, baudrate);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    mcp->i2c = i2c;
}

void set_resistance_i2c(int ohms, MCP_I2C* mcp, int pot){
/*
byte 1 - control/address byte
0x58
byte 2 - write byte
4 bits address
2 bits command (00)
1 bit x
1 bit D8
byte 3 - write data bits
D7:D0
*/

    uint8_t control_byte = 0x58;
    int step = (ohms * 256 + 50000) / 100000;
    uint8_t write_byte = 0;
    int address = i2c_address_select(pot);
    write_byte |= (address << 4);
    write_byte |= (step & 0x1FF);
    uint8_t data_byte = 0;
    data_byte |= step;
    i2c_write_blocking(mcp->i2c, 0, &control_byte, 1, true);
    i2c_write_blocking(mcp->i2c, 0, &write_byte, 1, true);
    i2c_write_blocking(mcp->i2c, 0, &data_byte, 1, true);

}

int i2c_address_select(int pot) {
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
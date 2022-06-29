/*
   Based on i2c_examples_main.c in
   https://github.com/espressif/esp-idf/tree/master/examples
*/
#include "driver/i2c.h"
#include <stdio.h>

#define DATA_LENGTH 512

#define I2C_SLAVE_SCL_IO (GPIO_NUM_17) // i2c slave clock
#define I2C_SLAVE_SDA_IO (GPIO_NUM_16) // i2c slave data
#define I2C_SLAVE_NUM I2C_NUM_0
#define I2C_SLAVE_TX_BUF_LEN (2 * DATA_LENGTH)
#define I2C_SLAVE_RX_BUF_LEN (2 * DATA_LENGTH)

#define ESP_SLAVE_ADDR 0x41 // you can set any 7-bit value

// i2c slave initialization
void
tg_i2c_init() {
  int i2c_slave_port = I2C_SLAVE_NUM;
  i2c_config_t conf_slave;
  conf_slave.sda_io_num = I2C_SLAVE_SDA_IO;
  conf_slave.sda_pullup_en = GPIO_PULLUP_DISABLE;
  conf_slave.scl_io_num = I2C_SLAVE_SCL_IO;
  conf_slave.scl_pullup_en = GPIO_PULLUP_DISABLE;
  conf_slave.mode = I2C_MODE_SLAVE;
  conf_slave.slave.addr_10bit_en = 0;
  conf_slave.slave.slave_addr = ESP_SLAVE_ADDR;
  i2c_param_config(i2c_slave_port, &conf_slave);
  i2c_driver_install(
      i2c_slave_port,
      conf_slave.mode,
      I2C_SLAVE_RX_BUF_LEN,
      I2C_SLAVE_TX_BUF_LEN,
      0);
}

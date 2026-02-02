#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display.h"
#include "main.h"

#if CONFIG_OPENAI_BOARD_M5_ATOMS3R

// Backlight is controlled via I2C power management IC LP5562 (address 0x30)
#define BL_I2C_PORT  I2C_NUM_0
#define BL_I2C_SDA   GPIO_NUM_45
#define BL_I2C_SCL   GPIO_NUM_0
#define BL_I2C_ADDR  0x30
#define BL_I2C_FREQ  400000

void oai_init_display(void) {
    i2c_config_t i2c_cfg = {};
    i2c_cfg.mode = I2C_MODE_MASTER;
    i2c_cfg.sda_io_num = BL_I2C_SDA;
    i2c_cfg.scl_io_num = BL_I2C_SCL;
    i2c_cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_cfg.master.clk_speed = BL_I2C_FREQ;
    i2c_param_config(BL_I2C_PORT, &i2c_cfg);
    i2c_driver_install(BL_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    uint8_t chip_enable[]  = {0x00, 0x40};
    uint8_t led_enable[]   = {0x08, 0x01};
    uint8_t led_mode[]     = {0x70, 0x00};
    uint8_t led_brightness[] = {0x0e, 255};

    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, chip_enable, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(1));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_enable, 2, pdMS_TO_TICKS(100));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_mode, 2, pdMS_TO_TICKS(100));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_brightness, 2, pdMS_TO_TICKS(100));

    i2c_driver_delete(BL_I2C_PORT);

    ESP_LOGI(LOG_TAG, "LCD backlight initialized");
}

#else

void oai_init_display(void) {}

#endif

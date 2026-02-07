#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/spi_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "display.h"
#include "main.h"

#if CONFIG_OPENAI_BOARD_M5_ATOMS3R

#include "images/icon_loading.h"
#include "images/icon_connected.h"
#include "images/icon_disconnected.h"

// Backlight control via LP5562 (I2C)
#define BL_I2C_PORT  I2C_NUM_0
#define BL_I2C_SDA   GPIO_NUM_45
#define BL_I2C_SCL   GPIO_NUM_0
#define BL_I2C_ADDR  0x30
#define BL_I2C_FREQ  400000

// LCD pins (GC9107, 128x128)
#define LCD_SPI_HOST SPI3_HOST
#define LCD_PIN_MOSI GPIO_NUM_21
#define LCD_PIN_SCLK GPIO_NUM_15
#define LCD_PIN_CS   GPIO_NUM_14
#define LCD_PIN_DC   GPIO_NUM_42
#define LCD_PIN_RST  GPIO_NUM_48

#define LCD_WIDTH    128
#define LCD_HEIGHT   128

static spi_device_handle_t spi_handle = NULL;

// Send command to LCD
static void lcd_cmd(uint8_t cmd) {
    gpio_set_level(LCD_PIN_DC, 0);
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_transmit(spi_handle, &t);
}

// Send data to LCD
static void lcd_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(LCD_PIN_DC, 1);
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_transmit(spi_handle, &t);
}

// Send single byte data
static void lcd_data_byte(uint8_t data) {
    lcd_data(&data, 1);
}

// Initialize LCD (GC9107)
static void lcd_init(void) {
    // Reset
    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Inner Register Enable 1
    lcd_cmd(0xFE);

    // Inner Register Enable 2
    lcd_cmd(0xEF);

    // Sleep out
    lcd_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Interface Pixel Format: 16bit/pixel (RGB565)
    lcd_cmd(0x3A);
    lcd_data_byte(0x55);

    // Memory Access Control
    lcd_cmd(0x36);
    lcd_data_byte(0x08);

    // Tearing Effect Line ON
    lcd_cmd(0x35);
    lcd_data_byte(0x00);

    // Display Inversion ON (GC9107 specific)
    lcd_cmd(0x21);

    // Display ON
    lcd_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// Set drawing window
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // GC9107 has 128x160 memory with offsets
    // M5GFX uses offset_x=2, offset_y=1 for AtomS3R
    uint16_t x_offset = 2;
    uint16_t y_offset = 1;

    lcd_cmd(0x2A);  // Column Address Set
    uint8_t col_data[] = {0, (uint8_t)(x0 + x_offset), 0, (uint8_t)(x1 + x_offset)};
    lcd_data(col_data, 4);

    lcd_cmd(0x2B);  // Row Address Set
    uint8_t row_data[] = {0, (uint8_t)(y0 + y_offset), 0, (uint8_t)(y1 + y_offset)};
    lcd_data(row_data, 4);

    lcd_cmd(0x2C);  // Memory Write
}

// Fill screen with color (RGB565)
static void lcd_fill_screen(uint16_t color) {
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    uint8_t color_hi = color >> 8;
    uint8_t color_lo = color & 0xFF;

    uint8_t line_buf[LCD_WIDTH * 2];
    for (int i = 0; i < LCD_WIDTH; i++) {
        line_buf[i * 2] = color_hi;
        line_buf[i * 2 + 1] = color_lo;
    }

    gpio_set_level(LCD_PIN_DC, 1);
    for (int y = 0; y < LCD_HEIGHT; y++) {
        spi_transaction_t t = {};
        t.length = LCD_WIDTH * 2 * 8;
        t.tx_buffer = line_buf;
        spi_device_transmit(spi_handle, &t);
    }
}

// Draw image centered on screen (64x64 icon)
static void lcd_draw_image(const uint16_t *image) {
    // Fill background with white
    lcd_fill_screen(0xFFFF);

    // Calculate offset to center the icon
    int x_offset = (LCD_WIDTH - ICON_WIDTH) / 2;   // 32
    int y_offset = (LCD_HEIGHT - ICON_HEIGHT) / 2; // 32

    // Draw the icon at center
    lcd_set_window(x_offset, y_offset, x_offset + ICON_WIDTH - 1, y_offset + ICON_HEIGHT - 1);

    uint8_t line_buf[ICON_WIDTH * 2];

    gpio_set_level(LCD_PIN_DC, 1);
    for (int y = 0; y < ICON_HEIGHT; y++) {
        for (int x = 0; x < ICON_WIDTH; x++) {
            uint16_t pixel = image[y * ICON_WIDTH + x];
            line_buf[x * 2] = pixel >> 8;
            line_buf[x * 2 + 1] = pixel & 0xFF;
        }
        spi_transaction_t t = {};
        t.length = ICON_WIDTH * 2 * 8;
        t.tx_buffer = line_buf;
        spi_device_transmit(spi_handle, &t);
    }
}

// Initialize backlight via I2C (LP5562)
static void backlight_init(void) {
    i2c_config_t i2c_cfg = {};
    i2c_cfg.mode = I2C_MODE_MASTER;
    i2c_cfg.sda_io_num = BL_I2C_SDA;
    i2c_cfg.scl_io_num = BL_I2C_SCL;
    i2c_cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_cfg.master.clk_speed = BL_I2C_FREQ;
    i2c_param_config(BL_I2C_PORT, &i2c_cfg);
    i2c_driver_install(BL_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    uint8_t chip_enable[]    = {0x00, 0x40};
    uint8_t led_enable[]     = {0x08, 0x01};
    uint8_t led_mode[]       = {0x70, 0x00};
    uint8_t led_brightness[] = {0x0e, 255};

    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, chip_enable, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(1));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_enable, 2, pdMS_TO_TICKS(100));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_mode, 2, pdMS_TO_TICKS(100));
    i2c_master_write_to_device(BL_I2C_PORT, BL_I2C_ADDR, led_brightness, 2, pdMS_TO_TICKS(100));

    i2c_driver_delete(BL_I2C_PORT);
}

void oai_init_display(void) {
    // Initialize GPIO for DC and RST
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << LCD_PIN_DC) | (1ULL << LCD_PIN_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io_conf);

    // Initialize SPI
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = LCD_PIN_MOSI;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = LCD_PIN_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2;
    spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 40 * 1000 * 1000;  // 40MHz
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = LCD_PIN_CS;
    dev_cfg.queue_size = 1;
    spi_bus_add_device(LCD_SPI_HOST, &dev_cfg, &spi_handle);

    // Initialize LCD
    lcd_init();

    // Initialize backlight
    backlight_init();

    // Set initial state (blue = initializing)
    oai_display_set_state(DISPLAY_STATE_INITIALIZING);

    ESP_LOGI(LOG_TAG, "LCD display initialized");
}

void oai_display_set_state(DisplayState state) {
    const uint16_t *image;
    switch (state) {
        case DISPLAY_STATE_INITIALIZING:
            image = icon_loading;
            break;
        case DISPLAY_STATE_CONNECTED:
            image = icon_connected;
            break;
        case DISPLAY_STATE_DISCONNECTED:
            image = icon_disconnected;
            break;
        default:
            image = icon_loading;
            break;
    }
    lcd_draw_image(image);
}

#else

void oai_init_display(void) {}
void oai_display_set_state(DisplayState state) {}

#endif

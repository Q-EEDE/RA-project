#ifndef APP_BOARD_H_
#define APP_BOARD_H_

#include <stdbool.h>
#include <stdint.h>
#include "app_types.h"

#ifndef __has_include
#define __has_include(x)    0
#endif

#if APP_USE_FSP_UART_CAMERA && __has_include("r_uart_api.h")
#include "r_uart_api.h"
#define APP_HAS_FSP_UART_CAMERA     (1U)
#else
#define APP_HAS_FSP_UART_CAMERA     (0U)
#endif

#if APP_USE_FSP_UART_CONTROL && __has_include("r_uart_api.h")
#include "r_uart_api.h"
#define APP_HAS_FSP_UART_CONTROL    (1U)
#else
#define APP_HAS_FSP_UART_CONTROL    (0U)
#endif

#if APP_USE_FSP_UART_HOST && __has_include("r_uart_api.h")
#include "r_uart_api.h"
#define APP_HAS_FSP_UART_HOST       (1U)
#else
#define APP_HAS_FSP_UART_HOST       (0U)
#endif

#if APP_USE_FSP_SPI_CAMERA && __has_include("r_spi_api.h")
#include "r_spi_api.h"
#define APP_HAS_FSP_SPI_CAMERA      (1U)
#else
#define APP_HAS_FSP_SPI_CAMERA      (0U)
#endif

#if APP_USE_FSP_I2C_CAMERA && __has_include("r_i2c_master_api.h")
#include "r_i2c_master_api.h"
#define APP_HAS_FSP_I2C_CAMERA      (1U)
#else
#define APP_HAS_FSP_I2C_CAMERA      (0U)
#endif

#if APP_HAS_FSP_UART_CAMERA
void app_camera_uart_callback(uart_callback_args_t * p_args);
#endif

#if APP_HAS_FSP_UART_CONTROL
void app_control_uart_callback(uart_callback_args_t * p_args);
#endif

#if APP_HAS_FSP_UART_HOST
void app_host_uart_callback(uart_callback_args_t * p_args);
#endif

#if APP_HAS_FSP_SPI_CAMERA
void app_camera_spi_callback(spi_callback_args_t * p_args);
#endif

#if APP_HAS_FSP_I2C_CAMERA
void app_camera_i2c_callback(i2c_master_callback_args_t * p_args);
#endif

typedef struct
{
    bool cs_high;
    bool miso_high;
    bool mosi_high;
    bool sck_high;
    app_status_t cs_status;
    app_status_t miso_status;
    app_status_t mosi_status;
    app_status_t sck_status;
} app_board_spi_pin_levels_t;

app_status_t app_board_init(void);
bool app_board_is_fruit_present(void);
app_status_t app_board_capture_button_init(void);
bool app_board_is_capture_button_pressed(void);
void app_board_delay_ms(uint32_t delay_ms);
void app_board_delay_us(uint32_t delay_us);
void app_board_debug_led_set(bool on);
void app_board_debug_led_raw_set(bool high);
void app_board_debug_led_blink(uint8_t count, uint32_t on_ms, uint32_t off_ms);

app_status_t app_board_camera_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms);
app_status_t app_board_camera_read_byte(uint8_t * p_byte, uint32_t timeout_ms);
app_status_t app_board_control_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms);

app_status_t app_board_host_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms);
app_status_t app_board_host_read_byte(uint8_t * p_byte, uint32_t timeout_ms);
bool app_board_host_has_byte(void);
uint32_t app_board_host_rx_irq_count(void);
uint8_t app_board_host_rx_last_byte(void);

app_status_t app_board_camera_cs_set(bool high);
app_status_t app_board_camera_spi_set_mode(uint8_t mode);
app_status_t app_board_camera_spi_transfer(const uint8_t * p_tx, uint8_t * p_rx, uint32_t length, uint32_t timeout_ms);
app_status_t app_board_camera_spi_bitbang_begin(void);
app_status_t app_board_camera_spi_bitbang_transfer(const uint8_t * p_tx, uint8_t * p_rx, uint32_t length);
app_status_t app_board_camera_spi_bitbang_end(uint8_t mode);
app_status_t app_board_camera_debug_read_spi_pins(app_board_spi_pin_levels_t * p_levels);
app_status_t app_board_camera_i2c_write_reg(uint8_t device_address, uint8_t reg, uint8_t value, uint32_t timeout_ms);
app_status_t app_board_camera_i2c_read_reg(uint8_t device_address, uint8_t reg, uint8_t * p_value, uint32_t timeout_ms);

#endif /* APP_BOARD_H_ */

#include "app_board.h"
#include <stddef.h>
#include <string.h>
#include "hal_data.h"
#include "app_config.h"

#if APP_HAS_FSP_UART_CAMERA
extern const uart_instance_t g_uart_camera;
#define APP_CAMERA_RX_BUFFER_SIZE    (512U)

static volatile uint8_t g_camera_rx_buffer[APP_CAMERA_RX_BUFFER_SIZE];
static volatile uint16_t g_camera_rx_write_index;
static volatile uint16_t g_camera_rx_read_index;
static volatile bool g_camera_uart_tx_complete;

static bool app_camera_ring_pop(uint8_t * p_byte);
static void app_camera_ring_push(uint8_t byte);
#endif

#if APP_HAS_FSP_UART_CONTROL
extern const uart_instance_t g_uart_control;
static volatile bool g_control_uart_tx_complete;
#endif

#if APP_HAS_FSP_UART_HOST
extern const uart_instance_t g_uart_host;
#define APP_HOST_RX_BUFFER_SIZE    (APP_HOST_LINK_RX_BUFFER_SIZE)
static volatile uint8_t g_host_rx_buffer[APP_HOST_RX_BUFFER_SIZE];
static volatile uint16_t g_host_rx_write_index;
static volatile uint16_t g_host_rx_read_index;
static volatile bool g_host_uart_tx_complete;
static volatile uint32_t g_host_rx_irq_count;
static volatile uint8_t g_host_rx_last_byte;
#if APP_HOST_LINK_RX_IRQ_LED_TOGGLE_ENABLED
static volatile bool g_host_rx_led_high;
#endif

static bool app_host_ring_pop(uint8_t * p_byte);
static void app_host_ring_push(uint8_t byte);
#endif

#if APP_HAS_FSP_SPI_CAMERA
extern const spi_instance_t g_spi_camera;
static volatile bool g_spi_transfer_complete;
static volatile bool g_spi_transfer_error;
static spi_cfg_t g_spi_camera_runtime_cfg;
static spi_extended_cfg_t g_spi_camera_runtime_ext_cfg;
static bool g_spi_camera_is_open;
#endif

#if APP_HAS_FSP_I2C_CAMERA
extern const i2c_master_instance_t g_i2c_camera;
static volatile bool g_i2c_transfer_complete;
static volatile bool g_i2c_transfer_error;
#endif

#if APP_HAS_FSP_UART_CAMERA || APP_HAS_FSP_UART_CONTROL || APP_HAS_FSP_UART_HOST
static app_status_t app_uart_write_blocking(const uart_instance_t * p_uart,
                                            volatile bool * p_tx_complete,
                                            const uint8_t * p_data,
                                            uint32_t length,
                                            uint32_t timeout_ms);
#endif

#if APP_HAS_FSP_SPI_CAMERA || APP_HAS_FSP_I2C_CAMERA
static app_status_t app_wait_for_transfer(volatile bool * p_complete,
                                          volatile bool * p_error,
                                          uint32_t timeout_ms);
#endif

#if APP_HAS_FSP_SPI_CAMERA
static app_status_t app_board_camera_spi_restore_pins(void);
static app_status_t app_board_read_pin_level(bsp_io_port_pin_t pin, bool * p_high);
static app_status_t app_board_camera_spi_bitbang_set_pin(bsp_io_port_pin_t pin, bool high);
static app_status_t app_board_camera_spi_bitbang_read_pin(bsp_io_port_pin_t pin, bool * p_high);
static void app_board_camera_spi_bitbang_delay(void);
#endif

#if APP_FRUIT_SENSOR_GPIO_ENABLED
#ifndef APP_FRUIT_SENSOR_PIN
#define APP_FRUIT_SENSOR_PIN    BSP_IO_PORT_04_PIN_00
#endif

static bool app_read_fruit_sensor_once(void);
#endif

#if APP_CAPTURE_BUTTON_GPIO_ENABLED
#ifndef APP_CAPTURE_BUTTON_PIN
#error "Define APP_CAPTURE_BUTTON_PIN in app_config.h before enabling APP_CAPTURE_BUTTON_GPIO_ENABLED."
#endif

static bool app_read_capture_button_once(void);
#endif

#if APP_DEBUG_LED_ENABLED
#ifndef APP_DEBUG_LED_PIN
#define APP_DEBUG_LED_PIN    BSP_IO_PORT_04_PIN_00
#endif
#endif

#if APP_ARDUCAM_CS_GPIO_ENABLED
#ifndef APP_ARDUCAM_CS_PIN
#define APP_ARDUCAM_CS_PIN    BSP_IO_PORT_00_PIN_01
#endif
#endif

#if APP_HAS_FSP_SPI_CAMERA
#ifndef APP_ARDUCAM_SPI_MISO_PIN
#define APP_ARDUCAM_SPI_MISO_PIN    BSP_IO_PORT_02_PIN_02
#endif

#ifndef APP_ARDUCAM_SPI_MOSI_PIN
#define APP_ARDUCAM_SPI_MOSI_PIN    BSP_IO_PORT_02_PIN_03
#endif

#ifndef APP_ARDUCAM_SPI_SCK_PIN
#define APP_ARDUCAM_SPI_SCK_PIN     BSP_IO_PORT_02_PIN_04
#endif
#endif

app_status_t app_board_init(void)
{
    app_status_t status = APP_STATUS_OK;

#if APP_HAS_FSP_UART_CAMERA
    fsp_err_t err = g_uart_camera.p_api->open(g_uart_camera.p_ctrl, g_uart_camera.p_cfg);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }
#elif APP_USE_FSP_UART_CAMERA
    status = APP_STATUS_NOT_CONFIGURED;
#endif

#if APP_HAS_FSP_UART_CONTROL
    fsp_err_t err_control = g_uart_control.p_api->open(g_uart_control.p_ctrl, g_uart_control.p_cfg);

    if (FSP_SUCCESS != err_control)
    {
        return APP_STATUS_IO_ERROR;
    }
#elif APP_USE_FSP_UART_CONTROL
    status = APP_STATUS_NOT_CONFIGURED;
#endif

#if APP_HAS_FSP_UART_HOST
    fsp_err_t err_host = g_uart_host.p_api->open(g_uart_host.p_ctrl, g_uart_host.p_cfg);

    if (FSP_SUCCESS != err_host)
    {
        return APP_STATUS_IO_ERROR;
    }

    g_host_rx_write_index = 0U;
    g_host_rx_read_index = 0U;
    g_host_rx_irq_count = 0U;
    g_host_rx_last_byte = 0U;
#if APP_HOST_LINK_RX_IRQ_LED_TOGGLE_ENABLED
    g_host_rx_led_high = false;
#endif
#elif APP_USE_FSP_UART_HOST
    status = APP_STATUS_NOT_CONFIGURED;
#endif

#if APP_HAS_FSP_SPI_CAMERA
    fsp_err_t err_spi = g_spi_camera.p_api->open(g_spi_camera.p_ctrl, g_spi_camera.p_cfg);

    if (FSP_SUCCESS != err_spi)
    {
        g_spi_camera_is_open = false;
        return APP_STATUS_IO_ERROR;
    }

    g_spi_camera_is_open = true;
#elif APP_USE_FSP_SPI_CAMERA
    status = APP_STATUS_NOT_CONFIGURED;
#endif

#if APP_HAS_FSP_I2C_CAMERA
    fsp_err_t err_i2c = g_i2c_camera.p_api->open(g_i2c_camera.p_ctrl, g_i2c_camera.p_cfg);

    if (FSP_SUCCESS != err_i2c)
    {
        return APP_STATUS_IO_ERROR;
    }
#elif APP_USE_FSP_I2C_CAMERA
    status = APP_STATUS_NOT_CONFIGURED;
#endif

    return status;
}

bool app_board_is_fruit_present(void)
{
#if APP_FRUIT_SENSOR_GPIO_ENABLED
    bool is_active = app_read_fruit_sensor_once();

    if (is_active)
    {
        app_board_delay_ms(APP_FRUIT_SENSOR_DEBOUNCE_MS);
        is_active = app_read_fruit_sensor_once();
    }

    return is_active;
#else
    return false;
#endif
}

app_status_t app_board_capture_button_init(void)
{
#if APP_CAPTURE_BUTTON_GPIO_ENABLED
    uint32_t pin_cfg = (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT;

#if APP_CAPTURE_BUTTON_PULLUP_ENABLED
    pin_cfg |= (uint32_t) IOPORT_CFG_PULLUP_ENABLE;
#endif

    return (FSP_SUCCESS == g_ioport.p_api->pinCfg(g_ioport.p_ctrl, APP_CAPTURE_BUTTON_PIN, pin_cfg)) ?
           APP_STATUS_OK : APP_STATUS_IO_ERROR;
#else
    return APP_STATUS_NOT_CONFIGURED;
#endif
}

bool app_board_is_capture_button_pressed(void)
{
#if APP_CAPTURE_BUTTON_GPIO_ENABLED
    bool is_pressed = app_read_capture_button_once();

    if (is_pressed)
    {
        app_board_delay_ms(APP_CAPTURE_BUTTON_DEBOUNCE_MS);
        is_pressed = app_read_capture_button_once();
    }

    return is_pressed;
#else
    return false;
#endif
}

void app_board_delay_ms(uint32_t delay_ms)
{
    if (delay_ms > 0U)
    {
        R_BSP_SoftwareDelay(delay_ms, BSP_DELAY_UNITS_MILLISECONDS);
    }
}

void app_board_delay_us(uint32_t delay_us)
{
    if (delay_us > 0U)
    {
        R_BSP_SoftwareDelay(delay_us, BSP_DELAY_UNITS_MICROSECONDS);
    }
}

void app_board_debug_led_set(bool on)
{
#if APP_DEBUG_LED_ENABLED
    bsp_io_level_t level;

#if APP_DEBUG_LED_ACTIVE_HIGH
    level = on ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
#else
    level = on ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
#endif

    (void) g_ioport.p_api->pinWrite(g_ioport.p_ctrl, APP_DEBUG_LED_PIN, level);
#else
    (void) on;
#endif
}

void app_board_debug_led_raw_set(bool high)
{
#if APP_DEBUG_LED_ENABLED
    bsp_io_level_t level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;

    (void) g_ioport.p_api->pinWrite(g_ioport.p_ctrl, APP_DEBUG_LED_PIN, level);
#else
    (void) high;
#endif
}

void app_board_debug_led_blink(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        app_board_debug_led_set(true);
        app_board_delay_ms(on_ms);
        app_board_debug_led_set(false);
        app_board_delay_ms(off_ms);
    }
}

app_status_t app_board_camera_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL != p_data) && (length > 0U))
    {
#if APP_HAS_FSP_UART_CAMERA
        status = app_uart_write_blocking(&g_uart_camera, &g_camera_uart_tx_complete, p_data, length, timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_camera_read_byte(uint8_t * p_byte, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_byte)
    {
#if APP_HAS_FSP_UART_CAMERA
        uint32_t waited_ms = 0U;
        status = APP_STATUS_TIMEOUT;

        do
        {
            if (app_camera_ring_pop(p_byte))
            {
                status = APP_STATUS_OK;
                break;
            }

            app_board_delay_ms(1U);
            waited_ms++;
        } while (waited_ms < timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_control_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL != p_data) && (length > 0U))
    {
#if APP_HAS_FSP_UART_CONTROL
        status = app_uart_write_blocking(&g_uart_control, &g_control_uart_tx_complete, p_data, length, timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_host_write(const uint8_t * p_data, uint32_t length, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL != p_data) && (length > 0U))
    {
#if APP_HAS_FSP_UART_HOST
        status = app_uart_write_blocking(&g_uart_host, &g_host_uart_tx_complete, p_data, length, timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_host_read_byte(uint8_t * p_byte, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_byte)
    {
#if APP_HAS_FSP_UART_HOST
        uint32_t waited_ms = 0U;
        status = APP_STATUS_TIMEOUT;

        do
        {
            if (app_host_ring_pop(p_byte))
            {
                status = APP_STATUS_OK;
                break;
            }

            app_board_delay_ms(1U);
            waited_ms++;
        } while (waited_ms < timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

bool app_board_host_has_byte(void)
{
#if APP_HAS_FSP_UART_HOST
    return (g_host_rx_read_index != g_host_rx_write_index);
#else
    return false;
#endif
}

uint32_t app_board_host_rx_irq_count(void)
{
#if APP_HAS_FSP_UART_HOST
    return g_host_rx_irq_count;
#else
    return 0U;
#endif
}

uint8_t app_board_host_rx_last_byte(void)
{
#if APP_HAS_FSP_UART_HOST
    return g_host_rx_last_byte;
#else
    return 0U;
#endif
}

app_status_t app_board_camera_cs_set(bool high)
{
#if APP_ARDUCAM_CS_GPIO_ENABLED
    bsp_io_level_t level;

#if APP_ARDUCAM_CS_ACTIVE_LOW
    level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
#else
    level = high ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
#endif

    return (FSP_SUCCESS == g_ioport.p_api->pinWrite(g_ioport.p_ctrl, APP_ARDUCAM_CS_PIN, level)) ?
           APP_STATUS_OK : APP_STATUS_IO_ERROR;
#else
    (void) high;
    return APP_STATUS_OK;
#endif
}

app_status_t app_board_camera_spi_set_mode(uint8_t mode)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

#if APP_HAS_FSP_SPI_CAMERA
    if (mode <= 3U)
    {
        fsp_err_t err;

        if (g_spi_camera_is_open)
        {
            (void) g_spi_camera.p_api->close(g_spi_camera.p_ctrl);
            g_spi_camera_is_open = false;
        }

        memcpy(&g_spi_camera_runtime_cfg, g_spi_camera.p_cfg, sizeof(g_spi_camera_runtime_cfg));
        memcpy(&g_spi_camera_runtime_ext_cfg,
               g_spi_camera.p_cfg->p_extend,
               sizeof(g_spi_camera_runtime_ext_cfg));

        g_spi_camera_runtime_cfg.p_extend = &g_spi_camera_runtime_ext_cfg;
        g_spi_camera_runtime_ext_cfg.spck_div.spbr = 124U;
        g_spi_camera_runtime_ext_cfg.spck_div.brdv = 2U;

        g_spi_camera_runtime_cfg.clk_polarity = (0U != (mode & 0x02U)) ?
                                                SPI_CLK_POLARITY_HIGH :
                                                SPI_CLK_POLARITY_LOW;
        g_spi_camera_runtime_cfg.clk_phase = (0U != (mode & 0x01U)) ?
                                             SPI_CLK_PHASE_EDGE_EVEN :
                                             SPI_CLK_PHASE_EDGE_ODD;

        err = g_spi_camera.p_api->open(g_spi_camera.p_ctrl, &g_spi_camera_runtime_cfg);
        if (FSP_SUCCESS == err)
        {
            g_spi_camera_is_open = true;
            status = APP_STATUS_OK;
        }
        else
        {
            status = APP_STATUS_IO_ERROR;
        }
    }
#else
    (void) mode;
    status = APP_STATUS_NOT_CONFIGURED;
#endif

    return status;
}

app_status_t app_board_camera_spi_transfer(const uint8_t * p_tx, uint8_t * p_rx, uint32_t length, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL != p_tx) && (NULL != p_rx) && (length > 0U))
    {
#if APP_HAS_FSP_SPI_CAMERA
        fsp_err_t err;

        if (!g_spi_camera_is_open)
        {
            return APP_STATUS_IO_ERROR;
        }

        g_spi_transfer_complete = false;
        g_spi_transfer_error = false;
        err = g_spi_camera.p_api->writeRead(g_spi_camera.p_ctrl, p_tx, p_rx, length, SPI_BIT_WIDTH_8_BITS);

        if (FSP_SUCCESS != err)
        {
            return APP_STATUS_IO_ERROR;
        }

        status = app_wait_for_transfer(&g_spi_transfer_complete, &g_spi_transfer_error, timeout_ms);
#else
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_camera_spi_bitbang_begin(void)
{
#if APP_HAS_FSP_SPI_CAMERA
    fsp_err_t err;

    if (g_spi_camera_is_open)
    {
        (void) g_spi_camera.p_api->close(g_spi_camera.p_ctrl);
        g_spi_camera_is_open = false;
    }

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_MISO_PIN,
                                 (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_MOSI_PIN,
                                 (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                                 (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_SCK_PIN,
                                 (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                                 (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    return app_board_camera_cs_set(true);
#else
    return APP_STATUS_NOT_CONFIGURED;
#endif
}

app_status_t app_board_camera_spi_bitbang_transfer(const uint8_t * p_tx, uint8_t * p_rx, uint32_t length)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL != p_tx) && (NULL != p_rx) && (length > 0U))
    {
#if APP_HAS_FSP_SPI_CAMERA
        status = APP_STATUS_OK;

        for (uint32_t i = 0U; (i < length) && (APP_STATUS_OK == status); i++)
        {
            uint8_t rx_byte = 0U;

            for (uint8_t bit = 0x80U; (bit > 0U) && (APP_STATUS_OK == status); bit >>= 1U)
            {
                bool miso_high = false;

                status = app_board_camera_spi_bitbang_set_pin(APP_ARDUCAM_SPI_MOSI_PIN,
                                                              0U != (p_tx[i] & bit));

                if (APP_STATUS_OK == status)
                {
                    app_board_camera_spi_bitbang_delay();
                    status = app_board_camera_spi_bitbang_set_pin(APP_ARDUCAM_SPI_SCK_PIN, true);
                }

                if (APP_STATUS_OK == status)
                {
                    app_board_camera_spi_bitbang_delay();
                    status = app_board_camera_spi_bitbang_read_pin(APP_ARDUCAM_SPI_MISO_PIN, &miso_high);
                }

                if (APP_STATUS_OK == status)
                {
                    if (miso_high)
                    {
                        rx_byte = (uint8_t) (rx_byte | bit);
                    }

                    status = app_board_camera_spi_bitbang_set_pin(APP_ARDUCAM_SPI_SCK_PIN, false);
                }

                if (APP_STATUS_OK == status)
                {
                    app_board_camera_spi_bitbang_delay();
                }
            }

            p_rx[i] = rx_byte;
        }
#else
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

app_status_t app_board_camera_spi_bitbang_end(uint8_t mode)
{
    app_status_t status;

#if APP_HAS_FSP_SPI_CAMERA
    (void) app_board_camera_cs_set(true);
    (void) app_board_camera_spi_bitbang_set_pin(APP_ARDUCAM_SPI_SCK_PIN, false);
    (void) app_board_camera_spi_bitbang_set_pin(APP_ARDUCAM_SPI_MOSI_PIN, false);

    status = app_board_camera_spi_restore_pins();

    if (APP_STATUS_OK == status)
    {
        status = app_board_camera_spi_set_mode(mode);
    }
#else
    (void) mode;
    status = APP_STATUS_NOT_CONFIGURED;
#endif

    return status;
}

app_status_t app_board_camera_debug_read_spi_pins(app_board_spi_pin_levels_t * p_levels)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_levels)
    {
        return status;
    }

    memset(p_levels, 0, sizeof(*p_levels));

#if APP_HAS_FSP_SPI_CAMERA && APP_ARDUCAM_CS_GPIO_ENABLED
    p_levels->cs_status = app_board_read_pin_level(APP_ARDUCAM_CS_PIN, &p_levels->cs_high);
    p_levels->miso_status = app_board_read_pin_level(APP_ARDUCAM_SPI_MISO_PIN, &p_levels->miso_high);
    p_levels->mosi_status = app_board_read_pin_level(APP_ARDUCAM_SPI_MOSI_PIN, &p_levels->mosi_high);
    p_levels->sck_status = app_board_read_pin_level(APP_ARDUCAM_SPI_SCK_PIN, &p_levels->sck_high);

    status = p_levels->cs_status;

    if (APP_STATUS_OK == status)
    {
        status = p_levels->miso_status;
    }

    if (APP_STATUS_OK == status)
    {
        status = p_levels->mosi_status;
    }

    if (APP_STATUS_OK == status)
    {
        status = p_levels->sck_status;
    }
#else
    p_levels->cs_status = APP_STATUS_NOT_CONFIGURED;
    p_levels->miso_status = APP_STATUS_NOT_CONFIGURED;
    p_levels->mosi_status = APP_STATUS_NOT_CONFIGURED;
    p_levels->sck_status = APP_STATUS_NOT_CONFIGURED;
    status = APP_STATUS_NOT_CONFIGURED;
#endif

    return status;
}

app_status_t app_board_camera_i2c_write_reg(uint8_t device_address, uint8_t reg, uint8_t value, uint32_t timeout_ms)
{
    app_status_t status;

    (void) device_address;

#if APP_HAS_FSP_I2C_CAMERA
    uint8_t buffer[2];
    fsp_err_t err;

    buffer[0] = reg;
    buffer[1] = value;
    g_i2c_transfer_complete = false;
    g_i2c_transfer_error = false;

    err = g_i2c_camera.p_api->write(g_i2c_camera.p_ctrl, buffer, sizeof(buffer), false);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    status = app_wait_for_transfer(&g_i2c_transfer_complete, &g_i2c_transfer_error, timeout_ms);
#else
    (void) reg;
    (void) value;
    (void) timeout_ms;
    status = APP_STATUS_NOT_CONFIGURED;
#endif

    return status;
}

app_status_t app_board_camera_i2c_read_reg(uint8_t device_address, uint8_t reg, uint8_t * p_value, uint32_t timeout_ms)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    (void) device_address;

    if (NULL != p_value)
    {
#if APP_HAS_FSP_I2C_CAMERA
        fsp_err_t err;

        g_i2c_transfer_complete = false;
        g_i2c_transfer_error = false;
        err = g_i2c_camera.p_api->write(g_i2c_camera.p_ctrl, &reg, 1U, true);

        if (FSP_SUCCESS != err)
        {
            return APP_STATUS_IO_ERROR;
        }

        status = app_wait_for_transfer(&g_i2c_transfer_complete, &g_i2c_transfer_error, timeout_ms);

        if (APP_STATUS_OK == status)
        {
            g_i2c_transfer_complete = false;
            g_i2c_transfer_error = false;
            err = g_i2c_camera.p_api->read(g_i2c_camera.p_ctrl, p_value, 1U, false);

            if (FSP_SUCCESS != err)
            {
                return APP_STATUS_IO_ERROR;
            }

            status = app_wait_for_transfer(&g_i2c_transfer_complete, &g_i2c_transfer_error, timeout_ms);
        }
#else
        (void) reg;
        (void) timeout_ms;
        status = APP_STATUS_NOT_CONFIGURED;
#endif
    }

    return status;
}

#if APP_HAS_FSP_UART_CAMERA
void app_camera_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        if (UART_EVENT_RX_CHAR == p_args->event)
        {
            app_camera_ring_push((uint8_t) p_args->data);
        }
        else if (UART_EVENT_TX_COMPLETE == p_args->event)
        {
            g_camera_uart_tx_complete = true;
        }
        else
        {
            /* Other UART events are ignored in this MVP. */
        }
    }
}

static bool app_camera_ring_pop(uint8_t * p_byte)
{
    bool has_data = false;

    if (g_camera_rx_read_index != g_camera_rx_write_index)
    {
        *p_byte = g_camera_rx_buffer[g_camera_rx_read_index];
        g_camera_rx_read_index = (uint16_t) ((g_camera_rx_read_index + 1U) % APP_CAMERA_RX_BUFFER_SIZE);
        has_data = true;
    }

    return has_data;
}

static void app_camera_ring_push(uint8_t byte)
{
    uint16_t next_index = (uint16_t) ((g_camera_rx_write_index + 1U) % APP_CAMERA_RX_BUFFER_SIZE);

    if (next_index != g_camera_rx_read_index)
    {
        g_camera_rx_buffer[g_camera_rx_write_index] = byte;
        g_camera_rx_write_index = next_index;
    }
}
#endif

#if APP_HAS_FSP_UART_CONTROL
void app_control_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        if (UART_EVENT_TX_COMPLETE == p_args->event)
        {
            g_control_uart_tx_complete = true;
        }
    }
}
#endif

#if APP_HAS_FSP_UART_HOST
void app_host_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        if (UART_EVENT_RX_CHAR == p_args->event)
        {
            uint8_t rx_byte = (uint8_t) p_args->data;

            app_host_ring_push(rx_byte);
            g_host_rx_last_byte = rx_byte;
            g_host_rx_irq_count++;

#if APP_HOST_LINK_RX_IRQ_LED_TOGGLE_ENABLED && APP_DEBUG_LED_ENABLED
            g_host_rx_led_high = !g_host_rx_led_high;
            (void) g_ioport.p_api->pinWrite(g_ioport.p_ctrl,
                                             APP_DEBUG_LED_PIN,
                                             g_host_rx_led_high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
#endif
        }
        else if (UART_EVENT_TX_COMPLETE == p_args->event)
        {
            g_host_uart_tx_complete = true;
        }
        else
        {
            /* Other UART events ignored. */
        }
    }
}

static bool app_host_ring_pop(uint8_t * p_byte)
{
    bool has_data = false;

    if (g_host_rx_read_index != g_host_rx_write_index)
    {
        *p_byte = g_host_rx_buffer[g_host_rx_read_index];
        g_host_rx_read_index = (uint16_t) ((g_host_rx_read_index + 1U) % APP_HOST_RX_BUFFER_SIZE);
        has_data = true;
    }

    return has_data;
}

static void app_host_ring_push(uint8_t byte)
{
    uint16_t next_index = (uint16_t) ((g_host_rx_write_index + 1U) % APP_HOST_RX_BUFFER_SIZE);

    if (next_index != g_host_rx_read_index)
    {
        g_host_rx_buffer[g_host_rx_write_index] = byte;
        g_host_rx_write_index = next_index;
    }
}
#endif

#if APP_HAS_FSP_SPI_CAMERA
void app_camera_spi_callback(spi_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        if (SPI_EVENT_TRANSFER_COMPLETE == p_args->event)
        {
            g_spi_transfer_complete = true;
        }
        else
        {
            g_spi_transfer_error = true;
        }
    }
}
#endif

#if APP_HAS_FSP_I2C_CAMERA
void app_camera_i2c_callback(i2c_master_callback_args_t * p_args)
{
    if (NULL != p_args)
    {
        if ((I2C_MASTER_EVENT_TX_COMPLETE == p_args->event) ||
            (I2C_MASTER_EVENT_RX_COMPLETE == p_args->event))
        {
            g_i2c_transfer_complete = true;
        }
        else
        {
            g_i2c_transfer_error = true;
        }
    }
}
#endif

#if APP_HAS_FSP_UART_CAMERA || APP_HAS_FSP_UART_CONTROL || APP_HAS_FSP_UART_HOST
static app_status_t app_uart_write_blocking(const uart_instance_t * p_uart,
                                            volatile bool * p_tx_complete,
                                            const uint8_t * p_data,
                                            uint32_t length,
                                            uint32_t timeout_ms)
{
    fsp_err_t err;
    uint32_t waited_ms = 0U;
    app_status_t status = APP_STATUS_TIMEOUT;

    *p_tx_complete = false;
    err = p_uart->p_api->write(p_uart->p_ctrl, p_data, length);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    do
    {
        if (*p_tx_complete)
        {
            status = APP_STATUS_OK;
            break;
        }

        app_board_delay_ms(1U);
        waited_ms++;
    } while (waited_ms < timeout_ms);

    return status;
}
#endif

#if APP_HAS_FSP_SPI_CAMERA || APP_HAS_FSP_I2C_CAMERA
static app_status_t app_wait_for_transfer(volatile bool * p_complete,
                                          volatile bool * p_error,
                                          uint32_t timeout_ms)
{
    uint32_t waited_ms = 0U;
    app_status_t status = APP_STATUS_TIMEOUT;

    do
    {
        if (*p_error)
        {
            status = APP_STATUS_IO_ERROR;
            break;
        }

        if (*p_complete)
        {
            status = APP_STATUS_OK;
            break;
        }

        app_board_delay_ms(1U);
        waited_ms++;
    } while (waited_ms < timeout_ms);

    return status;
}
#endif

#if APP_HAS_FSP_SPI_CAMERA
static app_status_t app_board_read_pin_level(bsp_io_port_pin_t pin, bool * p_high)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_high)
    {
        status = (FSP_SUCCESS == g_ioport.p_api->pinRead(g_ioport.p_ctrl, pin, &level)) ?
                 APP_STATUS_OK : APP_STATUS_IO_ERROR;

        if (APP_STATUS_OK == status)
        {
            *p_high = (BSP_IO_LEVEL_HIGH == level);
        }
    }

    return status;
}

static app_status_t app_board_camera_spi_restore_pins(void)
{
    fsp_err_t err;

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_MISO_PIN,
                                 (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                                 (uint32_t) IOPORT_PERIPHERAL_SPI);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_MOSI_PIN,
                                 (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                                 (uint32_t) IOPORT_PERIPHERAL_SPI);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    err = g_ioport.p_api->pinCfg(g_ioport.p_ctrl,
                                 APP_ARDUCAM_SPI_SCK_PIN,
                                 (uint32_t) IOPORT_CFG_PERIPHERAL_PIN |
                                 (uint32_t) IOPORT_PERIPHERAL_SPI);

    if (FSP_SUCCESS != err)
    {
        return APP_STATUS_IO_ERROR;
    }

    return APP_STATUS_OK;
}

static app_status_t app_board_camera_spi_bitbang_set_pin(bsp_io_port_pin_t pin, bool high)
{
    bsp_io_level_t level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;

    return (FSP_SUCCESS == g_ioport.p_api->pinWrite(g_ioport.p_ctrl, pin, level)) ?
           APP_STATUS_OK : APP_STATUS_IO_ERROR;
}

static app_status_t app_board_camera_spi_bitbang_read_pin(bsp_io_port_pin_t pin, bool * p_high)
{
    return app_board_read_pin_level(pin, p_high);
}

static void app_board_camera_spi_bitbang_delay(void)
{
    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MICROSECONDS);
}
#endif

#if APP_FRUIT_SENSOR_GPIO_ENABLED
static bool app_read_fruit_sensor_once(void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_HIGH;
    bool is_active = false;

    if (FSP_SUCCESS == g_ioport.p_api->pinRead(g_ioport.p_ctrl, APP_FRUIT_SENSOR_PIN, &level))
    {
#if APP_FRUIT_SENSOR_ACTIVE_LOW
        is_active = (BSP_IO_LEVEL_LOW == level);
#else
        is_active = (BSP_IO_LEVEL_HIGH == level);
#endif
    }

    return is_active;
}
#endif

#if APP_CAPTURE_BUTTON_GPIO_ENABLED
static bool app_read_capture_button_once(void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_HIGH;
    bool is_pressed = false;

    if (FSP_SUCCESS == g_ioport.p_api->pinRead(g_ioport.p_ctrl, APP_CAPTURE_BUTTON_PIN, &level))
    {
#if APP_CAPTURE_BUTTON_ACTIVE_LOW
        is_pressed = (BSP_IO_LEVEL_LOW == level);
#else
        is_pressed = (BSP_IO_LEVEL_HIGH == level);
#endif
    }

    return is_pressed;
}
#endif

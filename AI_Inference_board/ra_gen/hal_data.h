/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_sci_uart.h"
#include "r_uart_api.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
#include "r_spi.h"
FSP_HEADER
/** UART on SCI Instance. */
extern const uart_instance_t g_uart_host;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t g_uart_host_ctrl;
extern const uart_cfg_t g_uart_host_cfg;
extern const sci_uart_extended_cfg_t g_uart_host_cfg_extend;

#ifndef app_host_uart_callback
void app_host_uart_callback(uart_callback_args_t *p_args);
#endif
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_i2c_camera;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_i2c_camera_ctrl;
extern const i2c_master_cfg_t g_i2c_camera_cfg;

#ifndef app_camera_i2c_callback
void app_camera_i2c_callback(i2c_master_callback_args_t *p_args);
#endif
/** SPI on SPI Instance. */
extern const spi_instance_t g_spi_camera;

/** Access the SPI instance using these structures when calling API functions directly (::p_api is not used). */
extern spi_instance_ctrl_t g_spi_camera_ctrl;
extern const spi_cfg_t g_spi_camera_cfg;

/** Callback used by SPI Instance. */
#ifndef app_camera_spi_callback
void app_camera_spi_callback(spi_callback_args_t *p_args);
#endif

#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
#define g_spi_camera_P_TRANSFER_TX (NULL)
#else
    #define g_spi_camera_P_TRANSFER_TX (&RA_NOT_DEFINED)
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
#define g_spi_camera_P_TRANSFER_RX (NULL)
#else
    #define g_spi_camera_P_TRANSFER_RX (&RA_NOT_DEFINED)
#endif
#undef RA_NOT_DEFINED
/** UART on SCI Instance. */
extern const uart_instance_t g_uart_control;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t g_uart_control_ctrl;
extern const uart_cfg_t g_uart_control_cfg;
extern const sci_uart_extended_cfg_t g_uart_control_cfg_extend;

#ifndef app_control_uart_callback
void app_control_uart_callback(uart_callback_args_t *p_args);
#endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */

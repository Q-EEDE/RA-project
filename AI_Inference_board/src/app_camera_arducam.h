#ifndef APP_CAMERA_ARDUCAM_H_
#define APP_CAMERA_ARDUCAM_H_

#include "app_types.h"

#define APP_ARDUCAM_SPI_MODE_COUNT    (4U)

typedef struct
{
    uint8_t mode;
    app_status_t set_mode_status;
    app_status_t write_55_status;
    app_status_t read_55_status;
    uint8_t value_55;
    app_status_t check_55_status;
    app_status_t write_aa_status;
    app_status_t read_aa_status;
    uint8_t value_aa;
    app_status_t check_aa_status;
} app_arducam_spi_mode_result_t;

typedef struct
{
    app_status_t begin_status;
    app_status_t write_55_status;
    app_status_t read_55_status;
    uint8_t value_55;
    app_status_t check_55_status;
    app_status_t write_aa_status;
    app_status_t read_aa_status;
    uint8_t value_aa;
    app_status_t check_aa_status;
    app_status_t end_status;
} app_arducam_spi_bitbang_result_t;

typedef struct
{
    app_status_t spi_55_status;
    uint8_t spi_55_value;
    app_status_t spi_aa_status;
    uint8_t spi_aa_value;
    uint8_t spi_mode;
    app_arducam_spi_mode_result_t spi_modes[APP_ARDUCAM_SPI_MODE_COUNT];
    app_arducam_spi_bitbang_result_t spi_bitbang;
    app_status_t ov2640_pid_status;
    uint8_t ov2640_pid;
    app_status_t ov2640_ver_status;
    uint8_t ov2640_ver;
    app_status_t jpeg_init_status;
    app_status_t capture_timing_status;
    app_status_t fifo_flush_status;
    app_status_t final_status;
} app_arducam_test_result_t;

typedef enum
{
    APP_ARDUCAM_FRAME_STATUS_EMPTY = 0,
    APP_ARDUCAM_FRAME_STATUS_OK,
    APP_ARDUCAM_FRAME_STATUS_INVALID_ARGUMENT,
    APP_ARDUCAM_FRAME_STATUS_CAPTURE_TIMEOUT,
    APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_ZERO,
    APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_SMALL,
    APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_LARGE,
    APP_ARDUCAM_FRAME_STATUS_BUFFER_TOO_SMALL,
    APP_ARDUCAM_FRAME_STATUS_SPI_ERROR,
    APP_ARDUCAM_FRAME_STATUS_JPEG_HEADER_ERROR,
    APP_ARDUCAM_FRAME_STATUS_JPEG_TAIL_ERROR
} app_arducam_frame_status_t;

typedef void (* app_arducam_capture_debug_callback_t)(const char * p_step,
                                                      app_status_t status,
                                                      app_arducam_frame_status_t frame_status,
                                                      uint32_t value);

typedef struct
{
    const uint8_t * p_data;
    uint32_t fifo_length;
    uint32_t image_length;
    app_arducam_frame_status_t frame_status;
} app_arducam_frame_t;

app_status_t app_arducam_init(void);
app_status_t app_arducam_capture(app_image_t * p_image);
app_status_t app_arducam_capture_frame(app_arducam_frame_t * p_frame);
app_status_t app_arducam_capture_frame_debug(app_arducam_frame_t * p_frame,
                                             app_arducam_capture_debug_callback_t p_callback);
app_status_t app_arducam_self_test(app_arducam_test_result_t * p_result);
const char * app_arducam_frame_status_to_text(app_arducam_frame_status_t status);

#endif /* APP_CAMERA_ARDUCAM_H_ */

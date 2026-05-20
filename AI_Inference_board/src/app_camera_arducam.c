#include "app_camera_arducam.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app_board.h"
#include "app_config.h"
#include "app_jpeg_decode.h"
#include "app_ov2640_regs.h"
#include "app_protocol.h"

#define ARDUCHIP_TEST1              (0x00U)
#define ARDUCHIP_TIM                (0x03U)
#define ARDUCHIP_FIFO               (0x04U)
#define ARDUCHIP_TRIG               (0x41U)
#define FIFO_SIZE1                  (0x42U)
#define FIFO_SIZE2                  (0x43U)
#define FIFO_SIZE3                  (0x44U)
#define SINGLE_FIFO_READ            (0x3DU)
#define BURST_FIFO_READ             (0x3CU)

#define FIFO_CLEAR_MASK             (0x01U)
#define FIFO_START_MASK             (0x02U)
#define FIFO_RDPTR_RST_MASK         (0x10U)
#define FIFO_WRPTR_RST_MASK         (0x20U)
#define CAP_DONE_MASK               (0x08U)
#define VSYNC_LEVEL_MASK            (0x02U)

#define OV2640_REG_BANK_SEL         (0xFFU)
#define OV2640_REG_COM7             (0x12U)
#define OV2640_REG_PID              (0x0AU)
#define OV2640_REG_VER              (0x0BU)
#define OV2640_BANK_SENSOR          (0x01U)
#define OV2640_COM7_SRST            (0x80U)
#define OV2640_PID_VALUE            (0x26U)

#define SPI_WRITE_FLAG              (0x80U)
#define SPI_READ_MASK               (0x7FU)
#define ARDUCAM_SPI_CHUNK_BYTES     (32U)
#define APP_ARDUCAM_VERIFIED_SPI_MODE (0U)
#define APP_CAMERA_MAX_FRAME_SIZE   (64U * 1024U)
#define APP_CAMERA_MIN_JPEG_SIZE    (4U)
#define JPEG_SOI_BYTE0              (0xFFU)
#define JPEG_SOI_BYTE1              (0xD8U)
#define JPEG_EOI_BYTE0              (0xFFU)
#define JPEG_EOI_BYTE1              (0xD9U)

static uint8_t g_spi_tx_buffer[ARDUCAM_SPI_CHUNK_BYTES];
static uint8_t g_spi_rx_buffer[ARDUCAM_SPI_CHUNK_BYTES];
static uint8_t g_camera_frame_buffer[APP_CAMERA_MAX_FRAME_SIZE];
static uint8_t g_ov2640_jpeg_initialized;

static app_status_t app_arducam_spi_transfer_with_cs(const uint8_t * p_tx,
                                                     uint8_t * p_rx,
                                                     uint32_t length,
                                                     bool verbose_cs);
static app_status_t app_arducam_spi_write_reg(uint8_t reg, uint8_t value);
static app_status_t app_arducam_spi_read_reg(uint8_t reg, uint8_t * p_value);
static app_status_t app_arducam_spi_write_reg_verbose(uint8_t reg, uint8_t value);
static app_status_t app_arducam_spi_read_reg_verbose(uint8_t reg, uint8_t * p_value);
static app_status_t app_arducam_spi_bitbang_write_reg(uint8_t reg, uint8_t value);
static app_status_t app_arducam_spi_bitbang_read_reg(uint8_t reg, uint8_t * p_value);
static void app_arducam_test_spi_bitbang(app_arducam_spi_bitbang_result_t * p_result);
static void app_arducam_test_spi_register(app_arducam_spi_mode_result_t * p_mode,
                                          uint8_t test_value,
                                          app_status_t * p_result_status,
                                          uint8_t * p_result_value);
static void app_arducam_send_spi_reg_debug(const char * p_op,
                                           uint8_t reg,
                                           uint8_t value,
                                           app_status_t status);
static void app_arducam_send_spi_test_debug(uint8_t test_value, app_status_t status, uint8_t value);
static void app_arducam_send_spi_pin_debug(const char * p_tag);
static app_status_t app_arducam_ov2640_write_reg(uint8_t reg, uint8_t value);
static app_status_t app_arducam_ov2640_read_reg(uint8_t reg, uint8_t * p_value);
static app_status_t app_arducam_test_spi_bus(void);
static app_status_t app_arducam_reset_ov2640(void);
static app_status_t app_arducam_check_ov2640_id(void);
static app_status_t app_arducam_init_ov2640_jpeg(void);
static app_status_t app_arducam_write_ov2640_regs(const app_ov2640_reg_t * p_regs);
static app_status_t app_arducam_configure_capture_timing(void);
static app_status_t app_arducam_flush_fifo(void);
static app_status_t app_arducam_start_capture(void);
static app_status_t app_arducam_wait_capture_done(uint8_t * p_trigger);
static app_status_t app_arducam_read_fifo_length(uint32_t * p_length,
                                                 uint8_t * p_len1,
                                                 uint8_t * p_len2,
                                                 uint8_t * p_len3);
static app_status_t app_arducam_validate_fifo_length(uint32_t fifo_length,
                                                     app_arducam_frame_status_t * p_frame_status);
static app_status_t app_arducam_read_fifo_to_buffer(uint32_t fifo_length);
static app_status_t app_arducam_validate_jpeg(uint32_t fifo_length,
                                              uint32_t * p_image_length,
                                              app_arducam_frame_status_t * p_frame_status);
static app_arducam_frame_status_t app_arducam_io_status_to_frame_status(app_status_t status);
static void app_arducam_clear_frame(app_arducam_frame_t * p_frame);
static app_status_t app_arducam_capture_frame_common(app_arducam_frame_t * p_frame,
                                                     app_arducam_capture_debug_callback_t p_callback);
static void app_arducam_notify_capture_step(app_arducam_capture_debug_callback_t p_callback,
                                            const char * p_step,
                                            app_status_t status,
                                            app_arducam_frame_status_t frame_status,
                                            uint32_t value);

app_status_t app_arducam_init(void)
{
    app_status_t status = app_arducam_test_spi_bus();

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_reset_ov2640();
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_check_ov2640_id();
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_init_ov2640_jpeg();
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_configure_capture_timing();
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_flush_fifo();
    }

    return status;
}

app_status_t app_arducam_capture(app_image_t * p_image)
{
    app_arducam_frame_t frame;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_image)
    {
        return status;
    }

    status = app_arducam_capture_frame(&frame);

    if (APP_STATUS_OK == status)
    {
        status = app_jpeg_decode_to_gray96(frame.p_data, frame.image_length, p_image);
    }

    return status;
}

app_status_t app_arducam_capture_frame(app_arducam_frame_t * p_frame)
{
    return app_arducam_capture_frame_common(p_frame, NULL);
}

app_status_t app_arducam_capture_frame_debug(app_arducam_frame_t * p_frame,
                                             app_arducam_capture_debug_callback_t p_callback)
{
    return app_arducam_capture_frame_common(p_frame, p_callback);
}

static app_status_t app_arducam_capture_frame_common(app_arducam_frame_t * p_frame,
                                                     app_arducam_capture_debug_callback_t p_callback)
{
    uint32_t fifo_length = 0U;
    uint32_t image_length = 0U;
    uint8_t trigger = 0U;
    uint8_t len1 = 0U;
    uint8_t len2 = 0U;
    uint8_t len3 = 0U;
    app_arducam_frame_status_t frame_status = APP_ARDUCAM_FRAME_STATUS_INVALID_ARGUMENT;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_frame)
    {
        return status;
    }

    app_arducam_clear_frame(p_frame);

    app_arducam_notify_capture_step(p_callback, "BEGIN", APP_STATUS_OK, p_frame->frame_status, 0U);

    if (0U == g_ov2640_jpeg_initialized)
    {
        app_arducam_notify_capture_step(p_callback, "INIT_BEGIN", APP_STATUS_OK, p_frame->frame_status, 0U);
        status = app_arducam_init_ov2640_jpeg();
        frame_status = app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "INIT", status, frame_status, 0U);
    }
    else
    {
        status = APP_STATUS_OK;
        frame_status = APP_ARDUCAM_FRAME_STATUS_OK;
    }

    app_arducam_notify_capture_step(p_callback, "TIMING_BEGIN", APP_STATUS_OK, frame_status, 0U);
    if (APP_STATUS_OK == status)
    {
        status = app_arducam_configure_capture_timing();
        frame_status = app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "TIMING", status, frame_status, VSYNC_LEVEL_MASK);
    }

    app_arducam_notify_capture_step(p_callback, "FLUSH_BEGIN", APP_STATUS_OK, p_frame->frame_status, 0U);
    if (APP_STATUS_OK == status)
    {
        status = app_arducam_flush_fifo();
        frame_status = app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "FLUSH", status, frame_status, 0U);
    }

    if (APP_STATUS_OK == status)
    {
        app_arducam_notify_capture_step(p_callback, "START_BEGIN", APP_STATUS_OK, frame_status, 0U);
        status = app_arducam_start_capture();
        frame_status = app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "START", status, frame_status, 0U);
    }

    if (APP_STATUS_OK == status)
    {
        app_arducam_notify_capture_step(p_callback, "WAIT_BEGIN", APP_STATUS_OK, frame_status, 0U);
        status = app_arducam_wait_capture_done(&trigger);
        frame_status = (APP_STATUS_TIMEOUT == status) ?
                       APP_ARDUCAM_FRAME_STATUS_CAPTURE_TIMEOUT :
                       app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "WAIT", status, frame_status, trigger);
    }

    if (APP_STATUS_OK == status)
    {
        app_arducam_notify_capture_step(p_callback, "LEN_BEGIN", APP_STATUS_OK, frame_status, 0U);
        status = app_arducam_read_fifo_length(&fifo_length, &len1, &len2, &len3);
        frame_status = app_arducam_io_status_to_frame_status(status);
        p_frame->fifo_length = fifo_length;
        app_arducam_notify_capture_step(p_callback, "LEN_RAW1", status, frame_status, len1);
        app_arducam_notify_capture_step(p_callback, "LEN_RAW2", status, frame_status, len2);
        app_arducam_notify_capture_step(p_callback, "LEN_RAW3", status, frame_status, len3);
        app_arducam_notify_capture_step(p_callback, "LEN", status, frame_status, fifo_length);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_validate_fifo_length(fifo_length, &frame_status);
        app_arducam_notify_capture_step(p_callback, "LEN_CHECK", status, frame_status, fifo_length);
    }

    if (APP_STATUS_OK == status)
    {
        app_arducam_notify_capture_step(p_callback, "READ_BEGIN", APP_STATUS_OK, frame_status, fifo_length);
        status = app_arducam_read_fifo_to_buffer(fifo_length);
        frame_status = app_arducam_io_status_to_frame_status(status);
        app_arducam_notify_capture_step(p_callback, "READ", status, frame_status, fifo_length);
    }

    if (APP_STATUS_OK == status)
    {
        app_arducam_notify_capture_step(p_callback, "JPEG_BEGIN", APP_STATUS_OK, frame_status, fifo_length);
        status = app_arducam_validate_jpeg(fifo_length, &image_length, &frame_status);
        app_arducam_notify_capture_step(p_callback, "JPEG", status, frame_status, image_length);
    }

    if (APP_STATUS_OK == status)
    {
        p_frame->image_length = image_length;
        p_frame->frame_status = APP_ARDUCAM_FRAME_STATUS_OK;
    }
    else
    {
        p_frame->image_length = 0U;
        p_frame->frame_status = frame_status;
    }

    app_arducam_notify_capture_step(p_callback, "END", status, p_frame->frame_status, p_frame->image_length);

    return status;
}

static void app_arducam_notify_capture_step(app_arducam_capture_debug_callback_t p_callback,
                                            const char * p_step,
                                            app_status_t status,
                                            app_arducam_frame_status_t frame_status,
                                            uint32_t value)
{
    if (NULL != p_callback)
    {
        p_callback(p_step, status, frame_status, value);
    }
}

app_status_t app_arducam_self_test(app_arducam_test_result_t * p_result)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_result)
    {
        return status;
    }

    memset(p_result, 0, sizeof(*p_result));
    p_result->spi_55_status = APP_STATUS_IO_ERROR;
    p_result->spi_aa_status = APP_STATUS_IO_ERROR;
    p_result->jpeg_init_status = APP_STATUS_IO_ERROR;
    p_result->capture_timing_status = APP_STATUS_IO_ERROR;
    p_result->spi_bitbang.begin_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.write_55_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.read_55_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.check_55_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.write_aa_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.read_aa_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.check_aa_status = APP_STATUS_NOT_CONFIGURED;
    p_result->spi_bitbang.end_status = APP_STATUS_NOT_CONFIGURED;

    for (uint8_t i = 0U; i < APP_ARDUCAM_SPI_MODE_COUNT; i++)
    {
        p_result->spi_modes[i].mode = i;
        p_result->spi_modes[i].set_mode_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].write_55_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].read_55_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].check_55_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].write_aa_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].read_aa_status = APP_STATUS_NOT_CONFIGURED;
        p_result->spi_modes[i].check_aa_status = APP_STATUS_NOT_CONFIGURED;
    }

    app_arducam_spi_mode_result_t * p_mode = &p_result->spi_modes[APP_ARDUCAM_VERIFIED_SPI_MODE];

    status = app_board_camera_spi_set_mode(APP_ARDUCAM_VERIFIED_SPI_MODE);
    p_mode->set_mode_status = status;
    p_result->spi_mode = APP_ARDUCAM_VERIFIED_SPI_MODE;

    if (APP_STATUS_OK != status)
    {
        p_result->spi_55_status = status;
        p_result->spi_aa_status = status;
    }
    else
    {
        app_arducam_test_spi_register(p_mode, 0x55U, &p_result->spi_55_status, &p_result->spi_55_value);
        app_arducam_test_spi_register(p_mode, 0xAAU, &p_result->spi_aa_status, &p_result->spi_aa_value);
    }

    app_arducam_test_spi_bitbang(&p_result->spi_bitbang);

    if ((APP_STATUS_OK != p_result->spi_55_status) || (APP_STATUS_OK != p_result->spi_aa_status))
    {
        (void) app_board_camera_spi_set_mode(0U);
        p_result->spi_mode = 0U;
    }

    status = app_arducam_reset_ov2640();

    if (APP_STATUS_OK == status)
    {
        p_result->ov2640_pid_status = app_arducam_ov2640_write_reg(OV2640_REG_BANK_SEL, OV2640_BANK_SENSOR);
    }
    else
    {
        p_result->ov2640_pid_status = status;
    }

    if (APP_STATUS_OK == p_result->ov2640_pid_status)
    {
        p_result->ov2640_pid_status = app_arducam_ov2640_read_reg(OV2640_REG_PID, &p_result->ov2640_pid);
    }

    if ((APP_STATUS_OK == p_result->ov2640_pid_status) && (OV2640_PID_VALUE != p_result->ov2640_pid))
    {
        p_result->ov2640_pid_status = APP_STATUS_CAMERA_NOT_READY;
    }

    p_result->ov2640_ver_status = app_arducam_ov2640_read_reg(OV2640_REG_VER, &p_result->ov2640_ver);

    if ((APP_STATUS_OK == p_result->ov2640_pid_status) && (APP_STATUS_OK == p_result->ov2640_ver_status))
    {
        p_result->jpeg_init_status = app_arducam_init_ov2640_jpeg();
    }

    if (APP_STATUS_OK == p_result->jpeg_init_status)
    {
        p_result->capture_timing_status = app_arducam_configure_capture_timing();
    }

    p_result->fifo_flush_status = app_arducam_flush_fifo();
    p_result->final_status = APP_STATUS_OK;

    if (APP_STATUS_OK != p_result->spi_55_status)
    {
        p_result->final_status = p_result->spi_55_status;
    }
    else if (APP_STATUS_OK != p_result->spi_aa_status)
    {
        p_result->final_status = p_result->spi_aa_status;
    }
    else if (APP_STATUS_OK != p_result->ov2640_pid_status)
    {
        p_result->final_status = p_result->ov2640_pid_status;
    }
    else if (APP_STATUS_OK != p_result->ov2640_ver_status)
    {
        p_result->final_status = p_result->ov2640_ver_status;
    }
    else if (APP_STATUS_OK != p_result->jpeg_init_status)
    {
        p_result->final_status = p_result->jpeg_init_status;
    }
    else if (APP_STATUS_OK != p_result->capture_timing_status)
    {
        p_result->final_status = p_result->capture_timing_status;
    }
    else if (APP_STATUS_OK != p_result->fifo_flush_status)
    {
        p_result->final_status = p_result->fifo_flush_status;
    }
    else
    {
        /* All self-test checks passed. */
    }

    return p_result->final_status;
}

const char * app_arducam_frame_status_to_text(app_arducam_frame_status_t status)
{
    const char * p_text = "UNKNOWN";

    switch (status)
    {
        case APP_ARDUCAM_FRAME_STATUS_EMPTY:
        {
            p_text = "EMPTY";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_OK:
        {
            p_text = "OK";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_INVALID_ARGUMENT:
        {
            p_text = "INVALID_ARGUMENT";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_CAPTURE_TIMEOUT:
        {
            p_text = "CAPTURE_TIMEOUT";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_ZERO:
        {
            p_text = "FIFO_LENGTH_ZERO";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_SMALL:
        {
            p_text = "FIFO_LENGTH_TOO_SMALL";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_LARGE:
        {
            p_text = "FIFO_LENGTH_TOO_LARGE";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_BUFFER_TOO_SMALL:
        {
            p_text = "BUFFER_TOO_SMALL";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_SPI_ERROR:
        {
            p_text = "SPI_ERROR";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_JPEG_HEADER_ERROR:
        {
            p_text = "JPEG_HEADER_ERROR";
            break;
        }

        case APP_ARDUCAM_FRAME_STATUS_JPEG_TAIL_ERROR:
        {
            p_text = "JPEG_TAIL_ERROR";
            break;
        }

        default:
        {
            break;
        }
    }

    return p_text;
}

static app_status_t app_arducam_spi_transfer_with_cs(const uint8_t * p_tx,
                                                     uint8_t * p_rx,
                                                     uint32_t length,
                                                     bool verbose_cs)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL == p_tx) || (NULL == p_rx) || (0U == length))
    {
        return status;
    }

    status = app_board_camera_cs_set(false);

    if (APP_STATUS_OK == status)
    {
        if (verbose_cs)
        {
            (void) app_protocol_send_text("$DBG,CS,LOW\r\n");
            app_arducam_send_spi_pin_debug("AFTER_LOW");
        }

        app_board_delay_us(APP_ARDUCAM_CS_SETUP_US);
        status = app_board_camera_spi_transfer(p_tx, p_rx, length, APP_ARDUCAM_SPI_TIMEOUT_MS);
        app_board_delay_us(APP_ARDUCAM_CS_HOLD_US);

        app_status_t cs_status = app_board_camera_cs_set(true);

        if (verbose_cs)
        {
            (void) app_protocol_send_text("$DBG,CS,HIGH\r\n");
            app_arducam_send_spi_pin_debug("AFTER_HIGH");
        }

        if ((APP_STATUS_OK == status) && (APP_STATUS_OK != cs_status))
        {
            status = cs_status;
        }
    }

    return status;
}

static app_status_t app_arducam_spi_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];

    tx[0] = (uint8_t) (reg | SPI_WRITE_FLAG);
    tx[1] = value;

    return app_arducam_spi_transfer_with_cs(tx, rx, sizeof(tx), false);
}

static app_status_t app_arducam_spi_read_reg(uint8_t reg, uint8_t * p_value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_value)
    {
        return status;
    }

    tx[0] = (uint8_t) (reg & SPI_READ_MASK);
    tx[1] = 0x00U;

    status = app_arducam_spi_transfer_with_cs(tx, rx, sizeof(tx), false);

    if (APP_STATUS_OK == status)
    {
        *p_value = rx[1];
    }

    return status;
}

static app_status_t app_arducam_spi_write_reg_verbose(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    app_status_t status;

    tx[0] = (uint8_t) (reg | SPI_WRITE_FLAG);
    tx[1] = value;

    status = app_arducam_spi_transfer_with_cs(tx, rx, sizeof(tx), true);
    app_arducam_send_spi_reg_debug("WR", reg, value, status);

    return status;
}

static app_status_t app_arducam_spi_read_reg_verbose(uint8_t reg, uint8_t * p_value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    uint8_t value = 0U;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_value)
    {
        return status;
    }

    tx[0] = (uint8_t) (reg & SPI_READ_MASK);
    tx[1] = 0x00U;

    status = app_arducam_spi_transfer_with_cs(tx, rx, sizeof(tx), true);

    if (APP_STATUS_OK == status)
    {
        value = rx[1];
        *p_value = value;
    }

    app_arducam_send_spi_reg_debug("RD", reg, value, status);

    return status;
}

static void app_arducam_test_spi_register(app_arducam_spi_mode_result_t * p_mode,
                                          uint8_t test_value,
                                          app_status_t * p_result_status,
                                          uint8_t * p_result_value)
{
    uint8_t read_value = 0U;
    app_status_t write_status;
    app_status_t read_status = APP_STATUS_NOT_CONFIGURED;
    app_status_t check_status;

    if ((NULL == p_mode) || (NULL == p_result_status) || (NULL == p_result_value))
    {
        return;
    }

    if (0U != APP_ARDUCAM_SPI_SELFTEST_VERBOSE)
    {
        write_status = app_arducam_spi_write_reg_verbose(ARDUCHIP_TEST1, test_value);
    }
    else
    {
        write_status = app_arducam_spi_write_reg(ARDUCHIP_TEST1, test_value);
    }

    if (APP_STATUS_OK == write_status)
    {
        if (0U != APP_ARDUCAM_SPI_SELFTEST_VERBOSE)
        {
            read_status = app_arducam_spi_read_reg_verbose(ARDUCHIP_TEST1, &read_value);
        }
        else
        {
            read_status = app_arducam_spi_read_reg(ARDUCHIP_TEST1, &read_value);
        }
    }

    check_status = (APP_STATUS_OK == write_status) ? read_status : write_status;

    if ((APP_STATUS_OK == check_status) && (test_value != read_value))
    {
        check_status = APP_STATUS_IO_ERROR;
    }

    if (0x55U == test_value)
    {
        p_mode->write_55_status = write_status;
        p_mode->read_55_status = read_status;
        p_mode->value_55 = read_value;
        p_mode->check_55_status = check_status;
    }
    else
    {
        p_mode->write_aa_status = write_status;
        p_mode->read_aa_status = read_status;
        p_mode->value_aa = read_value;
        p_mode->check_aa_status = check_status;
    }

    *p_result_status = check_status;
    *p_result_value = read_value;

    if (0U != APP_ARDUCAM_SPI_SELFTEST_VERBOSE)
    {
        app_arducam_send_spi_test_debug(test_value, check_status, read_value);
    }
}

static void app_arducam_send_spi_reg_debug(const char * p_op,
                                           uint8_t reg,
                                           uint8_t value,
                                           app_status_t status)
{
    char line[96];
    int written = 0;

    if (NULL == p_op)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,SPI,%s,REG,%02X,VAL,%02X,RET,%s\r\n",
                       p_op,
                       (unsigned int) reg,
                       (unsigned int) value,
                       app_status_to_text(status));

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_arducam_send_spi_test_debug(uint8_t test_value, app_status_t status, uint8_t value)
{
    char line[64];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,SPI,TEST%02X,%s,%02X\r\n",
                       (unsigned int) test_value,
                       (APP_STATUS_OK == status) ? "OK" : "FAIL",
                       (unsigned int) value);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_arducam_send_spi_pin_debug(const char * p_tag)
{
    app_board_spi_pin_levels_t levels;
    char line[128];
    int written = 0;
    app_status_t status;

    if (NULL == p_tag)
    {
        return;
    }

    status = app_board_camera_debug_read_spi_pins(&levels);

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,PIN,%s,RET,%s,CS,%u,MISO,%u,MOSI,%u,SCK,%u\r\n",
                       p_tag,
                       app_status_to_text(status),
                       levels.cs_high ? 1U : 0U,
                       levels.miso_high ? 1U : 0U,
                       levels.mosi_high ? 1U : 0U,
                       levels.sck_high ? 1U : 0U);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static app_status_t app_arducam_spi_bitbang_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    app_status_t status;

    tx[0] = (uint8_t) (reg | SPI_WRITE_FLAG);
    tx[1] = value;

    status = app_board_camera_cs_set(false);

    if (APP_STATUS_OK == status)
    {
        app_board_delay_us(APP_ARDUCAM_CS_SETUP_US);
        status = app_board_camera_spi_bitbang_transfer(tx, rx, sizeof(tx));
        app_board_delay_us(APP_ARDUCAM_CS_HOLD_US);
        (void) app_board_camera_cs_set(true);
    }

    return status;
}

static app_status_t app_arducam_spi_bitbang_read_reg(uint8_t reg, uint8_t * p_value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_value)
    {
        return status;
    }

    tx[0] = (uint8_t) (reg & SPI_READ_MASK);
    tx[1] = 0x00U;

    status = app_board_camera_cs_set(false);

    if (APP_STATUS_OK == status)
    {
        app_board_delay_us(APP_ARDUCAM_CS_SETUP_US);
        status = app_board_camera_spi_bitbang_transfer(tx, rx, sizeof(tx));
        app_board_delay_us(APP_ARDUCAM_CS_HOLD_US);
        (void) app_board_camera_cs_set(true);
    }

    if (APP_STATUS_OK == status)
    {
        *p_value = rx[1];
    }

    return status;
}

static void app_arducam_test_spi_bitbang(app_arducam_spi_bitbang_result_t * p_result)
{
    if (NULL == p_result)
    {
        return;
    }

    p_result->begin_status = app_board_camera_spi_bitbang_begin();

    if (APP_STATUS_OK == p_result->begin_status)
    {
        p_result->write_55_status = app_arducam_spi_bitbang_write_reg(ARDUCHIP_TEST1, 0x55U);
    }

    if (APP_STATUS_OK == p_result->write_55_status)
    {
        p_result->read_55_status = app_arducam_spi_bitbang_read_reg(ARDUCHIP_TEST1, &p_result->value_55);
    }

    if (APP_STATUS_OK == p_result->read_55_status)
    {
        p_result->check_55_status = (0x55U == p_result->value_55) ? APP_STATUS_OK : APP_STATUS_IO_ERROR;
    }

    if (APP_STATUS_OK == p_result->begin_status)
    {
        p_result->write_aa_status = app_arducam_spi_bitbang_write_reg(ARDUCHIP_TEST1, 0xAAU);
    }

    if (APP_STATUS_OK == p_result->write_aa_status)
    {
        p_result->read_aa_status = app_arducam_spi_bitbang_read_reg(ARDUCHIP_TEST1, &p_result->value_aa);
    }

    if (APP_STATUS_OK == p_result->read_aa_status)
    {
        p_result->check_aa_status = (0xAAU == p_result->value_aa) ? APP_STATUS_OK : APP_STATUS_IO_ERROR;
    }

    p_result->end_status = app_board_camera_spi_bitbang_end(0U);
}

static app_status_t app_arducam_ov2640_write_reg(uint8_t reg, uint8_t value)
{
    return app_board_camera_i2c_write_reg(APP_ARDUCAM_I2C_ADDRESS, reg, value, APP_ARDUCAM_I2C_TIMEOUT_MS);
}

static app_status_t app_arducam_ov2640_read_reg(uint8_t reg, uint8_t * p_value)
{
    return app_board_camera_i2c_read_reg(APP_ARDUCAM_I2C_ADDRESS, reg, p_value, APP_ARDUCAM_I2C_TIMEOUT_MS);
}

static app_status_t app_arducam_test_spi_bus(void)
{
    uint8_t value = 0U;
    app_status_t status = app_arducam_spi_write_reg(ARDUCHIP_TEST1, 0x55U);

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_spi_read_reg(ARDUCHIP_TEST1, &value);
    }

    if ((APP_STATUS_OK == status) && (0x55U != value))
    {
        status = APP_STATUS_IO_ERROR;
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_spi_write_reg(ARDUCHIP_TEST1, 0xAAU);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_spi_read_reg(ARDUCHIP_TEST1, &value);
    }

    if ((APP_STATUS_OK == status) && (0xAAU != value))
    {
        status = APP_STATUS_IO_ERROR;
    }

    return status;
}

static app_status_t app_arducam_reset_ov2640(void)
{
    app_status_t status = app_arducam_ov2640_write_reg(OV2640_REG_BANK_SEL, OV2640_BANK_SENSOR);

    g_ov2640_jpeg_initialized = 0U;

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_ov2640_write_reg(OV2640_REG_COM7, OV2640_COM7_SRST);
    }

    if (APP_STATUS_OK == status)
    {
        app_board_delay_ms(100U);
    }

    return status;
}

static app_status_t app_arducam_init_ov2640_jpeg(void)
{
    app_status_t status = app_arducam_write_ov2640_regs(g_ov2640_jpeg_init_regs);

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_write_ov2640_regs(g_ov2640_yuv422_regs);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_write_ov2640_regs(g_ov2640_jpeg_regs);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_ov2640_write_reg(0xffU, 0x01U);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_ov2640_write_reg(0x15U, 0x00U);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_write_ov2640_regs(g_ov2640_320x240_jpeg_regs);
    }

    if (APP_STATUS_OK == status)
    {
        g_ov2640_jpeg_initialized = 1U;
        app_board_delay_ms(100U);
    }

    return status;
}

static app_status_t app_arducam_write_ov2640_regs(const app_ov2640_reg_t * p_regs)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_regs)
    {
        status = APP_STATUS_OK;

        for (uint32_t i = 0U;
             !((APP_OV2640_REG_END_REG == p_regs[i].reg) && (APP_OV2640_REG_END_VALUE == p_regs[i].value));
             i++)
        {
            status = app_arducam_ov2640_write_reg(p_regs[i].reg, p_regs[i].value);

            if (APP_STATUS_OK != status)
            {
                break;
            }
        }
    }

    return status;
}

static app_status_t app_arducam_configure_capture_timing(void)
{
    return app_arducam_spi_write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK);
}

static app_status_t app_arducam_check_ov2640_id(void)
{
    uint8_t pid = 0U;
    uint8_t ver = 0U;
    app_status_t status = app_arducam_ov2640_write_reg(OV2640_REG_BANK_SEL, OV2640_BANK_SENSOR);

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_ov2640_read_reg(OV2640_REG_PID, &pid);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_ov2640_read_reg(OV2640_REG_VER, &ver);
    }

    (void) ver;

    if ((APP_STATUS_OK == status) && (OV2640_PID_VALUE != pid))
    {
        status = APP_STATUS_CAMERA_NOT_READY;
    }

    return status;
}

static app_status_t app_arducam_flush_fifo(void)
{
    app_status_t status = app_arducam_spi_write_reg(ARDUCHIP_FIFO, FIFO_RDPTR_RST_MASK);

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_spi_write_reg(ARDUCHIP_FIFO, FIFO_WRPTR_RST_MASK);
    }

    if (APP_STATUS_OK == status)
    {
        status = app_arducam_spi_write_reg(ARDUCHIP_FIFO, FIFO_CLEAR_MASK);
    }

    return status;
}

static app_status_t app_arducam_start_capture(void)
{
    return app_arducam_spi_write_reg(ARDUCHIP_FIFO, FIFO_START_MASK);
}

static app_status_t app_arducam_wait_capture_done(uint8_t * p_trigger)
{
    uint32_t waited_ms = 0U;
    uint8_t trigger = 0U;
    app_status_t status = APP_STATUS_TIMEOUT;

    do
    {
        app_status_t read_status = app_arducam_spi_read_reg(ARDUCHIP_TRIG, &trigger);

        if (NULL != p_trigger)
        {
            *p_trigger = trigger;
        }

        if (APP_STATUS_OK != read_status)
        {
            status = read_status;
            break;
        }

        if (0U != (trigger & CAP_DONE_MASK))
        {
            status = APP_STATUS_OK;
            break;
        }

        app_board_delay_ms(1U);
        waited_ms++;
    } while (waited_ms < APP_ARDUCAM_CAPTURE_TIMEOUT_MS);

    return status;
}

static app_status_t app_arducam_read_fifo_length(uint32_t * p_length,
                                                 uint8_t * p_len1,
                                                 uint8_t * p_len2,
                                                 uint8_t * p_len3)
{
    uint8_t len1 = 0U;
    uint8_t len2 = 0U;
    uint8_t len3 = 0U;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_length)
    {
        return status;
    }

    status = app_arducam_spi_read_reg(FIFO_SIZE1, &len1);
    status = (APP_STATUS_OK == status) ? app_arducam_spi_read_reg(FIFO_SIZE2, &len2) : status;
    status = (APP_STATUS_OK == status) ? app_arducam_spi_read_reg(FIFO_SIZE3, &len3) : status;

    if (NULL != p_len1)
    {
        *p_len1 = len1;
    }

    if (NULL != p_len2)
    {
        *p_len2 = len2;
    }

    if (NULL != p_len3)
    {
        *p_len3 = len3;
    }

    if (APP_STATUS_OK == status)
    {
        *p_length = ((uint32_t) (len3 & 0x7FU) << 16) | ((uint32_t) len2 << 8) | (uint32_t) len1;
    }

    return status;
}

static app_status_t app_arducam_validate_fifo_length(uint32_t fifo_length,
                                                     app_arducam_frame_status_t * p_frame_status)
{
    app_status_t status = APP_STATUS_FRAME_ERROR;

    if (0U == fifo_length)
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_ZERO;
    }
    else if (fifo_length < APP_CAMERA_MIN_JPEG_SIZE)
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_SMALL;
    }
    else if (fifo_length > APP_ARDUCAM_FIFO_MAX_BYTES)
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_LARGE;
    }
    else if (fifo_length > APP_CAMERA_MAX_FRAME_SIZE)
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_OK;
        status = APP_STATUS_OK;
    }

    return status;
}

static app_status_t app_arducam_read_fifo_to_buffer(uint32_t fifo_length)
{
    uint32_t image_index = 0U;
    uint32_t bytes_to_read = fifo_length;
    app_status_t status;

    memset(g_camera_frame_buffer, 0, sizeof(g_camera_frame_buffer));
    memset(g_spi_tx_buffer, 0, sizeof(g_spi_tx_buffer));

    status = app_arducam_spi_write_reg(ARDUCHIP_FIFO, FIFO_RDPTR_RST_MASK);

    if (APP_STATUS_OK == status)
    {
        status = app_board_camera_cs_set(false);
    }

    if (APP_STATUS_OK == status)
    {
        uint8_t command_tx = BURST_FIFO_READ;
        uint8_t command_rx = 0U;

        status = app_board_camera_spi_transfer(&command_tx, &command_rx, 1U, APP_ARDUCAM_SPI_TIMEOUT_MS);
    }

    while ((APP_STATUS_OK == status) && (image_index < bytes_to_read))
    {
        uint32_t chunk_size = bytes_to_read - image_index;

        if (chunk_size > ARDUCAM_SPI_CHUNK_BYTES)
        {
            chunk_size = ARDUCAM_SPI_CHUNK_BYTES;
        }

        status = app_board_camera_spi_transfer(g_spi_tx_buffer, g_spi_rx_buffer, chunk_size, APP_ARDUCAM_SPI_TIMEOUT_MS);

        if (APP_STATUS_OK == status)
        {
            memcpy(&g_camera_frame_buffer[image_index], g_spi_rx_buffer, chunk_size);
            image_index += chunk_size;
        }
    }

    (void) app_board_camera_cs_set(true);

    return status;
}

static app_status_t app_arducam_validate_jpeg(uint32_t fifo_length,
                                              uint32_t * p_image_length,
                                              app_arducam_frame_status_t * p_frame_status)
{
    app_status_t status = APP_STATUS_FRAME_ERROR;
    uint32_t tail_index = 0U;

    *p_image_length = 0U;

    if ((JPEG_SOI_BYTE0 != g_camera_frame_buffer[0]) || (JPEG_SOI_BYTE1 != g_camera_frame_buffer[1]))
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_JPEG_HEADER_ERROR;
        return status;
    }

    for (uint32_t i = 1U; i < fifo_length; i++)
    {
        if ((JPEG_EOI_BYTE0 == g_camera_frame_buffer[i - 1U]) && (JPEG_EOI_BYTE1 == g_camera_frame_buffer[i]))
        {
            tail_index = i;
            break;
        }
    }

    if (0U == tail_index)
    {
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_JPEG_TAIL_ERROR;
    }
    else
    {
        *p_image_length = tail_index + 1U;
        *p_frame_status = APP_ARDUCAM_FRAME_STATUS_OK;
        status = APP_STATUS_OK;
    }

    return status;
}

static app_arducam_frame_status_t app_arducam_io_status_to_frame_status(app_status_t status)
{
    app_arducam_frame_status_t frame_status = APP_ARDUCAM_FRAME_STATUS_SPI_ERROR;

    if (APP_STATUS_OK == status)
    {
        frame_status = APP_ARDUCAM_FRAME_STATUS_OK;
    }
    else if (APP_STATUS_INVALID_ARGUMENT == status)
    {
        frame_status = APP_ARDUCAM_FRAME_STATUS_INVALID_ARGUMENT;
    }
    else
    {
        frame_status = APP_ARDUCAM_FRAME_STATUS_SPI_ERROR;
    }

    return frame_status;
}

static void app_arducam_clear_frame(app_arducam_frame_t * p_frame)
{
    p_frame->p_data = g_camera_frame_buffer;
    p_frame->fifo_length = 0U;
    p_frame->image_length = 0U;
    p_frame->frame_status = APP_ARDUCAM_FRAME_STATUS_EMPTY;
}

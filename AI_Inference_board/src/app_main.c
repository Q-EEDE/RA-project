#include "app_main.h"
#include <stdio.h>
#include "app_board.h"
#include "app_camera_arducam.h"
#include "app_camera_uart.h"
#include "app_config.h"
#include "app_host_link.h"
#include "app_inference.h"
#include "app_jpeg_decode.h"
#include "app_protocol.h"
#include "app_types.h"

static app_image_t g_capture_image;

typedef enum
{
    APP_CAPTURE_STAGE_NONE = 0,
    APP_CAPTURE_STAGE_CAP,
    APP_CAPTURE_STAGE_JDEC,
    APP_CAPTURE_STAGE_INF
} app_capture_stage_t;

static void app_send_error_result(app_status_t status);
static void app_send_arducam_test_report(const app_arducam_test_result_t * p_result);
static void app_send_arducam_test_line(const char * p_name, app_status_t status, uint8_t value);
static void app_send_arducam_spi_mode_line(const app_arducam_spi_mode_result_t * p_result);
static void app_send_arducam_spi_bitbang_line(const app_arducam_spi_bitbang_result_t * p_result);
static void app_send_capture_frame_debug(app_status_t capture_status, const app_arducam_frame_t * p_frame);
static void app_send_capture_frame_loop_debug(app_status_t capture_status, const app_arducam_frame_t * p_frame);
static void app_send_capture_step_debug(const char * p_step,
                                        app_status_t status,
                                        app_arducam_frame_status_t frame_status,
                                        uint32_t value);
static void app_send_jpeg_decode_debug(const app_jpeg_decode_info_t * p_info);
static void app_send_jpeg_decode_loop_debug(const app_jpeg_decode_info_t * p_info);
static void app_send_inference_debug(app_status_t status, const fruit_result_t * p_result);
static void app_send_self_test_continue_debug(uint8_t error_code);
static void app_send_self_test_skip_capture_debug(uint8_t error_code);
static void app_send_loop_start(uint16_t total_count);
static void app_send_loop_index(uint16_t index);
static void app_send_loop_result(uint16_t index, app_status_t status, app_capture_stage_t fail_stage);
static void app_send_loop_end(uint16_t total_count, uint16_t ok_count, uint16_t fail_count);
#if APP_HOST_LINK_RX_IRQ_DEBUG_ENABLED
static void app_send_host_rx_irq_debug(uint32_t irq_count, uint8_t last_byte);
#endif
static void app_send_u32_decimal(uint32_t value);
static uint8_t app_get_arducam_test_error_code(const app_arducam_test_result_t * p_result);
static uint8_t app_get_arducam_frame_error_code(app_arducam_frame_status_t frame_status);
static uint8_t app_get_fruit_grade_led_count(fruit_grade_t grade);
static const char * app_capture_stage_to_text(app_capture_stage_t stage);
static void app_show_power_on_test_led_status(const app_arducam_test_result_t * p_result);
static void app_debug_led_error_loop(uint8_t error_code);
static void app_debug_led_fast_error_loop(uint8_t error_code);
static void app_debug_led_result_loop(fruit_grade_t grade);
static void app_debug_led_startup_pin_test(void);
static void app_run_power_on_capture_test(void);
static void app_run_button_capture_mode(void);
static void app_run_button_single_capture(uint32_t trigger_count);
static void app_run_power_on_single_capture_test(void);
static void app_run_capture_loop_test(uint16_t total_count);
static app_status_t app_run_single_capture_inference(app_arducam_frame_t * p_frame,
                                                     app_jpeg_decode_info_t * p_decode_info,
                                                     fruit_result_t * p_result,
                                                     app_capture_stage_t * p_fail_stage);
#if APP_HOST_LINK_LOOPBACK_TEST_ENABLED && APP_HAS_FSP_UART_HOST
static void app_run_host_link_loopback_test(void);
#endif

void app_main(void)
{
    app_debug_led_startup_pin_test();

    app_status_t status = app_board_init();

    if (APP_STATUS_OK != status)
    {
        app_send_error_result(status);
        app_debug_led_error_loop(6U);
    }

    app_host_link_init();

    (void) app_protocol_send_text("$DBG,UART,READY,SCI9,115200,8N1,TX=P109,RX=P110\r\n");
    (void) app_protocol_send_text("$DBG,SPI_CFG,CH0,100000HZ,MANUAL_CS=P001\r\n");
#if APP_HAS_FSP_UART_HOST
    (void) app_protocol_send_text("$DBG,HOSTLINK,READY,SCI2,115200,8N1,TX=P302,RX=P301\r\n");
    (void) app_host_link_send_ready();
#else
    (void) app_protocol_send_text("$DBG,HOSTLINK,DISABLED,SET_APP_USE_FSP_UART_HOST=1\r\n");
#endif

#if APP_HOST_LINK_LOOPBACK_TEST_ENABLED && APP_HAS_FSP_UART_HOST
    app_run_host_link_loopback_test();
#endif

#if APP_USE_ARDUCAM_B0067
#if APP_ARDUCAM_POWER_ON_TEST_ENABLED
    app_arducam_test_result_t test_result;

    (void) app_protocol_send_text("$DBG,BOOT,START\r\n");
    status = app_arducam_self_test(&test_result);
    app_send_arducam_test_report(&test_result);
    app_show_power_on_test_led_status(&test_result);
#else
    status = app_arducam_init();
#endif

#if !APP_ARDUCAM_POWER_ON_CAPTURE_TEST_ENABLED
    if (APP_STATUS_OK != status)
    {
        app_send_error_result(status);
    }
#endif

#if APP_ARDUCAM_POWER_ON_CAPTURE_TEST_ENABLED
    if (APP_STATUS_OK == status)
    {
        app_send_self_test_continue_debug(0U);
        app_run_power_on_capture_test();
    }
    else
    {
        uint8_t error_code = app_get_arducam_test_error_code(&test_result);

        app_send_self_test_skip_capture_debug(error_code);
        app_debug_led_fast_error_loop(error_code);
    }
#endif
#endif

    while (1)
    {
        if (!app_board_is_fruit_present())
        {
            app_board_delay_ms(APP_MAIN_IDLE_DELAY_MS);
            continue;
        }

#if APP_USE_ARDUCAM_B0067
        status = app_arducam_capture(&g_capture_image);
#else
        status = app_camera_capture(&g_capture_image);
#endif

        if (APP_STATUS_OK == status)
        {
            fruit_result_t result;

            status = fruit_run_inference(&g_capture_image, &result);

            if (APP_STATUS_OK == status)
            {
                (void) app_protocol_send_result(&result);
            }
            else
            {
                app_send_error_result(status);
            }
        }
        else
        {
            app_send_error_result(status);
        }

        app_board_delay_ms(APP_DETECTION_COOLDOWN_MS);

#if APP_FRUIT_SENSOR_GPIO_ENABLED
        while (app_board_is_fruit_present())
        {
            app_board_delay_ms(APP_MAIN_IDLE_DELAY_MS);
        }
#endif
    }
}

static uint8_t app_get_arducam_test_error_code(const app_arducam_test_result_t * p_result)
{
    uint8_t error_code = 6U;

    if (NULL != p_result)
    {
        if (APP_STATUS_OK != p_result->spi_55_status)
        {
            error_code = 1U;
        }
        else if (APP_STATUS_OK != p_result->spi_aa_status)
        {
            error_code = 2U;
        }
        else if (APP_STATUS_OK != p_result->ov2640_pid_status)
        {
            error_code = 3U;
        }
        else if (APP_STATUS_OK != p_result->ov2640_ver_status)
        {
            error_code = 4U;
        }
        else if (APP_STATUS_OK != p_result->fifo_flush_status)
        {
            error_code = 5U;
        }
        else
        {
            error_code = 0U;
        }
    }

    return error_code;
}

static void app_show_power_on_test_led_status(const app_arducam_test_result_t * p_result)
{
    uint8_t error_code = app_get_arducam_test_error_code(p_result);

    if (0U == error_code)
    {
        app_board_debug_led_set(true);
        app_board_delay_ms(500U);
    }
    else
    {
        app_board_debug_led_blink(error_code, APP_DEBUG_LED_BLINK_ON_MS, APP_DEBUG_LED_BLINK_OFF_MS);
        app_board_delay_ms(APP_DEBUG_LED_GROUP_PAUSE_MS);
    }
}

static void app_run_power_on_capture_test(void)
{
    if (0U != APP_BUTTON_CAPTURE_MODE_ENABLED)
    {
        app_run_button_capture_mode();
    }
    else if (0U != APP_CAPTURE_LOOP_TEST_ENABLED)
    {
        app_run_capture_loop_test((uint16_t) APP_CAPTURE_LOOP_TEST_TOTAL);
    }
    else
    {
        app_run_power_on_single_capture_test();
    }
}

static void app_run_button_capture_mode(void)
{
    app_status_t button_status = app_board_capture_button_init();
    uint32_t trigger_count = 0U;
#if APP_HOST_LINK_RX_IRQ_DEBUG_ENABLED
    uint32_t last_host_rx_irq_count = app_board_host_rx_irq_count();
#endif

    (void) app_protocol_send_text("$DBG,BUTTON_MODE,ENTER\r\n");

    if (APP_STATUS_OK != button_status)
    {
        (void) app_protocol_send_text("$DBG,BUTTON,NOT_CONFIGURED,SET_APP_CAPTURE_BUTTON_PIN\r\n");
    }
    else
    {
        (void) app_protocol_send_text("$DBG,BUTTON,READY,PRESS_ONCE_TO_CAPTURE\r\n");
    }

    while (1)
    {
        bool triggered_by_button = false;
        bool triggered_by_host = false;
#if APP_HOST_LINK_RX_IRQ_DEBUG_ENABLED
        uint32_t host_rx_irq_count;
#endif

        /* 1. 优先看主控板有没有发命令过来 */
        app_host_cmd_t host_cmd = app_host_link_poll();

#if APP_HOST_LINK_RX_IRQ_DEBUG_ENABLED
        host_rx_irq_count = app_board_host_rx_irq_count();
        if (host_rx_irq_count != last_host_rx_irq_count)
        {
            last_host_rx_irq_count = host_rx_irq_count;
            app_send_host_rx_irq_debug(host_rx_irq_count, app_board_host_rx_last_byte());
        }
#endif

        if (APP_HOST_CMD_TRIG == host_cmd)
        {
            (void) app_host_link_send_ack("TRIG");
            (void) app_protocol_send_text("$DBG,HOSTLINK,RX,TRIG\r\n");
            triggered_by_host = true;
        }
        else if (APP_HOST_CMD_PING == host_cmd)
        {
            (void) app_host_link_send_pong();
            (void) app_protocol_send_text("$DBG,HOSTLINK,RX,PING\r\n");
        }
        else if (APP_HOST_CMD_UNKNOWN == host_cmd)
        {
            (void) app_host_link_send_error(APP_STATUS_FRAME_ERROR);
            (void) app_protocol_send_text("$DBG,HOSTLINK,RX,UNKNOWN\r\n");
        }
        else
        {
            /* 没有命令，继续走按键路径 */
        }

        /* 2. 再看按键有没有按下 */
        if ((!triggered_by_host) && (APP_STATUS_OK == button_status) && app_board_is_capture_button_pressed())
        {
            triggered_by_button = true;
        }

        if (triggered_by_button || triggered_by_host)
        {
            trigger_count++;
            app_run_button_single_capture(trigger_count);

            if (triggered_by_button)
            {
                /* 等按键释放，避免一次按住反复触发 */
                while (app_board_is_capture_button_pressed())
                {
                    app_board_delay_ms(APP_CAPTURE_BUTTON_RELEASE_POLL_MS);
                }

                if (APP_STATUS_OK == button_status)
                {
                    (void) app_protocol_send_text("$DBG,BUTTON,READY,PRESS_ONCE_TO_CAPTURE\r\n");
                }
            }

            continue;
        }

        app_board_delay_ms(APP_HOST_LINK_POLL_MS);
    }
}

static void app_run_button_single_capture(uint32_t trigger_count)
{
    app_arducam_frame_t frame = {0};
    app_jpeg_decode_info_t decode_info;
    fruit_result_t result =
    {
        .grade = FRUIT_GRADE_UNKNOWN,
        .score = 0U,
        .status = "NOT_RUN"
    };
    app_capture_stage_t fail_stage = APP_CAPTURE_STAGE_NONE;
    app_status_t status;
    char line[64];
    int written;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,BUTTON,CAPTURE,%lu\r\n",
                       (unsigned long) trigger_count);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }

    status = app_run_single_capture_inference(&frame, &decode_info, &result, &fail_stage);

    if ((APP_ARDUCAM_FRAME_STATUS_OK == frame.frame_status) && (frame.image_length > 0U))
    {
        app_send_capture_frame_loop_debug(APP_STATUS_OK, &frame);
        (void) debug_dump_jpeg_uart(frame.p_data, frame.image_length);
    }
    else
    {
        app_send_capture_frame_loop_debug(status, &frame);
    }

    if (APP_CAPTURE_STAGE_CAP != fail_stage)
    {
        app_send_jpeg_decode_loop_debug(&decode_info);
    }

    if ((APP_CAPTURE_STAGE_NONE == fail_stage) || (APP_CAPTURE_STAGE_INF == fail_stage))
    {
        app_send_inference_debug(status, &result);
    }

    if (APP_STATUS_OK == status)
    {
        (void) app_protocol_send_result(&result);
        (void) app_host_link_send_class_result(result.grade, result.score);
        (void) app_protocol_send_text("$DBG,BUTTON,CAPTURE,OK\r\n");
    }
    else
    {
        app_send_error_result(status);
        (void) app_host_link_send_error(status);
        app_send_loop_result(1U, status, fail_stage);
    }
}

static void app_run_power_on_single_capture_test(void)
{
    app_arducam_frame_t frame = {0};
    app_jpeg_decode_info_t decode_info;
    fruit_result_t result =
    {
        .grade = FRUIT_GRADE_UNKNOWN,
        .score = 0U,
        .status = "NOT_RUN"
    };
    app_status_t capture_status;
    app_status_t status;

    (void) app_protocol_send_text("$DBG,CAPTEST,ENTER\r\n");
    (void) app_protocol_send_text("$DBG,CAPTEST,BEFORE_CAPTURE\r\n");
    capture_status = app_arducam_capture_frame_debug(&frame, app_send_capture_step_debug);
    (void) app_protocol_send_text("$DBG,CAPTEST,AFTER_CAPTURE\r\n");
    app_send_capture_frame_debug(capture_status, &frame);

    if (APP_STATUS_OK == capture_status)
    {
        (void) debug_dump_jpeg_uart(frame.p_data, frame.image_length);

        status = app_jpeg_decode_to_gray96_ex(frame.p_data, frame.image_length, &g_capture_image, &decode_info);
        app_send_jpeg_decode_debug(&decode_info);

        if (APP_STATUS_OK == status)
        {
            status = fruit_run_inference(&g_capture_image, &result);
            app_send_inference_debug(status, &result);
        }

        if (APP_STATUS_OK == status)
        {
            (void) app_protocol_send_result(&result);
            (void) app_host_link_send_class_result(result.grade, result.score);
            app_board_debug_led_set(true);
            app_board_delay_ms(2000U);
            app_debug_led_result_loop(result.grade);
        }
        else
        {
            app_send_error_result(status);
            (void) app_host_link_send_error(status);
            app_debug_led_fast_error_loop(6U);
        }
    }
    else
    {
        app_send_error_result(capture_status);
        (void) app_host_link_send_error(capture_status);
        app_debug_led_fast_error_loop(app_get_arducam_frame_error_code(frame.frame_status));
    }
}

static void app_run_capture_loop_test(uint16_t total_count)
{
    uint16_t ok_count = 0U;
    uint16_t fail_count = 0U;
    bool jpeg_dump_done = false;

    if (0U == total_count)
    {
        total_count = 1U;
    }

    app_send_loop_start(total_count);

    for (uint16_t index = 1U; index <= total_count; index++)
    {
        app_arducam_frame_t frame = {0};
        app_jpeg_decode_info_t decode_info;
        fruit_result_t result =
        {
            .grade = FRUIT_GRADE_UNKNOWN,
            .score = 0U,
            .status = "NOT_RUN"
        };
        app_capture_stage_t fail_stage = APP_CAPTURE_STAGE_NONE;
        app_status_t status;

        app_send_loop_index(index);

        status = app_run_single_capture_inference(&frame, &decode_info, &result, &fail_stage);

        if ((APP_ARDUCAM_FRAME_STATUS_OK == frame.frame_status) && (frame.image_length > 0U))
        {
            app_send_capture_frame_loop_debug(APP_STATUS_OK, &frame);

            if (!jpeg_dump_done)
            {
                (void) debug_dump_jpeg_uart(frame.p_data, frame.image_length);
                jpeg_dump_done = true;
            }
        }
        else
        {
            app_send_capture_frame_loop_debug(status, &frame);
        }

        if (APP_CAPTURE_STAGE_CAP != fail_stage)
        {
            app_send_jpeg_decode_loop_debug(&decode_info);
        }

        if ((APP_CAPTURE_STAGE_NONE == fail_stage) || (APP_CAPTURE_STAGE_INF == fail_stage))
        {
            app_send_inference_debug(status, &result);
        }

        if (APP_STATUS_OK == status)
        {
            (void) app_protocol_send_result(&result);
            (void) app_host_link_send_class_result(result.grade, result.score);
            ok_count++;
        }
        else
        {
            app_send_error_result(status);
            (void) app_host_link_send_error(status);
            fail_count++;
        }

        app_send_loop_result(index, status, fail_stage);

        if (index < total_count)
        {
            app_board_delay_ms(APP_CAPTURE_LOOP_DELAY_MS);
        }
    }

    app_send_loop_end(total_count, ok_count, fail_count);
}

static app_status_t app_run_single_capture_inference(app_arducam_frame_t * p_frame,
                                                     app_jpeg_decode_info_t * p_decode_info,
                                                     fruit_result_t * p_result,
                                                     app_capture_stage_t * p_fail_stage)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_fail_stage)
    {
        *p_fail_stage = APP_CAPTURE_STAGE_CAP;
    }

    if (NULL != p_result)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = "NOT_RUN";
    }

    if ((NULL == p_frame) || (NULL == p_decode_info) || (NULL == p_result) || (NULL == p_fail_stage))
    {
        return status;
    }

    status = app_arducam_capture_frame(p_frame);

    if (APP_STATUS_OK != status)
    {
        *p_fail_stage = APP_CAPTURE_STAGE_CAP;
        return status;
    }

    status = app_jpeg_decode_to_gray96_ex(p_frame->p_data, p_frame->image_length, &g_capture_image, p_decode_info);

    if (APP_STATUS_OK != status)
    {
        *p_fail_stage = APP_CAPTURE_STAGE_JDEC;
        return status;
    }

    status = fruit_run_inference(&g_capture_image, p_result);

    if (APP_STATUS_OK != status)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
        p_result->status = app_status_to_text(status);
        *p_fail_stage = APP_CAPTURE_STAGE_INF;
        return status;
    }

    *p_fail_stage = APP_CAPTURE_STAGE_NONE;

    return APP_STATUS_OK;
}

static void app_send_capture_frame_debug(app_status_t capture_status, const app_arducam_frame_t * p_frame)
{
    char line[128];
    int written = 0;

    if (NULL == p_frame)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,CAP,%s,%s,FIFO,%lu,IMG,%lu\r\n",
                       app_status_to_text(capture_status),
                       app_arducam_frame_status_to_text(p_frame->frame_status),
                       (unsigned long) p_frame->fifo_length,
                       (unsigned long) p_frame->image_length);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_capture_frame_loop_debug(app_status_t capture_status, const app_arducam_frame_t * p_frame)
{
    char line[128];
    int written = 0;

    if (NULL == p_frame)
    {
        return;
    }

    if ((APP_STATUS_OK == capture_status) && (APP_ARDUCAM_FRAME_STATUS_OK == p_frame->frame_status))
    {
        written = snprintf(line,
                           sizeof(line),
                           "$DBG,CAP,OK,FIFO,%lu,IMG,%lu\r\n",
                           (unsigned long) p_frame->fifo_length,
                           (unsigned long) p_frame->image_length);
    }
    else
    {
        written = snprintf(line,
                           sizeof(line),
                           "$DBG,CAP,%s,%s,FIFO,%lu,IMG,%lu\r\n",
                           app_status_to_text(capture_status),
                           app_arducam_frame_status_to_text(p_frame->frame_status),
                           (unsigned long) p_frame->fifo_length,
                           (unsigned long) p_frame->image_length);
    }

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_capture_step_debug(const char * p_step,
                                        app_status_t status,
                                        app_arducam_frame_status_t frame_status,
                                        uint32_t value)
{
    if (NULL == p_step)
    {
        return;
    }

    (void) app_protocol_send_text("$DBG,CAPSTEP,");
    (void) app_protocol_send_text(p_step);
    (void) app_protocol_send_text(",");
    (void) app_protocol_send_text(app_status_to_text(status));
    (void) app_protocol_send_text(",");
    (void) app_protocol_send_text(app_arducam_frame_status_to_text(frame_status));
    (void) app_protocol_send_text(",");
    app_send_u32_decimal(value);
    (void) app_protocol_send_text("\r\n");
}

static void app_send_jpeg_decode_debug(const app_jpeg_decode_info_t * p_info)
{
    char line[160];
    int written = 0;

    if (NULL == p_info)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,JDEC,%s,JPEG,%lu,SRC_W,%u,SRC_H,%u,SCALE,%u,OUT_W,%u,OUT_H,%u,PREP,%u,DECOMP,%u\r\n",
                       app_status_to_text(p_info->status),
                       (unsigned long) p_info->jpeg_length,
                       (unsigned int) p_info->source_width,
                       (unsigned int) p_info->source_height,
                       (unsigned int) p_info->scale,
                       (unsigned int) p_info->output_width,
                       (unsigned int) p_info->output_height,
                       (unsigned int) p_info->prepare_result,
                       (unsigned int) p_info->decomp_result);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_jpeg_decode_loop_debug(const app_jpeg_decode_info_t * p_info)
{
    char line[96];
    int written = 0;

    if (NULL == p_info)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,JDEC,%s,OUT_W,%u,OUT_H,%u\r\n",
                       app_status_to_text(p_info->status),
                       (unsigned int) p_info->output_width,
                       (unsigned int) p_info->output_height);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_inference_debug(app_status_t status, const fruit_result_t * p_result)
{
    char line[96];
    char score_line[96];
    int written = 0;
    const char * p_model_status;

    if (NULL == p_result)
    {
        return;
    }

    p_model_status = (NULL != p_result->status) ? p_result->status : "NONE";

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,INF,%s,CLASS,%s,SCORE,%u,MODEL,%s\r\n",
                       app_status_to_text(status),
                       fruit_grade_to_text(p_result->grade),
                       (unsigned int) p_result->score,
                       p_model_status);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }

    written = snprintf(score_line,
                       sizeof(score_line),
                       "$DBG,SCORE,A,%u,B,%u,C,%u,U,%u\r\n",
                       (unsigned int) p_result->model_scores[0],
                       (unsigned int) p_result->model_scores[1],
                       (unsigned int) p_result->model_scores[2],
                       (unsigned int) p_result->model_scores[3]);

    if ((written > 0) && ((uint32_t) written < sizeof(score_line)))
    {
        (void) app_protocol_send_text(score_line);
    }
}

static void app_send_self_test_continue_debug(uint8_t error_code)
{
    char line[64];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,BOOT,SELFTEST_WARN,%u,CONTINUE_CAPTURE\r\n",
                       (unsigned int) error_code);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_self_test_skip_capture_debug(uint8_t error_code)
{
    char line[80];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,BOOT,SELFTEST_FAIL,%u,SKIP_CAPTURE\r\n",
                       (unsigned int) error_code);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_loop_start(uint16_t total_count)
{
    char line[64];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,LOOP,START,TOTAL,%u\r\n",
                       (unsigned int) total_count);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_loop_index(uint16_t index)
{
    char line[48];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,LOOP,IDX,%u\r\n",
                       (unsigned int) index);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_loop_result(uint16_t index, app_status_t status, app_capture_stage_t fail_stage)
{
    char line[80];
    int written = 0;

    if (APP_STATUS_OK == status)
    {
        written = snprintf(line,
                           sizeof(line),
                           "$DBG,LOOP,IDX,%u,OK\r\n",
                           (unsigned int) index);
    }
    else
    {
        written = snprintf(line,
                           sizeof(line),
                           "$DBG,LOOP,IDX,%u,FAIL,STAGE,%s\r\n",
                           (unsigned int) index,
                           app_capture_stage_to_text(fail_stage));
    }

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_loop_end(uint16_t total_count, uint16_t ok_count, uint16_t fail_count)
{
    char line[80];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,LOOP,END,TOTAL,%u,OK,%u,FAIL,%u\r\n",
                       (unsigned int) total_count,
                       (unsigned int) ok_count,
                       (unsigned int) fail_count);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

#if APP_HOST_LINK_RX_IRQ_DEBUG_ENABLED
static void app_send_host_rx_irq_debug(uint32_t irq_count, uint8_t last_byte)
{
    char line[80];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,HOSTLINK,RX_IRQ,%lu,LAST,%02X\r\n",
                       (unsigned long) irq_count,
                       (unsigned int) last_byte);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}
#endif

static void app_send_u32_decimal(uint32_t value)
{
    char text[11];
    uint8_t index = 0U;

    if (0U == value)
    {
        (void) app_protocol_send_text("0");
        return;
    }

    while ((value > 0U) && (index < sizeof(text)))
    {
        text[index] = (char) ('0' + (value % 10U));
        value /= 10U;
        index++;
    }

    while (index > 0U)
    {
        char digit[2];

        index--;
        digit[0] = text[index];
        digit[1] = '\0';
        (void) app_protocol_send_text(digit);
    }
}

static uint8_t app_get_arducam_frame_error_code(app_arducam_frame_status_t frame_status)
{
    uint8_t error_code = 6U;

    if (APP_ARDUCAM_FRAME_STATUS_CAPTURE_TIMEOUT == frame_status)
    {
        error_code = 1U;
    }
    else if ((APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_ZERO == frame_status) ||
             (APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_SMALL == frame_status) ||
             (APP_ARDUCAM_FRAME_STATUS_FIFO_LENGTH_TOO_LARGE == frame_status))
    {
        error_code = 2U;
    }
    else if (APP_ARDUCAM_FRAME_STATUS_BUFFER_TOO_SMALL == frame_status)
    {
        error_code = 3U;
    }
    else if (APP_ARDUCAM_FRAME_STATUS_JPEG_HEADER_ERROR == frame_status)
    {
        error_code = 4U;
    }
    else if (APP_ARDUCAM_FRAME_STATUS_JPEG_TAIL_ERROR == frame_status)
    {
        error_code = 5U;
    }
    else
    {
        error_code = 6U;
    }

    return error_code;
}

static uint8_t app_get_fruit_grade_led_count(fruit_grade_t grade)
{
    uint8_t count = 4U;

    if (FRUIT_GRADE_A == grade)
    {
        count = 1U;
    }
    else if (FRUIT_GRADE_B == grade)
    {
        count = 2U;
    }
    else if (FRUIT_GRADE_C == grade)
    {
        count = 3U;
    }
    else
    {
        count = 4U;
    }

    return count;
}

static const char * app_capture_stage_to_text(app_capture_stage_t stage)
{
    const char * p_text = "NONE";

    switch (stage)
    {
        case APP_CAPTURE_STAGE_CAP:
        {
            p_text = "CAP";
            break;
        }

        case APP_CAPTURE_STAGE_JDEC:
        {
            p_text = "JDEC";
            break;
        }

        case APP_CAPTURE_STAGE_INF:
        {
            p_text = "INF";
            break;
        }

        case APP_CAPTURE_STAGE_NONE:
        default:
        {
            p_text = "NONE";
            break;
        }
    }

    return p_text;
}

static void app_debug_led_error_loop(uint8_t error_code)
{
    if (0U == error_code)
    {
        error_code = 6U;
    }

    while (1)
    {
        app_board_debug_led_blink(error_code, APP_DEBUG_LED_BLINK_ON_MS, APP_DEBUG_LED_BLINK_OFF_MS);
        app_board_delay_ms(APP_DEBUG_LED_GROUP_PAUSE_MS);
    }
}

static void app_debug_led_fast_error_loop(uint8_t error_code)
{
    if (0U == error_code)
    {
        error_code = 6U;
    }

    while (1)
    {
        app_board_debug_led_blink(error_code, APP_DEBUG_LED_FAST_BLINK_ON_MS, APP_DEBUG_LED_FAST_BLINK_OFF_MS);
        app_board_delay_ms(APP_DEBUG_LED_FAST_GROUP_PAUSE_MS);
    }
}

static void app_debug_led_result_loop(fruit_grade_t grade)
{
    uint8_t count = app_get_fruit_grade_led_count(grade);

    while (1)
    {
        app_board_debug_led_blink(count, APP_DEBUG_LED_RESULT_ON_MS, APP_DEBUG_LED_RESULT_OFF_MS);
        app_board_delay_ms(APP_DEBUG_LED_RESULT_PAUSE_MS);
    }
}

static void app_debug_led_startup_pin_test(void)
{
    app_board_debug_led_set(true);
    app_board_delay_ms(500U);
    app_board_debug_led_set(false);
    app_board_delay_ms(500U);
    app_board_debug_led_set(true);
    app_board_delay_ms(500U);
    app_board_debug_led_set(false);
    app_board_delay_ms(500U);
}

static void app_send_error_result(app_status_t status)
{
    fruit_result_t result =
    {
        .grade = FRUIT_GRADE_UNKNOWN,
        .score = 0U,
        .status = app_status_to_text(status)
    };

    (void) app_protocol_send_result(&result);
}

static void app_send_arducam_test_report(const app_arducam_test_result_t * p_result)
{
    if (NULL != p_result)
    {
        for (uint8_t i = 0U; i < APP_ARDUCAM_SPI_MODE_COUNT; i++)
        {
            app_send_arducam_spi_mode_line(&p_result->spi_modes[i]);
        }

        app_send_arducam_spi_bitbang_line(&p_result->spi_bitbang);
        app_send_arducam_test_line("SPI55", p_result->spi_55_status, p_result->spi_55_value);
        app_send_arducam_test_line("SPIAA", p_result->spi_aa_status, p_result->spi_aa_value);
        app_send_arducam_test_line("SPIMODE", APP_STATUS_OK, p_result->spi_mode);
        app_send_arducam_test_line("PID", p_result->ov2640_pid_status, p_result->ov2640_pid);
        app_send_arducam_test_line("VER", p_result->ov2640_ver_status, p_result->ov2640_ver);
        app_send_arducam_test_line("JPEG_INIT", p_result->jpeg_init_status, 0U);
        app_send_arducam_test_line("CAP_TIMING", p_result->capture_timing_status, 0U);
        app_send_arducam_test_line("FIFO_FLUSH", p_result->fifo_flush_status, 0U);
        app_send_arducam_test_line("FINAL", p_result->final_status, 0U);
    }
}

static void app_send_arducam_spi_mode_line(const app_arducam_spi_mode_result_t * p_result)
{
    char line[192];
    int written = 0;

    if (NULL == p_result)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,SPIRAW,%u,%s,%s,%s,%02X,%s,%s,%s,%02X,%s\r\n",
                       (unsigned int) p_result->mode,
                       app_status_to_text(p_result->set_mode_status),
                       app_status_to_text(p_result->write_55_status),
                       app_status_to_text(p_result->read_55_status),
                       (unsigned int) p_result->value_55,
                       app_status_to_text(p_result->check_55_status),
                       app_status_to_text(p_result->write_aa_status),
                       app_status_to_text(p_result->read_aa_status),
                       (unsigned int) p_result->value_aa,
                       app_status_to_text(p_result->check_aa_status));

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_arducam_spi_bitbang_line(const app_arducam_spi_bitbang_result_t * p_result)
{
    char line[224];
    int written = 0;

    if (NULL == p_result)
    {
        return;
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,SPIBB,%s,%s,%s,%02X,%s,%s,%s,%02X,%s,%s\r\n",
                       app_status_to_text(p_result->begin_status),
                       app_status_to_text(p_result->write_55_status),
                       app_status_to_text(p_result->read_55_status),
                       (unsigned int) p_result->value_55,
                       app_status_to_text(p_result->check_55_status),
                       app_status_to_text(p_result->write_aa_status),
                       app_status_to_text(p_result->read_aa_status),
                       (unsigned int) p_result->value_aa,
                       app_status_to_text(p_result->check_aa_status),
                       app_status_to_text(p_result->end_status));

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

static void app_send_arducam_test_line(const char * p_name, app_status_t status, uint8_t value)
{
    char line[64];
    int written = 0;

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,%s,%s,%02X\r\n",
                       p_name,
                       app_status_to_text(status),
                       (unsigned int) value);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}

#if APP_HOST_LINK_LOOPBACK_TEST_ENABLED && APP_HAS_FSP_UART_HOST
/* SCI2 回环自测：要求把 P301 和 P302 用一根杜邦线短接。
 * 推理板从 SCI2 发出去的字节会立刻从自己的 RX 收回来，
 * 如果都能对上，说明 TX/RX/中断/回调全部正常。
 *
 * 测试结果通过 SCI9 调试口输出，不会污染 host 协议。 */
static void app_run_host_link_loopback_test(void)
{
    static const char test_pattern[] = "LOOPBACK_TEST_123\r\n";
    const uint32_t pattern_length = (uint32_t) (sizeof(test_pattern) - 1U);
    uint8_t rx_byte = 0U;
    uint32_t matched = 0U;
    uint32_t mismatched = 0U;
    char line[64];
    int written;

    (void) app_protocol_send_text("$DBG,LOOPBACK,BEGIN,SHORT_P301_TO_P302\r\n");

    /* 先把 host 接收环形缓冲清空，避免之前残留的字节干扰本次比对。
     * has_byte 为 true 时不断 read_byte 把字节丢掉。 */
    while (app_board_host_has_byte())
    {
        (void) app_board_host_read_byte(&rx_byte, 0U);
    }

    /* 把测试串通过 host UART 发出去。短接时这些字节会立刻被自己的 RX 接到。 */
    if (APP_STATUS_OK != app_board_host_write((const uint8_t *) test_pattern,
                                              pattern_length,
                                              APP_HOST_LINK_TX_TIMEOUT_MS))
    {
        (void) app_protocol_send_text("$DBG,LOOPBACK,END,FAIL,TX_ERROR\r\n");
        return;
    }

    /* 给硬件一点时间把 RX FIFO 填满。115200 波特率发 19 字节大约 1.7 ms，
     * 这里给 50 ms 余量足够。 */
    app_board_delay_ms(50U);

    /* 逐字节读回来比对。每个字节最多等 100 ms，总超时不会超过 2 秒。 */
    for (uint32_t i = 0U; i < pattern_length; i++)
    {
        if (APP_STATUS_OK != app_board_host_read_byte(&rx_byte, 100U))
        {
            written = snprintf(line,
                               sizeof(line),
                               "$DBG,LOOPBACK,RX_TIMEOUT,IDX,%lu\r\n",
                               (unsigned long) i);

            if ((written > 0) && ((uint32_t) written < sizeof(line)))
            {
                (void) app_protocol_send_text(line);
            }

            mismatched++;
            break;
        }

        if (rx_byte == (uint8_t) test_pattern[i])
        {
            matched++;
        }
        else
        {
            written = snprintf(line,
                               sizeof(line),
                               "$DBG,LOOPBACK,DIFF,IDX,%lu,EXP,%02X,GOT,%02X\r\n",
                               (unsigned long) i,
                               (unsigned int) (uint8_t) test_pattern[i],
                               (unsigned int) rx_byte);

            if ((written > 0) && ((uint32_t) written < sizeof(line)))
            {
                (void) app_protocol_send_text(line);
            }

            mismatched++;
        }
    }

    written = snprintf(line,
                       sizeof(line),
                       "$DBG,LOOPBACK,END,MATCH,%lu,MISS,%lu,TOTAL,%lu\r\n",
                       (unsigned long) matched,
                       (unsigned long) mismatched,
                       (unsigned long) pattern_length);

    if ((written > 0) && ((uint32_t) written < sizeof(line)))
    {
        (void) app_protocol_send_text(line);
    }
}
#endif

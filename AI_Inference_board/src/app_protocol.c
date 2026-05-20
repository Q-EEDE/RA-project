#include "app_protocol.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app_board.h"
#include "app_config.h"

#define APP_CONTROL_FRAME_MAX_LEN    (48U)
#define APP_JPEG_DUMP_LINE_MAX_LEN   (32U + (APP_JPEG_UART_DUMP_CHUNK_BYTES * 2U))

static uint8_t app_protocol_clamp_score(uint8_t score);
#if ENABLE_JPEG_UART_DUMP
static app_status_t app_protocol_send_jpeg_dump_data_line(uint32_t offset,
                                                          const uint8_t * p_data,
                                                          uint32_t length);
#endif

app_status_t app_protocol_send_result(const fruit_result_t * p_result)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_result)
    {
        return status;
    }

    status = app_protocol_send_class_result(p_result->grade, p_result->score);

    return status;
}

app_status_t app_protocol_send_class_result(fruit_grade_t grade, uint8_t score)
{
    char frame[APP_CONTROL_FRAME_MAX_LEN];
    int written = 0;
    app_status_t status;

    written = snprintf(frame,
                       sizeof(frame),
                       "$CLS,%s,%u\r\n",
                       fruit_grade_to_text(grade),
                       (unsigned int) app_protocol_clamp_score(score));

    if ((written <= 0) || ((uint32_t) written >= sizeof(frame)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    status = app_board_control_write((const uint8_t *) frame, (uint32_t) strlen(frame), APP_CONTROL_TX_TIMEOUT_MS);

    return status;
}

app_status_t app_protocol_debug_dump_jpeg_uart(const uint8_t * p_jpeg, uint32_t jpeg_length)
{
    return debug_dump_jpeg_uart(p_jpeg, jpeg_length);
}

app_status_t debug_dump_jpeg_uart(const uint8_t * p_jpeg, uint32_t jpeg_length)
{
#if ENABLE_JPEG_UART_DUMP
    char line[64];
    int written = 0;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL == p_jpeg) || (0U == jpeg_length) || (0U == APP_JPEG_UART_DUMP_CHUNK_BYTES))
    {
        return status;
    }

    written = snprintf(line, sizeof(line), "$DBG,JPG_DUMP,BEGIN,%lu\r\n", (unsigned long) jpeg_length);

    if ((written <= 0) || ((uint32_t) written >= sizeof(line)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    status = app_protocol_send_text(line);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    written = snprintf(line, sizeof(line), "$JPG,BEGIN,%lu\r\n", (unsigned long) jpeg_length);

    if ((written <= 0) || ((uint32_t) written >= sizeof(line)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    status = app_protocol_send_text(line);

    for (uint32_t offset = 0U; (offset < jpeg_length) && (APP_STATUS_OK == status); )
    {
        uint32_t chunk_length = jpeg_length - offset;

        if (chunk_length > APP_JPEG_UART_DUMP_CHUNK_BYTES)
        {
            chunk_length = APP_JPEG_UART_DUMP_CHUNK_BYTES;
        }

        status = app_protocol_send_jpeg_dump_data_line(offset, &p_jpeg[offset], chunk_length);
        offset += chunk_length;
    }

    if (APP_STATUS_OK == status)
    {
        written = snprintf(line, sizeof(line), "$JPG,END,%lu\r\n", (unsigned long) jpeg_length);

        if ((written <= 0) || ((uint32_t) written >= sizeof(line)))
        {
            status = APP_STATUS_FRAME_ERROR;
        }
        else
        {
            status = app_protocol_send_text(line);
        }
    }

    if (APP_STATUS_OK == status)
    {
        written = snprintf(line, sizeof(line), "$DBG,JPG_DUMP,END,%lu\r\n", (unsigned long) jpeg_length);

        if ((written <= 0) || ((uint32_t) written >= sizeof(line)))
        {
            status = APP_STATUS_FRAME_ERROR;
        }
        else
        {
            status = app_protocol_send_text(line);
        }
    }

    return status;
#else
    (void) p_jpeg;
    (void) jpeg_length;

    return APP_STATUS_OK;
#endif
}

app_status_t app_protocol_send_text(const char * p_text)
{
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL != p_text)
    {
        status = app_board_control_write((const uint8_t *) p_text,
                                         (uint32_t) strlen(p_text),
                                         APP_CONTROL_TX_TIMEOUT_MS);
    }

    return status;
}

static uint8_t app_protocol_clamp_score(uint8_t score)
{
    return (score > 100U) ? 100U : score;
}

#if ENABLE_JPEG_UART_DUMP
static app_status_t app_protocol_send_jpeg_dump_data_line(uint32_t offset,
                                                          const uint8_t * p_data,
                                                          uint32_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[APP_JPEG_DUMP_LINE_MAX_LEN];
    int written = 0;
    uint32_t index;

    if ((NULL == p_data) || (0U == length) || (length > APP_JPEG_UART_DUMP_CHUNK_BYTES))
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    written = snprintf(line, sizeof(line), "$JPG,DATA,%lu,", (unsigned long) offset);

    if ((written <= 0) || ((uint32_t) written >= sizeof(line)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    index = (uint32_t) written;

    for (uint32_t i = 0U; i < length; i++)
    {
        if ((index + 4U) >= sizeof(line))
        {
            return APP_STATUS_FRAME_ERROR;
        }

        line[index] = hex[(p_data[i] >> 4) & 0x0FU];
        index++;
        line[index] = hex[p_data[i] & 0x0FU];
        index++;
    }

    line[index] = '\r';
    index++;
    line[index] = '\n';
    index++;
    line[index] = '\0';

    return app_protocol_send_text(line);
}
#endif

#include "app_camera_uart.h"
#include <stddef.h>
#include <string.h>
#include "app_board.h"
#include "app_config.h"

static app_status_t app_camera_wait_for_header(void);
static app_status_t app_camera_read_checked_byte(uint8_t * p_byte, uint32_t * p_checksum);
static app_status_t app_camera_read_payload(app_image_t * p_image, uint32_t * p_checksum);

app_status_t app_camera_capture(app_image_t * p_image)
{
    const uint8_t command[] = APP_CAMERA_CAPTURE_COMMAND;
    uint32_t checksum = 0U;
    uint8_t version = 0U;
    uint8_t width = 0U;
    uint8_t height = 0U;
    uint8_t format = 0U;
    uint8_t length_l = 0U;
    uint8_t length_h = 0U;
    uint8_t received_checksum = 0U;
    uint16_t payload_length = 0U;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if (NULL == p_image)
    {
        return status;
    }

    status = app_board_camera_write(command, (uint32_t) strlen((const char *) command), APP_CAMERA_COMMAND_TIMEOUT_MS);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    status = app_camera_wait_for_header();

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    status = app_camera_read_checked_byte(&version, &checksum);
    status = (APP_STATUS_OK == status) ? app_camera_read_checked_byte(&width, &checksum) : status;
    status = (APP_STATUS_OK == status) ? app_camera_read_checked_byte(&height, &checksum) : status;
    status = (APP_STATUS_OK == status) ? app_camera_read_checked_byte(&format, &checksum) : status;
    status = (APP_STATUS_OK == status) ? app_camera_read_checked_byte(&length_l, &checksum) : status;
    status = (APP_STATUS_OK == status) ? app_camera_read_checked_byte(&length_h, &checksum) : status;

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    payload_length = (uint16_t) (((uint16_t) length_h << 8) | (uint16_t) length_l);

    if ((APP_CAMERA_FRAME_VERSION != version) ||
        (APP_IMAGE_WIDTH != width) ||
        (APP_IMAGE_HEIGHT != height) ||
        (APP_CAMERA_FRAME_FORMAT_GRAY8 != format) ||
        (APP_IMAGE_SIZE != payload_length))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    status = app_camera_read_payload(p_image, &checksum);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    status = app_board_camera_read_byte(&received_checksum, APP_CAMERA_BYTE_TIMEOUT_MS);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    if ((uint8_t) (checksum & 0xFFU) != received_checksum)
    {
        return APP_STATUS_CHECKSUM_ERROR;
    }

    return APP_STATUS_OK;
}

static app_status_t app_camera_wait_for_header(void)
{
    uint8_t byte = 0U;
    bool found_first_header = false;
    uint32_t attempt_count = (APP_CAMERA_HEADER_TIMEOUT_MS / APP_CAMERA_BYTE_TIMEOUT_MS) + 1U;
    app_status_t status = APP_STATUS_TIMEOUT;

    while (attempt_count > 0U)
    {
        status = app_board_camera_read_byte(&byte, APP_CAMERA_BYTE_TIMEOUT_MS);

        if (APP_STATUS_OK != status)
        {
            if (APP_STATUS_TIMEOUT != status)
            {
                break;
            }
        }
        else if (!found_first_header)
        {
            found_first_header = (APP_CAMERA_FRAME_HEADER0 == byte);
        }
        else if (APP_CAMERA_FRAME_HEADER1 == byte)
        {
            status = APP_STATUS_OK;
            break;
        }
        else
        {
            found_first_header = (APP_CAMERA_FRAME_HEADER0 == byte);
        }

        attempt_count--;
    }

    return status;
}

static app_status_t app_camera_read_checked_byte(uint8_t * p_byte, uint32_t * p_checksum)
{
    app_status_t status = app_board_camera_read_byte(p_byte, APP_CAMERA_BYTE_TIMEOUT_MS);

    if (APP_STATUS_OK == status)
    {
        *p_checksum += (uint32_t) *p_byte;
    }

    return status;
}

static app_status_t app_camera_read_payload(app_image_t * p_image, uint32_t * p_checksum)
{
    app_status_t status = APP_STATUS_OK;

    for (uint32_t i = 0U; i < APP_IMAGE_SIZE; i++)
    {
        status = app_camera_read_checked_byte(&p_image->pixels[i], p_checksum);

        if (APP_STATUS_OK != status)
        {
            break;
        }
    }

    return status;
}

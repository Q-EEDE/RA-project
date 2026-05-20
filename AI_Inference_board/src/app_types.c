#include "app_types.h"

const char * app_status_to_text(app_status_t status)
{
    const char * p_text = "UNKNOWN_ERROR";

    switch (status)
    {
        case APP_STATUS_OK:
        {
            p_text = "OK";
            break;
        }

        case APP_STATUS_TIMEOUT:
        {
            p_text = "CAMERA_TIMEOUT";
            break;
        }

        case APP_STATUS_FRAME_ERROR:
        {
            p_text = "CAMERA_FRAME_ERROR";
            break;
        }

        case APP_STATUS_CHECKSUM_ERROR:
        {
            p_text = "CAMERA_CHECKSUM_ERROR";
            break;
        }

        case APP_STATUS_NOT_CONFIGURED:
        {
            p_text = "NOT_CONFIGURED";
            break;
        }

        case APP_STATUS_INVALID_ARGUMENT:
        {
            p_text = "INVALID_ARGUMENT";
            break;
        }

        case APP_STATUS_IO_ERROR:
        {
            p_text = "IO_ERROR";
            break;
        }

        case APP_STATUS_CAMERA_NOT_READY:
        {
            p_text = "CAMERA_NOT_READY";
            break;
        }

        case APP_STATUS_UNSUPPORTED_FORMAT:
        {
            p_text = "UNSUPPORTED_FORMAT";
            break;
        }

        default:
        {
            break;
        }
    }

    return p_text;
}

const char * fruit_grade_to_text(fruit_grade_t grade)
{
    const char * p_text = "UNKNOWN";

    switch (grade)
    {
        case FRUIT_GRADE_A:
        {
            p_text = "A";
            break;
        }

        case FRUIT_GRADE_B:
        {
            p_text = "B";
            break;
        }

        case FRUIT_GRADE_C:
        {
            p_text = "C";
            break;
        }

        case FRUIT_GRADE_UNKNOWN:
        default:
        {
            p_text = "UNKNOWN";
            break;
        }
    }

    return p_text;
}

char fruit_grade_to_char(fruit_grade_t grade)
{
    char grade_char = 'U';

    switch (grade)
    {
        case FRUIT_GRADE_A:
        {
            grade_char = 'A';
            break;
        }

        case FRUIT_GRADE_B:
        {
            grade_char = 'B';
            break;
        }

        case FRUIT_GRADE_C:
        {
            grade_char = 'C';
            break;
        }

        case FRUIT_GRADE_UNKNOWN:
        default:
        {
            grade_char = 'U';
            break;
        }
    }

    return grade_char;
}

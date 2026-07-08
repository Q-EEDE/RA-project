#ifndef APP_TYPES_H_
#define APP_TYPES_H_

#include <stdint.h>
#include "app_config.h"

typedef enum
{
    APP_STATUS_OK = 0,
    APP_STATUS_TIMEOUT,
    APP_STATUS_FRAME_ERROR,
    APP_STATUS_CHECKSUM_ERROR,
    APP_STATUS_NOT_CONFIGURED,
    APP_STATUS_INVALID_ARGUMENT,
    APP_STATUS_IO_ERROR,
    APP_STATUS_CAMERA_NOT_READY,
    APP_STATUS_UNSUPPORTED_FORMAT
} app_status_t;

typedef enum
{
    FRUIT_GRADE_A = 0,
    FRUIT_GRADE_B,
    FRUIT_GRADE_C,
    FRUIT_GRADE_UNKNOWN
} fruit_grade_t;

typedef struct
{
    uint8_t pixels[APP_IMAGE_SIZE];
} app_image_t;

typedef struct
{
    uint32_t area;
    uint8_t mean_gray;
    uint8_t contrast;
} fruit_features_t;

typedef struct
{
    fruit_grade_t grade;
    uint8_t score;
    const char * status;
    uint8_t model_scores[APP_INFERENCE_OUTPUT_CLASS_COUNT];
} fruit_result_t;

const char * app_status_to_text(app_status_t status);
const char * fruit_grade_to_text(fruit_grade_t grade);
char fruit_grade_to_char(fruit_grade_t grade);

#endif /* APP_TYPES_H_ */

#include "app_inference.h"
#include <stddef.h>
#include "app_config.h"

app_status_t fruit_extract_features(const app_image_t * p_image, fruit_features_t * p_features)
{
    uint32_t sum = 0U;
    uint32_t area = 0U;
    uint8_t min_gray = 255U;
    uint8_t max_gray = 0U;

    if ((NULL == p_image) || (NULL == p_features))
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0U; i < APP_IMAGE_SIZE; i++)
    {
        uint8_t pixel = p_image->pixels[i];

        sum += (uint32_t) pixel;

        if (pixel < min_gray)
        {
            min_gray = pixel;
        }

        if (pixel > max_gray)
        {
            max_gray = pixel;
        }

        if (pixel > APP_BACKGROUND_THRESHOLD)
        {
            area++;
        }
    }

    p_features->area = area;
    p_features->mean_gray = (uint8_t) (sum / APP_IMAGE_SIZE);
    p_features->contrast = (uint8_t) (max_gray - min_gray);

    return APP_STATUS_OK;
}

app_status_t fruit_run_inference(const app_image_t * p_image, fruit_result_t * p_result)
{
    fruit_features_t features;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    if ((NULL == p_image) || (NULL == p_result))
    {
        return status;
    }

    status = fruit_extract_features(p_image, &features);

    if (APP_STATUS_OK != status)
    {
        return status;
    }

    p_result->status = "OK";

    if (features.area < APP_MIN_FRUIT_AREA)
    {
        p_result->grade = FRUIT_GRADE_UNKNOWN;
        p_result->score = 0U;
    }
    else if ((features.mean_gray > 120U) && (features.contrast > 60U))
    {
        p_result->grade = FRUIT_GRADE_A;
        p_result->score = 85U;
    }
    else if (features.mean_gray > 80U)
    {
        p_result->grade = FRUIT_GRADE_B;
        p_result->score = 70U;
    }
    else
    {
        p_result->grade = FRUIT_GRADE_C;
        p_result->score = 55U;
    }

    return APP_STATUS_OK;
}

#include "app_jpeg_decode.h"
#include <stddef.h>
#include <string.h>
#include "app_config.h"
#include "tjpgd.h"

#define APP_JPEG_TJPGD_WORK_SIZE    (16U * 1024U)
#define APP_JPEG_RGB_BYTES          (3U)
#define APP_JPEG_TJPGD_RESULT_NOT_RUN    (0xFFU)

typedef struct
{
    const uint8_t * p_jpeg;
    uint32_t jpeg_length;
    uint32_t offset;
    uint16_t output_width;
    uint16_t output_height;
    app_image_t * p_image;
} app_jpeg_decode_context_t;

static uint32_t g_tjpgd_work_pool[(APP_JPEG_TJPGD_WORK_SIZE + 3U) / 4U];

static size_t app_jpeg_input_func(JDEC * p_jdec, uint8_t * p_buffer, size_t length);
static int app_jpeg_output_func(JDEC * p_jdec, void * p_bitmap, JRECT * p_rect);
static void app_jpeg_init_decode_info(app_jpeg_decode_info_t * p_info, uint32_t jpeg_length);
static uint8_t app_jpeg_select_scale(uint16_t width, uint16_t height);
static uint16_t app_jpeg_scaled_dimension(uint16_t dimension, uint8_t scale);
static uint8_t app_jpeg_rgb_to_gray(uint8_t r, uint8_t g, uint8_t b);

app_status_t app_jpeg_decode_to_gray96(const uint8_t * p_jpeg,
                                       uint32_t jpeg_length,
                                       app_image_t * p_image)
{
    return app_jpeg_decode_to_gray96_ex(p_jpeg, jpeg_length, p_image, NULL);
}

app_status_t app_jpeg_decode_to_gray96_ex(const uint8_t * p_jpeg,
                                          uint32_t jpeg_length,
                                          app_image_t * p_image,
                                          app_jpeg_decode_info_t * p_info)
{
    JDEC jdec;
    JRESULT result;
    app_jpeg_decode_context_t context;
    uint8_t scale;
    app_status_t status = APP_STATUS_INVALID_ARGUMENT;

    app_jpeg_init_decode_info(p_info, jpeg_length);

    if ((NULL == p_jpeg) || (0U == jpeg_length) || (NULL == p_image))
    {
        if (NULL != p_info)
        {
            p_info->status = status;
        }

        return status;
    }

    memset(&jdec, 0, sizeof(jdec));
    memset(p_image->pixels, 0, sizeof(p_image->pixels));
    memset(&context, 0, sizeof(context));

    context.p_jpeg = p_jpeg;
    context.jpeg_length = jpeg_length;
    context.p_image = p_image;

    result = jd_prepare(&jdec,
                        app_jpeg_input_func,
                        g_tjpgd_work_pool,
                        sizeof(g_tjpgd_work_pool),
                        &context);

    if (NULL != p_info)
    {
        p_info->prepare_result = (uint8_t) result;
    }

    if (JDR_OK != result)
    {
        status = APP_STATUS_UNSUPPORTED_FORMAT;

        if (NULL != p_info)
        {
            p_info->status = status;
        }

        return status;
    }

    scale = app_jpeg_select_scale(jdec.width, jdec.height);
    context.output_width = app_jpeg_scaled_dimension(jdec.width, scale);
    context.output_height = app_jpeg_scaled_dimension(jdec.height, scale);

    if (NULL != p_info)
    {
        p_info->source_width = jdec.width;
        p_info->source_height = jdec.height;
        p_info->scale = scale;
        p_info->output_width = context.output_width;
        p_info->output_height = context.output_height;
    }

    if ((0U == context.output_width) || (0U == context.output_height))
    {
        status = APP_STATUS_FRAME_ERROR;

        if (NULL != p_info)
        {
            p_info->status = status;
        }

        return status;
    }

    result = jd_decomp(&jdec, app_jpeg_output_func, scale);

    if (NULL != p_info)
    {
        p_info->decomp_result = (uint8_t) result;
    }

    if (JDR_OK == result)
    {
        status = APP_STATUS_OK;
    }
    else
    {
        status = APP_STATUS_UNSUPPORTED_FORMAT;
    }

    if (NULL != p_info)
    {
        p_info->status = status;
    }

    return status;
}

static void app_jpeg_init_decode_info(app_jpeg_decode_info_t * p_info, uint32_t jpeg_length)
{
    if (NULL != p_info)
    {
        memset(p_info, 0, sizeof(*p_info));
        p_info->jpeg_length = jpeg_length;
        p_info->prepare_result = APP_JPEG_TJPGD_RESULT_NOT_RUN;
        p_info->decomp_result = APP_JPEG_TJPGD_RESULT_NOT_RUN;
        p_info->status = APP_STATUS_INVALID_ARGUMENT;
    }
}

static size_t app_jpeg_input_func(JDEC * p_jdec, uint8_t * p_buffer, size_t length)
{
    app_jpeg_decode_context_t * p_context = (app_jpeg_decode_context_t *) p_jdec->device;
    uint32_t requested = (uint32_t) length;
    uint32_t remaining;
    uint32_t read_length;

    if ((NULL == p_context) || (p_context->offset > p_context->jpeg_length))
    {
        return 0U;
    }

    remaining = p_context->jpeg_length - p_context->offset;

    read_length = (requested > remaining) ? remaining : requested;

    if ((NULL != p_buffer) && (read_length > 0U))
    {
        memcpy(p_buffer, &p_context->p_jpeg[p_context->offset], read_length);
    }

    p_context->offset += read_length;

    return (size_t) read_length;
}

static int app_jpeg_output_func(JDEC * p_jdec, void * p_bitmap, JRECT * p_rect)
{
    app_jpeg_decode_context_t * p_context = (app_jpeg_decode_context_t *) p_jdec->device;
    const uint8_t * p_rgb = (const uint8_t *) p_bitmap;
    uint32_t rect_width;
    uint32_t src_y;
    uint32_t src_x;
    uint32_t dst_y;
    uint32_t dst_x;
    uint32_t pixel_index;

    if ((NULL == p_context) || (NULL == p_context->p_image) || (NULL == p_rgb) || (NULL == p_rect))
    {
        return 0;
    }

    rect_width = (uint32_t) p_rect->right - (uint32_t) p_rect->left + 1U;

    for (src_y = (uint32_t) p_rect->top; src_y <= (uint32_t) p_rect->bottom; src_y++)
    {
        dst_y = (src_y * APP_IMAGE_HEIGHT) / (uint32_t) p_context->output_height;

        if (dst_y >= APP_IMAGE_HEIGHT)
        {
            continue;
        }

        for (src_x = (uint32_t) p_rect->left; src_x <= (uint32_t) p_rect->right; src_x++)
        {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t gray;

            dst_x = (src_x * APP_IMAGE_WIDTH) / (uint32_t) p_context->output_width;

            if (dst_x >= APP_IMAGE_WIDTH)
            {
                continue;
            }

            pixel_index = (((src_y - (uint32_t) p_rect->top) * rect_width) +
                           (src_x - (uint32_t) p_rect->left)) * APP_JPEG_RGB_BYTES;

            r = p_rgb[pixel_index];
            g = p_rgb[pixel_index + 1U];
            b = p_rgb[pixel_index + 2U];
            gray = app_jpeg_rgb_to_gray(r, g, b);

            p_context->p_image->pixels[(dst_y * APP_IMAGE_WIDTH) + dst_x] = gray;
        }
    }

    return 1;
}

static uint8_t app_jpeg_select_scale(uint16_t width, uint16_t height)
{
    uint8_t scale = 0U;

    for (uint8_t candidate = 3U; candidate > 0U; candidate--)
    {
        if ((app_jpeg_scaled_dimension(width, candidate) >= APP_IMAGE_WIDTH) &&
            (app_jpeg_scaled_dimension(height, candidate) >= APP_IMAGE_HEIGHT))
        {
            scale = candidate;
            break;
        }
    }

    return scale;
}

static uint16_t app_jpeg_scaled_dimension(uint16_t dimension, uint8_t scale)
{
    return (uint16_t) (dimension >> scale);
}

static uint8_t app_jpeg_rgb_to_gray(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t gray = (77U * (uint32_t) r) + (150U * (uint32_t) g) + (29U * (uint32_t) b);

    return (uint8_t) (gray >> 8);
}

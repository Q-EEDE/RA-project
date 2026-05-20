#ifndef APP_JPEG_DECODE_H_
#define APP_JPEG_DECODE_H_

#include "app_types.h"

typedef struct
{
    uint32_t jpeg_length;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t output_width;
    uint16_t output_height;
    uint8_t scale;
    uint8_t prepare_result;
    uint8_t decomp_result;
    app_status_t status;
} app_jpeg_decode_info_t;

app_status_t app_jpeg_decode_to_gray96(const uint8_t * p_jpeg,
                                       uint32_t jpeg_length,
                                       app_image_t * p_image);
app_status_t app_jpeg_decode_to_gray96_ex(const uint8_t * p_jpeg,
                                          uint32_t jpeg_length,
                                          app_image_t * p_image,
                                          app_jpeg_decode_info_t * p_info);

#endif /* APP_JPEG_DECODE_H_ */

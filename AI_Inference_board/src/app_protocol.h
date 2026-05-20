#ifndef APP_PROTOCOL_H_
#define APP_PROTOCOL_H_

#include "app_types.h"

app_status_t app_protocol_send_result(const fruit_result_t * p_result);
app_status_t app_protocol_send_class_result(fruit_grade_t grade, uint8_t score);
app_status_t app_protocol_debug_dump_jpeg_uart(const uint8_t * p_jpeg, uint32_t jpeg_length);
app_status_t debug_dump_jpeg_uart(const uint8_t * p_jpeg, uint32_t jpeg_length);
app_status_t app_protocol_send_text(const char * p_text);

#endif /* APP_PROTOCOL_H_ */

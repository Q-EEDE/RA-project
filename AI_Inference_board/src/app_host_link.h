#ifndef APP_HOST_LINK_H_
#define APP_HOST_LINK_H_

#include "app_types.h"

/* 主控板发来的命令类型 */
typedef enum
{
    APP_HOST_CMD_NONE = 0,    /* 没有完整命令 */
    APP_HOST_CMD_TRIG,        /* $TRIG  -> 主控触发拍照+识别 */
    APP_HOST_CMD_PING,        /* $PING  -> 主控心跳查询 */
    APP_HOST_CMD_UNKNOWN      /* 收到了一行，但不认识 */
} app_host_cmd_t;

/* 初始化 host_link 内部状态（行缓冲、计数器） */
void app_host_link_init(void);

/* 非阻塞读取一行命令。
 * 返回 APP_HOST_CMD_NONE 表示当前还没有完整命令，调用方应继续干别的事。
 * 返回其它值表示已经收到一帧并解析完成，调用方应立即处理并响应。 */
app_host_cmd_t app_host_link_poll(void);

/* 各类发往主控板的帧 */
app_status_t app_host_link_send_ready(void);
app_status_t app_host_link_send_ack(const char * p_cmd_name);
app_status_t app_host_link_send_pong(void);
app_status_t app_host_link_send_class_result(fruit_grade_t grade, uint8_t score);
app_status_t app_host_link_send_error(app_status_t status);
app_status_t app_host_link_send_text(const char * p_text);

#endif /* APP_HOST_LINK_H_ */

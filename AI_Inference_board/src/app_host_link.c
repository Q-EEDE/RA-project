#include "app_host_link.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app_board.h"
#include "app_config.h"

/* 行缓冲：用来累积主控板发来的字符，遇到 \n 就解析一次 */
static char g_host_line[APP_HOST_LINK_LINE_MAX_LEN];
static uint16_t g_host_line_index;
static bool g_host_line_overflow;

static app_host_cmd_t app_host_link_match_command(const char * p_line);
static uint8_t app_host_link_clamp_score(uint8_t score);

void app_host_link_init(void)
{
    g_host_line_index = 0U;
    g_host_line_overflow = false;
    memset(g_host_line, 0, sizeof(g_host_line));
}

app_host_cmd_t app_host_link_poll(void)
{
    app_host_cmd_t cmd = APP_HOST_CMD_NONE;
    uint8_t byte = 0U;

    /* 把所有已经到达的字节都搬到行缓冲，只要一遇到 \n 就立刻返回结果。
     * 这样不会因为缓冲一次性塞满而阻塞主循环。 */
    while (app_board_host_has_byte())
    {
        if (APP_STATUS_OK != app_board_host_read_byte(&byte, 0U))
        {
            break;
        }

        if (('\r' == (char) byte) || ('\n' == (char) byte))
        {
            if (0U == g_host_line_index)
            {
                /* 空行，忽略；继续读下一个字节 */
                continue;
            }

            if (g_host_line_overflow)
            {
                /* 之前因为太长被丢弃，整行作废 */
                g_host_line_index = 0U;
                g_host_line_overflow = false;
                cmd = APP_HOST_CMD_UNKNOWN;
                break;
            }

            g_host_line[g_host_line_index] = '\0';
            cmd = app_host_link_match_command(g_host_line);
            g_host_line_index = 0U;
            break;
        }

        if (g_host_line_index < (APP_HOST_LINK_LINE_MAX_LEN - 1U))
        {
            g_host_line[g_host_line_index] = (char) byte;
            g_host_line_index++;
        }
        else
        {
            /* 行已经太长，把后续字节全部吞掉，等到下一个换行再复位 */
            g_host_line_overflow = true;
        }
    }

    return cmd;
}

app_status_t app_host_link_send_ready(void)
{
    return app_host_link_send_text("$READY\r\n");
}

app_status_t app_host_link_send_ack(const char * p_cmd_name)
{
    char frame[24];
    int written;

    if (NULL == p_cmd_name)
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    written = snprintf(frame, sizeof(frame), "$ACK,%s\r\n", p_cmd_name);

    if ((written <= 0) || ((uint32_t) written >= sizeof(frame)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    return app_host_link_send_text(frame);
}

app_status_t app_host_link_send_pong(void)
{
    return app_host_link_send_text("$PONG\r\n");
}

app_status_t app_host_link_send_class_result(fruit_grade_t grade, uint8_t score)
{
    char frame[32];
    int written;

    written = snprintf(frame,
                       sizeof(frame),
                       "$CLS,%s,%u\r\n",
                       fruit_grade_to_text(grade),
                       (unsigned int) app_host_link_clamp_score(score));

    if ((written <= 0) || ((uint32_t) written >= sizeof(frame)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    return app_host_link_send_text(frame);
}

app_status_t app_host_link_send_error(app_status_t status)
{
    char frame[32];
    int written;

    written = snprintf(frame,
                       sizeof(frame),
                       "$ERR,%s\r\n",
                       app_status_to_text(status));

    if ((written <= 0) || ((uint32_t) written >= sizeof(frame)))
    {
        return APP_STATUS_FRAME_ERROR;
    }

    return app_host_link_send_text(frame);
}

app_status_t app_host_link_send_text(const char * p_text)
{
    if (NULL == p_text)
    {
        return APP_STATUS_INVALID_ARGUMENT;
    }

    return app_board_host_write((const uint8_t *) p_text,
                                (uint32_t) strlen(p_text),
                                APP_HOST_LINK_TX_TIMEOUT_MS);
}

static app_host_cmd_t app_host_link_match_command(const char * p_line)
{
    /* 严格按字符匹配，避免 strncmp 把 $TRIGGER 误判成 $TRIG */
    if (0 == strcmp(p_line, "$TRIG"))
    {
        return APP_HOST_CMD_TRIG;
    }

    if (0 == strcmp(p_line, "$PING"))
    {
        return APP_HOST_CMD_PING;
    }

    return APP_HOST_CMD_UNKNOWN;
}

static uint8_t app_host_link_clamp_score(uint8_t score)
{
    return (score > 100U) ? 100U : score;
}

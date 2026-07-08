#include "hal_data.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(BSP_MCU_R7FA6M5BF3CFP)
#error "This SCI9 polling implementation assumes the RA6M5 target used by the current control_board project."
#endif

/*
 * ============================================================
 * 水果分类打包机 - RA6M5 / FSP 版本
 *
 * 当前 FSP Stack:
 *   g_motor1_pwm ~ g_motor4_pwm : 4 路直流电机 PWM
 *   g_servo1_pwm ~ g_servo4_pwm : 4 路舵机 PWM
 *   g_timer_tick                : 1ms 系统节拍
 *
 * 注意:
 *   1. 8 路 PWM 当前全部使用 GTIOCA 输出，所以 dutyCycleSet() 第三个参数使用 GPT_IO_PIN_GTIOCA。
 *   2. 电机方向 GPIO 需要你在 FSP Pins 页面配置为 GPIO Output Initial Low。
 *   3. 按键/传感器 GPIO 需要你在 FSP Pins 页面配置为 GPIO Input，并按硬件选择上拉/下拉。
 * ============================================================
 */

/* ======================== GPIO 引脚映射 ======================== */

/* 电机方向脚：小电机仍按 IN1/IN2 使用。
 * 注意：大电机现在使用 BTS7960，MOTOR1_IN1/IN2 不再表示方向脚，
 *      而是作为 BTS7960 的 R_EN / L_EN 使能脚使用。 */
#define MOTOR1_IN1_PIN             (BSP_IO_PORT_01_PIN_00)  /* P100, P1-29 */
#define MOTOR1_IN2_PIN             (BSP_IO_PORT_01_PIN_01)  /* P101, P1-30 */
#define MOTOR2_IN1_PIN             (BSP_IO_PORT_01_PIN_02)  /* P102, P1-31 */
#define MOTOR2_IN2_PIN             (BSP_IO_PORT_01_PIN_03)  /* P103, P1-32 */
#define MOTOR3_IN1_PIN             (BSP_IO_PORT_01_PIN_04)  /* P104, P1-33 */
#define MOTOR3_IN2_PIN             (BSP_IO_PORT_01_PIN_05)  /* P105, P1-34 */
#define MOTOR4_IN1_PIN             (BSP_IO_PORT_05_PIN_00)  /* P500, P1-28 */
#define MOTOR4_IN2_PIN             (BSP_IO_PORT_05_PIN_01)  /* P501, P1-27 */

/* 输入脚：按钮和 E3F-DS30C4 NPN 常开光电开关都按低电平有效处理。 */
#define START_KEY_PIN              (KEY_PIN)                 /* P600, 绿色启动按钮 */
#define ESTOP_KEY_PIN              (BSP_IO_PORT_05_PIN_02)   /* P502, 急停常开触点，软件检测 */
#define CAMERA_SENSOR_PIN          (BSP_IO_PORT_06_PIN_01)   /* P601, 摄像头下光电开关 */
#define GATE1_SENSOR_PIN           (BSP_IO_PORT_05_PIN_03)   /* P503, 挡板1光电开关 */
#define GATE2_SENSOR_PIN           (BSP_IO_PORT_05_PIN_04)   /* P504, 挡板2光电开关 */

#define START_KEY_ACTIVE_LEVEL     (BSP_IO_LEVEL_LOW)
#define ESTOP_KEY_ACTIVE_LEVEL     (BSP_IO_LEVEL_LOW)
#define CAMERA_SENSOR_ACTIVE_LEVEL (BSP_IO_LEVEL_LOW)
#define GATE1_SENSOR_ACTIVE_LEVEL  (BSP_IO_LEVEL_LOW)
#define GATE2_SENSOR_ACTIVE_LEVEL  (BSP_IO_LEVEL_LOW)
#define CLASSIFICATION_LED_PIN     (LED3_PIN)
#define FRUIT_DETECTED_LED_PIN     (CLASSIFICATION_LED_PIN)

/* ======================== 机械动作参数 ======================== */

#define BIG_MOTOR_ID               (0U)    /* g_motor1_pwm, 大电机：分隔装置 */
#define SMALL_MOTOR_ID             (1U)    /* g_motor2_pwm, 小电机：水果传送带 */
#define GATE1_SERVO_ID             (0U)    /* g_servo1_pwm, 挡板1舵机 */
#define GATE2_SERVO_ID             (1U)    /* g_servo2_pwm, 挡板2舵机 */

/* 大电机 BTS7960 接线假设：
 *   RPWM -> g_motor1_pwm 输出
 *   LPWM -> GND，当前最小版只做单向转动
 *   R_EN -> MOTOR1_IN1_PIN
 *   L_EN -> MOTOR1_IN2_PIN
 */
#define BIG_MOTOR_DRIVER_BTS7960   (1U)

#define BIG_MOTOR_SPEED_PERCENT    (75U)
#define SMALL_MOTOR_SPEED_PERCENT  (80U)
#define MOTOR_STOP_SPEED_PERCENT   (0U)

#define SERVO_HOME_ANGLE_DEG       (90U)
#define GATE1_SORT_ANGLE_DEG       (30U)
#define GATE2_SORT_ANGLE_DEG       (150U)
#define SORT_TEST_FRUIT_TYPE       (FRUIT_TYPE_A)
#define SORT_USE_TEST_RESULT_ON_TIMEOUT (0U)

/* 常见舵机 20ms 周期下的高电平脉宽范围 */
#define SERVO_PERIOD_US            (20000U)
#define SERVO_MIN_PULSE_US         (500U)
#define SERVO_MAX_PULSE_US         (2500U)

/* 状态机延时 */
#define SENSOR_STABLE_TIME_MS      (30U)
#define START_KEY_STABLE_TIME_MS   (30U)
#define BIG_MOTOR_FEED_TIME_MS     (700U)
#define WAIT_CAMERA_TIMEOUT_MS     (8000U)
#define SERVO_SET_WAIT_MS          (500U)
#define SERVO_RETURN_WAIT_MS       (400U)
#define SORT_GATE_ACTION_HOLD_MS   (3000U)
#define WAIT_GATE_PASS_TIMEOUT_MS  (8000U)
#define CLASSIFICATION_WAIT_TIMEOUT_MS (3000U)
#define SORT_NEXT_WAIT_MS          (100U)
#define MAIN_LOOP_DELAY_MS         (1U)
#define SORT_CAMERA_STOP_TIME_MS   (1500U)
#define SORT_FRUIT_QUEUE_SIZE      (8U)
#define SORT_PENDING_CAPTURE_MAX   (SORT_FRUIT_QUEUE_SIZE)
#define SORT_AUTO_FEED_NEXT_ENABLE (1U)
#define CLASSIFICATION_LED_ON_MS   (150U)
#define CLASSIFICATION_LED_OFF_MS  (150U)

/* ======================== 按键测试模式 ======================== */

/*
 * 设为 1：只测试两个舵机，按键按下时舵机动作，松开时回中位。
 * 设为 0：恢复原来的水果分拣状态机。
 */
#define SERVO_BUTTON_TEST_ENABLE   (0U)
#define SERVO_TEST_LOOP_DELAY_MS   (10U)
#define SERVO1_TEST_BUTTON_PIN     (START_KEY_PIN)            /* P600 */
#define SERVO2_TEST_BUTTON_PIN     (CAMERA_SENSOR_PIN)        /* P601 */
#define SERVO1_TEST_ACTIVE_LEVEL   (START_KEY_ACTIVE_LEVEL)
#define SERVO2_TEST_ACTIVE_LEVEL   (CAMERA_SENSOR_ACTIVE_LEVEL)
#define SERVO1_TEST_ACTIVE_ANGLE_DEG (GATE1_SORT_ANGLE_DEG)
#define SERVO2_TEST_ACTIVE_ANGLE_DEG (GATE2_SORT_ANGLE_DEG)

/*
 * 设为 1：只测试两个电机，按键按下时电机运行，松开时停止。
 * P600 控制电机1，P601 控制电机2。
 */
#define MOTOR_BUTTON_TEST_ENABLE   (0U)
#define MOTOR_TEST_LOOP_DELAY_MS   (10U)
#define MOTOR1_TEST_SPEED_PERCENT  (BIG_MOTOR_SPEED_PERCENT)
#define MOTOR2_TEST_SPEED_PERCENT  (SMALL_MOTOR_SPEED_PERCENT)
#define MOTOR1_TEST_BUTTON_PIN     (START_KEY_PIN)            /* P600 */
#define MOTOR2_TEST_BUTTON_PIN     (CAMERA_SENSOR_PIN)        /* P601 */
#define MOTOR1_TEST_ACTIVE_LEVEL   (START_KEY_ACTIVE_LEVEL)
#define MOTOR2_TEST_ACTIVE_LEVEL   (CAMERA_SENSOR_ACTIVE_LEVEL)

/*
 * 设为 1：按 P600 启动顺序流程，按 P601 全局停止。
 * P502 作为急停/关闭按钮，光电1/2/3 分别使用 P601/P503/P504。
 * 当前全部按低电平有效处理。
 */
#define PHOTO_SEQUENCE_TEST_ENABLE (0U)
#define PHOTO_SEQUENCE_LOOP_DELAY_MS (10U)
#define PHOTO_SEQUENCE_STOP_TIME_MS  (2000U)
#define PHOTO_SEQUENCE_MOTOR_SPEED_PERCENT (SMALL_MOTOR_SPEED_PERCENT)
#define PHOTO_SEQUENCE_START_PIN    (START_KEY_PIN)           /* P600 */
#define PHOTO_SEQUENCE_STOP_PIN     (ESTOP_KEY_PIN)           /* P502 */
#define PHOTO_SEQUENCE_SENSOR1_PIN  (CAMERA_SENSOR_PIN)       /* P601 */
#define PHOTO_SEQUENCE_SENSOR2_PIN  (GATE1_SENSOR_PIN)        /* P503 */
#define PHOTO_SEQUENCE_SENSOR3_PIN  (GATE2_SENSOR_PIN)        /* P504 */
#define PHOTO_SEQUENCE_START_ACTIVE_LEVEL   (START_KEY_ACTIVE_LEVEL)
#define PHOTO_SEQUENCE_STOP_ACTIVE_LEVEL    (ESTOP_KEY_ACTIVE_LEVEL)
#define PHOTO_SEQUENCE_SENSOR1_ACTIVE_LEVEL (CAMERA_SENSOR_ACTIVE_LEVEL)
#define PHOTO_SEQUENCE_SENSOR2_ACTIVE_LEVEL (GATE1_SENSOR_ACTIVE_LEVEL)
#define PHOTO_SEQUENCE_SENSOR3_ACTIVE_LEVEL (GATE2_SENSOR_ACTIVE_LEVEL)
#define PHOTO_SEQUENCE_SERVO_READY_ANGLE_DEG (0U)
#define PHOTO_SEQUENCE_SERVO_ACTIVE_ANGLE_DEG (90U)

/* ======================== SCI9 串口参数 ======================== */

#define CONTROL_UART_BAUD_RATE     (115200U)
#define CONTROL_UART_FRAME_MAX_LEN  (24U)
#define CONTROL_UART_RXD_PIN       (BSP_IO_PORT_01_PIN_10)   /* P110, RXD9，接推理板 SCI2 TX */
#define CONTROL_UART_TXD_PIN       (BSP_IO_PORT_06_PIN_02)   /* P602, TXD9，接推理板 SCI2 RX */

#define SCI9_SMR_CKS_0             (0U)
#define SCI9_SCR_RE_MASK           (1U << 4U)
#define SCI9_SCR_TE_MASK           (1U << 5U)
#define SCI9_SCR_RIE_MASK          (1U << 6U)
#define SCI9_SSR_RDRF_MASK         (R_SCI0_SSR_RDRF_Msk)
#define SCI9_SSR_ORER_MASK         (R_SCI0_SSR_ORER_Msk)
#define SCI9_SSR_FER_MASK          (R_SCI0_SSR_FER_Msk)
#define SCI9_SSR_PER_MASK          (R_SCI0_SSR_PER_Msk)
#define SCI9_SSR_ERROR_MASK        (SCI9_SSR_ORER_MASK | SCI9_SSR_FER_MASK | SCI9_SSR_PER_MASK)

/* ======================== 类型定义 ======================== */

typedef enum e_motor_direction
{
    MOTOR_DIR_COAST = 0,    /* IN1=0, IN2=0，电机空转停止 */
    MOTOR_DIR_FORWARD,      /* IN1=1, IN2=0，正转 */
    MOTOR_DIR_REVERSE,      /* IN1=0, IN2=1，反转 */
    MOTOR_DIR_BRAKE         /* IN1=1, IN2=1，刹车，是否支持取决于驱动芯片 */
} motor_direction_t;

typedef enum e_sort_state
{
    SORT_IDLE = 0,              /* 上电待机，等待绿色启动按钮 */
    SORT_FEED_ORANGE,           /* 大电机放出一个橙子，小电机开始输送 */
    SORT_WAIT_CAMERA,           /* 等待摄像头下光电开关触发 */
    SORT_CAPTURE_OR_CLASSIFY,   /* 停小电机，触发拍照/分类，等待结果 */
    SORT_SET_GATE,              /* 根据分类结果设置两个挡板 */
    SORT_MOVE_TO_GATE,          /* 小电机继续运行，让橙子走向挡板 */
    SORT_WAIT_GATE_PASS,        /* 等待目标挡板光电确认橙子通过 */
    SORT_GATE_RETURN,           /* 挡板回到中位 */
    SORT_NEXT,                  /* 清理本轮状态，进入下一轮 */
    SORT_EMERGENCY_STOP,        /* 急停状态，释放后必须重新按启动 */
    SORT_ERROR                  /* 错误兜底 */
} sort_state_t;

typedef enum e_photo_sequence_state
{
    PHOTO_SEQ_IDLE = 0,             /* 等待 P600 启动 */
    PHOTO_SEQ_WAIT_FIRST_SENSOR1,   /* 等待光电1检测第一个物体 */
    PHOTO_SEQ_FIRST_STOP,           /* 电机2第一次停止 2s */
    PHOTO_SEQ_WAIT_SENSOR2,         /* 等待光电2检测第一个物体 */
    PHOTO_SEQ_WAIT_SECOND_SENSOR1,  /* 等待光电1检测第二个物体 */
    PHOTO_SEQ_SECOND_STOP,          /* 电机2第二次停止 2s */
    PHOTO_SEQ_WAIT_SENSOR3,         /* 等待光电3检测第二个物体 */
    PHOTO_SEQ_DONE                  /* 本轮动作完成，等待 P601 停止或复位 */
} photo_sequence_state_t;

typedef enum e_fruit_type
{
    FRUIT_TYPE_UNKNOWN = 0,
    FRUIT_TYPE_A,
    FRUIT_TYPE_B,
    FRUIT_TYPE_C
} fruit_type_t;

typedef enum e_target_gate
{
    TARGET_GATE_NONE = 0,
    TARGET_GATE_1,
    TARGET_GATE_2
} target_gate_t;

typedef struct st_motor_hw
{
    timer_instance_t const * p_pwm;
    bsp_io_port_pin_t       in1_pin;
    bsp_io_port_pin_t       in2_pin;
} motor_hw_t;

typedef struct st_servo_hw
{
    timer_instance_t const * p_pwm;
} servo_hw_t;

/* ======================== 全局变量 ======================== */

static motor_hw_t const g_motors[] =
{
    { &g_motor1_pwm, MOTOR1_IN1_PIN, MOTOR1_IN2_PIN },
    { &g_motor2_pwm, MOTOR2_IN1_PIN, MOTOR2_IN2_PIN },
    { &g_motor3_pwm, MOTOR3_IN1_PIN, MOTOR3_IN2_PIN },
    { &g_motor4_pwm, MOTOR4_IN1_PIN, MOTOR4_IN2_PIN },
};

static servo_hw_t const g_servos[] =
{
    { &g_servo1_pwm },
    { &g_servo2_pwm },
    { &g_servo3_pwm },
    { &g_servo4_pwm },
};

static volatile uint32_t g_system_ms = 0U;
static sort_state_t     g_sort_state = SORT_IDLE;
static bool             g_state_just_entered = true;
static uint32_t         g_state_enter_ms = 0U;
static volatile fruit_type_t g_pending_fruit_type = FRUIT_TYPE_UNKNOWN;
static volatile bool    g_control_uart_classification_ready = false;
static bool             g_control_uart_ready = false;
static char             g_control_uart_frame[CONTROL_UART_FRAME_MAX_LEN];
static uint32_t         g_control_uart_frame_len = 0U;
static target_gate_t    g_target_gate = TARGET_GATE_NONE;
static volatile fruit_type_t g_sort_fruit_queue[SORT_FRUIT_QUEUE_SIZE];
static volatile uint8_t g_sort_fruit_queue_head = 0U;
static volatile uint8_t g_sort_fruit_queue_tail = 0U;
static volatile uint8_t g_sort_fruit_queue_count = 0U;
static volatile uint8_t g_sort_pending_capture_count = 0U;
static volatile bool    g_sort_fault_requested = false;
static bool             g_camera_sensor_last_stable_active = false;
static bool             g_gate1_sensor_last_stable_active = false;
static bool             g_gate2_sensor_last_stable_active = false;
static bool             g_gate1_servo_active = false;
static bool             g_gate2_servo_active = false;
static uint32_t         g_gate1_servo_active_start_ms = 0U;
static uint32_t         g_gate2_servo_active_start_ms = 0U;
static volatile uint8_t g_classification_led_pending_blinks = 0U;
static uint8_t          g_classification_led_remaining_blinks = 0U;
static bool             g_classification_led_active = false;
static bool             g_classification_led_on = false;
static uint32_t         g_classification_led_last_toggle_ms = 0U;
static bool             g_camera_stop_active = false;
static uint32_t         g_camera_stop_start_ms = 0U;
static bool             g_big_motor_feed_active = false;
static uint32_t         g_big_motor_feed_start_ms = 0U;
static uint32_t         g_start_key_active_start_ms = 0U;
static uint32_t         g_camera_sensor_active_start_ms = 0U;
static uint32_t         g_gate1_sensor_active_start_ms = 0U;
static uint32_t         g_gate2_sensor_active_start_ms = 0U;
static photo_sequence_state_t g_photo_sequence_state = PHOTO_SEQ_IDLE;
static bool             g_photo_sequence_state_just_entered = true;
static uint32_t         g_photo_sequence_state_enter_ms = 0U;
static uint32_t         g_photo_sequence_start_active_start_ms = 0U;
static uint32_t         g_photo_sequence_sensor1_active_start_ms = 0U;
static uint32_t         g_photo_sequence_sensor2_active_start_ms = 0U;
static uint32_t         g_photo_sequence_sensor3_active_start_ms = 0U;

/* 推理板返回 $CLS 后，在串口回调里更新这个变量。 */
static volatile fruit_type_t g_current_fruit_type = FRUIT_TYPE_UNKNOWN;

/* ======================== 函数声明 ======================== */

static void     app_error_trap(fsp_err_t err);
static fsp_err_t app_init(void);
static fsp_err_t timer_open(timer_instance_t const * p_timer);
static fsp_err_t timer_start(timer_instance_t const * p_timer);
static fsp_err_t timer_open_start(timer_instance_t const * p_timer);

static fsp_err_t motors_init(void);
static fsp_err_t motor_set_direction(uint8_t motor_id, motor_direction_t direction);
static fsp_err_t motor_set_speed_percent(uint8_t motor_id, uint8_t speed_percent);
static fsp_err_t bts7960_big_motor_run(uint8_t speed_percent);
static fsp_err_t bts7960_big_motor_stop(void);
static fsp_err_t motor_run(uint8_t motor_id, uint8_t speed_percent);
static fsp_err_t motor_stop(uint8_t motor_id);
static fsp_err_t all_motors_stop(void);

static fsp_err_t servos_init(void);
static fsp_err_t servo_set_angle(uint8_t servo_id, uint32_t angle_deg);
static uint32_t  servo_angle_to_counts(timer_instance_t const * p_pwm, uint32_t angle_deg);

static bool      input_pin_is_active(bsp_io_port_pin_t pin, bsp_io_level_t active_level);
static bool      input_pin_is_stable_active(bsp_io_port_pin_t pin,
                                            bsp_io_level_t active_level,
                                            uint32_t * p_active_start_ms,
                                            uint32_t stable_time_ms);
static bool      start_key_is_stable_pressed(void);
static bool      estop_key_is_pressed(void);
static bool      camera_sensor_is_stable_active(void);
static bool      gate1_sensor_is_stable_active(void);
static bool      gate2_sensor_is_stable_active(void);
static bool      target_gate_sensor_is_stable_active(void);

static fsp_err_t capture_or_classify_start(void);
static fsp_err_t fruit_clear_action(void);
static fsp_err_t gates_home(void);
static fsp_err_t gates_safe(void);
static fsp_err_t set_gates_by_fruit_type(fruit_type_t fruit_type);
static void      sort_multi_reset(void);
static fsp_err_t sort_multi_run_once(void);
static fsp_err_t sort_start_feed_pulse(void);
static fsp_err_t sort_update_feed_pulse(void);
static fsp_err_t sort_update_camera_stop(void);
static fsp_err_t sort_update_gate_servo_returns(void);
static fsp_err_t classification_led_update(void);
static fsp_err_t classification_led_set(bool on);
static void      classification_led_request(fruit_type_t fruit_type);
static fsp_err_t sort_handle_camera_trigger(void);
static fsp_err_t sort_handle_gate1_trigger(void);
static fsp_err_t sort_handle_gate2_trigger(void);
static bool      stable_active_rising_edge(bool stable_active, bool * p_last_stable_active);
static bool      sort_queue_push(fruit_type_t fruit_type);
static bool      sort_queue_peek(fruit_type_t * p_fruit_type);
static bool      sort_queue_pop(fruit_type_t * p_fruit_type);
static void      sort_queue_clear(void);
static void      sort_capture_result_received(fruit_type_t fruit_type);

static fsp_err_t photo_sequence_test_init(void);
static fsp_err_t photo_sequence_test_run(void);
static void      photo_sequence_state_change(photo_sequence_state_t new_state);
static void      photo_sequence_reset_inputs(void);
static fsp_err_t photo_sequence_stop_all(void);
static bool      photo_sequence_start_is_stable_pressed(void);
static bool      photo_sequence_stop_is_pressed(void);
static bool      photo_sequence_sensor1_is_stable_active(void);
static bool      photo_sequence_sensor2_is_stable_active(void);
static bool      photo_sequence_sensor3_is_stable_active(void);

static fsp_err_t motor_button_test_init(void);
static fsp_err_t motor_button_test_run(void);

static fsp_err_t servo_button_test_init(void);
static fsp_err_t servo_button_test_run(void);

static void      sort_state_machine_run(void);
static void      sort_state_change(sort_state_t new_state);
static fruit_type_t get_effective_fruit_type(void);
static target_gate_t get_target_gate_by_fruit_type(fruit_type_t fruit_type);
static void      clear_one_orange_result(void);

static uint32_t  millis(void);
static bool      time_elapsed(uint32_t start_ms, uint32_t delay_ms);

/* ----- 与推理板的串口握手（单向：仅发送 $TRIG / $PING） ----- */
static fsp_err_t control_uart_init(void);
static fsp_err_t control_uart_send_trig(void);
static fsp_err_t control_uart_send_ping(void);   /* 开机自检：发一帧 $PING，验证 UART TX 是否真的工作 */
static fsp_err_t control_uart_handle_rx_char(uint8_t byte);
static bool      control_uart_process_frame(const char * p_frame);
static bool      control_uart_parse_cls_frame(const char * p_frame, fruit_type_t * p_fruit_type);
static bool      control_uart_parse_fruit_type(const char * p_text, fruit_type_t * p_fruit_type);
static bool      control_uart_is_prefix(const char * p_text, const char * p_prefix);

/* ======================== FSP 回调 ======================== */

/* SCI9 UART 回调（FSP 生成时 Callback 字段必须填这个名字） */
void uart_host_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (UART_EVENT_RX_CHAR == p_args->event)
    {
        (void) control_uart_handle_rx_char((uint8_t) p_args->data);
    }
}

void tick_timer_callback(timer_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    g_system_ms++;
}

/* ======================== 用户入口 ======================== */

void hal_entry(void)
{
#if PHOTO_SEQUENCE_TEST_ENABLE
    FSP_PARAMETER_NOT_USED(app_init);
    FSP_PARAMETER_NOT_USED(sort_state_machine_run);
    FSP_PARAMETER_NOT_USED(motor_button_test_init);
    FSP_PARAMETER_NOT_USED(motor_button_test_run);
    FSP_PARAMETER_NOT_USED(servo_button_test_init);
    FSP_PARAMETER_NOT_USED(servo_button_test_run);

    fsp_err_t err = photo_sequence_test_init();
    if (FSP_SUCCESS != err)
    {
        app_error_trap(err);
    }

    while (1)
    {
        err = photo_sequence_test_run();
        if (FSP_SUCCESS != err)
        {
            app_error_trap(err);
        }

        R_BSP_SoftwareDelay(PHOTO_SEQUENCE_LOOP_DELAY_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#elif MOTOR_BUTTON_TEST_ENABLE
    FSP_PARAMETER_NOT_USED(app_init);
    FSP_PARAMETER_NOT_USED(sort_state_machine_run);
    FSP_PARAMETER_NOT_USED(photo_sequence_test_init);
    FSP_PARAMETER_NOT_USED(photo_sequence_test_run);
    FSP_PARAMETER_NOT_USED(servo_button_test_init);
    FSP_PARAMETER_NOT_USED(servo_button_test_run);

    fsp_err_t err = motor_button_test_init();
    if (FSP_SUCCESS != err)
    {
        app_error_trap(err);
    }

    while (1)
    {
        err = motor_button_test_run();
        if (FSP_SUCCESS != err)
        {
            app_error_trap(err);
        }

        R_BSP_SoftwareDelay(MOTOR_TEST_LOOP_DELAY_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#elif SERVO_BUTTON_TEST_ENABLE
    FSP_PARAMETER_NOT_USED(app_init);
    FSP_PARAMETER_NOT_USED(sort_state_machine_run);
    FSP_PARAMETER_NOT_USED(photo_sequence_test_init);
    FSP_PARAMETER_NOT_USED(photo_sequence_test_run);
    FSP_PARAMETER_NOT_USED(motor_button_test_init);
    FSP_PARAMETER_NOT_USED(motor_button_test_run);

    fsp_err_t err = servo_button_test_init();
    if (FSP_SUCCESS != err)
    {
        app_error_trap(err);
    }

    while (1)
    {
        err = servo_button_test_run();
        if (FSP_SUCCESS != err)
        {
            app_error_trap(err);
        }

        R_BSP_SoftwareDelay(SERVO_TEST_LOOP_DELAY_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#else
    FSP_PARAMETER_NOT_USED(photo_sequence_test_init);
    FSP_PARAMETER_NOT_USED(photo_sequence_test_run);
    FSP_PARAMETER_NOT_USED(motor_button_test_init);
    FSP_PARAMETER_NOT_USED(motor_button_test_run);
    FSP_PARAMETER_NOT_USED(servo_button_test_init);
    FSP_PARAMETER_NOT_USED(servo_button_test_run);

    fsp_err_t err = app_init();
    if (FSP_SUCCESS != err)
    {
        app_error_trap(err);
    }

    while (1)
    {
        sort_state_machine_run();
        R_BSP_SoftwareDelay(MAIN_LOOP_DELAY_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#endif
}

/* ======================== 初始化模块 ======================== */

static fsp_err_t app_init(void)
{
    fsp_err_t err;

    err = motors_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = servos_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = timer_open_start(&g_timer_tick);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = all_motors_stop();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = gates_home();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* 打开与推理板的串口；失败也不影响电机/舵机工作，所以只置 ready 标志。
     * 注意：FSP 里 P401(LED2_PIN) 当前是 Disabled，不能当 GPIO 输出用，
     * 所以这里改成开机主动发一帧 $PING 给推理板做连通性自检——
     * 如果推理板调试串口能看到 $DBG,HOSTLINK,RX,PING，
     * 就说明 SCI9 TX、波特率、wiring 全部 OK。 */
    if (FSP_SUCCESS == control_uart_init())
    {
        g_control_uart_ready = true;
        (void) control_uart_send_ping();
    }
    else
    {
        g_control_uart_ready = false;
    }

    sort_state_change(SORT_IDLE);
    return FSP_SUCCESS;
}

static fsp_err_t timer_open_start(timer_instance_t const * p_timer)
{
    fsp_err_t err;

    err = timer_open(p_timer);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = timer_start(p_timer);
    return err;
}

static fsp_err_t timer_open(timer_instance_t const * p_timer)
{
    if (NULL == p_timer)
    {
        return FSP_ERR_ASSERTION;
    }

    return p_timer->p_api->open(p_timer->p_ctrl, p_timer->p_cfg);
}

static fsp_err_t timer_start(timer_instance_t const * p_timer)
{
    if (NULL == p_timer)
    {
        return FSP_ERR_ASSERTION;
    }

    return p_timer->p_api->start(p_timer->p_ctrl);
}

static void app_error_trap(fsp_err_t err)
{
    FSP_PARAMETER_NOT_USED(err);

    (void) all_motors_stop();

#if defined(DEBUG) || defined(_DEBUG)
    __BKPT(0);
#endif

    while (1)
    {
        /* 出错后停在这里，方便调试器定位。 */
    }
}

/* ======================== 光电顺序测试模块 ======================== */

static fsp_err_t photo_sequence_test_init(void)
{
    fsp_err_t err;

    err = motors_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = servos_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = timer_open_start(&g_timer_tick);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = photo_sequence_stop_all();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    photo_sequence_state_change(PHOTO_SEQ_IDLE);
    return FSP_SUCCESS;
}

static fsp_err_t photo_sequence_test_run(void)
{
    fsp_err_t err = FSP_SUCCESS;
    bool state_entry = g_photo_sequence_state_just_entered;

    g_photo_sequence_state_just_entered = false;

    if ((PHOTO_SEQ_IDLE != g_photo_sequence_state) && photo_sequence_stop_is_pressed())
    {
        err = photo_sequence_stop_all();
        if (FSP_SUCCESS == err)
        {
            photo_sequence_state_change(PHOTO_SEQ_IDLE);
        }
        return err;
    }

    switch (g_photo_sequence_state)
    {
        case PHOTO_SEQ_IDLE:
        {
            if (state_entry)
            {
                err = photo_sequence_stop_all();
            }

            if ((FSP_SUCCESS == err) && photo_sequence_start_is_stable_pressed())
            {
                photo_sequence_reset_inputs();

                err = servo_set_angle(GATE1_SERVO_ID, PHOTO_SEQUENCE_SERVO_READY_ANGLE_DEG);
                if (FSP_SUCCESS == err)
                {
                    err = servo_set_angle(GATE2_SERVO_ID, PHOTO_SEQUENCE_SERVO_READY_ANGLE_DEG);
                }
                if (FSP_SUCCESS == err)
                {
                    err = motor_run(SMALL_MOTOR_ID, PHOTO_SEQUENCE_MOTOR_SPEED_PERCENT);
                }
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_WAIT_FIRST_SENSOR1);
                }
            }
            break;
        }

        case PHOTO_SEQ_WAIT_FIRST_SENSOR1:
        {
            if (photo_sequence_sensor1_is_stable_active())
            {
                if (TARGET_GATE_1 == g_target_gate)
                {
                    err = servo_set_angle(GATE1_SERVO_ID, GATE1_SORT_ANGLE_DEG);
                }
                else
                {
                    err = servo_set_angle(GATE2_SERVO_ID, GATE2_SORT_ANGLE_DEG);
                }
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_FIRST_STOP);
                }
            }
            break;
        }

        case PHOTO_SEQ_FIRST_STOP:
        {
            if (time_elapsed(g_photo_sequence_state_enter_ms, PHOTO_SEQUENCE_STOP_TIME_MS))
            {
                err = motor_run(SMALL_MOTOR_ID, PHOTO_SEQUENCE_MOTOR_SPEED_PERCENT);
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_WAIT_SENSOR2);
                }
            }
            break;
        }

        case PHOTO_SEQ_WAIT_SENSOR2:
        {
            if (photo_sequence_sensor2_is_stable_active())
            {
                err = servo_set_angle(GATE1_SERVO_ID, PHOTO_SEQUENCE_SERVO_ACTIVE_ANGLE_DEG);
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_WAIT_SECOND_SENSOR1);
                }
            }
            break;
        }

        case PHOTO_SEQ_WAIT_SECOND_SENSOR1:
        {
            if (photo_sequence_sensor1_is_stable_active())
            {
                if (TARGET_GATE_1 == g_target_gate)
                {
                    err = servo_set_angle(GATE1_SERVO_ID, GATE1_SORT_ANGLE_DEG);
                }
                else
                {
                    err = servo_set_angle(GATE2_SERVO_ID, GATE2_SORT_ANGLE_DEG);
                }
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_SECOND_STOP);
                }
            }
            break;
        }

        case PHOTO_SEQ_SECOND_STOP:
        {
            if (time_elapsed(g_photo_sequence_state_enter_ms, PHOTO_SEQUENCE_STOP_TIME_MS))
            {
                err = motor_run(SMALL_MOTOR_ID, PHOTO_SEQUENCE_MOTOR_SPEED_PERCENT);
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_WAIT_SENSOR3);
                }
            }
            break;
        }

        case PHOTO_SEQ_WAIT_SENSOR3:
        {
            if (photo_sequence_sensor3_is_stable_active())
            {
                err = servo_set_angle(GATE2_SERVO_ID, PHOTO_SEQUENCE_SERVO_ACTIVE_ANGLE_DEG);
                if (FSP_SUCCESS == err)
                {
                    photo_sequence_state_change(PHOTO_SEQ_DONE);
                }
            }
            break;
        }

        case PHOTO_SEQ_DONE:
        default:
        {
            break;
        }
    }

    return err;
}

static void photo_sequence_state_change(photo_sequence_state_t new_state)
{
    g_photo_sequence_state = new_state;
    g_photo_sequence_state_just_entered = true;
    g_photo_sequence_state_enter_ms = millis();
    photo_sequence_reset_inputs();
}

static void photo_sequence_reset_inputs(void)
{
    g_photo_sequence_start_active_start_ms = 0U;
    g_photo_sequence_sensor1_active_start_ms = 0U;
    g_photo_sequence_sensor2_active_start_ms = 0U;
    g_photo_sequence_sensor3_active_start_ms = 0U;
}

static fsp_err_t photo_sequence_stop_all(void)
{
    fsp_err_t err;

    err = all_motors_stop();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = servo_set_angle(GATE1_SERVO_ID, PHOTO_SEQUENCE_SERVO_READY_ANGLE_DEG);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return servo_set_angle(GATE2_SERVO_ID, PHOTO_SEQUENCE_SERVO_READY_ANGLE_DEG);
}

static bool photo_sequence_start_is_stable_pressed(void)
{
    return input_pin_is_stable_active(PHOTO_SEQUENCE_START_PIN,
                                      PHOTO_SEQUENCE_START_ACTIVE_LEVEL,
                                      &g_photo_sequence_start_active_start_ms,
                                      START_KEY_STABLE_TIME_MS);
}

static bool photo_sequence_stop_is_pressed(void)
{
    return input_pin_is_active(PHOTO_SEQUENCE_STOP_PIN, PHOTO_SEQUENCE_STOP_ACTIVE_LEVEL);
}

static bool photo_sequence_sensor1_is_stable_active(void)
{
    return input_pin_is_stable_active(PHOTO_SEQUENCE_SENSOR1_PIN,
                                      PHOTO_SEQUENCE_SENSOR1_ACTIVE_LEVEL,
                                      &g_photo_sequence_sensor1_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

static bool photo_sequence_sensor2_is_stable_active(void)
{
    return input_pin_is_stable_active(PHOTO_SEQUENCE_SENSOR2_PIN,
                                      PHOTO_SEQUENCE_SENSOR2_ACTIVE_LEVEL,
                                      &g_photo_sequence_sensor2_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

static bool photo_sequence_sensor3_is_stable_active(void)
{
    return input_pin_is_stable_active(PHOTO_SEQUENCE_SENSOR3_PIN,
                                      PHOTO_SEQUENCE_SENSOR3_ACTIVE_LEVEL,
                                      &g_photo_sequence_sensor3_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

/* ======================== 电机按键测试模块 ======================== */

static fsp_err_t motor_button_test_init(void)
{
    fsp_err_t err;

    err = motors_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return all_motors_stop();
}

static fsp_err_t motor_button_test_run(void)
{
    static bool motor1_last_pressed = false;
    static bool motor2_last_pressed = false;
    static bool first_run = true;

    bool motor1_pressed = input_pin_is_active(MOTOR1_TEST_BUTTON_PIN, MOTOR1_TEST_ACTIVE_LEVEL);
    bool motor2_pressed = input_pin_is_active(MOTOR2_TEST_BUTTON_PIN, MOTOR2_TEST_ACTIVE_LEVEL);
    fsp_err_t err;

    if (first_run || (motor1_pressed != motor1_last_pressed))
    {
        if (motor1_pressed)
        {
            err = motor_run(BIG_MOTOR_ID, MOTOR1_TEST_SPEED_PERCENT);
        }
        else
        {
            err = motor_stop(BIG_MOTOR_ID);
        }

        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    if (first_run || (motor2_pressed != motor2_last_pressed))
    {
        if (motor2_pressed)
        {
            err = motor_run(SMALL_MOTOR_ID, MOTOR2_TEST_SPEED_PERCENT);
        }
        else
        {
            err = motor_stop(SMALL_MOTOR_ID);
        }

        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    motor1_last_pressed = motor1_pressed;
    motor2_last_pressed = motor2_pressed;
    first_run = false;

    return FSP_SUCCESS;
}

/* ======================== 舵机按键测试模块 ======================== */

static fsp_err_t servo_button_test_init(void)
{
    fsp_err_t err;

    err = motors_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = all_motors_stop();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = servos_init();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return gates_home();
}

static fsp_err_t servo_button_test_run(void)
{
    static bool servo1_last_pressed = false;
    static bool servo2_last_pressed = false;
    static bool first_run = true;

    bool servo1_pressed = input_pin_is_active(SERVO1_TEST_BUTTON_PIN, SERVO1_TEST_ACTIVE_LEVEL);
    bool servo2_pressed = input_pin_is_active(SERVO2_TEST_BUTTON_PIN, SERVO2_TEST_ACTIVE_LEVEL);
    fsp_err_t err;

    if (first_run || (servo1_pressed != servo1_last_pressed))
    {
        err = servo_set_angle(GATE1_SERVO_ID,
                              servo1_pressed ? SERVO1_TEST_ACTIVE_ANGLE_DEG : SERVO_HOME_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    if (first_run || (servo2_pressed != servo2_last_pressed))
    {
        err = servo_set_angle(GATE2_SERVO_ID,
                              servo2_pressed ? SERVO2_TEST_ACTIVE_ANGLE_DEG : SERVO_HOME_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    servo1_last_pressed = servo1_pressed;
    servo2_last_pressed = servo2_pressed;
    first_run = false;

    return FSP_SUCCESS;
}

/* ======================== 电机模块 ======================== */

static fsp_err_t motors_init(void)
{
    fsp_err_t err;

    for (uint8_t i = 0U; i < (sizeof(g_motors) / sizeof(g_motors[0])); i++)
    {
        err = timer_open(g_motors[i].p_pwm);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        /*
         * FSP 配置里的初始占空比可能不是 0%。
         * 所以先 open，再把 PWM 写成 0%，最后 start，避免上电瞬间电机误转。
         */
        err = motor_stop(i);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = timer_start(g_motors[i].p_pwm);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    return FSP_SUCCESS;
}

static fsp_err_t motor_set_direction(uint8_t motor_id, motor_direction_t direction)
{
    fsp_err_t      err;
    bsp_io_level_t in1_level = BSP_IO_LEVEL_LOW;
    bsp_io_level_t in2_level = BSP_IO_LEVEL_LOW;

    if (motor_id >= (sizeof(g_motors) / sizeof(g_motors[0])))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    switch (direction)
    {
        case MOTOR_DIR_FORWARD:
            in1_level = BSP_IO_LEVEL_HIGH;
            in2_level = BSP_IO_LEVEL_LOW;
            break;

        case MOTOR_DIR_REVERSE:
            in1_level = BSP_IO_LEVEL_LOW;
            in2_level = BSP_IO_LEVEL_HIGH;
            break;

        case MOTOR_DIR_BRAKE:
            in1_level = BSP_IO_LEVEL_HIGH;
            in2_level = BSP_IO_LEVEL_HIGH;
            break;

        case MOTOR_DIR_COAST:
        default:
            in1_level = BSP_IO_LEVEL_LOW;
            in2_level = BSP_IO_LEVEL_LOW;
            break;
    }

    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[motor_id].in1_pin, in1_level);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[motor_id].in2_pin, in2_level);
    return err;
}

static fsp_err_t motor_set_speed_percent(uint8_t motor_id, uint8_t speed_percent)
{
    fsp_err_t    err;
    timer_info_t info;
    uint32_t     duty_counts;

    if (motor_id >= (sizeof(g_motors) / sizeof(g_motors[0])))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if (speed_percent > 100U)
    {
        speed_percent = 100U;
    }

    err = g_motors[motor_id].p_pwm->p_api->infoGet(g_motors[motor_id].p_pwm->p_ctrl, &info);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    duty_counts = (uint32_t) (((uint64_t) info.period_counts * speed_percent) / 100U);

    err = g_motors[motor_id].p_pwm->p_api->dutyCycleSet(g_motors[motor_id].p_pwm->p_ctrl,
                                                        duty_counts,
                                                        GPT_IO_PIN_GTIOCA);
    return err;
}

static fsp_err_t bts7960_big_motor_run(uint8_t speed_percent)
{
    fsp_err_t err;

    /*
     * BTS7960 最小单向控制：
     *   R_EN/L_EN 置高使能驱动；
     *   RPWM 使用 g_motor1_pwm 输出速度；
     *   LPWM 外部接 GND，避免反向桥臂动作。
     */
    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[BIG_MOTOR_ID].in1_pin, BSP_IO_LEVEL_HIGH);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[BIG_MOTOR_ID].in2_pin, BSP_IO_LEVEL_HIGH);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    return motor_set_speed_percent(BIG_MOTOR_ID, speed_percent);
}

static fsp_err_t bts7960_big_motor_stop(void)
{
    fsp_err_t err;

    err = motor_set_speed_percent(BIG_MOTOR_ID, MOTOR_STOP_SPEED_PERCENT);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[BIG_MOTOR_ID].in1_pin, BSP_IO_LEVEL_LOW);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = g_ioport.p_api->pinWrite(g_ioport.p_ctrl, g_motors[BIG_MOTOR_ID].in2_pin, BSP_IO_LEVEL_LOW);
    return err;
}

static fsp_err_t motor_run(uint8_t motor_id, uint8_t speed_percent)
{
    fsp_err_t err;

#if BIG_MOTOR_DRIVER_BTS7960
    if (BIG_MOTOR_ID == motor_id)
    {
        return bts7960_big_motor_run(speed_percent);
    }
#endif

    err = motor_set_direction(motor_id, MOTOR_DIR_FORWARD);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = motor_set_speed_percent(motor_id, speed_percent);
    return err;
}

static fsp_err_t motor_stop(uint8_t motor_id)
{
    fsp_err_t err;

#if BIG_MOTOR_DRIVER_BTS7960
    if (BIG_MOTOR_ID == motor_id)
    {
        return bts7960_big_motor_stop();
    }
#endif

    err = motor_set_speed_percent(motor_id, MOTOR_STOP_SPEED_PERCENT);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = motor_set_direction(motor_id, MOTOR_DIR_COAST);
    return err;
}

static fsp_err_t all_motors_stop(void)
{
    fsp_err_t err;

    for (uint8_t i = 0U; i < (sizeof(g_motors) / sizeof(g_motors[0])); i++)
    {
        err = motor_stop(i);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    return FSP_SUCCESS;
}

/* ======================== 舵机模块 ======================== */

static fsp_err_t servos_init(void)
{
    fsp_err_t err;

    for (uint8_t i = 0U; i < (sizeof(g_servos) / sizeof(g_servos[0])); i++)
    {
        err = timer_open(g_servos[i].p_pwm);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        /*
         * 你的 FSP 舵机初始 duty 只能填 50%，但舵机需要约 7.5% 才是 90 度。
         * 这里在 start 输出 PWM 之前先写入 90 度脉宽，避免舵机上电抖动。
         */
        err = servo_set_angle(i, SERVO_HOME_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        err = timer_start(g_servos[i].p_pwm);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    return FSP_SUCCESS;
}

static fsp_err_t servo_set_angle(uint8_t servo_id, uint32_t angle_deg)
{
    uint32_t duty_counts;

    if (servo_id >= (sizeof(g_servos) / sizeof(g_servos[0])))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    duty_counts = servo_angle_to_counts(g_servos[servo_id].p_pwm, angle_deg);

    return g_servos[servo_id].p_pwm->p_api->dutyCycleSet(g_servos[servo_id].p_pwm->p_ctrl,
                                                         duty_counts,
                                                         GPT_IO_PIN_GTIOCA);
}

static uint32_t servo_angle_to_counts(timer_instance_t const * p_pwm, uint32_t angle_deg)
{
    timer_info_t info;
    uint32_t     pulse_us;
    uint32_t     pulse_counts;
    fsp_err_t    err;

    if (angle_deg > 180U)
    {
        angle_deg = 180U;
    }

    pulse_us = SERVO_MIN_PULSE_US +
               (((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle_deg) / 180U);

    err = p_pwm->p_api->infoGet(p_pwm->p_ctrl, &info);
    if (FSP_SUCCESS != err)
    {
        return 0U;
    }

    /*
     * 你的舵机 stack 周期是 20ms。
     * 这里不假设 1 count = 1us，而是根据 FSP 返回的 period_counts 进行比例换算。
     */
    pulse_counts = (uint32_t) (((uint64_t) info.period_counts * pulse_us) / SERVO_PERIOD_US);
    return pulse_counts;
}

/* ======================== 输入模块 ======================== */

static bool input_pin_is_active(bsp_io_port_pin_t pin, bsp_io_level_t active_level)
{
    bsp_io_level_t level = BSP_IO_LEVEL_HIGH;

    (void) g_ioport.p_api->pinRead(g_ioport.p_ctrl, pin, &level);
    return (active_level == level);
}

static bool input_pin_is_stable_active(bsp_io_port_pin_t pin,
                                       bsp_io_level_t active_level,
                                       uint32_t * p_active_start_ms,
                                       uint32_t stable_time_ms)
{
    if (NULL == p_active_start_ms)
    {
        return false;
    }

    if (input_pin_is_active(pin, active_level))
    {
        if (0U == *p_active_start_ms)
        {
            *p_active_start_ms = millis();
        }

        if (time_elapsed(*p_active_start_ms, stable_time_ms))
        {
            return true;
        }
    }
    else
    {
        *p_active_start_ms = 0U;
    }

    return false;
}

static bool start_key_is_stable_pressed(void)
{
    return input_pin_is_stable_active(START_KEY_PIN,
                                      START_KEY_ACTIVE_LEVEL,
                                      &g_start_key_active_start_ms,
                                      START_KEY_STABLE_TIME_MS);
}

static bool estop_key_is_pressed(void)
{
    return input_pin_is_active(ESTOP_KEY_PIN, ESTOP_KEY_ACTIVE_LEVEL);
}

static bool camera_sensor_is_stable_active(void)
{
    return input_pin_is_stable_active(CAMERA_SENSOR_PIN,
                                      CAMERA_SENSOR_ACTIVE_LEVEL,
                                      &g_camera_sensor_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

static bool gate1_sensor_is_stable_active(void)
{
    return input_pin_is_stable_active(GATE1_SENSOR_PIN,
                                      GATE1_SENSOR_ACTIVE_LEVEL,
                                      &g_gate1_sensor_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

static bool gate2_sensor_is_stable_active(void)
{
    return input_pin_is_stable_active(GATE2_SENSOR_PIN,
                                      GATE2_SENSOR_ACTIVE_LEVEL,
                                      &g_gate2_sensor_active_start_ms,
                                      SENSOR_STABLE_TIME_MS);
}

static bool target_gate_sensor_is_stable_active(void)
{
    switch (g_target_gate)
    {
        case TARGET_GATE_1:
            return gate1_sensor_is_stable_active();

        case TARGET_GATE_2:
            return gate2_sensor_is_stable_active();

        case TARGET_GATE_NONE:
        default:
            return false;
    }
}

static fsp_err_t capture_or_classify_start(void)
{
    fsp_err_t err = FSP_SUCCESS;

    /* 触发拍照/分类；级联联调时必须等待推理板返回真实 $CLS 结果。 */
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (g_sort_pending_capture_count >= SORT_PENDING_CAPTURE_MAX)
    {
        g_sort_fault_requested = true;
        return FSP_ERR_OVERFLOW;
    }

    if (!g_control_uart_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    err = control_uart_send_trig();
    if (FSP_SUCCESS == err)
    {
        g_sort_pending_capture_count++;
    }

    return err;
}

static fsp_err_t fruit_clear_action(void)
{
    /*
     * 物体离开光电开关后执行的动作。
     * 当前关闭 LED3，表示本次光电检测阶段结束。
     */
    return g_ioport.p_api->pinWrite(g_ioport.p_ctrl, FRUIT_DETECTED_LED_PIN, BSP_IO_LEVEL_LOW);
}

/* ======================== 状态机模块 ======================== */

/* ======================== Multi-fruit queue module ======================== */

static void sort_multi_reset(void)
{
    sort_queue_clear();
    g_sort_pending_capture_count = 0U;
    g_sort_fault_requested = false;
    g_target_gate = TARGET_GATE_NONE;
    g_pending_fruit_type = FRUIT_TYPE_UNKNOWN;
    g_current_fruit_type = FRUIT_TYPE_UNKNOWN;
    g_control_uart_classification_ready = false;
    g_camera_sensor_active_start_ms = 0U;
    g_gate1_sensor_active_start_ms = 0U;
    g_gate2_sensor_active_start_ms = 0U;
    g_camera_sensor_last_stable_active = false;
    g_gate1_sensor_last_stable_active = false;
    g_gate2_sensor_last_stable_active = false;
    g_gate1_servo_active = false;
    g_gate2_servo_active = false;
    g_gate1_servo_active_start_ms = 0U;
    g_gate2_servo_active_start_ms = 0U;
    g_classification_led_pending_blinks = 0U;
    g_classification_led_remaining_blinks = 0U;
    g_classification_led_active = false;
    g_classification_led_on = false;
    g_classification_led_last_toggle_ms = 0U;
    g_camera_stop_active = false;
    g_camera_stop_start_ms = 0U;
    g_big_motor_feed_active = false;
    g_big_motor_feed_start_ms = 0U;
    (void) fruit_clear_action();
}

static fsp_err_t sort_multi_run_once(void)
{
    fsp_err_t err;
    bool camera_active;
    bool gate1_active;
    bool gate2_active;

    if (g_sort_fault_requested)
    {
        return FSP_ERR_OVERFLOW;
    }

    err = sort_update_feed_pulse();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = sort_update_gate_servo_returns();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = classification_led_update();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = sort_update_camera_stop();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    camera_active = camera_sensor_is_stable_active();
    gate1_active = gate1_sensor_is_stable_active();
    gate2_active = gate2_sensor_is_stable_active();

    if (stable_active_rising_edge(camera_active, &g_camera_sensor_last_stable_active))
    {
        err = sort_handle_camera_trigger();
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    if (stable_active_rising_edge(gate1_active, &g_gate1_sensor_last_stable_active))
    {
        err = sort_handle_gate1_trigger();
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    if (stable_active_rising_edge(gate2_active, &g_gate2_sensor_last_stable_active))
    {
        err = sort_handle_gate2_trigger();
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    return FSP_SUCCESS;
}

static fsp_err_t sort_start_feed_pulse(void)
{
    if (g_big_motor_feed_active)
    {
        return FSP_SUCCESS;
    }

    g_big_motor_feed_active = true;
    g_big_motor_feed_start_ms = millis();
    return motor_run(BIG_MOTOR_ID, BIG_MOTOR_SPEED_PERCENT);
}

static fsp_err_t sort_update_feed_pulse(void)
{
    if (g_big_motor_feed_active &&
        time_elapsed(g_big_motor_feed_start_ms, BIG_MOTOR_FEED_TIME_MS))
    {
        g_big_motor_feed_active = false;
        return motor_stop(BIG_MOTOR_ID);
    }

    return FSP_SUCCESS;
}

static fsp_err_t sort_update_camera_stop(void)
{
    fsp_err_t err;

    if (!g_camera_stop_active)
    {
        return FSP_SUCCESS;
    }

    if (!time_elapsed(g_camera_stop_start_ms, SORT_CAMERA_STOP_TIME_MS))
    {
        return FSP_SUCCESS;
    }

    g_camera_stop_active = false;

    err = motor_run(SMALL_MOTOR_ID, SMALL_MOTOR_SPEED_PERCENT);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

#if SORT_AUTO_FEED_NEXT_ENABLE
    err = sort_start_feed_pulse();
#endif

    return err;
}

static fsp_err_t sort_update_gate_servo_returns(void)
{
    fsp_err_t err;

    if (g_gate1_servo_active &&
        time_elapsed(g_gate1_servo_active_start_ms, SORT_GATE_ACTION_HOLD_MS))
    {
        err = servo_set_angle(GATE1_SERVO_ID, SERVO_HOME_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        g_gate1_servo_active = false;
    }

    if (g_gate2_servo_active &&
        time_elapsed(g_gate2_servo_active_start_ms, SORT_GATE_ACTION_HOLD_MS))
    {
        err = servo_set_angle(GATE2_SERVO_ID, SERVO_HOME_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        g_gate2_servo_active = false;
    }

    return FSP_SUCCESS;
}

static fsp_err_t classification_led_update(void)
{
    uint8_t pending_blinks;
    uint32_t primask;
    fsp_err_t err;

    primask = __get_PRIMASK();
    __disable_irq();
    pending_blinks = g_classification_led_pending_blinks;
    g_classification_led_pending_blinks = 0U;
    if (0U == primask)
    {
        __enable_irq();
    }

    if (pending_blinks > 0U)
    {
        g_classification_led_active = true;
        g_classification_led_on = false;
        g_classification_led_remaining_blinks = pending_blinks;
        g_classification_led_last_toggle_ms = millis() - CLASSIFICATION_LED_OFF_MS;

        err = classification_led_set(false);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }

    if (!g_classification_led_active)
    {
        return FSP_SUCCESS;
    }

    if (g_classification_led_on)
    {
        if (time_elapsed(g_classification_led_last_toggle_ms, CLASSIFICATION_LED_ON_MS))
        {
            g_classification_led_on = false;
            g_classification_led_last_toggle_ms = millis();
            return classification_led_set(false);
        }
    }
    else
    {
        if (0U == g_classification_led_remaining_blinks)
        {
            g_classification_led_active = false;
            return classification_led_set(false);
        }

        if (time_elapsed(g_classification_led_last_toggle_ms, CLASSIFICATION_LED_OFF_MS))
        {
            g_classification_led_remaining_blinks--;
            g_classification_led_on = true;
            g_classification_led_last_toggle_ms = millis();
            return classification_led_set(true);
        }
    }

    return FSP_SUCCESS;
}

static fsp_err_t classification_led_set(bool on)
{
    return g_ioport.p_api->pinWrite(g_ioport.p_ctrl,
                                    CLASSIFICATION_LED_PIN,
                                    on ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static void classification_led_request(fruit_type_t fruit_type)
{
    uint8_t blink_count;

    switch (fruit_type)
    {
        case FRUIT_TYPE_A:
            blink_count = 1U;
            break;

        case FRUIT_TYPE_B:
            blink_count = 2U;
            break;

        case FRUIT_TYPE_C:
        case FRUIT_TYPE_UNKNOWN:
        default:
            blink_count = 3U;
            break;
    }

    g_classification_led_pending_blinks = blink_count;
}

static fsp_err_t sort_handle_camera_trigger(void)
{
    fsp_err_t err = capture_or_classify_start();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = motor_stop(SMALL_MOTOR_ID);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_camera_stop_active = true;
    g_camera_stop_start_ms = millis();

    return FSP_SUCCESS;
}

static fsp_err_t sort_handle_gate1_trigger(void)
{
    fruit_type_t fruit_type = FRUIT_TYPE_UNKNOWN;
    fsp_err_t err;

    if (!sort_queue_peek(&fruit_type))
    {
        return FSP_SUCCESS;
    }

    if (FRUIT_TYPE_A != fruit_type)
    {
        return FSP_SUCCESS;
    }

    (void) sort_queue_pop(&fruit_type);
    g_target_gate = TARGET_GATE_1;

    err = servo_set_angle(GATE1_SERVO_ID, GATE1_SORT_ANGLE_DEG);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_gate1_servo_active = true;
    g_gate1_servo_active_start_ms = millis();
    (void) fruit_clear_action();
    return FSP_SUCCESS;
}

static fsp_err_t sort_handle_gate2_trigger(void)
{
    fruit_type_t fruit_type = FRUIT_TYPE_UNKNOWN;
    fsp_err_t err;

    if (!sort_queue_peek(&fruit_type))
    {
        return FSP_SUCCESS;
    }

    if (FRUIT_TYPE_A == fruit_type)
    {
        g_sort_fault_requested = true;
        return FSP_ERR_OVERFLOW;
    }

    (void) sort_queue_pop(&fruit_type);

    if (FRUIT_TYPE_B == fruit_type)
    {
        g_target_gate = TARGET_GATE_2;
        err = servo_set_angle(GATE2_SERVO_ID, GATE2_SORT_ANGLE_DEG);
        if (FSP_SUCCESS != err)
        {
            return err;
        }

        g_gate2_servo_active = true;
        g_gate2_servo_active_start_ms = millis();
    }
    else
    {
        g_target_gate = TARGET_GATE_NONE;
    }

    (void) fruit_clear_action();
    return FSP_SUCCESS;
}

static bool stable_active_rising_edge(bool stable_active, bool * p_last_stable_active)
{
    bool rising_edge;

    if (NULL == p_last_stable_active)
    {
        return false;
    }

    rising_edge = (stable_active && !(*p_last_stable_active));
    *p_last_stable_active = stable_active;
    return rising_edge;
}

static bool sort_queue_push(fruit_type_t fruit_type)
{
    if (g_sort_fruit_queue_count >= SORT_FRUIT_QUEUE_SIZE)
    {
        return false;
    }

    g_sort_fruit_queue[g_sort_fruit_queue_tail] = fruit_type;
    g_sort_fruit_queue_tail = (uint8_t) ((g_sort_fruit_queue_tail + 1U) % SORT_FRUIT_QUEUE_SIZE);
    g_sort_fruit_queue_count++;
    return true;
}

static bool sort_queue_peek(fruit_type_t * p_fruit_type)
{
    uint32_t primask;
    bool result = false;

    if (NULL == p_fruit_type)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (g_sort_fruit_queue_count > 0U)
    {
        *p_fruit_type = g_sort_fruit_queue[g_sort_fruit_queue_head];
        result = true;
    }

    if (0U == primask)
    {
        __enable_irq();
    }

    return result;
}

static bool sort_queue_pop(fruit_type_t * p_fruit_type)
{
    uint32_t primask;
    bool result = false;

    if (NULL == p_fruit_type)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (g_sort_fruit_queue_count > 0U)
    {
        *p_fruit_type = g_sort_fruit_queue[g_sort_fruit_queue_head];
        g_sort_fruit_queue_head = (uint8_t) ((g_sort_fruit_queue_head + 1U) % SORT_FRUIT_QUEUE_SIZE);
        g_sort_fruit_queue_count--;
        result = true;
    }

    if (0U == primask)
    {
        __enable_irq();
    }

    return result;
}

static void sort_queue_clear(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    g_sort_fruit_queue_head = 0U;
    g_sort_fruit_queue_tail = 0U;
    g_sort_fruit_queue_count = 0U;

    if (0U == primask)
    {
        __enable_irq();
    }
}

static void sort_capture_result_received(fruit_type_t fruit_type)
{
    if (g_sort_pending_capture_count > 0U)
    {
        g_sort_pending_capture_count--;
    }

    g_pending_fruit_type = fruit_type;
    g_current_fruit_type = fruit_type;
    g_control_uart_classification_ready = true;
    classification_led_request(fruit_type);

    if (!sort_queue_push(fruit_type))
    {
        g_sort_fault_requested = true;
    }
}

static void sort_state_machine_run(void)
{
    fsp_err_t err = FSP_SUCCESS;
    bool state_entry = g_state_just_entered;

    g_state_just_entered = false;

    if ((SORT_EMERGENCY_STOP != g_sort_state) && estop_key_is_pressed())
    {
        /* 急停优先：任何状态下立即停机。 */
        (void) all_motors_stop();
        (void) gates_safe();
        sort_multi_reset();
        sort_state_change(SORT_EMERGENCY_STOP);
        return;
    }

    switch (g_sort_state)
    {
        case SORT_IDLE:
        {
            if (state_entry)
            {
                sort_multi_reset();
                err = all_motors_stop();
                if (FSP_SUCCESS == err)
                {
                    err = gates_home();
                }
            }

            if ((FSP_SUCCESS == err) && start_key_is_stable_pressed())
            {
                sort_multi_reset();
                sort_state_change(SORT_FEED_ORANGE);
            }
            break;
        }

        case SORT_FEED_ORANGE:
        {
            if (state_entry)
            {
                err = sort_start_feed_pulse();
                if (FSP_SUCCESS == err)
                {
                    err = motor_run(SMALL_MOTOR_ID, SMALL_MOTOR_SPEED_PERCENT);
                }
            }

            if ((FSP_SUCCESS == err) && time_elapsed(g_state_enter_ms, BIG_MOTOR_FEED_TIME_MS))
            {
                /* 大电机只运行一段时间，避免连续放出多个橙子。 */
                err = motor_stop(BIG_MOTOR_ID);
                if (FSP_SUCCESS == err)
                {
                    g_big_motor_feed_active = false;
                    sort_state_change(SORT_MOVE_TO_GATE);
                }
            }
            break;
        }

        case SORT_WAIT_CAMERA:
        {
            if (camera_sensor_is_stable_active())
            {
                /* 橙子到达拍照位置，先停小电机再触发分类。 */
                err = motor_stop(SMALL_MOTOR_ID);
                if (FSP_SUCCESS == err)
                {
                    err = capture_or_classify_start();
                }
                if (FSP_SUCCESS == err)
                {
                    sort_state_change(SORT_CAPTURE_OR_CLASSIFY);
                }
            }
            break;
        }

        case SORT_CAPTURE_OR_CLASSIFY:
        {
            /*
             * 已经触发拍照/分类。收到 $CLS 就使用真实分类；
             * 若 SORT_USE_TEST_RESULT_ON_TIMEOUT 为 0，超时会进入错误状态，避免掩盖串口未接通的问题。
             */
            if (g_control_uart_classification_ready)
            {
                sort_state_change(SORT_SET_GATE);
            }
            else if (time_elapsed(g_state_enter_ms, CLASSIFICATION_WAIT_TIMEOUT_MS))
            {
#if SORT_USE_TEST_RESULT_ON_TIMEOUT
                g_pending_fruit_type = SORT_TEST_FRUIT_TYPE;
                g_current_fruit_type = SORT_TEST_FRUIT_TYPE;
                g_control_uart_classification_ready = true;
                sort_state_change(SORT_SET_GATE);
#else
                sort_state_change(SORT_ERROR);
#endif
            }
            break;
        }

        case SORT_SET_GATE:
        {
            if (state_entry)
            {
                fruit_type_t fruit_type = get_effective_fruit_type();

                g_target_gate = get_target_gate_by_fruit_type(fruit_type);
                err = set_gates_by_fruit_type(fruit_type);
            }

            if ((FSP_SUCCESS == err) && time_elapsed(g_state_enter_ms, SERVO_SET_WAIT_MS))
            {
                sort_state_change(SORT_MOVE_TO_GATE);
            }
            break;
        }

        case SORT_MOVE_TO_GATE:
        {
            if (state_entry)
            {
                err = motor_run(SMALL_MOTOR_ID, SMALL_MOTOR_SPEED_PERCENT);
            }

            if (FSP_SUCCESS == err)
            {
                err = sort_multi_run_once();
            }
            break;
        }

        case SORT_WAIT_GATE_PASS:
        {
            if (TARGET_GATE_NONE == g_target_gate)
            {
                if (gate2_sensor_is_stable_active())
                {
                    sort_state_change(SORT_GATE_RETURN);
                }
                else if (time_elapsed(g_state_enter_ms, WAIT_GATE_PASS_TIMEOUT_MS))
                {
                    sort_state_change(SORT_ERROR);
                }
            }
            else if (target_gate_sensor_is_stable_active())
            {
                if (TARGET_GATE_1 == g_target_gate)
                {
                    err = servo_set_angle(GATE1_SERVO_ID, GATE1_SORT_ANGLE_DEG);
                }
                else
                {
                    err = servo_set_angle(GATE2_SERVO_ID, GATE2_SORT_ANGLE_DEG);
                }

                if (FSP_SUCCESS == err)
                {
                    sort_state_change(SORT_GATE_RETURN);
                }
                break;

                /* 目标挡板处检测到橙子通过，本轮分拣确认完成。 */
                err = motor_stop(SMALL_MOTOR_ID);
                if (FSP_SUCCESS == err)
                {
                    sort_state_change(SORT_GATE_RETURN);
                }
            }
            else if (time_elapsed(g_state_enter_ms, WAIT_GATE_PASS_TIMEOUT_MS))
            {
                sort_state_change(SORT_ERROR);
            }
            break;
        }

        case SORT_GATE_RETURN:
        {
            if (time_elapsed(g_state_enter_ms, SORT_GATE_ACTION_HOLD_MS))
            {
                err = gates_home();
                if (FSP_SUCCESS == err)
                {
                    sort_state_change(SORT_NEXT);
                }
            }
            break;
        }

        case SORT_NEXT:
        {
            if (time_elapsed(g_state_enter_ms, SORT_NEXT_WAIT_MS))
            {
                clear_one_orange_result();
                sort_state_change(SORT_FEED_ORANGE);
            }
            break;
        }

        case SORT_EMERGENCY_STOP:
        {
            if (state_entry)
            {
                (void) all_motors_stop();
                (void) gates_safe();
                sort_multi_reset();
            }

            /*
             * 急停释放后先回待机，并要求绿色按钮处于松开状态。
             * 这样不会因为一直按着启动按钮而自动重新启动。
             */
            if ((!estop_key_is_pressed()) &&
                (!input_pin_is_active(START_KEY_PIN, START_KEY_ACTIVE_LEVEL)))
            {
                g_start_key_active_start_ms = 0U;
                clear_one_orange_result();
                sort_state_change(SORT_IDLE);
            }
            break;
        }

        case SORT_ERROR:
        default:
        {
            (void) all_motors_stop();
            (void) gates_safe();
            sort_multi_reset();
            break;
        }
    }

    if (FSP_SUCCESS != err)
    {
        sort_state_change(SORT_ERROR);
    }
}

static void sort_state_change(sort_state_t new_state)
{
    g_sort_state = new_state;
    g_state_just_entered = true;
    g_state_enter_ms = millis();
}

static fsp_err_t gates_home(void)
{
    fsp_err_t err;

    err = servo_set_angle(GATE1_SERVO_ID, SERVO_HOME_ANGLE_DEG);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    err = servo_set_angle(GATE2_SERVO_ID, SERVO_HOME_ANGLE_DEG);
    return err;
}

static fsp_err_t gates_safe(void)
{
    return gates_home();
}

static fsp_err_t set_gates_by_fruit_type(fruit_type_t fruit_type)
{
    FSP_PARAMETER_NOT_USED(fruit_type);
    return gates_home();

    fsp_err_t err;

    switch (fruit_type)
    {
        case FRUIT_TYPE_A:
            /* A 类先走挡板1。 */
            err = servo_set_angle(GATE1_SERVO_ID, GATE1_SORT_ANGLE_DEG);
            if (FSP_SUCCESS != err)
            {
                return err;
            }
            return servo_set_angle(GATE2_SERVO_ID, SERVO_HOME_ANGLE_DEG);

        case FRUIT_TYPE_B:
            err = servo_set_angle(GATE1_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            if (FSP_SUCCESS != err)
            {
                return err;
            }
            return servo_set_angle(GATE2_SERVO_ID, GATE2_SORT_ANGLE_DEG);

        case FRUIT_TYPE_C:
        case FRUIT_TYPE_UNKNOWN:
            /* C 类和 UNKNOWN 走不合格直通通道。 */
            err = servo_set_angle(GATE1_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            if (FSP_SUCCESS != err)
            {
                return err;
            }
            return servo_set_angle(GATE2_SERVO_ID, SERVO_HOME_ANGLE_DEG);

        default:
            g_target_gate = TARGET_GATE_NONE;
            err = servo_set_angle(GATE1_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            if (FSP_SUCCESS != err)
            {
                return err;
            }
            return servo_set_angle(GATE2_SERVO_ID, SERVO_HOME_ANGLE_DEG);
    }
}

static fruit_type_t get_effective_fruit_type(void)
{
    if (FRUIT_TYPE_UNKNOWN != g_pending_fruit_type)
    {
        return g_pending_fruit_type;
    }

    return g_current_fruit_type;
}

static target_gate_t get_target_gate_by_fruit_type(fruit_type_t fruit_type)
{
    switch (fruit_type)
    {
        case FRUIT_TYPE_A:
            return TARGET_GATE_1;

        case FRUIT_TYPE_B:
            return TARGET_GATE_2;

        case FRUIT_TYPE_C:
        case FRUIT_TYPE_UNKNOWN:
        default:
            return TARGET_GATE_NONE;
    }
}

static void clear_one_orange_result(void)
{
    sort_multi_reset();
    g_target_gate = TARGET_GATE_NONE;
    g_pending_fruit_type = FRUIT_TYPE_UNKNOWN;
    g_current_fruit_type = FRUIT_TYPE_UNKNOWN;
    g_control_uart_classification_ready = false;
    g_camera_sensor_active_start_ms = 0U;
    g_gate1_sensor_active_start_ms = 0U;
    g_gate2_sensor_active_start_ms = 0U;
    (void) fruit_clear_action();
}

/* ======================== 时间工具 ======================== */

static uint32_t millis(void)
{
    return g_system_ms;
}

static bool time_elapsed(uint32_t start_ms, uint32_t delay_ms)
{
    return ((uint32_t) (millis() - start_ms) >= delay_ms);
}

/* ======================== 与推理板的串口通讯 ======================== */

/*
 * control_uart_init
 *   作用：打开 FSP 里名为 g_uart_host 的 SCI9 UART 实例，与推理板建立物理连接。
 *   返回：FSP_SUCCESS 表示成功，否则返回 FSP 错误码。
 *   说明：该实例需要先在 e² studio FSP 里手动添加（Stack -> Connectivity -> UART，
 *         Name=g_uart_host，Channel=9，Baud=115200，Callback=uart_host_callback）。
 *         第一步只用 TX，所以这里只 open，不订阅 RX 事件。
 */
static fsp_err_t control_uart_init(void)
{
    return g_uart_host.p_api->open(g_uart_host.p_ctrl, g_uart_host.p_cfg);
}

/*
 * control_uart_send_trig
 *   作用：向推理板发送一帧 "$TRIG\r\n"，要求其拍照+识别。
 *   返回：FSP_SUCCESS 表示提交发送成功（不等待对方回复）。
 *   说明：发送非阻塞，FSP 内部用 DMA / 中断完成；这里不阻塞主循环。
 */
static fsp_err_t control_uart_send_trig(void)
{
    static uint8_t const trig_frame[] = "$TRIG\r\n";
    return g_uart_host.p_api->write(g_uart_host.p_ctrl,
                                    trig_frame,
                                    sizeof(trig_frame) - 1U);
}

/*
 * control_uart_send_ping
 *   作用：开机时主动发一帧 "$PING\r\n" 给推理板，做连通性自检。
 *   返回：FSP_SUCCESS 表示提交发送成功（不等待对方回复）。
 *   预期：推理板的调试串口（SCI9, P109/P110）能打印
 *         $DBG,HOSTLINK,RX,PING
 *         以及紧跟的 $PONG 回复。
 *   说明：之所以加这一帧，是因为 LED2 在 FSP 里是 Disabled，
 *         无法用作"UART 是否打开成功"的可视化指示。
 */
static fsp_err_t control_uart_send_ping(void)
{
    static uint8_t const ping_frame[] = "$PING\r\n";
    return g_uart_host.p_api->write(g_uart_host.p_ctrl,
                                    ping_frame,
                                    sizeof(ping_frame) - 1U);
}

/*
 * control_uart_handle_rx_char
 *   作用：在 SCI9 RX 中断里逐字节收推理板回传数据，遇到 \r 或 \n 后解析一整帧。
 *   返回：FSP_SUCCESS 表示本字节已处理；帧过长时返回 FSP_ERR_OVERFLOW。
 */
static fsp_err_t control_uart_handle_rx_char(uint8_t byte)
{
    char ch = (char) byte;

    if (('\r' == ch) || ('\n' == ch))
    {
        if (0U == g_control_uart_frame_len)
        {
            return FSP_SUCCESS;
        }

        g_control_uart_frame[g_control_uart_frame_len] = '\0';
        (void) control_uart_process_frame(g_control_uart_frame);
        g_control_uart_frame_len = 0U;
        return FSP_SUCCESS;
    }

    if (g_control_uart_frame_len >= (CONTROL_UART_FRAME_MAX_LEN - 1U))
    {
        g_control_uart_frame_len = 0U;
        return FSP_ERR_OVERFLOW;
    }

    g_control_uart_frame[g_control_uart_frame_len] = ch;
    g_control_uart_frame_len++;

    return FSP_SUCCESS;
}

/*
 * control_uart_process_frame
 *   作用：处理推理板完整回传帧。当前只把 $CLS 写入分类结果；
 *         $READY/$PONG/$ACK/$ERR 暂时只接收不动作，后续可扩展状态显示。
 */
static bool control_uart_process_frame(const char * p_frame)
{
    fruit_type_t fruit_type = FRUIT_TYPE_UNKNOWN;

    if (NULL == p_frame)
    {
        return false;
    }

    if (control_uart_parse_cls_frame(p_frame, &fruit_type))
    {
        sort_capture_result_received(fruit_type);
        return true;
    }

    if (control_uart_is_prefix(p_frame, "$ERR,"))
    {
        sort_capture_result_received(FRUIT_TYPE_UNKNOWN);
        return true;
    }

    return false;
}

/*
 * control_uart_parse_cls_frame
 *   作用：解析 "$CLS,A,85" / "$CLS,B,70" / "$CLS,C,55"。
 *   说明：分数当前不参与控制，只用分类字母决定舵机角度。
 */
static bool control_uart_parse_cls_frame(const char * p_frame, fruit_type_t * p_fruit_type)
{
    const char * p_type_text;

    if ((NULL == p_frame) || (NULL == p_fruit_type))
    {
        return false;
    }

    if (!control_uart_is_prefix(p_frame, "$CLS,"))
    {
        return false;
    }

    p_type_text = &p_frame[5];

    return control_uart_parse_fruit_type(p_type_text, p_fruit_type);
}

static bool control_uart_parse_fruit_type(const char * p_text, fruit_type_t * p_fruit_type)
{
    if ((NULL == p_text) || (NULL == p_fruit_type))
    {
        return false;
    }

    switch (p_text[0])
    {
        case 'A':
        {
            *p_fruit_type = FRUIT_TYPE_A;
            return true;
        }

        case 'B':
        {
            *p_fruit_type = FRUIT_TYPE_B;
            return true;
        }

        case 'C':
        {
            *p_fruit_type = FRUIT_TYPE_C;
            return true;
        }

        case 'U':
        case 'u':
        {
            *p_fruit_type = FRUIT_TYPE_UNKNOWN;
            return true;
        }

        default:
        {
            *p_fruit_type = FRUIT_TYPE_UNKNOWN;
            return false;
        }
    }
}

static bool control_uart_is_prefix(const char * p_text, const char * p_prefix)
{
    if ((NULL == p_text) || (NULL == p_prefix))
    {
        return false;
    }

    while ('\0' != *p_prefix)
    {
        if (*p_text != *p_prefix)
        {
            return false;
        }

        p_text++;
        p_prefix++;
    }

    return true;
}

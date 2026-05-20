#include "hal_data.h"
#include <stdbool.h>
#include <stdint.h>

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

/* 电机方向脚：根据之前按原理图推荐的 P1 排针相近位置分配 */
#define MOTOR1_IN1_PIN             (BSP_IO_PORT_01_PIN_00)  /* P100, P1-29 */
#define MOTOR1_IN2_PIN             (BSP_IO_PORT_01_PIN_01)  /* P101, P1-30 */
#define MOTOR2_IN1_PIN             (BSP_IO_PORT_01_PIN_02)  /* P102, P1-31 */
#define MOTOR2_IN2_PIN             (BSP_IO_PORT_01_PIN_03)  /* P103, P1-32 */
#define MOTOR3_IN1_PIN             (BSP_IO_PORT_01_PIN_04)  /* P104, P1-33 */
#define MOTOR3_IN2_PIN             (BSP_IO_PORT_01_PIN_05)  /* P105, P1-34 */
#define MOTOR4_IN1_PIN             (BSP_IO_PORT_05_PIN_00)  /* P500, P1-28 */
#define MOTOR4_IN2_PIN             (BSP_IO_PORT_05_PIN_01)  /* P501, P1-27 */

/* 输入脚：P600 是板载 KEY，P601 可作为水果到位传感器 */
#define START_KEY_PIN              (KEY_PIN)                 /* P600, 低电平按下 */
#define FRUIT_SENSOR_PIN           (BSP_IO_PORT_06_PIN_01)   /* P601, 请按实际传感器确认有效电平 */
#define PHOTO_SENSOR_PIN           (FRUIT_SENSOR_PIN)        /* 光电开关输入，当前复用水果到位检测 */

#define START_KEY_ACTIVE_LEVEL     (BSP_IO_LEVEL_LOW)
#define FRUIT_SENSOR_ACTIVE_LEVEL  (BSP_IO_LEVEL_LOW)
#define PHOTO_SENSOR_ACTIVE_LEVEL  (FRUIT_SENSOR_ACTIVE_LEVEL)
#define FRUIT_DETECTED_LED_PIN     (LED3_PIN)

/* ======================== 机械动作参数 ======================== */

#define CONVEYOR_MOTOR_ID          (0U)    /* 当前默认用电机1作为输送电机 */
#define SORT_SERVO_ID              (0U)    /* 当前默认用舵机1作为分拣舵机 */

#define MOTOR_RUN_SPEED_PERCENT    (80U)
#define MOTOR_STOP_SPEED_PERCENT   (0U)

#define SERVO_HOME_ANGLE_DEG       (90U)
#define SERVO_SORT_ANGLE_A_DEG     (45U)
#define SERVO_SORT_ANGLE_B_DEG     (90U)
#define SERVO_SORT_ANGLE_C_DEG     (135U)

/* 常见舵机 20ms 周期下的高电平脉宽范围 */
#define SERVO_PERIOD_US            (20000U)
#define SERVO_MIN_PULSE_US         (500U)
#define SERVO_MAX_PULSE_US         (2500U)

/* 状态机延时 */
#define SENSOR_STABLE_TIME_MS      (30U)
#define MOTOR_STOP_WAIT_MS         (200U)
#define SERVO_SORT_HOLD_MS         (600U)
#define SERVO_RETURN_WAIT_MS       (400U)
#define MAIN_LOOP_DELAY_MS         (1U)

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
    SORT_STATE_IDLE = 0,        /* 初始待机 */
    SORT_STATE_MOTOR_RUN,       /* 启动输送电机 */
    SORT_STATE_WAIT_FRUIT,      /* 等待水果到位 */
    SORT_STATE_FRUIT_DETECTED,  /* 已检测到物体，执行检测反馈 */
    SORT_STATE_SORT_DELAY,      /* 电机已停止，舵机已分拣，等待光电开关恢复未触发 */
    SORT_STATE_SERVO_RETURN,    /* 舵机回到初始角度 */
    SORT_STATE_RESUME_MOTOR,    /* 电机继续运行 */
    SORT_STATE_ERROR            /* 错误兜底 */
} sort_state_t;

typedef enum e_fruit_type
{
    FRUIT_TYPE_UNKNOWN = 0,
    FRUIT_TYPE_A,
    FRUIT_TYPE_B,
    FRUIT_TYPE_C
} fruit_type_t;

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
static sort_state_t     g_sort_state = SORT_STATE_IDLE;
static uint32_t         g_state_enter_ms = 0U;
static uint32_t         g_sensor_active_start_ms = 0U;

/* 后续接 OpenMV 串口时，在串口回调里更新这个变量即可 */
static fruit_type_t     g_current_fruit_type = FRUIT_TYPE_A;

/* ======================== 函数声明 ======================== */

static void     app_error_trap(fsp_err_t err);
static fsp_err_t app_init(void);
static fsp_err_t timer_open(timer_instance_t const * p_timer);
static fsp_err_t timer_start(timer_instance_t const * p_timer);
static fsp_err_t timer_open_start(timer_instance_t const * p_timer);

static fsp_err_t motors_init(void);
static fsp_err_t motor_set_direction(uint8_t motor_id, motor_direction_t direction);
static fsp_err_t motor_set_speed_percent(uint8_t motor_id, uint8_t speed_percent);
static fsp_err_t motor_run(uint8_t motor_id, uint8_t speed_percent);
static fsp_err_t motor_stop(uint8_t motor_id);
static fsp_err_t all_motors_stop(void);

static fsp_err_t servos_init(void);
static fsp_err_t servo_set_angle(uint8_t servo_id, uint32_t angle_deg);
static uint32_t  servo_angle_to_counts(timer_instance_t const * p_pwm, uint32_t angle_deg);

static bool      start_key_is_pressed(void);
static bool      fruit_sensor_is_active(void);
static bool      fruit_sensor_is_stable_active(void);
static bool      photo_sensor_is_clear(void);
static fsp_err_t fruit_detected_action(void);
static fsp_err_t fruit_clear_action(void);

static void      sort_state_machine_run(void);
static void      sort_state_change(sort_state_t new_state);
static uint32_t  get_sort_angle_by_fruit_type(fruit_type_t fruit_type);

static uint32_t  millis(void);
static bool      time_elapsed(uint32_t start_ms, uint32_t delay_ms);

/* ======================== FSP 回调 ======================== */

void tick_timer_callback(timer_callback_args_t * p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);
    g_system_ms++;
}

/* ======================== 用户入口 ======================== */

void hal_entry(void)
{
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

    err = servo_set_angle(SORT_SERVO_ID, SERVO_HOME_ANGLE_DEG);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    sort_state_change(SORT_STATE_IDLE);
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

static fsp_err_t motor_run(uint8_t motor_id, uint8_t speed_percent)
{
    fsp_err_t err;

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

static bool start_key_is_pressed(void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_HIGH;

    (void) g_ioport.p_api->pinRead(g_ioport.p_ctrl, START_KEY_PIN, &level);
    return (START_KEY_ACTIVE_LEVEL == level);
}

static bool fruit_sensor_is_active(void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_HIGH;

    (void) g_ioport.p_api->pinRead(g_ioport.p_ctrl, PHOTO_SENSOR_PIN, &level);
    return (PHOTO_SENSOR_ACTIVE_LEVEL == level);
}

static bool fruit_sensor_is_stable_active(void)
{
    if (fruit_sensor_is_active())
    {
        if (0U == g_sensor_active_start_ms)
        {
            g_sensor_active_start_ms = millis();
        }

        if (time_elapsed(g_sensor_active_start_ms, SENSOR_STABLE_TIME_MS))
        {
            return true;
        }
    }
    else
    {
        g_sensor_active_start_ms = 0U;
    }

    return false;
}

static bool photo_sensor_is_clear(void)
{
    return !fruit_sensor_is_active();
}

static fsp_err_t fruit_detected_action(void)
{
    /*
     * 光电开关检测到物体时立即执行的动作。
     * 现在先用 LED3 点亮作为调试反馈；后续接 OpenMV 时，
     * 可以在这里触发拍照、发送识别请求或记录检测事件。
     */
    return g_ioport.p_api->pinWrite(g_ioport.p_ctrl, FRUIT_DETECTED_LED_PIN, BSP_IO_LEVEL_HIGH);
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

static void sort_state_machine_run(void)
{
    fsp_err_t err = FSP_SUCCESS;

    switch (g_sort_state)
    {
        case SORT_STATE_IDLE:
        {
            err = motor_stop(CONVEYOR_MOTOR_ID);
            if (FSP_SUCCESS == err)
            {
                err = servo_set_angle(SORT_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            }

            if ((FSP_SUCCESS == err) && start_key_is_pressed())
            {
                sort_state_change(SORT_STATE_MOTOR_RUN);
            }
            break;
        }

        case SORT_STATE_MOTOR_RUN:
        {
            err = motor_run(CONVEYOR_MOTOR_ID, MOTOR_RUN_SPEED_PERCENT);
            if (FSP_SUCCESS == err)
            {
                sort_state_change(SORT_STATE_WAIT_FRUIT);
            }
            break;
        }

        case SORT_STATE_WAIT_FRUIT:
        {
            if (fruit_sensor_is_stable_active())
            {
                err = fruit_detected_action();
                if (FSP_SUCCESS == err)
                {
                    sort_state_change(SORT_STATE_FRUIT_DETECTED);
                }
            }
            break;
        }

        case SORT_STATE_FRUIT_DETECTED:
        {
            uint32_t sort_angle = get_sort_angle_by_fruit_type(g_current_fruit_type);

            /*
             * 光电开关已经检测到物体。
             * 在同一个状态里同时下发：
             *   1. LED3 熄灭
             *   2. 电机停止
             *   3. 舵机立刻转到分拣角度
             */
            err = fruit_clear_action();
            if (FSP_SUCCESS == err)
            {
                err = motor_stop(CONVEYOR_MOTOR_ID);
            }
            if (FSP_SUCCESS == err)
            {
                err = servo_set_angle(SORT_SERVO_ID, sort_angle);
            }
            if (FSP_SUCCESS == err)
            {
                sort_state_change(SORT_STATE_SORT_DELAY);
            }
            break;
        }

        case SORT_STATE_SORT_DELAY:
        {
            /*
             * 舵机已经转到分拣角度。
             * 这里等待光电开关检测不到物体，再让舵机回中。
             */
            if (photo_sensor_is_clear())
            {
                g_sensor_active_start_ms = 0U;
                sort_state_change(SORT_STATE_SERVO_RETURN);
            }
            break;
        }

        case SORT_STATE_SERVO_RETURN:
        {
            err = servo_set_angle(SORT_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            if ((FSP_SUCCESS == err) && time_elapsed(g_state_enter_ms, SERVO_RETURN_WAIT_MS))
            {
                sort_state_change(SORT_STATE_RESUME_MOTOR);
            }
            break;
        }

        case SORT_STATE_RESUME_MOTOR:
        {
            g_sensor_active_start_ms = 0U;
            err = motor_run(CONVEYOR_MOTOR_ID, MOTOR_RUN_SPEED_PERCENT);
            if (FSP_SUCCESS == err)
            {
                sort_state_change(SORT_STATE_WAIT_FRUIT);
            }
            break;
        }

        case SORT_STATE_ERROR:
        default:
        {
            (void) all_motors_stop();
            (void) servo_set_angle(SORT_SERVO_ID, SERVO_HOME_ANGLE_DEG);
            break;
        }
    }

    if (FSP_SUCCESS != err)
    {
        sort_state_change(SORT_STATE_ERROR);
    }
}

static void sort_state_change(sort_state_t new_state)
{
    g_sort_state = new_state;
    g_state_enter_ms = millis();
}

static uint32_t get_sort_angle_by_fruit_type(fruit_type_t fruit_type)
{
    switch (fruit_type)
    {
        case FRUIT_TYPE_A:
            return SERVO_SORT_ANGLE_A_DEG;

        case FRUIT_TYPE_B:
            return SERVO_SORT_ANGLE_B_DEG;

        case FRUIT_TYPE_C:
            return SERVO_SORT_ANGLE_C_DEG;

        case FRUIT_TYPE_UNKNOWN:
        default:
            return SERVO_HOME_ANGLE_DEG;
    }
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

/**
 * @file drv8833.c
 * @brief DRV8833 双H桥电机驱动器控制（支持 4 个电机）

 */

#include "drv8833.h"
#include "tim.h"   /* 提供 htim2 / htim3 的 extern 声明和 TIM_CHANNEL_x 宏 */

/* TIM2 / TIM3 在 CubeMX(.ioc) 里设的 Counter Period（自动重装载值 ARR）。
   你的工程：TIM2/TIM3 Period = 3599，对应约 20kHz。速度 0~100% 会按比例映射到 0~PWM_ARR。 */
#define PWM_ARR 3599

/* 最大速度值（百分比） */
#define MAX_SPEED 100

/* 把"逻辑名"映射到"具体定时器 + 两路通道"（IN1/IN2 对应 DRV8833 一个 H 桥） */
typedef struct {
    TIM_HandleTypeDef *htim;   /* 指向哪个定时器（TIM2 还是 TIM3） */
    uint32_t ch_in1;           /* 该电机的 IN1 接在定时器的第几路通道 */
    uint32_t ch_in2;           /* 该电机的 IN2 接在第几路通道 */
} MotorMap;

/* static = 只在 drv8833.c 内部可见；const = 编译后固定不变。
   用 [名字] = 写法精确把每个电机放到对应位置（C99 指定初始化）。 */
static const MotorMap motor_map[MOTOR_COUNT] = {
    /* 左前：TIM2 CH1(PA0)=IN1, CH2(PA1)=IN2 */
    [MOTOR_LF] = {&htim2, TIM_CHANNEL_1, TIM_CHANNEL_2},
    /* 右前：TIM3 CH1(PA6)=IN1, CH2(PA7)=IN2 */
    [MOTOR_RF] = {&htim3, TIM_CHANNEL_1, TIM_CHANNEL_2},
    /* 左后：TIM2 CH3(PA2)=IN1, CH4(PA3)=IN2 */
    [MOTOR_LB] = {&htim2, TIM_CHANNEL_3, TIM_CHANNEL_4},
    /* 右后：TIM3 CH3(PB0)=IN1, CH4(PB1)=IN2 */
    [MOTOR_RB] = {&htim3, TIM_CHANNEL_3, TIM_CHANNEL_4},
};

/* 当前衰减模式（默认 SLOW_DECAY，和你原文件一致） */
static DecayMode currentDecayMode = SLOW_DECAY;

/* 速度百分比(0~100) → CCR 计数值(0~PWM_ARR)。
   例：speed=50 → 50*3599/100 = 1799，占空比约 50%。 */
static inline uint32_t __SpeedToCCR(uint8_t speed)
{
    return (uint32_t)speed * PWM_ARR / 100u;
}

/* 初始化：启动 TIM2/TIM3 全部 8 路 PWM，并让 4 个电机先滑行(停) */
void DRV8833_Init(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

    for (int i = 0; i < MOTOR_COUNT; i++) {
        DRV8833_Coast((MotorID)i);
    }
}

void DRV8833_SetDecayMode(DecayMode mode)
{
    currentDecayMode = mode;
}

/* 电机正转（约定：朝"车前进方向"为正） */
void DRV8833_Forward(MotorID id, uint8_t speed)
{
    if (id < 0 || id >= MOTOR_COUNT) return;   /* 越界保护，防止数组访问出错 */
    if (speed > MAX_SPEED) speed = MAX_SPEED;   /* 限幅 */

    const MotorMap *m = &motor_map[id];         /* 指针 m 指向该电机的映射项 */
    uint32_t pwm = __SpeedToCCR(speed);

    if (currentDecayMode == FAST_DECAY) {
        /* IN1=PWM, IN2=0 */
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, pwm);
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, 0);
    } else {
        /* SLOW_DECAY：IN1 恒高，IN2 输出互补 PWM（PWM_ARR - pwm） */
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, PWM_ARR);
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, PWM_ARR - pwm);
    }
}

/* 电机反转 */
void DRV8833_Backward(MotorID id, uint8_t speed)
{
    if (id < 0 || id >= MOTOR_COUNT) return;
    if (speed > MAX_SPEED) speed = MAX_SPEED;

    const MotorMap *m = &motor_map[id];
    uint32_t pwm = __SpeedToCCR(speed);

    if (currentDecayMode == FAST_DECAY) {
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, 0);
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, pwm);
    } else {
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, PWM_ARR - pwm);
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, PWM_ARR);
    }
}

/* 刹车：IN1=IN2=高（输出端被短路，电流制动，停得快） */
void DRV8833_Brake(MotorID id)
{
    if (id < 0 || id >= MOTOR_COUNT) return;
    const MotorMap *m = &motor_map[id];
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, PWM_ARR);
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, PWM_ARR);
}

/* 滑行/停：IN1=IN2=0（输出端高阻，靠惯性慢慢停，不耗电） */
void DRV8833_Coast(MotorID id)
{
    if (id < 0 || id >= MOTOR_COUNT) return;
    const MotorMap *m = &motor_map[id];
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, 0);
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, 0);
}

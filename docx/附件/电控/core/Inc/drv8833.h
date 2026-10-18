#ifndef __DRV8833_H_
#define __DRV8833_H_

#include <stdint.h>

/**
 * 衰减模式：
 *  FAST_DECAY 用单路 PWM（IN1=PWM, IN2=0），调速线性、停转靠 Coast；
 *  SLOW_DECAY 用双路互补 PWM（IN1 恒高、IN2 输出互补），刹车力度大、低速更稳。
 * 默认 SLOW_DECAY（和你原文件一致）。
 */
typedef enum {
    SLOW_DECAY,
    FAST_DECAY
} DecayMode;

/** 四个电机的编号，代码里用名字代替引脚号，可读性更好 */
typedef enum {
    MOTOR_LF = 0,   /* 左前 Left Front */
    MOTOR_RF,       /* 右前 Right Front */
    MOTOR_LB,       /* 左后 Left Back */
    MOTOR_RB,       /* 右后 Right Back */
    MOTOR_COUNT     /* 电机总数，顺便当数组长度 */
} MotorID;

void DRV8833_Init(void);                       /* 启动 8 路 PWM，并先让车滑行停车 */
void DRV8833_SetDecayMode(DecayMode mode);     /* 切换衰减模式 */
void DRV8833_Forward(MotorID id, uint8_t speed);   /* 电机正转（speed 0~100） */
void DRV8833_Backward(MotorID id, uint8_t speed);  /* 电机反转 */
void DRV8833_Brake(MotorID id);                /* 刹车（IN1=IN2=高，短路制动） */
void DRV8833_Coast(MotorID id);                /* 滑行/停（IN1=IN2=0，高阻） */

#endif /* __DRV8833_H_ */

#ifndef __CAR_H
#define __CAR_H

#include "drv8833.h"   /* 用到 MotorID 和 DRV8833_Forward / DRV8833_Backward */

/* 默认速度（百分比）。先固定 50，等接手柄后再动态改 */
#ifndef CAR_SPEED
#define CAR_SPEED 50
#endif

/* ============ 整车动作 =============
   就是你给的那张运动方向表：正转 = DRV8833_Forward，反转 = DRV8833_Backward。
   四个参数的顺序固定为：左前、右前、左后、右后。 */
void Car_Forward(void);     /* 前进 */
void Car_Backward(void);    /* 后退 */
void Car_SlideRight(void);  /* 向右平移 */
void Car_SlideLeft(void);   /* 向左平移 */
void Car_RotateCW(void);    /* 原地顺时针旋转 */
void Car_RotateCCW(void);   /* 原地逆时针旋转 */
void Car_Stop(void);        /* 停车（滑行） */

/* ============ 麦轮全向解算（模拟量） ============
   vx : -100~100，右平移为正
   vy : -100~100，前进为正
   w  : -100~100，原地顺时针为正
   三个量叠加到四个轮子后做等比限幅，方向感不会失真。
   某轮结果为 0 → 该轮滑行（Coast）。 */
void Car_Mecanum(int8_t vx, int8_t vy, int8_t w);

#endif /* __CAR_H */

/**
  ******************************************************************************
  * @file    ps2.h
  * @brief   PS2 无线手柄驱动（软件 SPI 位操作版本）
  *          适配：STM32F103xB + HAL 库，主频 72MHz
  *
  *  引脚（按你的引脚表）：
  *    DAT  = PA12   输入（CubeMX 里务必设为 Input + Pull-up）
  *    CMD  = PB12   输出
  *    CS   = PB13   输出
  *    CLK  = PB14   输出
  *
  *  用法（在 main.c 里）：
  *    USER CODE BEGIN Includes :  #include "ps2.h"
  *    USER CODE BEGIN 2        :  PS2_Init();
  *    USER CODE BEGIN WHILE    :  if (PS2_ReadData()) { PS2_Data d = PS2_GetData(); ... }
  ******************************************************************************
  */

#ifndef __PS2_H
#define __PS2_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================ 一帧解析后的结果 ============================ */
typedef struct
{
    uint8_t mode;      /* 手柄工作模式：0x41 = 数字模式, 0x73 = 模拟模式 */
    uint8_t btn1;      /* 按键组 1（注意：位=0 表示"按下"！） */
    uint8_t btn2;      /* 按键组 2（注意：位=0 表示"按下"！） */
    uint8_t rjoy_x;    /* 右摇杆 X：0~255，中值约 128 */
    uint8_t rjoy_y;    /* 右摇杆 Y：0~255，中值约 128 */
    uint8_t ljoy_x;    /* 左摇杆 X：0~255，中值约 128 */
    uint8_t ljoy_y;    /* 左摇杆 Y：0~255，中值约 128 */
    uint8_t ok;        /* 本次通信是否成功：1 = 收到 0x5A（通了） */
} PS2_Data;

/* ================================ 对外接口 ================================ */

/* 初始化：配置空闲电平、开 DWT 计时、进入模拟模式、丢弃前几帧 */
void     PS2_Init(void);

/* 读一帧并解析。返回 1 = 成功（收到 0x5A），0 = 失败（数据不可信） */
uint8_t  PS2_ReadData(void);

/* 取最近一次解析结果（PS2_ReadData 成功后才有效） */
PS2_Data PS2_GetData(void);

/* 取最近一帧的原始 9 字节（调试打印用） */
const uint8_t *PS2_GetRaw(void);

/* 尝试把手柄切到【模拟模式】。
 * 数字模式(mode=0x41)下摇杆字节恒为 0xFF、摇杆完全失效，只有按键能用。
 * 内部重发 0x43/0x44/0x43 配置序列，再读一帧确认。
 * 返回 1 = 现在已是模拟模式(0x73)，0 = 还在数字模式。
 * 注意：部分山寨接收器不认这条配置序列，那种情况只能手动按手柄上的
 *       MODE / ANALOG 键切换。 */
uint8_t  PS2_TryEnterAnalog(void);

/* ============================== 按键位定义 ============================== */
/* 用法：PS2_IS_PRESSED(d.btn1, PS2_BTN1_UP)  →  为真表示"上"被按下 */

#define PS2_BTN1_SELECT    0x01u   /* bit0 */
#define PS2_BTN1_L3        0x02u   /* bit1 */
#define PS2_BTN1_R3        0x04u   /* bit2 */
#define PS2_BTN1_START     0x08u   /* bit3 */
#define PS2_BTN1_UP        0x10u   /* bit4 */
#define PS2_BTN1_RIGHT     0x20u   /* bit5 */
#define PS2_BTN1_DOWN      0x40u   /* bit6 */
#define PS2_BTN1_LEFT      0x80u   /* bit7 */

#define PS2_BTN2_L2        0x01u   /* bit0 */
#define PS2_BTN2_R2        0x02u   /* bit1 */
#define PS2_BTN2_L1        0x04u   /* bit2 */
#define PS2_BTN2_R1        0x08u   /* bit3 */
#define PS2_BTN2_TRIANGLE  0x10u   /* bit4   △ */
#define PS2_BTN2_CIRCLE    0x20u   /* bit5   ○ */
#define PS2_BTN2_CROSS     0x40u   /* bit6   × */
#define PS2_BTN2_SQUARE    0x80u   /* bit7   □ */

/* 按下判断：因为"按下 = 0"，所以是"与上掩码等于 0" */
#define PS2_IS_PRESSED(byte, bitmask)   (((byte) & (bitmask)) == 0u)

/* 摇杆是否在中间死区内（±deadzone）。用于给摇杆做归零判断 */
#define PS2_JOY_CENTER(v, deadzone)     (((v) > (uint8_t)(128 - (deadzone))) && \
                                         ((v) < (uint8_t)(128 + (deadzone))))

#ifdef __cplusplus
}
#endif

#endif /* __PS2_H */

/**
  ******************************************************************************
  * @file    ps2.c
  * @brief   PS2 无线手柄驱动（软件 SPI 位操作版本）
  *          适配：STM32F103xB + HAL 库，主频 72MHz
  ******************************************************************************
  *  数据流一句话：
  *    PS2_ReadData() → PS2_Exchange(9字节) → PS2_TransferByte()×9
  *                   → 用宏翻转 CS/CLK/CMD 引脚 + 读 DAT
  *
  *  一帧 = CS 拉低 → 连续收发 9 个字节（72 拍）→ CS 拉高
  *  1 拍 = CLK 一个"低-高" → 发 1 位 + 收 1 位
  ******************************************************************************
  */

#include "ps2.h"
#include "main.h"      /* 引入 HAL 库、GPIO 定义、SystemCoreClock 等 */

/* ============================== 1. 引脚定义 ============================== */
/* 只在这里写一次物理引脚，下面全部用短名字，换引脚只改这一块 */

#define PS2_CS_PORT     GPIOB
#define PS2_CS_PIN      GPIO_PIN_13

#define PS2_CLK_PORT    GPIOB
#define PS2_CLK_PIN     GPIO_PIN_14

#define PS2_CMD_PORT    GPIOB
#define PS2_CMD_PIN     GPIO_PIN_12

#define PS2_DAT_PORT    GPIOA
#define PS2_DAT_PIN     GPIO_PIN_12

/* 引脚操作宏：把长长的 HAL 调用包成短名字，读写更清爽 */
#define CS_H()      HAL_GPIO_WritePin(PS2_CS_PORT,  PS2_CS_PIN,  GPIO_PIN_SET)
#define CS_L()      HAL_GPIO_WritePin(PS2_CS_PORT,  PS2_CS_PIN,  GPIO_PIN_RESET)
#define CLK_H()     HAL_GPIO_WritePin(PS2_CLK_PORT, PS2_CLK_PIN, GPIO_PIN_SET)
#define CLK_L()     HAL_GPIO_WritePin(PS2_CLK_PORT, PS2_CLK_PIN, GPIO_PIN_RESET)
#define CMD_H()     HAL_GPIO_WritePin(PS2_CMD_PORT, PS2_CMD_PIN, GPIO_PIN_SET)
#define CMD_L()     HAL_GPIO_WritePin(PS2_CMD_PORT, PS2_CMD_PIN, GPIO_PIN_RESET)

/* DAT 是输入脚，读它的电平；返回 GPIO_PIN_SET(高) 或 GPIO_PIN_RESET(低) */
#define DAT_READ()  HAL_GPIO_ReadPin(PS2_DAT_PORT, PS2_DAT_PIN)

/* ============================== 2. 时间参数 ============================== */
/* 每个时钟半周期延时（µs）。读数全是 0/FF 或时好时坏时，试着加大到 10~20 */
#define PS2_CLK_DELAY_US    10u
/* 字节与字节之间、CS 前后的小延时（µs） */
#define PS2_FRAME_DELAY_US  5u

/* ============================== 3. 内部变量 ============================== */
static PS2_Data g_ps2;             /* 最近一次解析结果 */
static uint8_t  g_raw[9];          /* 最近一帧原始 9 字节（调试用） */

/* 轮询读取时主机固定发送的命令序列 */
static const uint8_t PS2_CMD_POLL[9] =
    { 0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ============================== 4. 微秒延时 ============================== */
/* 用 Cortex-M3 内核自带的 DWT 周期计数器，72MHz 下 1µs = 72 个计数，最准 */

static void PS2_DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* 允许跟踪 */
    DWT->CYCCNT = 0u;                                 /* 计数器清零 */
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;             /* 启动计数 */
}

static void PS2_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);   /* 72 → 1µs=72 计数 */

    while ((DWT->CYCCNT - start) < ticks)
    {
        /* 空转等待 */
    }
}

/* 如果 DWT 在你的板子上不好用，把上面这个函数整体换成下面这种空循环：
 *
 *   static void PS2_DelayUs(uint32_t us)
 *   {
 *       volatile uint32_t n = us * 8u;   // 8 是经验值，需按实测微调
 *       while (n--) { }
 *   }
 */

/* ========================= 5. 核心：收发 1 个字节 ========================= */
/* 一个字节 = 8 位，循环 8 次，每次：
 *   ① 把 tx 当前最低位放到 CMD 上
 *   ② 打一拍（CLK 拉低 → 延时 → 拉高 → 延时）
 *   ③ 读 DAT，得到 1 位，拼到 rx 的第 i 位
 *   ④ tx 右移一位，换下一位
 * （低位先发 / 低位先收，即 LSB first）
 */
static uint8_t PS2_TransferByte(uint8_t tx)
{
    uint8_t rx = 0u;
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        /* ① 发当前位 */
        if ((tx & 0x01u) != 0u) { CMD_H(); } else { CMD_L(); }

        /* ② 先把这一位采下来，再打时钟
         *   这颗接收器在 CS 拉低时就已经把 bit0 摆到 DAT 上了。
         *   如果等打完一拍再采，读到的永远是"下一位"，
         *   整帧会右移一位（0x5A 变 0xAD），校验永远不通过。
         *   所以采样点必须放在时钟沿之前。 */
        if (DAT_READ() == GPIO_PIN_SET)
        {
            rx |= (uint8_t)(1u << i);
        }

        /* ③ 打一拍，让接收器换到下一位 */
        CLK_L();
        PS2_DelayUs(PS2_CLK_DELAY_US);
        CLK_H();
        PS2_DelayUs(PS2_CLK_DELAY_US);

        /* ④ 换下一位 */
        tx >>= 1;
    }

    return rx;
}

/* ========================== 6. 交换一整帧（9 字节） ========================== */
/* CS 拉低 → 连续收发 9 个字节 → CS 拉高。
 * 注意：CS 低电平期间必须一口气传完，中间不要加 HAL_Delay 之类的长延时。
 */
static void PS2_Exchange(const uint8_t cmd[9], uint8_t recv[9])
{
    uint8_t i;

    CS_L();
    PS2_DelayUs(PS2_FRAME_DELAY_US);

    for (i = 0u; i < 9u; i++)
    {
        recv[i] = PS2_TransferByte(cmd[i]);
        PS2_DelayUs(PS2_FRAME_DELAY_US);
    }

    CS_H();
    PS2_DelayUs(PS2_FRAME_DELAY_US);
}

/* ============================== 7. 初始化 ============================== */
void PS2_Init(void)
{
    /* 7.1 上电先把三根输出线放到空闲态（都是高） */
    CS_H();
    CLK_H();
    CMD_H();

    /* 7.2 启动微秒延时用的计数器 */
    PS2_DWT_Init();

    /* 7.3 等接收器上电稳定 */
    HAL_Delay(50u);

    /* 7.4 让手柄进入"模拟模式"，这样摇杆才有 0~255 的数据。
     *     如果你的手柄本来就在模拟模式（mode 读到 0x73），这几步也可以留着，
     *     它们不会把已经配置好的手柄弄坏。
     */
    {
        static const uint8_t cfg_enter[9]  = {0x01,0x43,0x00,0x01,0x00,0x00,0x00,0x00,0x00};
        static const uint8_t cfg_analog[9] = {0x01,0x44,0x00,0x01,0x00,0x00,0x00,0x00,0x00};
        static const uint8_t cfg_exit[9]   = {0x01,0x43,0x00,0x00,0x5A,0x5A,0x5A,0x5A,0x5A};
        uint8_t tmp[9];

        PS2_Exchange(cfg_enter,  tmp);  HAL_Delay(10u);
        PS2_Exchange(cfg_analog, tmp);  HAL_Delay(10u);
        PS2_Exchange(cfg_exit,   tmp);  HAL_Delay(10u);
    }

    /* 7.5 清空解析结果，并丢弃一帧（第一帧往往不稳） */
    g_ps2.mode = 0u;  g_ps2.btn1 = 0u;  g_ps2.btn2 = 0u;
    g_ps2.rjoy_x = 0u; g_ps2.rjoy_y = 0u;
    g_ps2.ljoy_x = 0u; g_ps2.ljoy_y = 0u;
    g_ps2.ok = 0u;

    {
        uint8_t tmp[9];
        PS2_Exchange(PS2_CMD_POLL, tmp);
    }
}

/* ===================== 7.9 运行时重新进入模拟模式 =====================
 *
 * 为什么需要这个函数？
 *   很多仿 PS2 无线手柄【每次重新上电】都会回到数字模式(mode=0x41)，
 *   这时四个摇杆字节恒为 0xFF，摇杆彻底失效，只有按键还能用。
 *   表现出来的现象就是：按键能控舵机，但推摇杆车一动不动。
 *   PS2_Init() 里虽然发过一次配置序列，但手柄中途掉电/接触不良重连后
 *   又会退回数字模式，所以运行期间也要能重新切一次。
 *
 * 时序：三条 0x43/0x44/0x43，和 PS2_Init 里完全一样。
 * ==================================================================== */
uint8_t PS2_TryEnterAnalog(void)
{
    static const uint8_t cfg_enter[9]  = {0x01,0x43,0x00,0x01,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t cfg_analog[9] = {0x01,0x44,0x00,0x01,0x00,0x00,0x00,0x00,0x00};
    static const uint8_t cfg_exit[9]   = {0x01,0x43,0x00,0x00,0x5A,0x5A,0x5A,0x5A,0x5A};
    uint8_t tmp[9];

    PS2_Exchange(cfg_enter,  tmp);
    PS2_DelayUs(PS2_FRAME_DELAY_US);
    PS2_Exchange(cfg_analog, tmp);
    PS2_DelayUs(PS2_FRAME_DELAY_US);
    PS2_Exchange(cfg_exit,   tmp);
    PS2_DelayUs(PS2_FRAME_DELAY_US);

    /* 发完立刻验一次：真正切成功的话，这一帧的 mode 就是 0x73 */
    if ((PS2_ReadData() != 0u) && (g_ps2.mode == 0x73u))
    {
        return 1u;
    }
    return 0u;
}

/* ============================== 8. 读一帧 + 解析 ============================== */
uint8_t PS2_ReadData(void)
{
    /* 8.1 拿一帧原始数据 */
    PS2_Exchange(PS2_CMD_POLL, g_raw);

    /* 8.2 校验：回传第 3 个字节（下标 2）必须是 0x5A */
    if (g_raw[2] != 0x5A)
    {
        g_ps2.ok = 0u;
        return 0u;                  /* 没握手成功，本帧数据作废 */
    }

    /* 8.3 解析到结构体 */
    g_ps2.mode   = g_raw[1];        /* 0x41 数字 / 0x73 模拟 */
    g_ps2.btn1   = g_raw[3];        /* 按键组 1（按下 = 0） */
    g_ps2.btn2   = g_raw[4];        /* 按键组 2（按下 = 0） */
    g_ps2.rjoy_x = g_raw[5];        /* 右摇杆 X */
    g_ps2.rjoy_y = g_raw[6];        /* 右摇杆 Y */
    g_ps2.ljoy_x = g_raw[7];        /* 左摇杆 X */
    g_ps2.ljoy_y = g_raw[8];        /* 左摇杆 Y */
    g_ps2.ok     = 1u;

    return 1u;
}

/* ============================== 9. 取结果 / 原始数据 ============================== */
PS2_Data PS2_GetData(void)
{
    return g_ps2;
}

const uint8_t *PS2_GetRaw(void)
{
    return g_raw;                   /* 返回数组首地址，外部按 raw[0..8] 用 */
}

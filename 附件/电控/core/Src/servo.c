/**
 * ============================================================================
 *  servo.c —— 四路舵机驱动实现
 * ============================================================================
 *
 *  本文件只干一件事：把"脉宽微秒数"写进定时器的比较寄存器。
 *  角度从哪来、什么时候该转，一概不管 —— 那是 arm.c 的事。
 *
 *  分层：
 *      arm.c    哪个键 → 哪路舵机 → 朝哪转   （按键语义）
 *        ↓
 *      servo.c  钳位 + 记账 + 写 CCR        （本文件）
 *        ↓
 *      tim.c    HAL 硬件层（不用动）
 * ============================================================================
 */
#include "servo.h"
#include "tim.h"        /* 用到 htim1 / htim4 两个句柄，以及 __HAL_TIM_SET_COMPARE */

/* ---------------------------------------------------------------------------
 *  一、每路舵机的配置表 —— 整个模块只有这一处"硬信息"
 *
 *  一行 = 一路舵机。以后加舵机、换引脚、换限位值，都只改这张表，
 *  下面的函数一个字都不用动。
 *
 *  字段含义：
 *    id       逻辑编号（1/2/3/6），对外用它
 *    htim     挂在哪个定时器上
 *    channel  哪个通道
 *    min_us   下限脉宽（往这一头转会停住）
 *    max_us   上限脉宽
 *    pos_us   当前位置（软件记账，不是实测）
 *
 *  为什么要用表而不是 switch？
 *    原来那种 switch(id) 的写法，加一路就要改三个地方（case、
 *    Start、数组长度），漏一个就是诡异的 bug。用表的话，
 *    Start 和钳位都是循环，加一行就完事。
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint8_t            id;         /* 逻辑编号 */
    TIM_HandleTypeDef *htim;       /* 定时器句柄 */
    uint32_t           channel;    /* 通道 */
    uint16_t           min_us;     /* 下限脉宽 us */
    uint16_t           max_us;     /* 上限脉宽 us */
    uint16_t           pos_us;     /* 当前脉宽 us（记账值） */
} Servo_Slot;

static Servo_Slot s_slot[SERVO_COUNT] =
{
    /* ── 逻辑编号          定时器   通道              下限                  上限                  初始位置 ── */
    { SERVO_ID_BIG1,   &htim4, TIM_CHANNEL_1, SERVO_BIG1_MIN_US,   SERVO_BIG1_MAX_US,   SERVO_INIT_US },
    { SERVO_ID_BIG2,   &htim4, TIM_CHANNEL_2, SERVO_BIG2_MIN_US,   SERVO_BIG2_MAX_US,   SERVO_INIT_US },
    { SERVO_ID_BIG3,   &htim4, TIM_CHANNEL_3, SERVO_BIG3_MIN_US,   SERVO_BIG3_MAX_US,   SERVO_INIT_US },
    { SERVO_ID_SMALL6, &htim1, TIM_CHANNEL_4, SERVO_SMALL6_MIN_US, SERVO_SMALL6_MAX_US, SERVO_INIT_US },
};


/* ---------------------------------------------------------------------------
 *  二、内部工具
 * ------------------------------------------------------------------------- */

/**
 * 逻辑编号 → 数组槽位。
 *
 * 这是整个文件里最重要的一道保险：把"编号 6"和"下标 3"这件事
 * 关在这个函数里，外面永远看不到下标。
 *
 * 找不到返回 -1，调用方直接 return。宁可不动，也不越界写内存。
 */
static int8_t find_slot(uint8_t id)
{
    uint8_t i;

    for (i = 0u; i < SERVO_COUNT; i++)
    {
        if (s_slot[i].id == id)
        {
            return (int8_t)i;
        }
    }
    return -1;
}


/**
 * 把某个槽位的 pos_us 真正写进定时器。
 * 只做"写寄存器"这一件事，钳位不在这里做（见 Servo_Step / Servo_SetPulse）。
 */
static void write_slot(const Servo_Slot *s)
{
    __HAL_TIM_SET_COMPARE(s->htim, s->channel, s->pos_us);
}


/* ---------------------------------------------------------------------------
 *  三、初始化
 * ------------------------------------------------------------------------- */
void Servo_Init(void)
{
    uint8_t i;

    /* 3.1 先把四路 CCR 全设成中位。
     *
     * 顺序很重要：先写 CCR，再启动 PWM。
     * 反过来的话，PWM 一启动那一瞬间输出的是 tim.c 里 CubeMX 存的
     * 初值（1500），等下一句 SetPulse 才改成目标值 —— 如果哪天把
     * SERVO_INIT_US 调成 1200，上电就会先甩到 90 度再回 72 度，
     * 机构"啪"一下打在弹丸上。
     *
     * 现在 SERVO_INIT_US 正好也是 1500 所以看不出差别，但别依赖这个巧合。 */
    for (i = 0u; i < SERVO_COUNT; i++)
    {
        s_slot[i].pos_us = SERVO_INIT_US;
        write_slot(&s_slot[i]);
    }

    /* 3.2 启动 PWM。
     *
     * 注意只启动 3 个 TIM4 通道（PB6/PB7/PB8），
     * PB9 那个 CH4 是空着的，不启动 —— 启动了也没人接，纯浪费。
     *
     * 还要注意：TIM4 的 4 个通道共享同一个 ARR，所以四路必然是
     * 同一个 50Hz 频率。这正是"4 路舵机全塞一个定时器"能成立的原因，
     * 也是为什么舵机不能和电机（20kHz）共用一个定时器。 */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);   /* 大舵机1  PB6  */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);   /* 大舵机2  PB7  */
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);   /* 大舵机3  PB8  */

    /* 3.3 小舵机走 TIM1_CH4（PA11）。
     *
     * TIM1 是"高级定时器"，比 TIM2/3/4 多一个总闸门叫 MOE
     * （Main Output Enable）。这个闸门不开，寄存器全配对了，
     * 示波器上还是一条死平线 —— 这是 STM32 最经典的坑之一。
     *
     * 好消息：HAL_TIM_PWM_Start() 内部会帮你把 MOE 打开。
     * 所以用 HAL 就这一句够了。
     *
     * 坏消息：如果哪天你换成标准库或直接写寄存器，
     * 必须自己补一句 TIM_CtrlPWMOutputs(TIM1, ENABLE)，
     * 否则"PA11 配了却不出波形"，你会去查引脚、查复用、查时钟，
     * 查半天发现是差这一句。
     *
     * 症状记忆法：TIM4 那三路好使、就 PA11 死活不动 → 先查 MOE。 */
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);   /* 小舵机6  PA11 */
}


/* ---------------------------------------------------------------------------
 *  四、直接指定脉宽（标定用的主力函数）
 * ------------------------------------------------------------------------- */
void Servo_SetPulse(uint8_t id, uint16_t pulse_us)
{
    int8_t s = find_slot(id);

    if (s < 0)
    {
        return;     /* 编号不存在，宁可不做，也不越界 */
    }

    /* 钳位：超范围就卡在边界，绝不让越界的值真的打到舵机上。
     * 越界脉宽的后果是舵机顶到内部机械限位、持续堵转、发烫。 */
    if (pulse_us < s_slot[s].min_us) { pulse_us = s_slot[s].min_us; }
    if (pulse_us > s_slot[s].max_us) { pulse_us = s_slot[s].max_us; }

    s_slot[s].pos_us = pulse_us;
    write_slot(&s_slot[s]);
}


/* ---------------------------------------------------------------------------
 *  五、查询当前脉宽（记账值）
 * ------------------------------------------------------------------------- */
uint16_t Servo_GetPulse(uint8_t id)
{
    int8_t s = find_slot(id);

    if (s < 0)
    {
        return SERVO_INIT_US;
    }
    return s_slot[s].pos_us;
}


/* ---------------------------------------------------------------------------
 *  六、走一步（按键每按一拍就调一次）
 * ------------------------------------------------------------------------- */
void Servo_Step(uint8_t id, int8_t dir)
{
    int8_t  s = find_slot(id);
    int32_t v;

    if (s < 0)
    {
        return;
    }

    /* ── 中间结果必须用 int32_t 存，不能用 uint16_t ──
     *
     * uint16_t 的范围是 0~65535，没有负数。
     * 假设某个舵机当前是 700us（正好在下限），再往外减 20：
     *   int32_t  → -20，钳位后变成 700，安全；
     *   uint16_t → 65516，那个数写进 CCR 会让舵机猛甩到另一头。
     *
     * 正确写法是：中间过程用有符号数算 → 钳位 → 最后再转回 uint16_t。
     * 这个坑在写"减到边界以下"的逻辑时几乎人人踩一次。 */
    v = (int32_t)s_slot[s].pos_us
      + (dir > 0 ? (int32_t)SERVO_STEP_US : -(int32_t)SERVO_STEP_US);

    /* ── 先钳位，再写寄存器。顺序不能反 ──
     * 反过来的话，越界的那一下已经打到舵机上了，钳位就只是
     * 事后的心理安慰。 */
    if (v < (int32_t)s_slot[s].min_us) { v = (int32_t)s_slot[s].min_us; }
    if (v > (int32_t)s_slot[s].max_us) { v = (int32_t)s_slot[s].max_us; }

    s_slot[s].pos_us = (uint16_t)v;
    write_slot(&s_slot[s]);
}

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : PS2 手柄 → 四麦轮小车 + 四舵机机械臂（分功能版主程序）
  *
  *  这个文件是【完整成品】，可以整个替换你的 Core/Src/main.c。
  *  建议先把你原来的 main.c 备份一份（或者记住 CubeIDE 里 Ctrl+Z 能撤回）。
  *
  *  所有改动都写在 USER CODE BEGIN / END 之间，
  *  以后用 CubeMX 重新生成工程时不会被冲掉。
  *
  *  while(1) 里只有两行：CTRL_Update() 和 HAL_Delay(CTRL_PERIOD_MS)。
  *  原来那段"前进2秒/后退2秒"的演示循环已经删掉了。
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ps2.h"      /* 调试打印用：PS2_ReadData / PS2_GetData */
#include "ctrl.h"     /* 分功能总调度：CTRL_Init / CTRL_Update */
#include "drv8833.h"  /* 电机自检直接调驱动，绕开手柄 */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ============================================================================
 *  MOTOR_SELFTEST —— 电机自检（绕开手柄，直接给占空比）
 *
 *  1 = 开机跑自检序列，不读手柄、不进主循环
 *  0 = 正常模式（手柄遥控）
 *
 *  自检会按下面顺序驱动电机，每一步都会通过串口打印动作名：
 *     ① 四轮同时前进 40%      ② 四轮同时后退 40%
 *     ③ 左前单独转            ④ 右前单独转
 *     ⑤ 左后单独转            ⑥ 右后单独转
 *  用法：把车架起来（轮子悬空）再上电，看哪个轮子不动 / 转的方向不对。
 * ========================================================================== */
#define MOTOR_SELFTEST   0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * printf 重定向。
 * 你原来的工程缺这个函数，导致串口助手什么都收不到。
 * 原理：syscalls.c 里的 _write() 会逐个调用 __io_putchar()，
 *       我们把它接到串口 1 的发送上，printf 就自动从 PA9 出去了。
 * 不需要串口调试的话，这一整段可以删掉。
 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

#if MOTOR_SELFTEST
/* ----------------------------------------------------------------------------
 *  电机自检：完全不碰手柄，直接按固定占空比点四个电机
 *  用来看 DRV8833 供电、STBY、接线、方向是否有问题
 * -------------------------------------------------------------------------- */
static void MotorSelfTest(void)
{
    static const char *name[4] = { "LF", "RF", "LB", "RB" };
    int round, k;

    printf("\r\n*** MOTOR SELFTEST START (请把车架起来) ***\r\n");

    for (round = 0; round < 2; round++)
    {
        printf("-- round %d --\r\n", round + 1);

        printf("A 四轮同时【前进】40%%\r\n");
        for (k = 0; k < 4; k++) { DRV8833_Forward((MotorID)k, 40); }
        HAL_Delay(3000);
        for (k = 0; k < 4; k++) { DRV8833_Coast((MotorID)k); }
        HAL_Delay(800);

        printf("B 四轮同时【后退】40%%\r\n");
        for (k = 0; k < 4; k++) { DRV8833_Backward((MotorID)k, 40); }
        HAL_Delay(3000);
        for (k = 0; k < 4; k++) { DRV8833_Coast((MotorID)k); }
        HAL_Delay(800);

        for (k = 0; k < 4; k++)
        {
            printf("C%d 只转 %s（45%%）\r\n", k + 1, name[k]);
            DRV8833_Forward((MotorID)k, 45);
            HAL_Delay(1500);
            DRV8833_Coast((MotorID)k);
            HAL_Delay(700);
        }

        printf("-- round %d end --\r\n", round + 1);
        HAL_Delay(1500);
    }

    printf("*** SELFTEST 一轮结束，紧接着重来 ***\r\n");
}
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

#if MOTOR_SELFTEST
  DRV8833_Init();                 /* 只初始化电机，不碰手柄和舵机 */
  printf("*** MOTOR SELFTEST MODE ***\r\n");
  while (1)
  {
      MotorSelfTest();            /* 永不返回，自检结束自动重来 */
  }
#else
  CTRL_Init();     /* 一条搞定：电机 + 舵机 + 手柄 全部初始化 */
  printf("*** PS2 麦轮小车 + 四舵机机械臂 就绪 ***\r\n");
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_print = 0;       /* 上次调试打印时刻 */
  while (1)
  {
    CTRL_Update();                 /* 读手柄 → 驱动小车 → 驱动机械臂 */
    HAL_Delay(CTRL_PERIOD_MS);     /* 每 20ms 跑一次（舵机 2°/次 ≈ 100°/s） */

    /* ── 调试打印：每 500ms 一行 ──
       重点看 MODE：
         MODE:73 = 模拟模式，摇杆可用（正常）
         MODE:41 = 数字模式，摇杆恒为 0，底盘已被保护性停车
       推摇杆时 L:/R: 的数值应随之变化，不变就是手柄链路的问题。 */
    if ((HAL_GetTick() - last_print) >= 500)
    {
        last_print = HAL_GetTick();
        if (PS2_ReadData())
        {
            PS2_Data d = PS2_GetData();
            printf("MODE:%02X BTN1:%02X BTN2:%02X L:%3d,%3d R:%3d,%3d\r\n",
                   d.mode, d.btn1, d.btn2,
                   d.ljoy_x, d.ljoy_y, d.rjoy_x, d.rjoy_y);
        }
        else
        {
            printf("PS2: no frame\r\n");
        }
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

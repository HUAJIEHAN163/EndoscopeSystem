/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "i2c.h"
#include "ipcc.h"
#include "openamp.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "openamp.h"
#include "virt_uart.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBOUNCE_MS  200  // 按键消抖时间
#define PWM_MAX      999  // PWM 最大占空比
#define PWM_STEP     100  // 每次按键调节步进

// SHT30
#define SHT30_ADDR         (0x44 << 1)
#define SHT30_CMD_MEASURE  {0x24, 0x00}
#define SHT30_TIMEOUT      100

// RPMsg
#define RPMSG_SERVICE_NAME  "rpmsg-tty"
#define RPMSG_RX_BUF_SIZE   64

// 温度保护
#define TEMP_WARNING   410  // 41.0°C
#define TEMP_CRITICAL  430  // 43.0°C
#define PWM_SAFE_DUTY  200  // 安全模式 20%
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static volatile uint32_t led_duty = 100;
static volatile uint8_t  led_on = 1;
static volatile uint32_t last_key1_tick = 0;
static volatile uint32_t last_key2_tick = 0;

// SHT30
static volatile int16_t  sht30_temp = 0;
static volatile int16_t  sht30_humi = 0;
static volatile uint8_t  sht30_ok = 0;

// VIRT_UART (RPMsg 虚拟串口)
static VIRT_UART_HandleTypeDef huart0;
static volatile uint8_t rpmsg_rx_buf[RPMSG_RX_BUF_SIZE];
static volatile uint16_t rpmsg_rx_len = 0;
static volatile uint8_t  rpmsg_rx_flag = 0;
static volatile uint8_t  rpmsg_ready = 0;

// 按键事件标志（TIM3 中断中置位，主循环中处理）
static volatile uint8_t key1_event = 0;
static volatile uint8_t key2_event = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart);
static void parse_command(const uint8_t *data, uint16_t len);
static void rpmsg_send_str(const char *str);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// --- VIRT_UART 接收回调 ---
static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  uint16_t copy_len = (huart->RxXferSize < RPMSG_RX_BUF_SIZE - 1)
                      ? huart->RxXferSize : RPMSG_RX_BUF_SIZE - 1;
  memcpy((void *)rpmsg_rx_buf, huart->pRxBuffPtr, copy_len);
  rpmsg_rx_buf[copy_len] = '\0';
  rpmsg_rx_len = copy_len;
  rpmsg_rx_flag = 1;
}

// --- RPMsg 发送字符串 ---
static void rpmsg_send_str(const char *str)
{
  if (rpmsg_ready)
    VIRT_UART_Transmit(&huart0, (void *)str, strlen(str));
}

// --- printf 重定向到 RPMsg ---
int __io_putchar(int ch)
{
  static char tx_buf[128];
  static uint8_t tx_idx = 0;

  tx_buf[tx_idx++] = (char)ch;
  if (ch == '\n' || tx_idx >= sizeof(tx_buf) - 1)
  {
    tx_buf[tx_idx] = '\0';
    rpmsg_send_str(tx_buf);
    tx_idx = 0;
  }
  return ch;
}

// --- 指令解析 (A7 → M4) ---
static void parse_command(const uint8_t *data, uint16_t len)
{
  if (len < 2) return;
  char type = data[0];
  int value = 0;

  // 解析数值部分
  for (uint16_t i = 1; i < len; i++)
  {
    if (data[i] >= '0' && data[i] <= '9')
      value = value * 10 + (data[i] - '0');
    else if (data[i] == '\n' || data[i] == '\r')
      break;
  }

  if (type == 'L' || type == 'l')
  {
    // 亮度指令: L0~L100 → 映射到 PWM 0~999
    if (value > 100) value = 100;
    led_duty = (uint32_t)value * PWM_MAX / 100;
    led_on = (value > 0) ? 1 : 0;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_on ? led_duty : 0);
    printf("[M4] LED=%d%% (PWM=%lu)\r\n", value, (unsigned long)led_duty);
  }
}

// --- SHT30 读取 ---
static HAL_StatusTypeDef SHT30_Read(int16_t *temp, int16_t *humi)
{
  uint8_t cmd[2] = SHT30_CMD_MEASURE;
  uint8_t data[6];
  HAL_StatusTypeDef ret;

  ret = HAL_I2C_Master_Transmit(&hi2c1, SHT30_ADDR, cmd, 2, SHT30_TIMEOUT);
  if (ret != HAL_OK) return ret;

  HAL_Delay(20);

  ret = HAL_I2C_Master_Receive(&hi2c1, SHT30_ADDR, data, 6, SHT30_TIMEOUT);
  if (ret != HAL_OK) return ret;

  uint16_t raw_temp = (data[0] << 8) | data[1];
  *temp = (int16_t)(-450 + (1750 * (int32_t)raw_temp) / 65535);

  uint16_t raw_humi = (data[3] << 8) | data[4];
  *humi = (int16_t)((1000 * (int32_t)raw_humi) / 65535);

  return HAL_OK;
}

// --- 温度超限保护 ---
static void check_temperature(int16_t temp)
{
  if (temp > TEMP_CRITICAL)
  {
    led_duty = 0;
    led_on = 0;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0);
    rpmsg_send_str("A2\n");
    printf("[M4] CRITICAL! T=%d.%d, LED OFF\r\n", temp/10, temp%10);
  }
  else if (temp > TEMP_WARNING)
  {
    led_duty = PWM_SAFE_DUTY;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_on ? PWM_SAFE_DUTY : 0);
    rpmsg_send_str("A1\n");
    printf("[M4] WARNING! T=%d.%d, duty→20%%\r\n", temp/10, temp%10);
  }
}

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

  if(IS_ENGINEERING_BOOT_MODE())
  {
    /* Configure the system clock */
    SystemClock_Config();
  }
  else
  {
    /* IPCC initialisation */
    MX_IPCC_Init();
    /* OpenAmp initialisation ---------------------------------*/
    MX_OPENAMP_Init(RPMSG_REMOTE, NULL);
  }

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, led_duty);
  HAL_TIM_Base_Start_IT(&htim3);

  // 创建 VIRT_UART 虚拟串口（仅在 Production Boot 模式下，即 A7 加载 M4 时）
  if (!IS_ENGINEERING_BOOT_MODE())
  {
    if (VIRT_UART_Init(&huart0) == VIRT_UART_OK)
    {
      VIRT_UART_RegisterCallback(&huart0, VIRT_UART_RXCPLT_CB_ID,
                                 VIRT_UART0_RxCpltCallback);
      rpmsg_ready = 1;
    }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 1. 轮询 RPMsg 消息
    OPENAMP_check_for_message();

    // 2. 处理收到的 A7 指令
    if (rpmsg_rx_flag)
    {
      rpmsg_rx_flag = 0;
      parse_command((const uint8_t *)rpmsg_rx_buf, rpmsg_rx_len);
    }

    // 3. 处理按键事件（从 TIM3 中断中传递）
    if (key1_event)
    {
      key1_event = 0;
      rpmsg_send_str("K1\n");
      printf("[M4] KEY1 → freeze\r\n");
    }
    if (key2_event)
    {
      key2_event = 0;
      rpmsg_send_str("K2\n");
      printf("[M4] KEY2 → capture\r\n");
    }

    // 4. 每秒读取 SHT30 + 上报
    static uint32_t last_sht30_tick = 0;
    if (HAL_GetTick() - last_sht30_tick >= 1000)
    {
      last_sht30_tick = HAL_GetTick();
      int16_t t, h;
      if (SHT30_Read(&t, &h) == HAL_OK)
      {
        sht30_temp = t;
        sht30_humi = h;
        sht30_ok = 1;

        // 上报温湿度
        char buf[32];
        snprintf(buf, sizeof(buf), "T%d\n", t);
        rpmsg_send_str(buf);
        snprintf(buf, sizeof(buf), "H%d\n", h);
        rpmsg_send_str(buf);

        // 温度保护
        check_temperature(t);
      }
      else
      {
        sht30_ok = 0;
      }
    }

    HAL_Delay(10);  // 主循环 ~100Hz
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDivValue = RCC_HSI_DIV1;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** RCC Clock Config
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_ACLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3|RCC_CLOCKTYPE_PCLK4
                              |RCC_CLOCKTYPE_PCLK5;
  RCC_ClkInitStruct.AXISSInit.AXI_Clock = RCC_AXISSOURCE_HSI;
  RCC_ClkInitStruct.AXISSInit.AXI_Div = RCC_AXI_DIV1;
  RCC_ClkInitStruct.MCUInit.MCU_Clock = RCC_MCUSSOURCE_HSI;
  RCC_ClkInitStruct.MCUInit.MCU_Div = RCC_MCU_DIV1;
  RCC_ClkInitStruct.APB4_Div = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5_Div = RCC_APB5_DIV1;
  RCC_ClkInitStruct.APB1_Div = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2_Div = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB3_Div = RCC_APB3_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief  TIM3 周期中断回调 (10ms)
  *         用于按键轮询（替代 EXTI 中断，因为 STM32MP1 的 EXTI 被 A7 管理）
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM3) return;

  static uint8_t key1_prev = 0;  // 上一次的 KEY1 状态
  static uint8_t key2_prev = 0;  // 上一次的 KEY2 状态
  uint32_t now = HAL_GetTick();

  // 读取当前电平（高电平有效）
  uint8_t key1_now = (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET) ? 1 : 0;
  uint8_t key2_now = (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_SET) ? 1 : 0;

  // KEY1: 上升沿 + 消抖 → 设置事件标志（主循环中通过 RPMsg 发送）
  if (key1_now && !key1_prev && (now - last_key1_tick > DEBOUNCE_MS))
  {
    last_key1_tick = now;
    key1_event = 1;
  }

  // KEY2: 上升沿 + 消抖 → 设置事件标志
  if (key2_now && !key2_prev && (now - last_key2_tick > DEBOUNCE_MS))
  {
    last_key2_tick = now;
    key2_event = 1;
  }

  key1_prev = key1_now;
  key2_prev = key2_now;
}

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

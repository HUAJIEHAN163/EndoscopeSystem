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
#include "FreeRTOS.h"
#include "task.h"
#include "openamp.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "openamp.h"
#include "virt_uart.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void* osThreadId;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBOUNCE_MS  200
#define PWM_PERIOD   40
#define SHT30_ADDR         (0x44 << 1)
#define SHT30_CMD_MEASURE  {0x24, 0x00}
#define SHT30_TIMEOUT      100
#define RPMSG_RX_BUF_SIZE   64
#define TEMP_WARNING   410
#define TEMP_CRITICAL  430
#define PWM_SAFE_DUTY  20
#define LED_PWM_Pin        GPIO_PIN_10
#define LED_PWM_GPIO_Port  GPIOB
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
IPCC_HandleTypeDef hipcc;

TIM_HandleTypeDef htim3;

osThreadId defaultTaskHandle;
/* USER CODE BEGIN PV */
volatile uint32_t led_duty = 10;
static volatile uint32_t last_key1_tick = 0;
static volatile uint32_t last_key2_tick = 0;
volatile int16_t  sht30_temp = 0;
volatile int16_t  sht30_humi = 0;
volatile uint8_t  sht30_ok = 0;

VIRT_UART_HandleTypeDef huart0;
volatile uint8_t rpmsg_rx_buf[RPMSG_RX_BUF_SIZE];
volatile uint16_t rpmsg_rx_len = 0;
volatile uint8_t  rpmsg_rx_flag = 0;
volatile uint8_t  rpmsg_ready = 0;
volatile uint8_t key1_event = 0;
volatile uint8_t key2_event = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_IPCC_Init(void);
static void MX_TIM3_Init(void);
int MX_OPENAMP_Init(int RPMsgRole, rpmsg_ns_bind_cb ns_bind_cb);
void StartDefaultTask(void const * argument);

/* USER CODE BEGIN PFP */
static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart);
void parse_command(const uint8_t *data, uint16_t len);
void rpmsg_send_str(const char *str);
HAL_StatusTypeDef SHT30_Read(int16_t *temp, int16_t *humi);
void check_temperature(int16_t temp);
extern void MX_FREERTOS_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void VIRT_UART0_RxCpltCallback(VIRT_UART_HandleTypeDef *huart)
{
  uint16_t copy_len = (huart->RxXferSize < RPMSG_RX_BUF_SIZE - 1)
                      ? huart->RxXferSize : RPMSG_RX_BUF_SIZE - 1;
  memcpy((void *)rpmsg_rx_buf, huart->pRxBuffPtr, copy_len);
  rpmsg_rx_buf[copy_len] = '\0';
  rpmsg_rx_len = copy_len;
  rpmsg_rx_flag = 1;
}

void rpmsg_send_str(const char *str)
{
  if (rpmsg_ready)
    VIRT_UART_Transmit(&huart0, (void *)str, strlen(str));
}

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

void parse_command(const uint8_t *data, uint16_t len)
{
  if (len < 2) return;
  char type = data[0];
  int value = 0;
  for (uint16_t i = 1; i < len; i++)
  {
    if (data[i] >= '0' && data[i] <= '9')
      value = value * 10 + (data[i] - '0');
    else if (data[i] == '\n' || data[i] == '\r')
      break;
  }
  if (type == 'L' || type == 'l')
  {
    if (value > 100) value = 100;
    led_duty = value;
    if (value == 0)
      HAL_GPIO_WritePin(LED_PWM_GPIO_Port, LED_PWM_Pin, GPIO_PIN_RESET);
    printf("[M4] LED=%d%%\r\n", value);
  }
}

/* --- 软件 I2C 实现 --- */

static void soft_i2c_delay(void)
{
  for (volatile int i = 0; i < 50; i++);  // ~5us @64MHz
}

static void SDA_HIGH(void) { HAL_GPIO_WritePin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin, GPIO_PIN_SET); }
static void SDA_LOW(void)  { HAL_GPIO_WritePin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin, GPIO_PIN_RESET); }
static void SCL_HIGH(void) { HAL_GPIO_WritePin(SOFT_I2C_SCL_GPIO_Port, SOFT_I2C_SCL_Pin, GPIO_PIN_SET); }
static void SCL_LOW(void)  { HAL_GPIO_WritePin(SOFT_I2C_SCL_GPIO_Port, SOFT_I2C_SCL_Pin, GPIO_PIN_RESET); }
static uint8_t SDA_READ(void) { return HAL_GPIO_ReadPin(SOFT_I2C_SDA_GPIO_Port, SOFT_I2C_SDA_Pin); }

static void soft_i2c_start(void)
{
  SDA_HIGH(); SCL_HIGH(); soft_i2c_delay();
  SDA_LOW(); soft_i2c_delay();
  SCL_LOW(); soft_i2c_delay();
}

static void soft_i2c_stop(void)
{
  SDA_LOW(); soft_i2c_delay();
  SCL_HIGH(); soft_i2c_delay();
  SDA_HIGH(); soft_i2c_delay();
}

static uint8_t soft_i2c_write_byte(uint8_t byte)
{
  for (int i = 7; i >= 0; i--)
  {
    if (byte & (1 << i)) SDA_HIGH(); else SDA_LOW();
    soft_i2c_delay();
    SCL_HIGH(); soft_i2c_delay();
    SCL_LOW(); soft_i2c_delay();
  }
  SDA_HIGH(); soft_i2c_delay();
  SCL_HIGH(); soft_i2c_delay();
  uint8_t ack = !SDA_READ();
  SCL_LOW(); soft_i2c_delay();
  return ack;
}

static uint8_t soft_i2c_read_byte(uint8_t ack)
{
  uint8_t byte = 0;
  SDA_HIGH();
  for (int i = 7; i >= 0; i--)
  {
    SCL_HIGH(); soft_i2c_delay();
    if (SDA_READ()) byte |= (1 << i);
    SCL_LOW(); soft_i2c_delay();
  }
  if (ack) SDA_LOW(); else SDA_HIGH();
  soft_i2c_delay();
  SCL_HIGH(); soft_i2c_delay();
  SCL_LOW(); soft_i2c_delay();
  SDA_HIGH();
  return byte;
}

HAL_StatusTypeDef SHT30_Read(int16_t *temp, int16_t *humi)
{
  uint8_t addr7 = 0x44;

  // 发送测量命令 0x2400
  soft_i2c_start();
  if (!soft_i2c_write_byte(addr7 << 1)) { soft_i2c_stop(); return HAL_ERROR; }
  if (!soft_i2c_write_byte(0x24)) { soft_i2c_stop(); return HAL_ERROR; }
  if (!soft_i2c_write_byte(0x00)) { soft_i2c_stop(); return HAL_ERROR; }
  soft_i2c_stop();

  HAL_Delay(2);  // 等待 SHT30 测量完成（原 20ms，实际 high repeatability 模式只需 15ms）
  vTaskDelay(pdMS_TO_TICKS(18));  // 剩余等待用 vTaskDelay 让出 CPU，避免总线占用

  // 读取 6 字节
  soft_i2c_start();
  if (!soft_i2c_write_byte((addr7 << 1) | 1)) { soft_i2c_stop(); return HAL_ERROR; }
  uint8_t data[6];
  for (int i = 0; i < 5; i++) data[i] = soft_i2c_read_byte(1);
  data[5] = soft_i2c_read_byte(0);
  soft_i2c_stop();

  uint16_t raw_temp = (data[0] << 8) | data[1];
  *temp = (int16_t)(-450 + (1750 * (int32_t)raw_temp) / 65535);
  uint16_t raw_humi = (data[3] << 8) | data[4];
  *humi = (int16_t)((1000 * (int32_t)raw_humi) / 65535);
  return HAL_OK;
}

void check_temperature(int16_t temp)
{
  if (temp > TEMP_CRITICAL)
  {
    led_duty = 0;
    HAL_GPIO_WritePin(LED_PWM_GPIO_Port, LED_PWM_Pin, GPIO_PIN_RESET);
    rpmsg_send_str("A2\n");
  }
  else if (temp > TEMP_WARNING)
  {
    led_duty = PWM_SAFE_DUTY;
    rpmsg_send_str("A1\n");
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
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim3);

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

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* USER CODE BEGIN RTOS_THREADS */
  MX_FREERTOS_Init();
  vTaskStartScheduler();
  /* USER CODE END RTOS_THREADS */

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* FreeRTOS scheduler running, should not reach here */
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

/**
  * @brief IPCC Initialization Function
  * @param None
  * @retval None
  */
static void MX_IPCC_Init(void)
{

  /* USER CODE BEGIN IPCC_Init 0 */

  /* USER CODE END IPCC_Init 0 */

  /* USER CODE BEGIN IPCC_Init 1 */

  /* USER CODE END IPCC_Init 1 */
  hipcc.Instance = IPCC;
  if (HAL_IPCC_Init(&hipcc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IPCC_Init 2 */

  /* USER CODE END IPCC_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 63;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 49;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, SOFT_I2C_SDA_Pin|SOFT_I2C_SCL_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : KEY2_Pin */
  GPIO_InitStruct.Pin = KEY2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PF15 PF14 */
  GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_I2C1;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pin : PB10 (LED_PWM) */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY1_Pin */
  GPIO_InitStruct.Pin = KEY1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(KEY1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SOFT_I2C_SDA_Pin SOFT_I2C_SCL_Pin */
  GPIO_InitStruct.Pin = SOFT_I2C_SDA_Pin|SOFT_I2C_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM3) return;

  /* 软件 PWM 500Hz + 按键轮询 */
  static uint8_t pwm_counter = 0;
  pwm_counter++;
  if (pwm_counter >= PWM_PERIOD) pwm_counter = 0;

  uint8_t threshold = (uint8_t)(led_duty * PWM_PERIOD / 100);
  if (threshold > 0 && pwm_counter < threshold)
    HAL_GPIO_WritePin(LED_PWM_GPIO_Port, LED_PWM_Pin, GPIO_PIN_SET);
  else
    HAL_GPIO_WritePin(LED_PWM_GPIO_Port, LED_PWM_Pin, GPIO_PIN_RESET);

  /* 按键轮询：每 200 次中断 (10ms) 检测一次 */
  static uint16_t key_divider = 0;
  if (++key_divider < 200) return;
  key_divider = 0;

  /* 按键检测 */
  static uint8_t key1_prev = 0;
  static uint8_t key2_prev = 0;
  uint32_t now = HAL_GetTick();
  uint8_t key1_now = (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET) ? 1 : 0;
  uint8_t key2_now = (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_SET) ? 1 : 0;
  if (key1_now && !key1_prev && (now - last_key1_tick > DEBOUNCE_MS))
  {
    last_key1_tick = now;
    key1_event = 1;
  }
  if (key2_now && !key2_prev && (now - last_key2_tick > DEBOUNCE_MS))
  {
    last_key2_tick = now;
    key2_event = 1;
  }
  key1_prev = key1_now;
  key2_prev = key2_now;
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  /* USER CODE END 5 */
}

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

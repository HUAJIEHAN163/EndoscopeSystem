/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern VIRT_UART_HandleTypeDef huart0;
extern volatile uint8_t rpmsg_rx_buf[];
extern volatile uint16_t rpmsg_rx_len;
extern volatile uint8_t  rpmsg_rx_flag;
extern volatile uint8_t  rpmsg_ready;
extern volatile uint8_t  key1_event;
extern volatile uint8_t  key2_event;
extern volatile int16_t  sht30_temp;
extern volatile int16_t  sht30_humi;
extern volatile uint8_t  sht30_ok;
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void Task_Main(void *argument);
static void Task_Temperature(void *argument);

extern void rpmsg_send_str(const char *str);
extern void parse_command(const uint8_t *data, uint16_t len);
extern void check_temperature(int16_t temp);
extern HAL_StatusTypeDef SHT30_Read(int16_t *temp, int16_t *humi);
/* USER CODE END FunctionPrototypes */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void MX_FREERTOS_Init(void)
{
  xTaskCreate(Task_Main, "Main", 512, NULL, 2, NULL);
  xTaskCreate(Task_Temperature, "Temp", 384, NULL, 1, NULL);
}

static void Task_Main(void *argument)
{
  (void)argument;
  for (;;)
  {
    OPENAMP_check_for_message();

    if (rpmsg_rx_flag)
    {
      rpmsg_rx_flag = 0;
      parse_command((const uint8_t *)rpmsg_rx_buf, rpmsg_rx_len);
    }

    if (key1_event)
    {
      key1_event = 0;
      rpmsg_send_str("K1\n");
    }
    if (key2_event)
    {
      key2_event = 0;
      rpmsg_send_str("K2\n");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void Task_Temperature(void *argument)
{
  (void)argument;
  for (;;)
  {
    int16_t t, h;
    if (SHT30_Read(&t, &h) == HAL_OK)
    {
      sht30_temp = t;
      sht30_humi = h;
      sht30_ok = 1;
      char buf[32];
      snprintf(buf, sizeof(buf), "T%d\n", t);
      rpmsg_send_str(buf);
      snprintf(buf, sizeof(buf), "H%d\n", h);
      rpmsg_send_str(buf);
      check_temperature(t);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

/* USER CODE END Application */

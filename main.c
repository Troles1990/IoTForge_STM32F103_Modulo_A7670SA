/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.h"
#include "GFX_FUNCTIONS.h"
#include "fonts.h"
#include "stdio.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "usbd_cdc_if.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define IOTF_APN          "internet.newww.com"
#define IOTF_BROKER       "mqtt.iaintegracion.space"
#define IOTF_PORT         8883
#define IOTF_THING_ID     "tu_id_nodo"
#define IOTF_DEVICE_ID    "tu_id_dispositivo"
#define IOTF_DEVICE_TOKEN "tu_token_del_dispositivo"
#define IOTF_VAR_ID       "tu_id_variable"
#define IOTF_CA_FILE      "isrgrootx1.pem"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
char sim_rx[512];
char tx_buffer[256];
char adc_payload[64];
char usb_tx[256];
char printData[32];
char mqtt_topic[128];
uint16_t readValue = 0;
uint32_t lastStatusMs = 0;
bool mqttReady = false;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN PFP */
static void sendRaw(const char *data);
static void sendAT(const char *cmd, uint32_t delay_ms);
static bool respHas(const char *needle);
static void mqttPublishRaw(const char *topic, const char *payload);
static void publishStatus(const char *status);
static void mqttPublish(uint16_t adcValue);
static void setupA7672G(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void sendRaw(const char *data)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)data, strlen(data), 3000);
}

static bool respHas(const char *needle)
{
    return strstr(sim_rx, needle) != NULL;
}

static void sendAT(const char *cmd, uint32_t delay_ms)
{
    memset(sim_rx, 0, sizeof(sim_rx));
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 3000);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 3000);

    uint16_t idx = 0;
    uint8_t byte;
    uint32_t t = HAL_GetTick();
    while ((HAL_GetTick() - t) < delay_ms && idx < sizeof(sim_rx) - 1)
    {
        if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK)
        {
            sim_rx[idx++] = byte;
            t = HAL_GetTick();
            if (idx >= 4 &&
                (strstr(sim_rx, "OK\r\n") || strstr(sim_rx, "ERROR")))
                break;
        }
    }
    sim_rx[idx] = '\0';

    snprintf(usb_tx, sizeof(usb_tx), "> %s\r\n< %s\r\n", cmd, sim_rx);
    CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));
}

static void mqttPublishRaw(const char *topic, const char *payload)
{
    if (!mqttReady) return;

    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CMQTTTOPIC=0,%d", (int)strlen(topic));
    sendAT(tx_buffer, 300);
    sendRaw(topic);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\x1A", 1, 1000);
    HAL_Delay(500);
    HAL_UART_Receive(&huart1, (uint8_t *)sim_rx, sizeof(sim_rx) - 1, 1500);
    __HAL_UART_FLUSH_DRREGISTER(&huart1);

    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CMQTTPAYLOAD=0,%d", (int)strlen(payload));
    sendAT(tx_buffer, 300);
    sendRaw(payload);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\x1A", 1, 1000);
    HAL_Delay(500);
    HAL_UART_Receive(&huart1, (uint8_t *)sim_rx, sizeof(sim_rx) - 1, 1500);
    __HAL_UART_FLUSH_DRREGISTER(&huart1);

    sendAT("AT+CMQTTPUB=0,1,60", 3000); // ← sube de 1500 a 3000ms

    snprintf(usb_tx, sizeof(usb_tx), "PUB [%s] => %s\r\n", topic, payload);
    CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));
}

static void publishStatus(const char *status)
{
	HAL_Delay(500); // ← espera que el módulo libere el buffer
    snprintf(mqtt_topic, sizeof(mqtt_topic), "iotforge/%s/status", IOTF_DEVICE_ID);
    mqttPublishRaw(mqtt_topic, status);
}

static void mqttPublish(uint16_t adcValue)
{
    snprintf(mqtt_topic, sizeof(mqtt_topic), "iotforge/%s/%s", IOTF_THING_ID, IOTF_VAR_ID);
    snprintf(adc_payload, sizeof(adc_payload), "%u", adcValue);
    mqttPublishRaw(mqtt_topic, adc_payload);
}

static void setupA7672G(void)
{
    mqttReady = false;

    sendAT("AT", 500);
    sendAT("ATE0", 300);
    sendAT("AT+CPIN?", 500);
    sendAT("AT+CSQ", 500);
    sendAT("AT+CREG?", 500);
    sendAT("AT+CGREG?", 500);
    sendAT("AT+CGATT=1", 1500);

    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CGDCONT=1,\"IP\",\"%s\"", IOTF_APN);
    sendAT(tx_buffer, 1000);

    sendAT("AT+CGACT=1,1", 2000);
    sendAT("AT+CMQTTDISC=0,60", 1000);
    sendAT("AT+CMQTTREL=0", 500);
    sendAT("AT+CMQTTSTOP", 1000);
    sendAT("AT+NETCLOSE", 3000);
    sendAT("AT+NETOPEN", 4000);

    sendAT("AT+CSSLCFG=\"sslversion\",0,4", 500);
    sendAT("AT+CSSLCFG=\"authmode\",0,1", 500);
    sendAT("AT+CSSLCFG=\"enableSNI\",0,1", 500);

    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CSSLCFG=\"cacert\",0,\"%s\"", IOTF_CA_FILE);
    sendAT(tx_buffer, 800);

    sendAT("AT+CMQTTSTART", 1500);

    snprintf(tx_buffer, sizeof(tx_buffer), "AT+CMQTTACCQ=0,\"%s-iotf\",1", IOTF_DEVICE_ID);
    sendAT(tx_buffer, 800);

    sendAT("AT+CMQTTSSLCFG=0,0", 500);

    snprintf(tx_buffer, sizeof(tx_buffer),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1,\"%s\",\"%s\"",
             IOTF_BROKER, IOTF_PORT,
             IOTF_DEVICE_ID, IOTF_DEVICE_TOKEN);
    sendAT(tx_buffer, 7000);

    if (respHas("+CMQTTCONNECT: 0,0") || respHas("OK"))
    {
        mqttReady = true;
        publishStatus("ONLINE");
        lastStatusMs = HAL_GetTick();
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

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET); // ← backlight ON
  ST7735_Init();
  ST7735_FillScreen(ST7735_BLACK);
  ST7735_WriteString(0, 0, "A7672 MQTT", Font_11x18, ST7735_RED, ST7735_BLACK);
  ST7735_WriteString(0, 30, "IoTForge", Font_7x10, ST7735_GREEN, ST7735_BLACK);
  ST7735_WriteString(0, 50, "STM32", Font_11x18, ST7735_BLUE, ST7735_BLACK);
  HAL_Delay(1000);

  setupA7672G();
  /* USER CODE END 2 */

  while (1)
    {
      HAL_ADC_Start(&hadc1);
      HAL_ADC_PollForConversion(&hadc1, 1000);
      readValue = HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);

      snprintf(usb_tx, sizeof(usb_tx), "ADC: %u\r\n", readValue);
      CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));

      snprintf(printData, sizeof(printData), "%u", readValue);
      ST7735_FillScreen(ST7735_BLACK);
      ST7735_WriteString(0, 0, "ADC Value", Font_11x18, ST7735_RED, ST7735_BLACK);
      ST7735_WriteString(0, 30, printData, Font_11x18, ST7735_GREEN, ST7735_BLACK);

      if (mqttReady)
      {
        mqttPublish(readValue);
        ST7735_WriteString(0, 60, "MQTT OK", Font_7x10, ST7735_GREEN, ST7735_BLACK);

        if ((HAL_GetTick() - lastStatusMs) >= 30000)
        {
          publishStatus("ONLINE");
          lastStatusMs = HAL_GetTick();
        }
      }
      else
      {
          ST7735_WriteString(0, 60, "Reconectando", Font_7x10, ST7735_RED, ST7735_BLACK);
          setupA7672G();
      }

      HAL_Delay(3000);
    }

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

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

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, CS_Pin|DC_Pin|LED_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RES_GPIO_Port, RES_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = CS_Pin|DC_Pin|LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RES_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RES_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
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
}
#endif /* USE_FULL_ASSERT */

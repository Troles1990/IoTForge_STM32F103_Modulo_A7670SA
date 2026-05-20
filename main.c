/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32 + A7670SA -> IoTForge MQTT TLS
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

// ============================================================
// BLOQUE IOTFORGE - LIBRERIAS REQUERIDAS - NO MOVER
// ============================================================

#include "st7735.h"
#include "GFX_FUNCTIONS.h"
#include "fonts.h"
#include "usbd_cdc_if.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// ============================================================
// BLOQUE IOTFORGE - CREDENCIALES Y BROKER
// El usuario solo debe reemplazar los valores entre comillas.
// ============================================================

#define IOTF_APN          "internet.newww.com"
#define IOTF_BROKER       "mqtt.iaintegracion.space"
#define IOTF_PORT         8883
#define IOTF_THING_ID     "tu_id_nodo"
#define IOTF_DEVICE_ID    "tu_id_dispositivo"
#define IOTF_DEVICE_TOKEN "tu_token_del_dispositivo"
#define IOTF_VAR_ID       "tu_id_variable"
#define IOTF_CA_FILE      "isrgrootx1.pem"

// ============================================================
// BLOQUE IOTFORGE - INTERVALOS BASE
// HEARTBEAT_MS mantiene el dispositivo ONLINE en IoTForge.
// PUBLISH_MS controla cada cuanto se publica la variable de ejemplo.
// ============================================================

#define HEARTBEAT_MS 30000UL
#define PUBLISH_MS   3000UL

// ============================================================
// ZONA DEL USUARIO - CONSTANTES PROPIAS
// Agrega aqui defines de sensores, actuadores, limites o modos.
// Ejemplos:
//   #define ADC_MAX_VALUE 4095
//   #define RELAY_ON      GPIO_PIN_SET
// ============================================================

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

// ============================================================
// BLOQUE IOTFORGE - BUFFERS Y ESTADO MQTT - NO MOVER
// ============================================================

char sim_rx[512];
char tx_buffer[256];
char payload_buffer[64];
char usb_tx[256];
char mqtt_topic[128];

uint32_t lastStatusMs = 0;
uint32_t lastPublishMs = 0;
bool mqttReady = false;

// ============================================================
// ZONA DEL USUARIO - VARIABLES DE APLICACION
// Agrega aqui lecturas, estados, banderas o valores de proceso.
// ============================================================

uint16_t readValue = 0;
char printData[32];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN PFP */

// ============================================================
// BLOQUE IOTFORGE - PROTOTIPOS INTERNOS - NO MOVER
// ============================================================

static void sendRaw(const char *data);
static void sendAT(const char *cmd, uint32_t timeout_ms);
static bool respHas(const char *needle);
static void mqttPublishRaw(const char *topic, const char *payload);
static void publishStatus(const char *status);
static void mqttPublishValue(uint16_t value);
static void setupA7670SA(void);

// ============================================================
// ZONA DEL USUARIO - PROTOTIPOS PROPIOS
// Agrega aqui tus funciones de sensores, pantalla o actuadores.
// ============================================================

static void userHardwareInit(void);
static void userReadInputs(void);
static void userUpdateDisplay(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// ============================================================
// BLOQUE IOTFORGE - ENVIO RAW AL MODULO A7670SA - NO MOVER
// ============================================================

static void sendRaw(const char *data)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)data, strlen(data), 3000);
}

// ============================================================
// BLOQUE IOTFORGE - BUSCAR TEXTO EN RESPUESTA AT - NO MOVER
// ============================================================

static bool respHas(const char *needle)
{
  return strstr(sim_rx, needle) != NULL;
}

// ============================================================
// BLOQUE IOTFORGE - COMANDOS AT CON LOG USB - NO MOVER
// ============================================================

static void sendAT(const char *cmd, uint32_t timeout_ms)
{
  memset(sim_rx, 0, sizeof(sim_rx));

  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 3000);
  HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 3000);

  uint16_t idx = 0;
  uint8_t byte = 0;
  uint32_t t = HAL_GetTick();

  while ((HAL_GetTick() - t) < timeout_ms && idx < sizeof(sim_rx) - 1)
  {
    if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK)
    {
      sim_rx[idx++] = (char)byte;
      sim_rx[idx] = '\0';
      t = HAL_GetTick();

      if (strstr(sim_rx, "OK\r\n") || strstr(sim_rx, "ERROR"))
      {
        break;
      }
    }
  }

  sim_rx[idx] = '\0';

  snprintf(usb_tx, sizeof(usb_tx), "> %s\r\n< %s\r\n", cmd, sim_rx);
  CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));
}

// ============================================================
// BLOQUE IOTFORGE - PUBLICACION MQTT POR AT - NO MOVER
// ============================================================

static void mqttPublishRaw(const char *topic, const char *payload)
{
  if (!mqttReady) return;

  snprintf(tx_buffer, sizeof(tx_buffer), "AT+CMQTTTOPIC=0,%d", (int)strlen(topic));
  sendAT(tx_buffer, 300);
  sendRaw(topic);
  HAL_UART_Transmit(&huart1, (uint8_t *)"\x1A", 1, 1000);
  HAL_Delay(500);

  memset(sim_rx, 0, sizeof(sim_rx));
  HAL_UART_Receive(&huart1, (uint8_t *)sim_rx, sizeof(sim_rx) - 1, 1500);
  __HAL_UART_FLUSH_DRREGISTER(&huart1);

  snprintf(tx_buffer, sizeof(tx_buffer), "AT+CMQTTPAYLOAD=0,%d", (int)strlen(payload));
  sendAT(tx_buffer, 300);
  sendRaw(payload);
  HAL_UART_Transmit(&huart1, (uint8_t *)"\x1A", 1, 1000);
  HAL_Delay(500);

  memset(sim_rx, 0, sizeof(sim_rx));
  HAL_UART_Receive(&huart1, (uint8_t *)sim_rx, sizeof(sim_rx) - 1, 1500);
  __HAL_UART_FLUSH_DRREGISTER(&huart1);

  sendAT("AT+CMQTTPUB=0,1,60", 3000);

  snprintf(usb_tx, sizeof(usb_tx), "PUB [%s] => %s\r\n", topic, payload);
  CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));
}

// ============================================================
// BLOQUE IOTFORGE - HEARTBEAT - NO MOVER
// Publica ONLINE en iotforge/{DEVICE_ID}/status.
// ============================================================

static void publishStatus(const char *status)
{
  HAL_Delay(500);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "iotforge/%s/status", IOTF_DEVICE_ID);
  mqttPublishRaw(mqtt_topic, status);
}

// ============================================================
// ZONA DEL USUARIO - PUBLICAR DATOS
// Reemplaza value/payload por tu dato real si no usas ADC.
// Mantener mqttPublishRaw() para enviar a IoTForge.
// ============================================================

static void mqttPublishValue(uint16_t value)
{
  snprintf(mqtt_topic, sizeof(mqtt_topic), "iotforge/%s/%s", IOTF_THING_ID, IOTF_VAR_ID);
  snprintf(payload_buffer, sizeof(payload_buffer), "%u", value);
  mqttPublishRaw(mqtt_topic, payload_buffer);
}

// ============================================================
// BLOQUE IOTFORGE - CONFIGURACION A7670SA + MQTT TLS - NO MOVER
// ============================================================

static void setupA7670SA(void)
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

  if (respHas("+CMQTTCONNECT: 0,0"))
  {
    mqttReady = true;
    publishStatus("ONLINE");
    lastStatusMs = HAL_GetTick();
    lastPublishMs = HAL_GetTick();
  }
}

// ============================================================
// ZONA DEL USUARIO - CONFIGURACION DE HARDWARE PROPIO
// Agrega aqui pantalla, sensores, salidas o estado inicial.
// ============================================================

static void userHardwareInit(void)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  ST7735_Init();
  ST7735_FillScreen(ST7735_BLACK);
  ST7735_WriteString(0, 0, "A7670SA MQTT", Font_11x18, ST7735_RED, ST7735_BLACK);
  ST7735_WriteString(0, 30, "IoTForge", Font_7x10, ST7735_GREEN, ST7735_BLACK);
  ST7735_WriteString(0, 50, "STM32", Font_11x18, ST7735_BLUE, ST7735_BLACK);
  HAL_Delay(1000);
}

// ============================================================
// ZONA DEL USUARIO - LECTURA DE SENSORES
// Reemplaza esta funcion si tu proyecto no usa ADC.
// ============================================================

static void userReadInputs(void)
{
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 1000);
  readValue = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  snprintf(usb_tx, sizeof(usb_tx), "ADC: %u\r\n", readValue);
  CDC_Transmit_FS((uint8_t *)usb_tx, strlen(usb_tx));
}

// ============================================================
// ZONA DEL USUARIO - PANTALLA O INDICADORES
// Modifica esta funcion segun tu display o elimina su contenido.
// ============================================================

static void userUpdateDisplay(void)
{
  snprintf(printData, sizeof(printData), "%u", readValue);

  ST7735_FillScreen(ST7735_BLACK);
  ST7735_WriteString(0, 0, "ADC Value", Font_11x18, ST7735_RED, ST7735_BLACK);
  ST7735_WriteString(0, 30, printData, Font_11x18, ST7735_GREEN, ST7735_BLACK);

  if (mqttReady)
  {
    ST7735_WriteString(0, 60, "MQTT OK", Font_7x10, ST7735_GREEN, ST7735_BLACK);
  }
  else
  {
    ST7735_WriteString(0, 60, "Reconectando", Font_7x10, ST7735_RED, ST7735_BLACK);
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

  // ============================================================
  // BLOQUE STM32CUBEIDE - INICIALIZACION HAL - NO MOVER
  // ============================================================

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 2 */

  // ============================================================
  // ZONA DEL USUARIO - INICIO DE HARDWARE PROPIO
  // ============================================================

  userHardwareInit();

  // ============================================================
  // BLOQUE IOTFORGE - INICIO CELULAR + MQTT TLS - NO MOVER
  // ============================================================

  setupA7670SA();

  /* USER CODE END 2 */

  while (1)
  {
    // ==========================================================
    // ZONA DEL USUARIO - LOGICA PRINCIPAL
    // Lee sensores, calcula estados y actualiza pantalla.
    // ==========================================================

    userReadInputs();
    userUpdateDisplay();

    // ==========================================================
    // BLOQUE IOTFORGE - RECONEXION + HEARTBEAT + PUBLICACION
    // Mantener este bloque para conservar conexion con IoTForge.
    // ==========================================================

    if (!mqttReady)
    {
      setupA7670SA();
    }
    else
    {
      uint32_t now = HAL_GetTick();

      if ((now - lastStatusMs) >= HEARTBEAT_MS)
      {
        publishStatus("ONLINE");
        lastStatusMs = now;
      }

      if ((now - lastPublishMs) >= PUBLISH_MS)
      {
        mqttPublishValue(readValue);
        lastPublishMs = now;
      }
    }

    HAL_Delay(100);
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
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */

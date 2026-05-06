# IoTForge_STM32F103
Proyecto de comunicacion hacia iotforge desde un stm32f103 utilizando un modulo  SIMCOM A7670SA Serie LTE Cat 1 Módulo TTL, con un sim LTE 28 band (Probado en mexico newww).

!.- Descarga el proyecto del autor original https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058/blob/main/README.md
2.- Sustituya el main.c por el que esta aqui en el repositorio y agrega tus ID de variables, ID dispostivo, ID token etc de IoTForge.
3.- Sustituye ST7735.h por el de este repositorio si trabajas con una LCD TFT 1.8 TFT SPI 128*160 v1.1
4.- Haz build en STM32CubeIDE y carga a tu stm32f103.



Links de driver del modulo https://github.com/TDLOGY/SIMCOM_USB_DRIVER/tree/main
Link programa sscom32E   https://drive.google.com/file/d/0B4GOwiN2Qm96R2V0dVFlSXltVWs/view?resourcekey=0-SR9QQdTR1vm3Zg7-tPDJIg
Link comandos modulo https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/module/sim7680/A76XX%20Series%20MQTT_EX_AT%20Command%20Manual_V1.00.pdf
Link manual modulo https://manuals.plus/ae/1005006666698901#google_vignette o en aliexpress (SIMCOM A7670SA Serie LTE Cat 1 Módulo TTL)

Conexion Fuente
Fuente 5V 2A+
    │
    ├──► Pin V (CN101) del A7670SA
    └──► GND común con STM32

STM32 PA9  (TX) ──► Pin R (CN101) RXD módulo
STM32 PA10 (RX) ──► Pin T (CN101) TXD módulo
GND común

PC ──USB-C──► Blue Pill (CDC Virtual COM


LCD TFT Connexion
LCD	STM32
VCC	3.3V
GND	GND
SCL	PA5
SDA	PA7
CS	PA1
DC/A0	PA2
LED/BL	PA3
RES	PB0


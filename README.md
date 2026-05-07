# STM32 + A7670SA → IoTForge
### Guía de integración MQTT celular con pantalla TFT ST7735

---

## Requisitos

**Hardware**
- STM32F103C8T6 (Blue Pill)
- Módulo celular A7670SA con breakout board (CN101 6 pines)
- Pantalla TFT ST7735 1.8" (128x160)
- Fuente de alimentación 5V / 2A para el módulo
- SIM con datos activados

**Software**
- STM32CubeIDE
- STM32CubeProgrammer
- Repositorio base: [SW-MCU-STM32-MQTT-058](https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058)

---

## Conexiones

### LCD ST7735 → STM32

| LCD     | STM32 |
|---------|-------|
| VCC     | 3.3V  |
| GND     | GND   |
| SCL     | PA5   |
| SDA     | PA7   |
| CS      | PA1   |
| DC/A0   | PA2   |
| LED/BL  | PA3   |
| RES     | PB0   |

### A7670SA (CN101) → STM32

| CN101 Pin | Señal  | STM32 |
|-----------|--------|-------|
| G (1,6)   | GND    | GND   |
| V (2)     | VCC    | **Fuente externa 5V 2A** |
| T (4)     | TXD    | PA10 (RX) |
| R (5)     | RXD    | PA9 (TX)  |

> ⚠️ **GND común obligatorio** entre STM32, módulo A7670SA y fuente externa.  
> ⚠️ El pin SLEEP (S) y PWRKEY (K) van a GND del CN101.  
> ⚠️ TXD/RXD del módulo son 3.3V TTL — conexión directa al STM32 sin convertidor.

---

## Paso 1 — Crear dispositivo en IoTForge

1. Ingresa a [iotforge.iaintegracion.space](https://iotforge.iaintegracion.space)
2. Ve a **Nodo** y crea tu nodo
3. Ve a **Variables** crea nueva variable 
4. Ve a **Dispositivos** crea nuevo dispositivo stm32
5. Anota:
   - `DEVICE_ID`
   - `DEVICE_TOKEN`
   - 'VARIABLE ID'
   - 'THING ID (NODO)'
 
____

## Paso 2 — Descargar certificado TLS

IoTForge usa TLS con certificado ISRG Root X1 (Let's Encrypt).

1. Descarga el certificado:  
   [https://letsencrypt.org/certs/isrgrootx1.pem](https://letsencrypt.org/certs/isrgrootx1.pem)
2. Guarda el archivo como `isrgrootx1.pem`

---

## Paso 3 — Cargar certificado al módulo A7670SA

Conecta el módulo por USB a tu PC e instala el driver **SIMCom USB Drivers A7670**.  
Abre el puerto **SimTech HS-USB AT Port 9001** a 115200 baudios.

Ejecuta los siguientes comandos AT:

```
AT+CPIN?
```
Debe responder `+CPIN: READY` — SIM reconocida.

```
AT+FSCREATE="isrgrootx1.pem"
```
Crea el archivo en el sistema de archivos del módulo.

```
AT+FSWRITE="isrgrootx1.pem",0,<tamaño_en_bytes>,5000
```
Pega el contenido del certificado cuando el módulo lo solicite, termina con `Ctrl+Z`.

```
AT+FSLS
```
Verifica que `isrgrootx1.pem` aparezca en la lista.

---

## Paso 4 — Configurar el proyecto STM32

### 4.1 Descargar el repositorio base

Descarga el proyecto desde:  
[https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058](https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058)

Ábrelo en **STM32CubeIDE**.

### 4.2 Reemplazar `main.c`

Reemplaza el archivo `Core/Src/main.c` con el `main.c` de este ejemplo.

Actualiza tus credenciales IoTForge en los `#define` al inicio del archivo:

```c
#define IOTF_APN          "tu.apn.operador"
#define IOTF_BROKER       "mqtt.iaintegracion.space"
#define IOTF_PORT         8883
#define IOTF_THING_ID     "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_DEVICE_ID    "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_DEVICE_TOKEN "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define IOTF_VAR_ID       "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_CA_FILE      "isrgrootx1.pem"
```

### 4.3 Reemplazar `st7735.h`

Reemplaza el archivo `Core/Inc/st7735.h` con el de este ejemplo.

El cambio clave es activar el bloque correcto para pantalla **1.8" 128x160**:

```c
#define ST7735_IS_160X128 1
#define ST7735_WIDTH  128
#define ST7735_HEIGHT 160
#define ST7735_XSTART 0
#define ST7735_YSTART 0
#define ST7735_ROTATION (ST7735_MADCTL_MX | ST7735_MADCTL_MY)
```

> Si la imagen aparece desplazada prueba con `XSTART 2` y `YSTART 1` (variante WaveShare).

---

## Paso 5 — Compilar y cargar

1. Compila el proyecto en STM32CubeIDE (`Ctrl+B`)
2. Conecta el ST-Link al STM32
3. Carga el firmware (`Run → Run`)
4. Conecta el USB-C de la Blue Pill para ver los logs por puerto COM virtual

---

## Paso 6 — Verificar funcionamiento

Abre el puerto COM virtual (115200 baudios) y verifica el log de inicio:

```
> AT+CREG?
< +CREG: 0,1
OK
> AT+NETOPEN
< OK
> AT+CMQTTSTART
< OK
> AT+CMQTTCONNECT=0,"tcp://mqtt.iaintegracion.space:8883",...
< +CMQTTCONNECT: 0,0
OK
```

`+CMQTTCONNECT: 0,0` = conexión exitosa ✅

En la pantalla LCD verás:
- **ADC Value** — valor del sensor en tiempo real
- **MQTT OK** — conexión activa

En el dashboard de IoTForge el dispositivo aparecerá como **ONLINE** y los datos llegarán cada 3 segundos.

---

## Flujo de datos

```
Sensor ADC (PB1)
      │
      ▼
   STM32F103
      │ UART 115200
      ▼
  A7670SA LTE
      │ TLS 8883
      ▼
mqtt.iaintegracion.space
      │
      ▼
  IoTForge Dashboard
```

---

## Notas importantes

- El módulo A7670SA requiere **mínimo 2A de pico** — usar fuente dedicada, no alimentar desde USB-UART
- El pin **PWRKEY (K)** del CN101 debe conectarse a GND para arranque automático
- El certificado `isrgrootx1.pem` debe cargarse **una sola vez** al módulo — persiste aunque se apague
- Los datos se publican en el topic: `iotforge/{THING_ID}/{VAR_ID}`
- El heartbeat se publica en: `iotforge/{DEVICE_ID}/status` cada 30 segundos

---

*Guía generada para IoTForge — Ejemplo STM32 A7670SA*



Links de driver del modulo https://github.com/TDLOGY/SIMCOM_USB_DRIVER/tree/main
Link programa sscom32E   https://drive.google.com/file/d/0B4GOwiN2Qm96R2V0dVFlSXltVWs/view?resourcekey=0-SR9QQdTR1vm3Zg7-tPDJIg
Link comandos modulo https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/module/sim7680/A76XX%20Series%20MQTT_EX_AT%20Command%20Manual_V1.00.pdf
Link manual modulo https://manuals.plus/ae/1005006666698901#google_vignette o en aliexpress (SIMCOM A7670SA Serie LTE Cat 1 Módulo TTL)



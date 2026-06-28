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
- SSCOM32E — para cargar el certificado al módulo
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
> Un GND suelto o sin conectar causa corrupción en el UART — caracteres extraños, módulo que no responde y reconexiones constantes.
> ⚠️ El pin SLEEP (S) y PWRKEY (K) van a GND del CN101.
> ⚠️ TXD/RXD del módulo son 3.3V TTL — conexión directa al STM32 sin convertidor.

---

## Paso 1 — Crear dispositivo en IoTForge

1. Ingresa a [iotforge.iaintegracion.space](https://iotforge.iaintegracion.space)
2. Ve a **Nodo** y crea tu nodo
3. Ve a **Variables** crea nueva variable
4. Ve a **Dispositivos** crea nuevo dispositivo STM32
5. Anota:
   - `DEVICE_ID`
   - `DEVICE_TOKEN`
   - `VARIABLE_ID`
   - `THING_ID` (Nodo)

---

## Paso 2 — Descargar certificado TLS

IoTForge usa TLS con certificado ISRG Root X1 (Let's Encrypt).

1. Descarga el certificado:
   [https://letsencrypt.org/certs/isrgrootx1.pem](https://letsencrypt.org/certs/isrgrootx1.pem)
2. Guarda el archivo como `isrgrootx1.pem`
3. Verifica el tamaño exacto en PowerShell — lo necesitarás en el Paso 3:

```powershell
(Get-Item "C:\ruta\isrgrootx1.pem").Length
```

---

## Paso 3 — Cargar certificado al módulo A7670SA

### 3.1 — Conectar el módulo

Conecta el módulo A7670SA **directamente a la PC por USB** (no el UART del STM32) e instala el driver **SIMCom USB Drivers A7670**.

En el Administrador de dispositivos aparecerán tres puertos:

| Puerto | Uso |
|--------|-----|
| SimTech HS-USB AT Port 9011 | ✅ Comandos AT — usar este |
| SimTech HS-USB Diagnostics 9011 | Flash de firmware |
| SimTech HS-USB NMEA 9011 | GPS |

Abre **SSCOM32E** y selecciona el puerto **AT Port 9011** a **115200 baudios**.

### 3.2 — Verificar SIM y almacén de certificados

```
AT+CPIN?
```
Debe responder `+CPIN: READY` — SIM reconocida.

```
AT+CMEE=2
AT+CCERTLIST
```
Si responde solo `OK` sin listar nada, el almacén está vacío y listo.

### 3.3 — Cargar el certificado con SSCOM32E

> ⚠️ El firmware A131B03 del A7670SA-MASA **no soporta** `AT+FSCREATE` / `AT+FSWRITE`.
> Usar siempre `AT+CCERTDOWN` para cargar certificados.

1. En SSCOM, escribe el comando con el tamaño exacto del archivo y envía:

```
AT+CCERTDOWN="isrgrootx1.pem",<tamaño_en_bytes>
```

2. El módulo responde con `>` — en ese momento:
   - Haz clic en **OpenFile** y selecciona el archivo `isrgrootx1.pem`
   - Haz clic en **SendFile**
   - SSCOM envía el archivo automáticamente

3. El módulo responde `OK` al finalizar la transferencia.

### 3.4 — Verificar que el certificado quedó cargado

```
AT+CCERTLIST
```
Debe responder:
```
+CCERTLIST: "isrgrootx1.pem"
OK
```

> ✅ El certificado **persiste aunque se apague el módulo** — solo se carga una vez.

---

## Paso 4 — Calcular credenciales MQTT v2

IoTForge usa autenticación MQTT v2 con credenciales derivadas del token del dispositivo:

- **Username:** `DEVICE_ID` + `_v2`
- **Password:** SHA256 del `DEVICE_TOKEN` en hexadecimal

Calcula el SHA256 de tu token en PowerShell:

```powershell
$token = "TU_DEVICE_TOKEN_AQUI"
$bytes = [System.Text.Encoding]::UTF8.GetBytes($token)
$hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
($hash | ForEach-Object { $_.ToString("x2") }) -join ""
```

O en Linux/Mac:

```bash
echo -n "TU_DEVICE_TOKEN_AQUI" | sha256sum | cut -d' ' -f1
```

Guarda el resultado — lo usarás como `IOTF_MQTT_PASS` en el código.

---

## Paso 5 — Configurar el proyecto STM32

### 5.1 Descargar el repositorio base

Descarga el proyecto desde:
[https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058](https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058)

Ábrelo en **STM32CubeIDE**.

### 5.2 Reemplazar `main.c`

Reemplaza el archivo `Core/Src/main.c` con el `main.c` de este repositorio.

Actualiza tus credenciales IoTForge en los `#define` al inicio del archivo:

```c
#define IOTF_APN          "tu.apn.operador"
#define IOTF_BROKER       "mqtt.iaintegracion.space"
#define IOTF_PORT         8883
#define IOTF_THING_ID     "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_DEVICE_ID    "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_MQTT_USER    "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx_v2"
#define IOTF_MQTT_PASS    "sha256_del_token_calculado_en_paso_4"
#define IOTF_VAR_ID       "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define IOTF_CA_FILE      "isrgrootx1.pem"
```

> ⚠️ `IOTF_MQTT_USER` es el `DEVICE_ID` con `_v2` al final.
> ⚠️ `IOTF_MQTT_PASS` es el SHA256 del token calculado en el Paso 4 — **no el token raw**.

### 5.3 Reemplazar `st7735.h`

Reemplaza el archivo `Core/Inc/st7735.h` con el de este repositorio.

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

## Paso 6 — Compilar y cargar

1. Compila el proyecto en STM32CubeIDE (`Ctrl+B`)
2. Conecta el ST-Link al STM32
3. Carga el firmware (`Run → Run`)
4. Conecta el USB-C de la Blue Pill para ver los logs por puerto COM virtual

---

## Paso 7 — Verificar funcionamiento

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
PUB [iotforge/DEVICE_ID/status] => ONLINE
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
- El firmware `A131B03A7670M6C_M` no soporta `AT+FSCREATE` — usar `AT+CCERTDOWN`
- Los datos se publican en el topic: `iotforge/{THING_ID}/{VAR_ID}`
- El heartbeat se publica en: `iotforge/{DEVICE_ID}/status` cada 30 segundos
- **GND común es crítico** — un GND suelto causa caracteres corruptos en UART y reconexiones constantes

---

## Troubleshooting

| Síntoma | Causa | Solución |
|---------|-------|----------|
| Caracteres `▒` en el log | GND suelto o no común | Verificar GND entre STM32, módulo y fuente |
| Módulo se reinicia (`*ATREADY`) | Alimentación insuficiente o GND suelto | Fuente dedicada 5V 2A + GND común |
| `AT+FSCREATE` da ERROR | Firmware A131B03 no lo soporta | Usar `AT+CCERTDOWN` |
| `AT+CCERTDOWN` no existe | Puerto incorrecto | Abrir AT Port 9011, no el UART del STM32 |
| `+CMQTTCONNECT: 0,3` | Credenciales incorrectas | Verificar `IOTF_MQTT_USER` y `IOTF_MQTT_PASS` |
| `+CMQTTCONNECT: 0,34` | Certificado cargado con `\r\n` | Recargar con SendFile desde SSCOM |
| `+CMQTTCONNECT: 0,9` | Puerto 8883 bloqueado por ISP | Probar con otra SIM o verificar APN |
| No conecta MQTT | Certificado no cargado | Verificar con `AT+CCERTLIST` |
| SSCOM no recibe respuestas | Puerto equivocado | Usar COM del AT Port 9011 |
| `+CGREG: 0,0` | SIM sin datos activos | Verificar plan de datos y APN |

---

*Guía generada para IoTForge — Ejemplo STM32 A7670SA*

---

## Links de recursos

- [Driver del módulo SIMCom USB](https://github.com/TDLOGY/SIMCOM_USB_DRIVER/tree/main)
- [SSCOM32E](https://drive.google.com/file/d/0B4GOwiN2Qm96R2V0dVFlSXltVWs/view?resourcekey=0-SR9QQdTR1vm3Zg7-tPDJIg)
- [Comandos MQTT del módulo A76XX](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/module/sim7680/A76XX%20Series%20MQTT_EX_AT%20Command%20Manual_V1.00.pdf)
- [Manual del módulo A7670SA](https://manuals.plus/ae/1005006666698901#google_vignette)

# STM32 + A7670SA → IoTForge
### Guía de integración MQTT celular con pantalla TFT ST7735

Esta guía conserva el procedimiento completo de preparación, certificado, pantalla y carga, y lo complementa con **Device V2** y el diagnóstico de la prueba en hardware. Se confirmó conexión MQTT y recepción de valores en IoTForge con la base del firmware actual.

**Ruta de trabajo:** conexiones → dispositivo y credenciales → certificado → proyecto CubeIDE → carga → verificación en IoTForge.

Este repositorio contiene archivos para integrar en el proyecto base; no incluye un proyecto CubeIDE completo. Todos los valores de autenticación publicados son ejemplos.

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
> La alimentación y los niveles UART descritos corresponden al breakout usado en este proyecto. Verifica el manual de tu placa antes de conectar VCC, SLEEP (S) y PWRKEY (K), incluyendo sus puentes de fábrica; no extrapoles estas conexiones al módulo SIMCom sin breakout.

**Conexión que se validó:** PA9/TX → R/RXD del módem; PA10/RX ← T/TXD del módem. USART1 se configura a **115200, 8N1 y sin control de flujo**. La entrada ADC del ejemplo es **PB1**.

Comprueba continuidad de los dos jumpers con las placas apagadas. Durante la prueba, un jumper TX abierto causó ausencia total de respuesta; sustituirlo permitió recibir `AT` y `OK` sin cambiar la lógica UART.

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

`VARIABLE_ID` se coloca en `IOTF_VAR_ID`. Los IDs de nodo, variable y dispositivo son distintos: utiliza los del mismo conjunto configurado en IoTForge.

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

Los nombres y números COM pueden variar según el driver. El **USB AT del módem** sirve para enviar comandos manuales; el **USB del STM32** muestra los logs del firmware. Detén el programa del STM32 durante la preparación manual para evitar secuencias AT simultáneas.

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

El hash debe tener **64 caracteres hexadecimales minúsculos**, calculados sobre el token original, sin espacios adicionales ni salto de línea. No vuelvas a calcular SHA-256 sobre un hash ya calculado.

| Campo | Contenido |
|---|---|
| `IOTF_DEVICE_ID` | ID original, sin sufijo |
| `IOTF_MQTT_USER` | Ese mismo ID seguido de `_v2` |
| `IOTF_MQTT_PASS` | SHA-256 del token original |
| Topics | IDs originales, sin añadir `_v2` |

Calcula el SHA256 de tu token en PowerShell:

```powershell
$token = "TU_DEVICE_TOKEN_AQUI"
$bytes = [System.Text.Encoding]::UTF8.GetBytes($token)
$hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
($hash | ForEach-Object { $_.ToString("x2") }) -join ""
```

O en Linux:

```bash
echo -n "TU_DEVICE_TOKEN_AQUI" | sha256sum | cut -d' ' -f1
```

En macOS puedes usar `printf '%s' 'TU_DEVICE_TOKEN_AQUI' | shasum -a 256` y tomar solo los 64 caracteres del hash.

Guarda el resultado — lo usarás como `IOTF_MQTT_PASS` en el código.

**El token y el hash son secretos.** No publiques ninguno, ni HEX/BIN que los contengan. Los ejemplos anteriores son para cálculo local; evita guardar comandos con credenciales reales en archivos compartidos.

El firmware comprueba el formato del usuario y del hash antes de conectar. Esa comprobación no valida que el token pertenezca al dispositivo: la autenticación real la realiza el broker.

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

En la prueba con SIM WEEX se utilizó `internet.weex.mx`. Configura el APN de tu operador y verifica el valor efectivo con `AT+CGDCONT?`; no reutilices el de otra SIM por defecto.

### 5.3 Reemplazar `st7735.h`

Reemplaza el archivo `Core/Inc/st7735.h` con el de este repositorio.

El archivo publicado se llama [ST7735.h](ST7735.h). Conserva en el proyecto el nombre que utiliza su `#include`.

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
4. Conecta el USB de la Blue Pill para ver los logs por puerto COM virtual

### 6.1 — Evitar cargar un archivo viejo

Si cargas con CubeProgrammer, revisa la fecha del HEX/BIN seleccionado. Compilar únicamente el objetivo ELF puede dejar HEX/BIN de una compilación anterior. Genera los artefactos de la compilación actual y comprueba que la descarga termine con `Download verified successfully`.

Si aparecen líneas `MINI AT`, está ejecutándose el programa mínimo de diagnóstico. El firmware completo muestra la secuencia de red, TLS, MQTT y lecturas ADC. Después de cargar, reinicia y vuelve a abrir el COM si se desconectó.

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
> AT+CMQTTCONNECT=<credenciales ocultas>
< +CMQTTCONNECT: 0,0
OK
PUB [iotforge/DEVICE_ID/status] => ONLINE
```

`+CMQTTCONNECT: 0,0` = conexión exitosa ✅

**Un `OK` previo no confirma autenticación MQTT.** El firmware espera la línea completa `+CMQTTCONNECT: 0,0` antes de marcar la sesión como conectada. El log oculta el comando con las credenciales y evita mostrar su eco previo al resultado.

En la pantalla LCD verás:

- **ADC Value** — valor del sensor en tiempo real
- **MQTT OK** — conexión activa

En el dashboard de IoTForge verifica que el dispositivo aparezca **ONLINE** y que cambie el valor de la variable. En la configuración validada, el ADC se lee cada 3 minutos, el valor se publica cada 5 minutos y el heartbeat se publica cada 5 minutos. La LCD se redibuja cada 1.5 minutos; las esperas bloqueantes de comandos AT pueden alargar los intervalos reales.

La línea `PUB [...]` indica el envío realizado por el programa; por sí sola no acredita recepción en IoTForge. Para validar la entrega, comprueba el resultado final `+CMQTTPUB: 0,0` cuando esté disponible o la actualización en el dashboard, como se hizo en la prueba.

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
- Configura **PWRKEY (K)** según el manual y los puentes de fábrica de tu breakout
- El certificado `isrgrootx1.pem` debe cargarse **una sola vez** al módulo — persiste aunque se apague
- El firmware `A131B03A7670M6C_M` no soporta `AT+FSCREATE` — usar `AT+CCERTDOWN`
- Los datos se publican en el topic: `iotforge/{THING_ID}/{VAR_ID}`
- El heartbeat se publica en: `iotforge/{DEVICE_ID}/status` cada 5 minutos
- La lectura ADC se realiza cada 3 minutos y el valor conserva la última muestra hasta la siguiente publicación MQTT
- La LCD se actualiza cada 1.5 minutos sin limpiar toda la pantalla en cada ciclo
- **GND común es crítico** — un GND suelto causa caracteres corruptos en UART y reconexiones constantes

---

## Troubleshooting

| Síntoma | Posible causa o comprobación | Acción |
|---------|-------|----------|
| Caracteres `▒` en el log | GND suelto o no común | Verificar GND entre STM32, módulo y fuente |
| Módulo se reinicia (`*ATREADY`) | Alimentación insuficiente o GND suelto | Fuente dedicada 5V 2A + GND común |
| `AT+FSCREATE` da ERROR | Firmware A131B03 no lo soporta | Usar `AT+CCERTDOWN` |
| `AT+CCERTDOWN` no existe | Puerto incorrecto | Abrir AT Port 9011, no el UART del STM32 |
| `+CMQTTCONNECT: 0,<error>` | Fallo de conexión; el código no identifica por sí solo la causa | Consultar el manual de la versión del módem y revisar red, TLS y credenciales |
| No conecta MQTT | Certificado no cargado | Verificar con `AT+CCERTLIST` |
| SSCOM no recibe respuestas | Puerto equivocado | Usar COM del AT Port 9011 |
| `+CGREG: 0,0` | Sin registro en ese servicio | Revisar también `CEREG`, SIM, cobertura y estado de registro |
| `AT` sin respuesta, `rx=0` | Jumper abierto, cruce incorrecto, módem apagado o UART no disponible | Medir continuidad, verificar PA9→R y PA10←T, GND y alimentación |
| `MINI AT` después de cargar | Firmware mínimo o HEX/BIN anterior | Regenerar y cargar el artefacto completo actual |
| MQTT conecta pero la variable no cambia | IDs o asociación de la variable | Revisar `THING_ID`, `VAR_ID` y configuración de IoTForge |

### Diagnóstico paso a paso

**1. UART.** Antes de cambiar APN o credenciales, confirma `AT → OK`. En el log, `tx=0` significa `HAL_OK`, no cero bytes transmitidos. `rx=0` significa que el programa no recibió bytes durante esa lectura; no basta para atribuir la falla al código o al cableado.

La respuesta que se obtuvo al reemplazar el jumper fue:

```text
rx=9 HALerr=0x00000000 SRerr=0x00000000
RXHEX: 41 54 0D 0D 0A 4F 4B 0D 0A
```

`41 54` es el eco `AT`; `4F 4B` es `OK`. El diagnóstico detallado `UARTDBG` del firmware público aparece cuando falla el primer `AT`.

**2. SIM y red.** Con el programa STM32 detenido, consulta desde el puerto AT del módem:

```text
AT
AT+CMEE=2
AT+IPR?
AT+IFC?
AT+CSCLK?
AT+CPIN?
AT+CSQ
AT+CEREG?
AT+CGREG?
AT+CGATT?
AT+CGDCONT?
AT+CGPADDR=1
AT+NETOPEN?
```

En el equipo probado: `IPR=115200`, `IFC=0,0`, `CSCLK=0`, SIM `READY`, `CGATT=1` y una dirección IP asignada. Compara el APN de `CGDCONT?` con el configurado para tu SIM.

**3. TLS.** Comprueba el certificado almacenado y la fecha del módem:

```text
AT+CCERTLIST
AT+CSSLCFG=0
AT+CCLK?
```

El archivo seleccionado debe coincidir con `IOTF_CA_FILE`. Si el reloj vuelve a 1970, revisa su sincronización antes de investigar la autenticación MQTT.

**4. MQTT e IoTForge.** Busca `+CMQTTCONNECT: 0,0`, después `ONLINE` y finalmente cambios en la variable. Guarda el resultado de error completo si falla; no compartas el comando de conexión con credenciales.

### Opciones del firmware y personalización

- `IOTF_CONTINUE_AFTER_AT_FAILURE=1` conserva la secuencia probada aunque falle el primer `AT`. En `0`, espera 3 segundos y reintenta sin avanzar a red.
- `HEARTBEAT_MS` y `PUBLISH_MS` controlan las publicaciones cada 5 minutos.
- `ADC_SAMPLE_MS` controla la lectura ADC cada 3 minutos.
- `LCD_UPDATE_MS` controla la actualización de la LCD cada 1.5 minutos.
- `userHardwareInit()` inicializa el hardware de aplicación.
- `userReadInputs()` obtiene la lectura ADC; cámbiala para tu sensor.
- `userUpdateDisplay()` controla la presentación en pantalla.
- `mqttPublishValue()` prepara el valor que se publica.

La versión V2 incorpora validación del formato de credenciales, espera del resultado completo de conexión y registro USB con espera acotada cuando está ocupado. Conserva las funciones de conexión y publicación al personalizar sensores o pantalla.

---

*Guía generada para IoTForge — Ejemplo STM32 A7670SA*

---

## Links de recursos

- [Driver del módulo SIMCom USB](https://github.com/TDLOGY/SIMCOM_USB_DRIVER/tree/main)
- [SSCOM32E](https://drive.google.com/file/d/0B4GOwiN2Qm96R2V0dVFlSXltVWs/view?resourcekey=0-SR9QQdTR1vm3Zg7-tPDJIg)
- [Comandos MQTT del módulo A76XX](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/module/sim7680/A76XX%20Series%20MQTT_EX_AT%20Command%20Manual_V1.00.pdf)
- [Manual del módulo A7670SA](https://manuals.plus/ae/1005006666698901#google_vignette)

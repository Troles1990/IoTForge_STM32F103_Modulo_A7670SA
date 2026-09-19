# STM32 + A7670SA → IoTForge Device V2

Ejemplo de MQTT sobre TLS con STM32F103C8T6, módem A7670SA y pantalla ST7735.
Lee el ADC de PB1, publica su valor y envía un heartbeat `ONLINE`.

La base de este firmware se probó en hardware: conexión V2 aceptada y valores visibles en IoTForge. El repositorio usa credenciales de ejemplo; debes reemplazarlas antes de cargarlo.

## 1. Conecta el hardware

Necesitas una Blue Pill, el breakout A7670SA, SIM con datos, pantalla ST7735 1.8" 128×160, ST-LINK y una fuente dedicada de 5 V / 2 A para el breakout utilizado en este proyecto.

### Módem → STM32

| Pin del breakout | Conectar a |
|---|---|
| R / RXD | PA9 / USART1_TX |
| T / TXD | PA10 / USART1_RX |
| G / GND | GND común con STM32 y fuente |
| V / VCC | Fuente externa de 5 V |

**TX va a RX y RX va a TX.** Comprueba continuidad de los jumpers: un cable abierto puede dejar el registro en `rx=0` aunque la transmisión indique éxito.

La alimentación y los niveles UART dependen del breakout. Estas conexiones corresponden a la placa con conector G/R/T/K/V/G/S usada en la prueba; no extrapoles su alimentación al módulo SIMCom sin placa. Para K/PWRKEY y S/SLEEP, sigue el manual de tu placa y sus puentes de fábrica.

### Pantalla y sensor

| Señal | STM32 |
|---|---|
| LCD VCC / GND | 3.3 V / GND |
| LCD SCL / SDA | PA5 / PA7 |
| LCD CS / DC | PA1 / PA2 |
| LCD LED / RES | PA3 / PB0 |
| Entrada ADC | PB1 |

## 2. Prepara el proyecto

1. Abre en STM32CubeIDE el [proyecto base STM32-MQTT-058](https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058).
2. Sustituye `Core/Src/main.c` por [main.c](main.c).
3. Sustituye el encabezado de pantalla `Core/Inc/st7735.h` por [ST7735.h](ST7735.h), conservando el nombre que usa el proyecto.
4. Mantén USART1 en **115200, 8N1, sin control de flujo**, PA9 TX y PA10 RX.

Este repositorio contiene los archivos para integrar en el proyecto base, no un proyecto CubeIDE completo.

## 3. Configura Device V2

En [IoTForge](https://iotforge.iaintegracion.space), crea o identifica tu nodo, variable y dispositivo. Necesitas:

| Valor | Uso |
|---|---|
| `THING_ID` | ID del nodo |
| `VAR_ID` | ID de la variable |
| `DEVICE_ID` | ID del dispositivo |
| `DEVICE_TOKEN` | Token del dispositivo, usado para calcular el hash |

La autenticación V2 utiliza:

- Usuario: `DEVICE_ID` seguido de `_v2`.
- Contraseña: **SHA-256 del token original**, en 64 caracteres hexadecimales minúsculos.
- El sufijo `_v2` solo va en el usuario MQTT; el ID y los topics conservan el ID original.

Calcula el hash localmente con PowerShell. Introduce el token sin espacios adicionales ni saltos de línea:

```powershell
$token = Read-Host "DEVICE_TOKEN"
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($token)
    ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join ""
} finally {
    $sha.Dispose()
    Remove-Variable token, bytes -ErrorAction SilentlyContinue
}
```

Cambia estos valores al principio de `main.c`:

```c
#define IOTF_APN          "tu.apn.operador"
#define IOTF_BROKER       "mqtt.iaintegracion.space"
#define IOTF_PORT         8883
#define IOTF_THING_ID     "TU_THING_ID"
#define IOTF_DEVICE_ID    "TU_DEVICE_ID"
#define IOTF_MQTT_USER    "TU_DEVICE_ID_v2"
#define IOTF_MQTT_PASS    "SHA256_DEL_DEVICE_TOKEN_64_HEX_MINUSCULAS"
#define IOTF_VAR_ID       "TU_VAR_ID"
#define IOTF_CA_FILE      "isrgrootx1.pem"
```

En la prueba con WEEX se utilizó `internet.weex.mx`; usa el APN de tu SIM.
No publiques el token ni su hash: **el hash también es una credencial MQTT**. Tampoco compartas HEX/BIN compilados con tus credenciales.

El firmware comprueba el formato del usuario y del hash antes de conectar. Esta comprobación no demuestra que el token pertenezca al dispositivo: eso lo valida el broker.

## 4. Carga el certificado en el módem

Esta preparación se hace desde el **puerto USB AT del módem** con SSCOM u otro terminal. Es distinto del COM USB del STM32, que muestra los logs del programa.

1. Descarga [ISRG Root X1](https://letsencrypt.org/certs/isrgrootx1.pem) y guárdalo como `isrgrootx1.pem`.
2. Consulta el tamaño exacto:

```powershell
(Get-Item "C:\ruta\isrgrootx1.pem").Length
```

3. Envía lo siguiente, sustituyendo el tamaño:

```text
AT
AT+CMEE=2
AT+CCERTDOWN="isrgrootx1.pem",<tamaño_en_bytes>
```

4. Al aparecer `>`, usa **OpenFile → SendFile** en SSCOM para enviar el archivo.
5. Espera `OK` y verifica:

```text
AT+CCERTLIST
```

Debe aparecer `"isrgrootx1.pem"`. El certificado queda almacenado en el módem; no hace falta enviarlo cada arranque. El firmware selecciona ese archivo para TLS.

## 5. Compila, carga y comprueba

1. Compila en CubeIDE y verifica que termine sin errores.
2. Carga el firmware con ST-LINK.
3. Si utilizas CubeProgrammer, selecciona el HEX/BIN **recién generado**. Recompilar solo el ELF no siempre regenera esos archivos.
4. Abre el COM USB del STM32 en MobaXterm o tu terminal. Reinicia la placa para ver el arranque.
5. Comprueba esta respuesta:

```text
> AT
< OK

> AT+CMQTTCONNECT=<credenciales ocultas>
< +CMQTTCONNECT: 0,0

PUB [iotforge/DEVICE_ID/status] => ONLINE
PUB [iotforge/THING_ID/VAR_ID] => 2126
```

**`+CMQTTCONNECT: 0,0` confirma la conexión MQTT.** Un `OK` previo no basta: el firmware espera la línea de resultado completa.

Finalmente verifica que IoTForge muestre el dispositivo conectado y que la variable cambie. La línea local `PUB` indica el envío realizado por el programa; no sustituye la confirmación de recepción en IoTForge.

| Publicación | Topic | Intervalo configurado |
|---|---|---|
| Estado `ONLINE` | `iotforge/{DEVICE_ID}/status` | 30 s |
| Valor ADC | `iotforge/{THING_ID}/{VAR_ID}` | 3 s |

Los comandos AT son bloqueantes, por lo que los intervalos reales pueden ser mayores.

## Si algo falla

Empieza por UART, después red y finalmente MQTT:

| Síntoma | Qué revisar |
|---|---|
| `AT` sin respuesta / `rx=0` | Continuidad de ambos jumpers, cruce TX/RX, GND, alimentación y estado del módem |
| Caracteres extraños | Baudios, GND, contactos y niveles eléctricos |
| `AT` responde pero no hay red | SIM, registro, APN e IP |
| No conecta con TLS | Certificado seleccionado y reloj del módem |
| MQTT rechaza la conexión | Usuario con `_v2`, hash de 64 caracteres y dispositivo correspondiente |
| Conecta pero no cambia la variable | `THING_ID`, `VAR_ID` y asociación en IoTForge |
| Sigues viendo `MINI AT` | Se cargó el firmware de diagnóstico o un artefacto viejo |

Para inspeccionar el módem manualmente, detén primero el programa del STM32 para evitar comandos simultáneos:

```text
AT
AT+IPR?
AT+IFC?
AT+CSCLK?
AT+CPIN?
AT+CSQ
AT+CEREG?
AT+CGATT?
AT+CGDCONT?
AT+CGPADDR=1
AT+NETOPEN?
AT+CCERTLIST
AT+CCLK?
```

En la configuración probada: `IPR=115200`, `IFC=0,0`, `CSCLK=0`.
Si falla el primer `AT`, el firmware imprime diagnóstico UART; `tx=0` significa `HAL_OK`, no cero bytes enviados.

La opción `IOTF_CONTINUE_AFTER_AT_FAILURE=1` conserva la secuencia probada. Puedes ponerla en `0` para reintentar `AT` sin avanzar a red cuando el módem no responda.

## Personalizar la aplicación

- `userHardwareInit()`: pantalla y hardware propio.
- `userReadInputs()`: lectura del sensor.
- `userUpdateDisplay()`: contenido de la pantalla.
- `mqttPublishValue()`: dato enviado a la variable.

La base validada incluye autenticación V2, espera del resultado MQTT completo y registro USB con espera acotada cuando está ocupado. La copia pública sustituye las credenciales por ejemplos y reduce el diagnóstico UART a fallos.

[Drivers USB SIMCom](https://github.com/TDLOGY/SIMCOM_USB_DRIVER/tree/main) · [Proyecto base](https://github.com/republicofmakers/SW-MCU-STM32-MQTT-058)

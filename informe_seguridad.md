# Seguridad en sistemas IoT: Análisis de vulnerabilidades y protección en comunicación MQTT y almacenamiento de firmware en ESP32-S3

---

## Descripción del trabajo

Este trabajo simula un sistema IoT real compuesto por un microcontrolador ESP32-S3 que envía telemetría a una plataforma de gestión de dispositivos (ThingsBoard) ejecutada en local mediante Docker. Sobre este entorno se identifican y demuestran dos vectores de ataque distintos: la interceptación pasiva de tráfico de red no cifrado mediante captura de paquetes, y la extracción de credenciales mediante acceso físico al hardware y lectura de la memoria flash del dispositivo.

Para cada ataque se aplica la medida de protección correspondiente, evaluando su efectividad. El trabajo cubre, por tanto, tanto la explotación de vulnerabilidades reales como el proceso de mitigación, siguiendo un ciclo completo de análisis de seguridad en un entorno embebido conectado.

---

## Entorno de simulación

### Descripción general

El entorno está formado por tres elementos principales: el dispositivo embebido (ESP32-S3), la red local WiFi, y el servidor de telemetría ThingsBoard ejecutándose en contenedores Docker sobre una máquina local.

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Red local (WiFi)                           │
│                                                                     │
│   ┌──────────────┐    MQTT / MQTTS     ┌──────────────────────┐    │
│   │   ESP32-S3   │ ──────────────────► │    ThingsBoard CE    │    │
│   │              │    puerto 1883      │    (Docker)          │    │
│   │  Firmware    │    puerto 8883 TLS  │    puerto 8080 HTTP  │    │
│   │  (ESP-IDF)   │                     │    puerto 8883 MQTTS │    │
│   └──────────────┘                     └──────────┬───────────┘    │
│                                                    │                │
│                                         ┌──────────▼───────────┐   │
│                                         │    PostgreSQL 16      │   │
│                                         │    (Docker)           │   │
│                                         │    puerto 5432        │   │
│                                         └──────────────────────┘   │
│                                                                     │
│   ┌──────────────┐                                                  │
│   │   Atacante   │  (Wireshark / esptool.py)                       │
│   │   (PC)       │ ◄──────────── misma red / acceso físico         │
│   └──────────────┘                                                  │
└─────────────────────────────────────────────────────────────────────┘
```

### Componentes del sistema

#### ESP32-S3 — Dispositivo IoT

El firmware está desarrollado con ESP-IDF v5.5.1 en C. El dispositivo realiza las siguientes operaciones de forma periódica (cada 5 segundos por defecto):

1. **Recogida de datos sensoriales:**
   - Temperatura interna del MCU
   - Nivel de señal WiFi (RSSI)
   - Memoria heap libre

2. **Serialización del payload con Protocol Buffers (nanopb):**

```proto
syntax = "proto3";
package telemetry;

message SensorDataReading {
  double mcu_temp = 1;
  int32  rssi     = 2;
  uint32 free_heap = 3;
}
```

   El mensaje se codifica en binario (formato protobuf), resultando en un payload compacto de aproximadamente 26 bytes.

3. **Provisioning automático:** Al arrancar por primera vez, el dispositivo se registra en ThingsBoard usando una clave y secreto preconfigurados, obteniendo un token de acceso que se almacena en la NVS (Non-Volatile Storage) del ESP32.

4. **Publicación MQTT:** El payload codificado se publica en el topic `v1/devices/me/telemetry`.

#### ThingsBoard Community Edition — Servidor de telemetría

Se ejecuta en Docker mediante `docker-compose up`. Los servicios levantados son:

| Servicio       | Imagen                      | Puerto relevante    |
|----------------|-----------------------------|---------------------|
| ThingsBoard CE | thingsboard/tb-postgres:4.2.1.1 | 8080 (HTTP), 8883 (MQTTS) |
| PostgreSQL 16  | postgres:16                 | 5432                |

ThingsBoard recibe los mensajes protobuf, los interpreta y los almacena como series temporales de telemetría, accesibles a través de su dashboard web en `http://localhost:8080`.

#### Infraestructura de certificados TLS

Los certificados del servidor residen en el directorio `certs/`:

```
certs/
├── server_mqtt.pem       ← Certificado público del servidor MQTT
└── server_mqtt_key.pem   ← Clave privada del servidor MQTT
```

El certificado se monta en el contenedor ThingsBoard y también se **embebe directamente en el firmware** del ESP32-S3 en tiempo de compilación, mediante la directiva `EMBED_TXTFILES` del sistema de build CMake de ESP-IDF. Esto permite al dispositivo verificar la identidad del servidor sin necesidad de una CA pública.

---

## Explicación del ataque simulado

Se simulan dos ataques independientes que aprovechan dos superficies de ataque distintas: la red y el hardware físico.

---

### Ataque 1: Interceptación de tráfico MQTT sin cifrar (Wireshark)

#### Descripción

Cuando el dispositivo está configurado para comunicarse por MQTT plano (puerto 1883, sin TLS), los mensajes viajan en texto claro por la red local. Cualquier equipo en la misma red con acceso a captura de paquetes puede leer el contenido de las comunicaciones entre el ESP32-S3 y ThingsBoard.

#### Condiciones del ataque

- El atacante está en la misma red WiFi que el dispositivo y el servidor.
- El firmware usa el broker en `mqtt://192.168.1.119:1883` (sin TLS).
- No se requiere ningún privilegio especial más allá de estar en la red.

#### Proceso de explotación

**Paso 1 — Captura de paquetes con Wireshark**

Se inicia Wireshark sobre la interfaz de red correspondiente y se aplica el filtro:

```
mqtt
```

Al conectarse el ESP32-S3, Wireshark muestra los mensajes MQTT intercambiados: el handshake de conexión, la suscripción a atributos y, periódicamente, los mensajes `PUBLISH` al topic `v1/devices/me/telemetry`.

**Paso 2 — Extracción del payload**

Al inspeccionar un paquete `PUBLISH`, el campo "Message" del disector MQTT de Wireshark muestra los bytes del payload en hexadecimal. Por ejemplo:

```
09 00 00 00 00 00 31 40 10 e8 ff ff ff ff 11 8b c9 c1 0
```

**Paso 3 — Decodificación del payload protobuf**

Los bytes extraídos se decodifican con `protoc`:

```bash
echo "090000000000314010e8ffffffff118bc9c10" | xxd -r -p | protoc --decode_raw
```

Salida:

```
1: 0x4031300000000000   ← campo mcu_temp (double IEEE 754)
2: -24                  ← campo rssi (int32, valor negativo en dBm)
3: 209675               ← campo free_heap (uint32, bytes libres)
```

El campo 3 (`free_heap`) es directamente legible como entero. Los campos 1 y 2 requieren conversión, pero con una simple operación de reinterpretación de bytes o un conversor IEEE 754 online se obtienen los valores originales:

- **Campo 1:** `0x4031300000000000` → `17.1875 °C` (temperatura del MCU)
- **Campo 2:** `-24` → `-24 dBm` (nivel de señal WiFi)
- **Campo 3:** `209675` → `209675 bytes` de heap libre

El atacante obtiene así, de forma pasiva y sin ningún tipo de autenticación, todos los datos de telemetría enviados por el dispositivo. Adicionalmente, durante el handshake MQTT inicial, Wireshark revela también el **token de acceso del dispositivo** en el campo Password del paquete CONNECT, lo que permitiría a un atacante suplantar al dispositivo o inyectar datos falsos en ThingsBoard.

---

### Ataque 2: Extracción de certificados por acceso físico a la flash (esptool.py)

#### Descripción

Una vez que el canal de comunicación está protegido con TLS (Ataque 1 ya mitigado), la siguiente superficie de ataque es el propio hardware. El certificado del servidor TLS se encuentra embebido en el firmware, y la flash del ESP32-S3 no está cifrada por defecto. Con acceso físico al dispositivo, es posible leer toda la memoria flash y extraer el certificado.

#### Condiciones del ataque

- El atacante tiene acceso físico al dispositivo (o a la línea de producción).
- El dispositivo está conectado por USB al equipo del atacante.
- No se requieren credenciales ni conocimientos del firmware.

#### Proceso de explotación

**Paso 1 — Lectura completa de la flash**

Con el dispositivo conectado por USB (`/dev/ttyACM0`), se usa `esptool.py` para volcar toda la memoria flash (4 MB en el ESP32-S3) a un archivo binario:

```bash
esptool.py --port /dev/ttyACM0 read_flash 0 ALL flash_content.bin
```

La operación no requiere interrumpir el funcionamiento del dispositivo si se hace en el momento adecuado; basta con reiniciar en modo bootloader.

**Paso 2 — Localización del certificado en el binario**

Los certificados PEM embebidos en el firmware tienen una cabecera ASCII característica. Se pueden localizar con:

```bash
strings flash_content.bin | grep -A 50 "BEGIN CERTIFICATE"
```

O simplemente inspeccionando el binario con un editor hexadecimal. La cadena `-----BEGIN CERTIFICATE-----` aparece en texto claro dentro del volcado, ya que el firmware no está cifrado.

**Paso 3 — Extracción y análisis del certificado**

Se copia el bloque PEM a un archivo:

```
-----BEGIN CERTIFICATE-----
MIIBxTCCAW+gAwIBAgIUe...
...
-----END CERTIFICATE-----
```

Y se analiza con OpenSSL:

```bash
openssl x509 -in certificado.pem -text -noout
```

Salida (fragmento):

```
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: ...
        Issuer: CN=ThingsBoard Self-Signed, O=Lab Local
        Validity
            Not Before: ...
            Not After :  ...
        Subject: CN=192.168.1.119
        Subject Public Key Info:
            Public Key Algorithm: rsaEncryption
                Public-Key: (2048 bit)
                ...
```

Con este certificado el atacante obtiene:

- La **dirección IP del servidor** ThingsBoard (`CN=192.168.1.119`), confirmando la topología de red.
- El **certificado público del servidor**, que puede usarse para montar ataques de tipo **man-in-the-middle**: si el atacante puede interceptar el tráfico (por ejemplo, con ARP spoofing), puede presentar este mismo certificado para hacerse pasar por el servidor legítimo, siempre que el dispositivo lo tenga hardcodeado y no realice verificación de revocación.
- En escenarios con clave privada también embebida (mala práctica), el atacante podría directamente suplantar al servidor.

---

## Medidas de protección

### Protección 1: Cifrado del canal de comunicación con TLS (MQTTS)

#### Descripción

La primera medida de protección consiste en habilitar TLS en la comunicación MQTT, pasando del broker en `mqtt://...:1883` al broker seguro `mqtts://...:8883`. De este modo, todo el tráfico entre el ESP32-S3 y ThingsBoard viaja cifrado.

#### Implementación

**En el firmware (ESP-IDF):**

El cliente MQTT se configura con verificación de certificado del servidor:

```c
esp_mqtt_client_config_t mqtt_cfg = {
    .broker.address.uri = CONFIG_BROKER_URL,  // "mqtts://192.168.1.119:8883"
    .broker.verification.certificate = (const char *)server_cert_pem_start,
};
```

El certificado del servidor se embebe en el binario en tiempo de compilación mediante CMake:

```cmake
target_add_binary_data(${COMPONENT_LIB} "server_mqtt.pem" TEXT)
```

**En ThingsBoard (docker-compose.yml):**

```yaml
environment:
  MQTT_SSL_ENABLED: "true"
  MQTT_SSL_BIND_PORT: "8883"
  MQTT_SSL_CREDENTIALS_TYPE: "PEM"
  MQTT_SSL_PEM_CERT: "/etc/thingsboard/conf/server_mqtt.pem"
  MQTT_SSL_PEM_KEY: "/etc/thingsboard/conf/server_mqtt_key.pem"
```

#### Verificación de la efectividad

Con TLS activo, al capturar el mismo tráfico con Wireshark se observa que los paquetes MQTT han sido reemplazados por tráfico TLS. El filtro `mqtt` no devuelve ningún resultado. Wireshark solo muestra los handshakes TLS (`Client Hello`, `Server Hello`, `Certificate`, `Finished`) y a continuación registros `Application Data` completamente opacos, sin posibilidad de ver el contenido de los mensajes ni el token de acceso del dispositivo.

---

### Protección 2: Cifrado de la memoria flash (ESP32 Flash Encryption)

#### Descripción

Para mitigar el ataque de extracción de certificados por lectura directa de la flash, ESP-IDF proporciona la funcionalidad de **Flash Encryption**. Esta característica cifra el contenido de la flash con una clave AES-256 almacenada en los eFuses del chip. El contenido solo es descifrado de forma transparente en el interior del chip durante la ejecución; cualquier lectura externa (por ejemplo con esptool.py) devuelve datos cifrados ilegibles.

#### Habilitación

La flash encryption se habilita desde la configuración del proyecto con `idf.py menuconfig`:

```
Security features
  └─ Enable flash encryption on boot
       └─ Flash Encryption Mode: Development / Release
```

En modo **Development** la clave se genera en el dispositivo al primer arranque y se puede deshabilitar mediante eFuses para poder reflashear durante el desarrollo. En modo **Release** la clave se genera una sola vez y el dispositivo queda permanentemente protegido.

Al flashear el firmware con flash encryption activa y conectar `esptool.py` para leer la flash:

```bash
esptool.py --port /dev/ttyACM0 read_flash 0 ALL flash_content.bin
```

El volcado resultante contiene únicamente datos cifrados. La búsqueda de `BEGIN CERTIFICATE` no produce ningún resultado, y el contenido del binario es indistinguible de ruido aleatorio.

#### Consideraciones adicionales

- La flash encryption protege también el token MQTT almacenado en la NVS y las claves de provisioning hardcodeadas en el firmware.
- Se recomienda combinar flash encryption con **Secure Boot** para evitar que un atacante reemplace el firmware por uno no autorizado, incluso sin poder leer el firmware original.
- En entornos de producción, las claves privadas del servidor no deberían embeberse en el firmware del cliente. Lo correcto es que el dispositivo solo conozca el certificado público del servidor (para verificar la conexión TLS), nunca la clave privada.

---


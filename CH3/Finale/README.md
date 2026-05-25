# Sistema IoT - Monitoreo Calidad del Aire
**Universidad de la Sabana · Challenge #3 · 2026-1**

## 👥 Integrantes
- Juan David González
- Jorge Luis Alarcón
- Jaime Andrés Olarte

---

## 🏗️ Arquitectura del Sistema

```
ESP32 (Sensores) ← Hardware
  ├── BME280 (Temperatura, Humedad, Presión)
  ├── PMS5003 (PM2.5 - Partículas)
  └── MQ-135 (Gas)
       ↓ MQTT (Topic: sabana/aire/datos)
       
Raspberry Pi Gateway ← Gateway
  ├── Mosquitto Broker (Puerto 1883)
  ├── bridge.py (MQTT → SQLite → Ubidots + IA)
  ├── dashboard.py (Flask Web Server)
  └── ia_module.py (Gemini AI Predictions)
       ↓ HTTP API
       
Cloud Services ← Nube
  └── Ubidots (Dashboard + Analytics)
```

---

## 📦 Instalación - Raspberry Pi

### 1. Dependencias del Sistema

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients python3-pip python3-venv sqlite3
```

### 2. Configurar Proyecto

```bash
mkdir ~/iot-challenge3
cd ~/iot-challenge3

# Copiar archivos:
# - bridge.py
# - dashboard.py
# - ia_module.py
# - requirements.txt

# Crear entorno virtual
python3 -m venv venv
source venv/bin/activate

# Instalar dependencias
pip install -r requirements.txt
```

### 3. Configurar Credenciales

**Editar `bridge.py` (línea 49):**
```python
"ubidots_token": "BBFF-TU_TOKEN_AQUI",
```

**Editar `ia_module.py` (línea 27):**
```python
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "AIzaSy_TU_KEY_AQUI")
```

### 4. Instalar Servicios Systemd

```bash
# Copiar servicios
sudo cp iot-bridge.service /etc/systemd/system/
sudo cp iot-dashboard.service /etc/systemd/system/

# Recargar systemd
sudo systemctl daemon-reload

# Habilitar auto-inicio
sudo systemctl enable iot-bridge.service
sudo systemctl enable iot-dashboard.service

# Iniciar servicios
sudo systemctl start iot-bridge.service
sudo systemctl start iot-dashboard.service
```

### 5. Verificar Instalación

```bash
# Ver estado
sudo systemctl status iot-bridge.service
sudo systemctl status iot-dashboard.service

# Ver logs en tiempo real
sudo journalctl -u iot-bridge.service -f
```

---

## 🔌 Instalación - ESP32

### 1. Librerías Arduino IDE

Instalar desde Library Manager:
- Adafruit BME280
- Adafruit BMP280
- Adafruit SSD1306
- Adafruit GFX Library
- DHT sensor library
- PubSubClient (by Nick O'Leary)

### 2. Configurar WiFi y MQTT

**Editar `main_ch3.ino`:**

```cpp
// Líneas 53-54: Configuración WiFi
const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASSWORD = "TU_PASSWORD";

// Línea 60: IP de la Raspberry Pi
const char* MQTT_BROKER = "192.168.X.X";  // Cambiar por IP real
```

### 3. Subir Código

1. Conectar ESP32 via USB
2. Seleccionar placa: **ESP32 Dev Module**
3. Seleccionar puerto: COMx (Windows) o /dev/ttyUSBx (Linux)
4. Click en **Upload**

---

## ☁️ Configuración Ubidots

### 1. Crear Cuenta
- URL: https://industrial.ubidots.com/accounts/signup_industrial/
- Seleccionar plan **FREE**

### 2. Obtener Token
1. Login → Click en tu nombre (arriba derecha)
2. **API Credentials**
3. Copiar **Default token** (empieza con `BBFF-`)

### 3. Verificar Datos
1. Ir a **Devices** (menú izquierdo)
2. Buscar dispositivo **esp32-aire** (se crea automáticamente)
3. Debe tener 8 variables:
   - `pm25`
   - `gas_ppm`
   - `temperatura`
   - `humedad`
   - `presion`
   - `estado_num`
   - `alarma`
   - `pms_ok`

---

## 🔑 Configuración Gemini AI

### 1. Obtener API Key
1. URL: https://aistudio.google.com/app/apikey
2. Login con cuenta Google
3. Click **Create API key**
4. Copiar la key (empieza con `AIza`)

### 2. Configurar en ia_module.py
Editar línea 27 con tu API key

---

## 🌐 Acceso al Sistema

### Dashboards Disponibles

| Servicio | URL | Credenciales |
|----------|-----|--------------|
| **Dashboard Flask** | `http://IP_RASPBERRY:5000` | usuario: `sabana`<br>password: `sabana2026` |
| **Dashboard ESP32** | `http://IP_ESP32/` | Sin login |
| **Ubidots** | `https://industrial.ubidots.com/` | Tu cuenta Ubidots |

### Encontrar IPs

**Raspberry Pi:**
```bash
hostname -I
```

**ESP32:**
- Mira el OLED (muestra la IP)
- O el Serial Monitor en Arduino IDE

---

## 🔧 Comandos Útiles

### Servicios

```bash
# Ver logs
sudo journalctl -u iot-bridge.service -f
sudo journalctl -u iot-dashboard.service -f

# Reiniciar servicios
sudo systemctl restart iot-bridge.service
sudo systemctl restart iot-dashboard.service

# Detener servicios
sudo systemctl stop iot-bridge.service
sudo systemctl stop iot-dashboard.service

# Ver últimas 50 líneas de logs
sudo journalctl -u iot-bridge.service -n 50
```

### Base de Datos

```bash
cd ~/iot-challenge3

# Ver total de lecturas
sqlite3 aire.db "SELECT COUNT(*) FROM lecturas;"

# Ver total de predicciones IA
sqlite3 aire.db "SELECT COUNT(*) FROM predictions;"

# Ver últimas 5 lecturas
sqlite3 aire.db "SELECT id, time(ts_local) as hora, pm25, temp, estado FROM lecturas ORDER BY id DESC LIMIT 5;"

# Ver última predicción IA
sqlite3 aire.db "SELECT timestamp, prediccion_6h, prioridad, razonamiento FROM predictions ORDER BY id DESC LIMIT 1;"
```

---

## 📊 Funcionalidades

### ✅ Monitoreo en Tiempo Real
- Lectura de sensores cada 2 segundos
- Publicación MQTT cada 5 segundos
- Almacenamiento local en SQLite
- Sincronización con Ubidots cada 10 segundos

### 🤖 Inteligencia Artificial
- Predicción de calidad del aire para próximas 6 horas
- Generación de recomendaciones para autoridades
- Análisis de tendencias semanales
- Actualización automática cada 30 minutos

### 📈 Dashboards
- **Flask**: Estadísticas, historial, predicciones IA
- **ESP32**: Dashboard embebido con histórico
- **Ubidots**: Visualización en la nube

### 🔔 Alertas
- LEDs indicadores (Verde/Amarillo/Rojo)
- Buzzer para condiciones críticas
- Sistema de estados: BUENA, MODERADA, MALA, MUY MALA

---

## 📁 Estructura de Archivos

```
iot-challenge3/
├── bridge.py              # Gateway MQTT → SQLite → Ubidots
├── dashboard.py           # Servidor Flask con IA
├── ia_module.py          # Módulo Gemini AI
├── requirements.txt       # Dependencias Python
├── aire.db               # Base de datos SQLite (auto-generada)
├── venv/                 # Entorno virtual Python
├── iot-bridge.service    # Systemd service (bridge)
└── iot-dashboard.service # Systemd service (dashboard)
```

---

## ⚙️ Especificaciones Técnicas

### Hardware
- **Microcontrolador**: ESP32 Dev Module
- **Sensores**:
  - BME280: Temperatura, Humedad, Presión (I2C 0x76)
  - PMS5003: PM2.5 (UART2)
  - MQ-135: Gas (ADC GPIO 32)
- **Display**: OLED SSD1306 128x64 (I2C 0x3C)
- **Gateway**: Raspberry Pi (cualquier modelo con WiFi)

### Software
- **ESP32**: Arduino IDE, FreeRTOS
- **Raspberry Pi**: Raspberry Pi OS, Python 3.11+
- **Cloud**: Ubidots Industrial IoT
- **IA**: Google Gemini 2.0 Flash

### Protocolos
- **MQTT**: Mosquitto 2.0.21
- **HTTP**: Flask 3.0.3, Ubidots API
- **Base de Datos**: SQLite 3

---

## 🐛 Troubleshooting

### ESP32 no conecta a WiFi
```cpp
// Verificar en main_ch3.ino líneas 53-54
const char* WIFI_SSID = "TU_SSID";  // Exacto, case-sensitive
const char* WIFI_PASSWORD = "TU_PASSWORD";

// WiFi debe ser 2.4 GHz (ESP32 no soporta 5 GHz)
```

### bridge.py no recibe datos MQTT
```bash
# Verificar Mosquitto
sudo systemctl status mosquitto

# Probar MQTT manualmente
mosquitto_sub -t "sabana/aire/datos" -v

# Si no ves mensajes, verificar IP en ESP32 (línea 60)
```

### Predicciones IA no aparecen
```bash
# Verificar que hay suficientes lecturas
sqlite3 aire.db "SELECT COUNT(*) FROM lecturas;"
# Debe tener al menos 10

# Ver logs del bridge
sudo journalctl -u iot-bridge.service -n 50 | grep "IA"

# Verificar API key de Gemini en ia_module.py línea 27
```

### Dashboard Flask no carga
```bash
# Verificar servicio
sudo systemctl status iot-dashboard.service

# Verificar puerto 5000
sudo netstat -tulpn | grep 5000

# Reiniciar servicio
sudo systemctl restart iot-dashboard.service
```

---

## 📚 Documentación Adicional

- **Wiki del Proyecto**: [Enlace a GitHub Wiki]
- **Video Demo**: [Enlace a YouTube]
- **Ubidots Docs**: https://docs.ubidots.com/
- **Gemini AI**: https://ai.google.dev/

---

## 📝 Licencia

Proyecto académico - Universidad de la Sabana  
Challenge #3 - IoT 2026-1

---

## 🆘 Soporte

Para dudas o problemas, contactar al equipo del proyecto.

**Versión**: 1.0.0  
**Fecha**: Mayo 2026

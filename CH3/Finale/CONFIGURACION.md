# Guía de Configuración Rápida

## ⚙️ Credenciales a Configurar

### 1. Ubidots Token
1. Crear cuenta: https://industrial.ubidots.com/
2. Ir a: **Profile → API Credentials**
3. Copiar el **Default token** (empieza con `BBFF-`)
4. **Editar `bridge.py` línea 49:**
   ```python
   "ubidots_token": "BBFF-TU_TOKEN_AQUI",
   ```

### 2. Gemini AI API Key
1. Ir a: https://aistudio.google.com/app/apikey
2. Login con cuenta Google
3. Click **Create API key**
4. Copiar la key (empieza con `AIza`)
5. **Editar `ia_module.py` línea 27:**
   ```python
   GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "AIzaSy_TU_KEY_AQUI")
   ```

### 3. WiFi ESP32
**Editar `main_ch3.ino` líneas 53-54:**
```cpp
const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASSWORD = "TU_PASSWORD";
```
⚠️ **Importante**: WiFi debe ser **2.4 GHz** (ESP32 no soporta 5 GHz)

### 4. IP Raspberry Pi
1. Conectar Raspberry Pi a la red
2. Obtener IP: `hostname -I`
3. **Editar `main_ch3.ino` línea 60:**
   ```cpp
   const char* MQTT_BROKER = "192.168.X.X";  // Poner IP real de RPi
   ```

---

## 📋 Orden de Instalación

### ✅ Paso 1: Raspberry Pi
```bash
# 1. Instalar dependencias del sistema
sudo apt update
sudo apt install -y mosquitto mosquitto-clients python3-pip python3-venv

# 2. Crear directorio del proyecto
mkdir ~/iot-challenge3
cd ~/iot-challenge3

# 3. Copiar archivos Python
# - bridge.py
# - dashboard.py
# - ia_module.py
# - requirements.txt

# 4. Crear entorno virtual
python3 -m venv venv
source venv/bin/activate

# 5. Instalar dependencias
pip install -r requirements.txt
```

### ✅ Paso 2: Configurar Credenciales
1. Editar `bridge.py` → Token Ubidots (línea 49)
2. Editar `ia_module.py` → API Key Gemini (línea 27)

### ✅ Paso 3: Instalar Servicios Systemd
```bash
# 1. Copiar servicios
sudo cp iot-bridge.service /etc/systemd/system/
sudo cp iot-dashboard.service /etc/systemd/system/

# 2. Recargar systemd
sudo systemctl daemon-reload

# 3. Habilitar auto-inicio
sudo systemctl enable iot-bridge.service
sudo systemctl enable iot-dashboard.service

# 4. Iniciar servicios
sudo systemctl start iot-bridge.service
sudo systemctl start iot-dashboard.service
```

### ✅ Paso 4: ESP32
1. Abrir `main_ch3.ino` en Arduino IDE
2. Configurar WiFi (líneas 53-54)
3. Configurar IP Raspberry Pi (línea 60)
4. Seleccionar placa: **ESP32 Dev Module**
5. Click **Upload**

### ✅ Paso 5: Verificación Final
```bash
# Raspberry Pi - Ver servicios
sudo systemctl status iot-bridge.service
sudo systemctl status iot-dashboard.service

# ESP32 - Serial Monitor debe mostrar:
# ✓ WiFi conectado
# ✓ MQTT conectado
# ✓ Lecturas cada 2s

# Dashboard Web
# Abrir en navegador: http://IP_RASPBERRY:5000
# Login: sabana / sabana2026
```

---

## 🔍 Verificación de Componentes

### Mosquitto (Raspberry Pi)
```bash
sudo systemctl status mosquitto
# Debe mostrar: active (running)

# Probar suscripción MQTT
mosquitto_sub -t "sabana/aire/datos" -v
# Debe mostrar mensajes JSON del ESP32 cada 5s
```

### Base de Datos
```bash
cd ~/iot-challenge3
sqlite3 aire.db "SELECT COUNT(*) FROM lecturas;"
# Debe mostrar un número > 0 después de algunos minutos
```

### Ubidots
1. Login: https://industrial.ubidots.com/
2. Ir a **Devices**
3. Debe aparecer `esp32-aire` con 8 variables
4. Gráficos deben mostrar datos en tiempo real

### Gemini IA
```bash
cd ~/iot-challenge3
source venv/bin/activate
python3 ia_module.py
# Debe mostrar predicciones y recomendaciones
```

---

## ⚠️ Problemas Comunes

### ESP32 no conecta a WiFi
- ✅ Verificar SSID correcto (case-sensitive)
- ✅ WiFi debe ser **2.4 GHz**
- ✅ Password correcto
- ✅ Reiniciar ESP32

### Bridge no recibe datos MQTT
- ✅ Verificar Mosquitto: `sudo systemctl status mosquitto`
- ✅ Verificar IP en ESP32 (línea 60) coincide con RPi
- ✅ Ver logs: `sudo journalctl -u iot-bridge.service -f`

### Dashboard no carga
- ✅ Verificar servicio: `sudo systemctl status iot-dashboard.service`
- ✅ Verificar puerto: `sudo netstat -tulpn | grep 5000`
- ✅ Verificar firewall no bloquea puerto 5000

### Predicciones IA no aparecen
- ✅ Debe haber al menos 10 lecturas en la BD
- ✅ Verificar API key Gemini en `ia_module.py` línea 27
- ✅ Ver logs: `sudo journalctl -u iot-bridge.service | grep IA`

---

## 📞 Checklist Final

Antes de grabar el video:

- [ ] Raspberry Pi: Servicios corriendo (`systemctl status`)
- [ ] ESP32: Serial Monitor muestra datos cada 2s
- [ ] Base de datos: Al menos 50 lecturas
- [ ] Dashboard Flask: Accesible en navegador
- [ ] Ubidots: Variables recibiendo datos
- [ ] IA: Al menos 1 predicción generada
- [ ] LEDs funcionando (Verde/Amarillo/Rojo)
- [ ] OLED mostrando datos

---

**¡Listo para el Challenge! 🚀**

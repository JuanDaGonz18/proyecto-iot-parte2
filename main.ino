/*
 * UNIVERSIDAD DE LA SABANA - IoT CHALLENGE #2
 * Sistema de Monitoreo de Calidad del Aire
 * Con Servidor Web Embebido y Dashboard
 * 
 * VERSIÓN PARA ARDUINO IDE Y HARDWARE REAL
 * 
 * Hardware:
 * - ESP32
 * - BME280 (I2C: 0x76)
 * - MQ-135 (ADC: GPIO 32)
 * - PMS5003 (UART: GPIO 16/17)
 * - OLED SSD1306 (I2C: 0x3C)
 * - LEDs: Verde(26), Amarillo(27), Rojo(14)
 * - Buzzer: GPIO 25 (opcional)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ===== CONFIGURACIÓN WIFI =====
const char* ssid = "IoT_AirQuality";
const char* password = "sabana2026";

// ===== CREDENCIALES DASHBOARD =====
const char* auth_user = "admin";
const char* auth_pass = "sabana123";

// ===== PINES =====
#define LED_VERDE 26
#define LED_AMARILLO 27
#define LED_ROJO 14
#define BUZZER_PIN 25

#define SDA_PIN 21
#define SCL_PIN 22

#define MQ135_PIN 32
#define PMS_RX 16  // RX2 del ESP32
#define PMS_TX 17  // TX2 del ESP32

// ===== MQ-135 CONFIGURACIÓN =====
#define RLOAD 10.0           // Resistencia de carga (10kΩ)
#define R0 41.2              // Resistencia base del sensor
#define FACTOR_DIVISOR 3.0   // Factor del divisor de voltaje (20k + 10k)
#define PARA 116.6020682
#define PARB -2.769034857

// ===== SENSORES =====
Adafruit_BME280 bme;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WebServer server(80);
HardwareSerial pmsSerial(2); // UART2 para PMS5003

// ===== VARIABLES GLOBALES =====
float temperatura = 0.0;
float humedad = 0.0;
float presion = 0.0;
float pm25 = 0.0;
float pm10 = 0.0;
float gas_ppm = 0.0;
int adc_mq135 = 0;

String estado_calidad = "BUENA";
unsigned long ultimo_guardado = 0;
unsigned long tiempo_precalentamiento = 180000; // 3 minutos
unsigned long tiempo_inicio = 0;
bool precalentamiento_completo = false;

const unsigned long INTERVALO_GUARDADO = 60000; // 1 minuto

// ===== UMBRALES (Colombia 2026) =====
// PM2.5 (µg/m³)
#define PM25_BUENA_MAX 12.0
#define PM25_MODERADA_MAX 35.0

// Gases (ppm)
#define GAS_BUENA_MAX 800.0
#define GAS_MODERADA_MAX 1200.0

// Temperatura (°C)
#define TEMP_BUENA_MIN 18.0
#define TEMP_BUENA_MAX 24.0
#define TEMP_MODERADA_MIN 10.0
#define TEMP_MODERADA_MAX 32.0

// Humedad (%)
#define HUM_BUENA_MIN 40.0
#define HUM_BUENA_MAX 60.0
#define HUM_MODERADA_MIN 30.0
#define HUM_MODERADA_MAX 70.0

// Presión (hPa) - Bogotá 2600m
#define PRES_BUENA_MIN 745.0
#define PRES_BUENA_MAX 760.0
#define PRES_MODERADA_MIN 735.0
#define PRES_MODERADA_MAX 770.0

// ===== PROTOTIPOS =====
void taskLeerSensores(void *pvParameters);
void taskServidorWeb(void *pvParameters);
void taskFusionDatos(void *pvParameters);
void taskActualizarDisplay(void *pvParameters);
void leerBME280();
void leerMQ135();
void leerPMS5003();
void calcularCalidadAire();
void actualizarAlertas();
void guardarDatos();
String generarHTML();
String generarDashboardHTML();
String generarDatosJSON();
String generarHistoricoJSON();
void handleRoot();
void handleDashboard();
void handleAPI();
void handleHistorico();
void handleLogin();
bool verificarAutenticacion();

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  tiempo_inicio = millis();
  
  Serial.println("\n\n╔════════════════════════════════════════════════╗");
  Serial.println("║  SISTEMA IoT - CALIDAD DEL AIRE v2.0          ║");
  Serial.println("║  Universidad de la Sabana - Challenge #2      ║");
  Serial.println("║  Con Servidor Web y FreeRTOS                   ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  // Inicializar pines
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MQ135_PIN, INPUT);
  
  // Inicializar I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("✓ Bus I2C inicializado");
  
  // Inicializar BME280
  if (bme.begin(0x76, &Wire)) {
    Serial.println("✓ BME280 inicializado en 0x76");
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_0_5);
  } else if (bme.begin(0x77, &Wire)) {
    Serial.println("✓ BME280 inicializado en 0x77");
  } else {
    Serial.println("✗ Error BME280");
  }
  
  // Inicializar OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("✓ OLED inicializado");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Sistema IoT v2.0");
    display.println("Iniciando...");
    display.display();
  } else {
    Serial.println("✗ Error OLED");
  }
  
  // Inicializar PMS5003
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);
  Serial.println("✓ PMS5003 UART inicializado");
  
  // Inicializar SPIFFS
  if (SPIFFS.begin(true)) {
    Serial.println("✓ SPIFFS inicializado");
  } else {
    Serial.println("✗ Error SPIFFS");
  }
  
  // Configurar WiFi como Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.println("\n✓ WiFi Access Point configurado");
  Serial.print("  SSID: ");
  Serial.println(ssid);
  Serial.print("  Password: ");
  Serial.println(password);
  Serial.print("  IP: ");
  Serial.println(IP);
  Serial.println("\n  Accede al dashboard en: http://" + IP.toString());
  Serial.println("  Usuario: admin | Contraseña: sabana123");
  
  // Configurar servidor web
  server.on("/", handleRoot);
  server.on("/dashboard", handleDashboard);
  server.on("/api/datos", handleAPI);
  server.on("/api/historico", handleHistorico);
  server.on("/login", HTTP_POST, handleLogin);
  
  server.begin();
  Serial.println("✓ Servidor web iniciado\n");
  
  // Mensaje de precalentamiento MQ-135
  Serial.println("⏳ MQ-135 precalentando (3 minutos)...");
  Serial.println("   El sensor necesita estabilizarse antes de lecturas precisas.\n");
  
  // Crear tareas FreeRTOS
  xTaskCreatePinnedToCore(
    taskLeerSensores,
    "LeerSensores",
    4096,
    NULL,
    2,  // Prioridad alta
    NULL,
    0   // Core 0
  );
  
  xTaskCreatePinnedToCore(
    taskFusionDatos,
    "FusionDatos",
    4096,
    NULL,
    1,  // Prioridad media
    NULL,
    0   // Core 0
  );
  
  xTaskCreatePinnedToCore(
    taskActualizarDisplay,
    "ActualizarDisplay",
    4096,
    NULL,
    1,
    NULL,
    0   // Core 0
  );
  
  xTaskCreatePinnedToCore(
    taskServidorWeb,
    "ServidorWeb",
    8192,
    NULL,
    1,
    NULL,
    1   // Core 1
  );
  
  Serial.println("✓ Tareas FreeRTOS creadas");
  Serial.println("\n════════════════════════════════════════════════");
  Serial.println("Sistema listo. Monitoreando...\n");
}

// ===== LOOP (vacío, usamos FreeRTOS) =====
void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// ===== TAREA 1: LEER SENSORES =====
void taskLeerSensores(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 2000 / portTICK_PERIOD_MS; // 2 segundos
  
  while (1) {
    // Verificar precalentamiento
    if (!precalentamiento_completo && (millis() - tiempo_inicio >= tiempo_precalentamiento)) {
      precalentamiento_completo = true;
      Serial.println("\n✅ MQ-135 precalentamiento completo\n");
    }
    
    leerBME280();
    leerMQ135();
    leerPMS5003();
    
    // Guardar datos cada minuto
    if (millis() - ultimo_guardado > INTERVALO_GUARDADO) {
      guardarDatos();
      ultimo_guardado = millis();
    }
    
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ===== TAREA 2: FUSIÓN DE DATOS =====
void taskFusionDatos(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 3000 / portTICK_PERIOD_MS; // 3 segundos
  
  while (1) {
    calcularCalidadAire();
    actualizarAlertas();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ===== TAREA 3: ACTUALIZAR DISPLAY =====
void taskActualizarDisplay(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 1000 / portTICK_PERIOD_MS; // 1 segundo
  
  while (1) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    
    display.println("IoT Air Quality");
    display.println("---------------");
    display.print("T: ");
    display.print(temperatura, 1);
    display.println(" C");
    display.print("H: ");
    display.print(humedad, 0);
    display.println(" %");
    display.print("PM2.5: ");
    display.print(pm25, 0);
    display.println(" ug/m3");
    display.print("Gas: ");
    display.print(gas_ppm, 0);
    display.println(" ppm");
    
    display.setTextSize(2);
    display.setCursor(0, 50);
    if (estado_calidad.length() <= 8) {
      display.print(estado_calidad);
    } else {
      display.setTextSize(1);
      display.setCursor(0, 50);
      display.print(estado_calidad);
    }
    
    display.display();
    
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ===== TAREA 4: SERVIDOR WEB =====
void taskServidorWeb(void *pvParameters) {
  while (1) {
    server.handleClient();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ===== FUNCIÓN: LEER BME280 =====
void leerBME280() {
  temperatura = bme.readTemperature();
  humedad = bme.readHumidity();
  presion = bme.readPressure() / 100.0F;
  
  // Validar lecturas
  if (isnan(temperatura) || temperatura < -40 || temperatura > 85) {
    temperatura = 22.0; // Valor por defecto
  }
  if (isnan(humedad) || humedad < 0 || humedad > 100) {
    humedad = 50.0;
  }
  if (isnan(presion) || presion < 300 || presion > 1100) {
    presion = 753.0; // Bogotá
  }
}

// ===== FUNCIÓN: LEER MQ-135 =====
void leerMQ135() {
  adc_mq135 = analogRead(MQ135_PIN);
  
  // Calcular voltaje real considerando divisor
  float voltaje_divisor = (adc_mq135 / 4095.0) * 3.3;
  float voltaje_real = voltaje_divisor * FACTOR_DIVISOR;
  
  // Calcular resistencia del sensor
  float rs = ((5.0 * RLOAD) / voltaje_real) - RLOAD;
  
  // Calcular ratio
  float ratio = rs / R0;
  
  // Calcular PPM usando curva característica
  gas_ppm = PARA * pow(ratio, PARB);
  
  // Limitar a rango válido
  if (gas_ppm < 400) gas_ppm = 400;
  if (gas_ppm > 5000) gas_ppm = 5000;
  
  // Si está en precalentamiento, mostrar advertencia
  if (!precalentamiento_completo) {
    gas_ppm = 400; // Valor mínimo durante precalentamiento
  }
}

// ===== FUNCIÓN: LEER PMS5003 =====
void leerPMS5003() {
  // Estructura del frame PMS5003
  uint8_t buffer[32];
  int index = 0;
  bool frameStart = false;
  
  // Buscar inicio de frame (0x42 0x4D)
  while (pmsSerial.available() > 0) {
    uint8_t byte = pmsSerial.read();
    
    if (byte == 0x42 && !frameStart) {
      buffer[index++] = byte;
      frameStart = true;
    } else if (frameStart) {
      buffer[index++] = byte;
      
      if (index >= 32) {
        // Frame completo, extraer PM2.5 y PM10
        if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
          pm25 = (buffer[12] << 8) | buffer[13];
          pm10 = (buffer[14] << 8) | buffer[15];
        }
        index = 0;
        frameStart = false;
        break;
      }
    }
  }
  
  // Validar lecturas
  if (pm25 > 500) pm25 = 0;
  if (pm10 > 500) pm10 = 0;
}

// ===== FUNCIÓN: CALCULAR CALIDAD DEL AIRE =====
void calcularCalidadAire() {
  int parametros_malos = 0;
  int parametros_moderados = 0;
  
  // Evaluar PM2.5
  if (pm25 > PM25_MODERADA_MAX) parametros_malos++;
  else if (pm25 > PM25_BUENA_MAX) parametros_moderados++;
  
  // Evaluar Gases
  if (gas_ppm > GAS_MODERADA_MAX) parametros_malos++;
  else if (gas_ppm > GAS_BUENA_MAX) parametros_moderados++;
  
  // Evaluar Temperatura
  if (temperatura < TEMP_MODERADA_MIN || temperatura > TEMP_MODERADA_MAX) {
    parametros_malos++;
  } else if (temperatura < TEMP_BUENA_MIN || temperatura > TEMP_BUENA_MAX) {
    parametros_moderados++;
  }
  
  // Evaluar Humedad
  if (humedad < HUM_MODERADA_MIN || humedad > HUM_MODERADA_MAX) {
    parametros_malos++;
  } else if (humedad < HUM_BUENA_MIN || humedad > HUM_BUENA_MAX) {
    parametros_moderados++;
  }
  
  // Evaluar Presión
  if (presion < PRES_MODERADA_MIN || presion > PRES_MODERADA_MAX) {
    parametros_malos++;
  } else if (presion < PRES_BUENA_MIN || presion > PRES_BUENA_MAX) {
    parametros_moderados++;
  }
  
  // Clasificación
  if (parametros_malos >= 2) {
    estado_calidad = "MUY MALA";
  } else if (parametros_malos == 1 || parametros_moderados >= 3) {
    estado_calidad = "MALA";
  } else if (parametros_moderados >= 1) {
    estado_calidad = "MODERADA";
  } else {
    estado_calidad = "BUENA";
  }
}

// ===== FUNCIÓN: ACTUALIZAR ALERTAS =====
void actualizarAlertas() {
  if (estado_calidad == "BUENA") {
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_ROJO, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  } else if (estado_calidad == "MODERADA") {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, HIGH);
    digitalWrite(LED_ROJO, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  } else if (estado_calidad == "MALA") {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_ROJO, HIGH);
    
    // Buzzer intermitente
    static unsigned long ultimo_beep = 0;
    if (millis() - ultimo_beep > 1000) {
      digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      ultimo_beep = millis();
    }
  } else { // MUY MALA
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_ROJO, HIGH);
    
    // Buzzer patrón rápido
    static unsigned long ultimo_beep = 0;
    if (millis() - ultimo_beep > 500) {
      digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      ultimo_beep = millis();
    }
  }
}

// ===== FUNCIÓN: GUARDAR DATOS =====
void guardarDatos() {
  File file = SPIFFS.open("/historico.csv", FILE_APPEND);
  if (file) {
    file.print(millis());
    file.print(",");
    file.print(temperatura);
    file.print(",");
    file.print(humedad);
    file.print(",");
    file.print(presion);
    file.print(",");
    file.print(pm25);
    file.print(",");
    file.print(gas_ppm);
    file.print(",");
    file.println(estado_calidad);
    file.close();
  }
}

// ===== HANDLERS SERVIDOR WEB =====
void handleRoot() {
  String html = generarHTML();
  server.send(200, "text/html", html);
}

void handleDashboard() {
  if (!verificarAutenticacion()) {
    server.sendHeader("Location", "/login");
    server.send(303);
    return;
  }
  
  String html = generarDashboardHTML();
  server.send(200, "text/html", html);
}

void handleAPI() {
  String json = generarDatosJSON();
  server.send(200, "application/json", json);
}

void handleHistorico() {
  String json = generarHistoricoJSON();
  server.send(200, "application/json", json);
}

void handleLogin() {
  if (server.hasArg("user") && server.hasArg("pass")) {
    if (server.arg("user") == auth_user && server.arg("pass") == auth_pass) {
      server.sendHeader("Location", "/dashboard");
      server.sendHeader("Set-Cookie", "auth=ok");
      server.send(303);
      return;
    }
  }
  server.send(401, "text/plain", "Credenciales incorrectas");
}

bool verificarAutenticacion() {
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("auth=ok") != -1) {
      return true;
    }
  }
  return false;
}

// ===== FUNCIÓN: GENERAR HTML PRINCIPAL =====
String generarHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>IoT Air Quality</title>";
  html += "<style>";
  html += "body{font-family:Arial;margin:0;padding:20px;background:#f0f0f0;text-align:center}";
  html += ".container{max-width:600px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
  html += "h1{color:#2c3e50;margin-bottom:20px}";
  html += ".btn{display:inline-block;padding:15px 30px;margin:10px;background:#3498db;color:white;text-decoration:none;border-radius:5px;font-size:18px}";
  html += ".btn:hover{background:#2980b9}";
  html += ".info{margin:20px 0;padding:15px;background:#ecf0f1;border-radius:5px}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🌍 Sistema IoT - Calidad del Aire</h1>";
  html += "<div class='info'>";
  html += "<h2>Universidad de la Sabana</h2>";
  html += "<p>Challenge #2 - Monitoreo Ambiental</p>";
  html += "</div>";
  html += "<a href='/dashboard' class='btn'>📊 Acceder al Dashboard</a>";
  html += "<div class='info' style='margin-top:30px'>";
  html += "<p><strong>Estado Actual:</strong> " + estado_calidad + "</p>";
  html += "<p><strong>PM2.5:</strong> " + String(pm25, 1) + " µg/m³</p>";
  html += "<p><strong>Temperatura:</strong> " + String(temperatura, 1) + " °C</p>";
  html += "</div>";
  html += "<div style='margin-top:20px;font-size:12px;color:#7f8c8d'>";
  html += "<p>Usuario: admin | Contraseña: sabana123</p>";
  html += "</div>";
  html += "</div></body></html>";
  return html;
}

// ===== FUNCIÓN: GENERAR DASHBOARD HTML =====
String generarDashboardHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Dashboard - IoT Air Quality</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  html += "<style>";
  html += "* {margin:0;padding:0;box-sizing:border-box}";
  html += "body{font-family:Arial;background:#f5f5f5;padding:20px}";
  html += ".header{background:#2c3e50;color:white;padding:20px;border-radius:10px;margin-bottom:20px;text-align:center}";
  html += ".header h1{font-size:24px;margin-bottom:5px}";
  html += ".header p{font-size:14px;opacity:0.8}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-bottom:20px}";
  html += ".card{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}";
  html += ".card h3{color:#2c3e50;margin-bottom:15px;font-size:16px;text-transform:uppercase}";
  html += ".value{font-size:32px;font-weight:bold;color:#3498db;margin:10px 0}";
  html += ".unit{font-size:14px;color:#7f8c8d}";
  html += ".status{padding:10px;border-radius:5px;text-align:center;font-weight:bold;margin-top:10px}";
  html += ".status-buena{background:#2ecc71;color:white}";
  html += ".status-moderada{background:#f39c12;color:white}";
  html += ".status-mala{background:#e74c3c;color:white}";
  html += ".chart-container{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:20px}";
  html += ".timestamp{text-align:center;color:#7f8c8d;font-size:14px;margin-top:20px}";
  html += "</style></head><body>";
  
  html += "<div class='header'>";
  html += "<h1>🌍 Dashboard - Monitoreo de Calidad del Aire</h1>";
  html += "<p>Universidad de la Sabana | Challenge #2 IoT</p>";
  html += "</div>";
  
  html += "<div class='grid'>";
  
  // Cards
  html += "<div class='card'><h3>🌡️ Temperatura</h3><div class='value' id='temp'>" + String(temperatura, 1) + "</div><div class='unit'>°C</div></div>";
  html += "<div class='card'><h3>💧 Humedad</h3><div class='value' id='hum'>" + String(humedad, 0) + "</div><div class='unit'>%</div></div>";
  html += "<div class='card'><h3>🌫️ PM2.5</h3><div class='value' id='pm25'>" + String(pm25, 1) + "</div><div class='unit'>µg/m³</div></div>";
  html += "<div class='card'><h3>🏭 Gases</h3><div class='value' id='gas'>" + String(gas_ppm, 0) + "</div><div class='unit'>ppm</div></div>";
  html += "<div class='card'><h3>📊 Presión</h3><div class='value' id='pres'>" + String(presion, 1) + "</div><div class='unit'>hPa</div></div>";
  html += "<div class='card'><h3>✅ Estado General</h3><div class='status status-buena' id='estado'>" + estado_calidad + "</div></div>";
  
  html += "</div>";
  
  // Gráfica
  html += "<div class='chart-container'><canvas id='chart'></canvas></div>";
  html += "<div class='timestamp'>Última actualización: <span id='time'>--:--:--</span></div>";
  
  // JavaScript
  html += "<script>";
  html += "let chart;let datos={temp:[],hum:[],pm25:[],gas:[],labels:[]};";
  html += "function actualizarDatos(){fetch('/api/datos').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('temp').innerText=d.temperatura.toFixed(1);";
  html += "document.getElementById('hum').innerText=d.humedad.toFixed(0);";
  html += "document.getElementById('pm25').innerText=d.pm25.toFixed(1);";
  html += "document.getElementById('gas').innerText=d.gas.toFixed(0);";
  html += "document.getElementById('pres').innerText=d.presion.toFixed(1);";
  html += "document.getElementById('estado').innerText=d.estado;";
  html += "let estadoLower=d.estado.toLowerCase().replace(' ','');";
  html += "document.getElementById('estado').className='status status-'+estadoLower;";
  html += "let now=new Date();document.getElementById('time').innerText=now.toLocaleTimeString();";
  html += "datos.temp.push(d.temperatura);datos.hum.push(d.humedad);datos.pm25.push(d.pm25);datos.gas.push(d.gas);datos.labels.push(now.toLocaleTimeString());";
  html += "if(datos.temp.length>20){datos.temp.shift();datos.hum.shift();datos.pm25.shift();datos.gas.shift();datos.labels.shift();}";
  html += "if(chart)chart.update();});}";
  html += "window.onload=function(){let ctx=document.getElementById('chart').getContext('2d');";
  html += "chart=new Chart(ctx,{type:'line',data:{labels:datos.labels,datasets:[";
  html += "{label:'Temperatura (°C)',data:datos.temp,borderColor:'#e74c3c',fill:false},";
  html += "{label:'Humedad (%)',data:datos.hum,borderColor:'#3498db',fill:false},";
  html += "{label:'PM2.5 (µg/m³)',data:datos.pm25,borderColor:'#95a5a6',fill:false}";
  html += "]},options:{responsive:true,scales:{y:{beginAtZero:true}}}});";
  html += "actualizarDatos();setInterval(actualizarDatos,3000);};";
  html += "</script></body></html>";
  return html;
}

// ===== FUNCIÓN: GENERAR JSON DE DATOS ACTUALES =====
String generarDatosJSON() {
  StaticJsonDocument<256> doc;
  doc["temperatura"] = temperatura;
  doc["humedad"] = humedad;
  doc["presion"] = presion;
  doc["pm25"] = pm25;
  doc["gas"] = gas_ppm;
  doc["estado"] = estado_calidad;
  doc["timestamp"] = millis();
  
  String json;
  serializeJson(doc, json);
  return json;
}

// ===== FUNCIÓN: GENERAR JSON DE HISTÓRICO =====
String generarHistoricoJSON() {
  File file = SPIFFS.open("/historico.csv", FILE_READ);
  String json = "[";
  
  if (file) {
    int count = 0;
    while (file.available() && count < 50) {
      String line = file.readStringUntil('\n');
      if (line.length() > 0) {
        if (count > 0) json += ",";
        json += "{\"data\":\"" + line + "\"}";
        count++;
      }
    }
    file.close();
  }
  
  json += "]";
  return json;
}

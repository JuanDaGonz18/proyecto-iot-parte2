/*
 * ============================================================
 *  Sistema IoT — Monitoreo Calidad del Aire
 *  Universidad de la Sabana — IoT 2026-1 — Challenge #2
 *
 *  Integrantes:
 *    - Juan David González   (Hardware + Sensores + FreeRTOS)
 *    - Jorge Luis Alarcón    (WiFi + WebServer + Dashboard)
 *    - Jaime Andrés Olarte   (Fusión + Histórico + Wiki)
 *
 *  Arquitectura:
 *    - sensorTask (FreeRTOS, Core 0): lectura de PMS5003,
 *      BME280 y MQ-135 cada 2 s. Protege variables con mutex.
 *    - loop() (Core 1): lógica de fusión, alertas físicas,
 *      OLED y atención de solicitudes HTTP del WebServer.
 *
 *  Endpoints HTTP:
 *    GET  /             → Dashboard HTML completo
 *    GET  /datos        → JSON con estado actual + histórico
 *    POST /alarma/off   → Silencia el buzzer remotamente
 *
 *  Restricción de acceso:
 *    Solo dispositivos en la misma WLAN pueden alcanzar
 *    la IP del ESP32. No requiere Internet.
 * ============================================================
 */

// ============================================================
//  LIBRERÍAS
// ============================================================
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================
//  CONFIGURACIÓN WIFI — cambiar por los datos de la red local
// ============================================================
const char* WIFI_SSID     = "NOMBRE_DE_LA_RED";
const char* WIFI_PASSWORD = "CONTRASENA_WIFI";

// ============================================================
//  PINES
// ============================================================
#define MQ135_PIN      32
#define LED_VERDE      26
#define LED_AMARILLO   27
#define LED_ROJO       14
#define BUZZER         25
#define PMS_RX         16   // ESP32 RX2 ← PMS5003 TX
#define PMS_TX         17   // ESP32 TX2 → PMS5003 RX
#define I2C_SDA        21
#define I2C_SCL        22

// ============================================================
//  DISPLAY OLED
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================
//  SENSORES
// ============================================================
Adafruit_BME280  bme;
HardwareSerial   pmsSerial(2);

// ============================================================
//  UMBRALES DE CALIDAD DEL AIRE
//  Basados en OMS 2021 y Resolución 2254/2017 MADS Colombia
// ============================================================
#define PM25_BUENO      12.0f   // µg/m³
#define PM25_MODERADO   35.0f
#define PM25_MALO       55.0f

#define GAS_BUENO       70.0f   // ppm (escala aprox.)
#define GAS_MODERADO   150.0f
#define GAS_MALO       300.0f

// Umbrales meteorológicos para Chía (~2560 m s.n.m.)
#define TEMP_INVERSION  30.0f   // °C
#define HUM_INVERSION   40.0f   // %
#define PRESION_BAJA   730.0f   // hPa

// ============================================================
//  ESTADOS
// ============================================================
enum CalidadAire { BUENA, MODERADA, MALA, MUY_MALA };
const char* ESTADOS_STR[] = { "BUENA", "MODERADA", "MALA", "MUY MALA" };
const char* ESTADOS_COLOR[] = { "#2ecc71", "#f1c40f", "#e74c3c", "#c0392b" };

// ============================================================
//  VARIABLES GLOBALES COMPARTIDAS
//  Protegidas por datosMutex entre sensorTask y loop()
// ============================================================
volatile float      temperatura  = 25.0f;
volatile float      humedad      = 50.0f;
volatile float      presion      = 748.0f;
volatile float      pm25_ugm3   = 0.0f;
volatile float      gas_ppm     = 0.0f;
volatile bool       pms_ok      = false;
volatile CalidadAire estadoActual = BUENA;
volatile bool       alarmaActiva = true;

SemaphoreHandle_t   datosMutex;

// Estadísticas
volatile unsigned long lecturas_totales  = 0;
volatile unsigned long alertas_criticas  = 0;

// ============================================================
//  HISTÓRICO — array circular de 30 lecturas
// ============================================================
#define HISTORICO_SIZE 30

struct Lectura {
  unsigned long timestamp;  // segundos desde arranque
  float  pm25, gas, temp, hum, pres;
  CalidadAire estado;
};

Lectura historico[HISTORICO_SIZE];
int     historicoIdx   = 0;
int     historicoCount = 0;

// ============================================================
//  WEBSERVER
// ============================================================
WebServer server(80);

// ============================================================
//  PROTOTIPOS DE FUNCIONES
// ============================================================
void sensorTask(void* pvParameters);
void leerPMS5003();
void leerBME280();
void leerMQ135();
void evaluarCalidadAire();
void activarAlertas();
void actualizarDisplay();
void mostrarDatosSerial();
void guardarHistorico();
void testLEDs();

// WebServer handlers
void handleDashboard();
void handleDatos();
void handleAlarmaOff();
void handleNotFound();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔══════════════════════════════════╗");
  Serial.println("║  Sistema IoT — Calidad del Aire  ║");
  Serial.println("║  Universidad de la Sabana 2026-1 ║");
  Serial.println("╚══════════════════════════════════╝\n");

  // --- I2C y OLED ---
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED no detectado. Halting.");
    while (1);
  }
  Serial.println("✓ OLED OK");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Sistema IoT");
  display.println("Calidad del Aire");
  display.println("");
  display.println("Iniciando...");
  display.display();

  // --- BME280: esperar hasta que responda ---
  Serial.print("Buscando BME280");
  while (!bme.begin(0x76)) {
    Serial.print(".");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ERROR SENSOR");
    display.println("");
    display.println("No se detecta");
    display.println("BME/BMP280");
    display.println("");
    display.println("Reintentando...");
    display.display();
    delay(2000);
  }
  Serial.println("\n✓ BME280 OK");

  // --- PMS5003 (UART2) ---
  pmsSerial.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);
  Serial.println("✓ PMS5003 UART inicializado");

  // --- Pines actuadores ---
  pinMode(LED_VERDE,    OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO,     OUTPUT);
  pinMode(BUZZER,       OUTPUT);
  testLEDs();

  // --- Mutex FreeRTOS ---
  datosMutex = xSemaphoreCreateMutex();
  if (datosMutex == NULL) {
    Serial.println("❌ No se pudo crear el mutex. Halting.");
    while (1);
  }
  Serial.println("✓ Mutex FreeRTOS creado");

  // --- WiFi ---
  Serial.print("Conectando a WiFi: ");
  Serial.println(WIFI_SSID);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Conectando WiFi...");
  display.println(WIFI_SSID);
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi conectado");
    Serial.print("  IP del ESP32: ");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi conectado!");
    display.println("");
    display.println("Tablero en:");
    display.print("http://");
    display.println(WiFi.localIP());
    display.display();
    delay(3000);
  } else {
    Serial.println("\n⚠ WiFi no disponible. Modo sin tablero.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi no disp.");
    display.println("Solo modo local");
    display.display();
    delay(2000);
  }

  // --- Rutas del WebServer ---
  server.on("/",           HTTP_GET,  handleDashboard);
  server.on("/datos",      HTTP_GET,  handleDatos);
  server.on("/alarma/off", HTTP_POST, handleAlarmaOff);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("✓ WebServer HTTP iniciado en puerto 80");

  // --- Tarea FreeRTOS para sensores (Core 0) ---
  xTaskCreatePinnedToCore(
    sensorTask,    // función
    "sensorTask",  // nombre
    8192,          // stack en bytes
    NULL,          // parámetros
    2,             // prioridad (2 = mayor que loop)
    NULL,          // handle (no necesario)
    0              // Core 0 (loop corre en Core 1)
  );
  Serial.println("✓ sensorTask FreeRTOS creado en Core 0");
  Serial.println("\n✓ Sistema listo. Iniciando monitoreo...\n");
}

// ============================================================
//  LOOP PRINCIPAL (Core 1)
//  Se encarga de: lógica, alertas, OLED y HTTP
// ============================================================
void loop() {
  server.handleClient();  // atender solicitudes del dashboard

  evaluarCalidadAire();
  activarAlertas();
  actualizarDisplay();
  mostrarDatosSerial();

  lecturas_totales++;
  delay(2000);
}

// ============================================================
//  TAREA FREERTOS — ADQUISICIÓN DE SENSORES (Core 0)
//  Se ejecuta cada 2 segundos de forma independiente
// ============================================================
void sensorTask(void* pvParameters) {
  for (;;) {
    leerPMS5003();
    leerBME280();
    leerMQ135();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ============================================================
//  MÓDULO 1A — LEER PMS5003 (PM2.5 por UART)
//  Trama: 32 bytes, cabecera 0x42 0x4D
//  PM2.5 atmosférico: bytes [12] (high) y [13] (low)
// ============================================================
void leerPMS5003() {
  static uint8_t buffer[32];

  while (pmsSerial.available() >= 32) {
    if (pmsSerial.peek() == 0x42) {
      pmsSerial.readBytes(buffer, 32);

      if (buffer[0] == 0x42 && buffer[1] == 0x4D) {
        float valor = (float)(buffer[12] * 256 + buffer[13]);

        // Validación de rango físico (0–1000 µg/m³)
        if (valor >= 0.0f && valor <= 1000.0f) {
          if (xSemaphoreTake(datosMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            pm25_ugm3 = valor;
            pms_ok    = true;
            xSemaphoreGive(datosMutex);
          }
        }
        return;
      }
    } else {
      pmsSerial.read();  // descartar byte hasta alinear la trama
    }
  }

  // Sin datos en este ciclo
  if (xSemaphoreTake(datosMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    pms_ok = false;
    xSemaphoreGive(datosMutex);
  }
}

// ============================================================
//  MÓDULO 1B — LEER BME280 (temperatura, humedad, presión)
//  Dirección I2C: 0x76
//  Validación: descarta lecturas fuera de rango físico
// ============================================================
void leerBME280() {
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;

  // Rango válido: -40…85 °C, 300…1100 hPa
  if (t > -40.0f && t < 85.0f && p > 300.0f && p < 1100.0f) {
    if (xSemaphoreTake(datosMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      temperatura = t;
      presion     = p;
      humedad     = (!isnan(h) && h >= 0.0f && h <= 100.0f) ? h : -1.0f;
      xSemaphoreGive(datosMutex);
    }
  }
  // Si la lectura es inválida, se conserva el último valor válido
}

// ============================================================
//  MÓDULO 1C — LEER MQ-135 (gas, escala aproximada)
//  ADC 12 bits (0–4095) mapeado a 0–500 ppm
//  NOTA: escala de demostración, no calibrada con gas real
// ============================================================
void leerMQ135() {
  int   adc = analogRead(MQ135_PIN);
  float ppm = (float)map(adc, 0, 4095, 0, 500);

  if (xSemaphoreTake(datosMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    gas_ppm = ppm;
    xSemaphoreGive(datosMutex);
  }
}

// ============================================================
//  MÓDULO 2 — LÓGICA DE FUSIÓN DE SENSORES
//  Combina PM2.5, gas y condiciones meteorológicas para
//  clasificar la calidad del aire en 4 niveles.
//  También guarda la lectura en el histórico.
// ============================================================
void evaluarCalidadAire() {
  // Leer copia local de las variables protegidas
  float  _pm25, _gas, _temp, _hum, _pres;

  if (xSemaphoreTake(datosMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    _pm25 = pm25_ugm3;
    _gas  = gas_ppm;
    _temp = temperatura;
    _hum  = humedad;
    _pres = presion;
    xSemaphoreGive(datosMutex);
  } else {
    return;  // si no pudo tomar el mutex, saltar este ciclo
  }

  // --- Niveles de PM2.5 ---
  bool pm25_critico  = (_pm25 > PM25_MALO);
  bool pm25_moderado = (_pm25 > PM25_MODERADO && _pm25 <= PM25_MALO);
  bool pm25_leve     = (_pm25 > PM25_BUENO    && _pm25 <= PM25_MODERADO);

  // --- Niveles de gas ---
  bool gas_critico   = (_gas > GAS_MALO);
  bool gas_moderado  = (_gas > GAS_MODERADO && _gas <= GAS_MALO);
  bool gas_leve      = (_gas > GAS_BUENO    && _gas <= GAS_MODERADO);

  // --- Condiciones meteorológicas desfavorables ---
  // Inversión térmica: aire caliente + baja humedad → contaminantes atrapados
  // Presión baja: aire estancado (umbral ajustado para 2560 m s.n.m.)
  bool inversion_termica    = (_temp > TEMP_INVERSION && _hum < HUM_INVERSION);
  bool presion_baja_local   = (_pres < PRESION_BAJA);
  bool condicion_desfavorable = inversion_termica || presion_baja_local;

  // --- Algoritmo de fusión ---
  CalidadAire nuevo;

  if (pm25_critico || gas_critico) {
    // Nivel 4: contaminante crítico en cualquier sensor
    nuevo = MUY_MALA;
    alertas_criticas++;
  } else if ((pm25_moderado && gas_leve)  ||
             (pm25_leve     && gas_moderado) ||
             (pm25_moderado && condicion_desfavorable) ||
             (gas_moderado  && condicion_desfavorable)) {
    // Nivel 3: múltiples factores adversos combinados
    nuevo = MALA;
  } else if (pm25_leve || gas_leve || condicion_desfavorable) {
    // Nivel 2: un factor adverso leve
    nuevo = MODERADA;
  } else {
    // Nivel 1: todos los parámetros dentro de rango normal
    nuevo = BUENA;
  }

  estadoActual = nuevo;
  guardarHistorico();
}

// ============================================================
//  MÓDULO 3 — ALERTAS FÍSICAS
//  LEDs y buzzer según nivel de calidad.
//  Si alarmaActiva == false, el buzzer se silencia
//  (puede desactivarse remotamente desde el dashboard).
// ============================================================
void activarAlertas() {
  digitalWrite(LED_VERDE,    LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO,     LOW);
  digitalWrite(BUZZER,       LOW);

  switch (estadoActual) {
    case BUENA:
      digitalWrite(LED_VERDE, HIGH);
      break;

    case MODERADA:
      digitalWrite(LED_AMARILLO, HIGH);
      break;

    case MALA:
      digitalWrite(LED_ROJO, HIGH);
      if (alarmaActiva) {
        digitalWrite(BUZZER, HIGH); delay(100);
        digitalWrite(BUZZER, LOW);
      }
      break;

    case MUY_MALA:
      digitalWrite(LED_ROJO, HIGH);
      if (alarmaActiva) {
        // Patrón doble beep distinguible del nivel MALA
        digitalWrite(BUZZER, HIGH); delay(200);
        digitalWrite(BUZZER, LOW);  delay(100);
        digitalWrite(BUZZER, HIGH); delay(200);
        digitalWrite(BUZZER, LOW);
      }
      break;
  }
}

// ============================================================
//  MÓDULO 4 — PANTALLA OLED
//  Muestra todas las variables en tiempo real.
//  Indicador visual del estado en la esquina inferior derecha.
// ============================================================
void actualizarDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Encabezado
  display.setCursor(0, 0);
  display.println("CALIDAD DEL AIRE");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // IP del ESP32 (útil para acceder al dashboard)
  display.setCursor(0, 13);
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
  } else {
    display.print("Sin WiFi");
  }

  // Valores de sensores
  display.setCursor(0, 24);
  display.print("PM2.5: ");
  if (pms_ok) { display.print(pm25_ugm3, 0); display.print(" ug"); }
  else          { display.print("-- (calent.)"); }

  display.setCursor(0, 34);
  display.print("Gas:   "); display.print(gas_ppm, 0); display.print(" ppm");

  display.setCursor(0, 44);
  display.print("T:"); display.print(temperatura, 1);
  display.print(" H:");
  if (humedad < 0) display.print("--");
  else              display.print(humedad, 0);
  display.print("%");

  display.setCursor(0, 54);
  display.print("P:"); display.print(presion, 0); display.print("hPa");

  // Estado con indicador gráfico
  display.setCursor(70, 54);
  display.print(ESTADOS_STR[estadoActual]);

  // Indicador visual (círculo para BUENA, triángulo para alertas)
  if (estadoActual == BUENA) {
    display.drawCircle(122, 57, 4, SSD1306_WHITE);
  } else {
    display.fillTriangle(118, 62, 126, 62, 122, 54, SSD1306_WHITE);
  }

  display.display();
}

// ============================================================
//  GUARDAR HISTÓRICO
//  Array circular de HISTORICO_SIZE entradas.
//  Se llama desde evaluarCalidadAire() en cada ciclo.
// ============================================================
void guardarHistorico() {
  historico[historicoIdx] = {
    millis() / 1000UL,
    pm25_ugm3, gas_ppm, temperatura, humedad, presion,
    estadoActual
  };
  historicoIdx = (historicoIdx + 1) % HISTORICO_SIZE;
  if (historicoCount < HISTORICO_SIZE) historicoCount++;
}

// ============================================================
//  SERIAL — Diagnóstico completo cada ciclo
// ============================================================
void mostrarDatosSerial() {
  Serial.println("╔════════════════════════════════════════════╗");
  Serial.printf( "║  Lectura #%lu | Alertas críticas: %lu\n",
                 lecturas_totales, alertas_criticas);
  Serial.println("╠════════════════════════════════════════════╣");
  Serial.println("║ CONTAMINANTES:");
  Serial.printf( "║  PM2.5: %.1f µg/m³ %s\n", pm25_ugm3,
                 pms_ok ? "" : "[sin datos - calentando]");
  Serial.printf( "║  Gas:   %.1f ppm\n", gas_ppm);
  Serial.println("║ METEOROLOGÍA:");
  Serial.printf( "║  Temp:  %.1f °C\n", temperatura);
  Serial.printf( "║  Hum:   %.1f %%\n", humedad);
  Serial.printf( "║  Pres:  %.1f hPa\n", presion);
  Serial.println("║ RED:");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("║  IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("║  WiFi desconectado");
  }
  Serial.printf( "║ ESTADO: %s | Alarma: %s\n",
                 ESTADOS_STR[estadoActual],
                 alarmaActiva ? "ACTIVA" : "SILENCIADA");
  Serial.println("╚════════════════════════════════════════════╝\n");
}

// ============================================================
//  TEST DE LEDS Y BUZZER AL ARRANQUE
// ============================================================
void testLEDs() {
  Serial.println("  → Test de actuadores...");
  const int pines[] = { LED_VERDE, LED_AMARILLO, LED_ROJO };
  for (int p : pines) {
    digitalWrite(p, HIGH); delay(250); digitalWrite(p, LOW);
  }
  digitalWrite(BUZZER, HIGH); delay(150); digitalWrite(BUZZER, LOW);
  Serial.println("  ✓ Test completado");
}

// ============================================================
//  WEBSERVER — GET /
//  Sirve el dashboard HTML completo.
//  Usa sendContent() para no saturar la RAM del ESP32.
// ============================================================
void handleDashboard() {
  String ip = WiFi.localIP().toString();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  // HEAD
  server.sendContent(
    "<!DOCTYPE html><html lang='es'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Calidad del Aire — Sabana Centro</title>"
    "<style>"
    "  *{box-sizing:border-box;margin:0;padding:0}"
    "  body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:16px}"
    "  h1{font-size:1.2rem;font-weight:600;margin-bottom:4px}"
    "  .sub{font-size:.75rem;color:#94a3b8;margin-bottom:20px}"
    "  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:20px}"
    "  .card{background:#1e293b;border-radius:10px;padding:14px}"
    "  .card .label{font-size:.7rem;color:#64748b;text-transform:uppercase;letter-spacing:.05em}"
    "  .card .val{font-size:1.6rem;font-weight:700;margin:4px 0 2px}"
    "  .card .unit{font-size:.75rem;color:#94a3b8}"
    "  .estado-badge{display:inline-block;padding:6px 16px;border-radius:20px;font-weight:700;font-size:.9rem;margin-bottom:20px}"
    "  .BUENA{background:#064e3b;color:#6ee7b7}"
    "  .MODERADA{background:#78350f;color:#fde68a}"
    "  .MALA{background:#7f1d1d;color:#fca5a5}"
    "  .MUY{background:#581c87;color:#e9d5ff}"
    "  .btn{background:#3b82f6;color:#fff;border:none;padding:10px 22px;border-radius:8px;"
    "       font-size:.9rem;cursor:pointer;margin-bottom:20px}"
    "  .btn:hover{background:#2563eb}"
    "  table{width:100%;border-collapse:collapse;font-size:.78rem}"
    "  th{background:#1e293b;padding:8px;text-align:left;color:#64748b;font-weight:500}"
    "  td{padding:7px 8px;border-bottom:1px solid #1e293b}"
    "  tr:nth-child(even) td{background:#0f172a}"
    "  .ok{color:#6ee7b7} .warn{color:#fde68a} .bad{color:#fca5a5} .crit{color:#e879f9}"
    "  .ip{font-size:.7rem;color:#475569;margin-bottom:16px}"
    "</style></head><body>"
  );

  // HEADER
  server.sendContent(
    "<h1>Sistema IoT — Calidad del Aire</h1>"
    "<p class='sub'>Universidad de la Sabana · Chía, Cundinamarca · 2026-1</p>"
    "<p class='ip'>Servidor: http://"
  );
  server.sendContent(ip);
  server.sendContent(
    " &nbsp;|&nbsp; Actualización automática cada 3 s</p>"
  );

  // ESTADO Y BOTÓN
  server.sendContent(
    "<div id='badge' class='estado-badge'>Cargando...</div><br>"
    "<button class='btn' onclick='apagarAlarma()'>Silenciar alarma física</button>"
  );

  // TARJETAS DE VALORES ACTUALES
  server.sendContent(
    "<div class='grid'>"
    "  <div class='card'><div class='label'>PM2.5</div>"
    "    <div class='val' id='pm25'>--</div><div class='unit'>µg/m³</div></div>"
    "  <div class='card'><div class='label'>Gas</div>"
    "    <div class='val' id='gas'>--</div><div class='unit'>ppm</div></div>"
    "  <div class='card'><div class='label'>Temperatura</div>"
    "    <div class='val' id='temp'>--</div><div class='unit'>°C</div></div>"
    "  <div class='card'><div class='label'>Humedad</div>"
    "    <div class='val' id='hum'>--</div><div class='unit'>%</div></div>"
    "  <div class='card'><div class='label'>Presión</div>"
    "    <div class='val' id='pres'>--</div><div class='unit'>hPa</div></div>"
    "</div>"
  );

  // TABLA HISTÓRICO
  server.sendContent(
    "<h2 style='font-size:.9rem;color:#94a3b8;margin-bottom:10px'>"
    "Histórico reciente (últimas 30 lecturas)</h2>"
    "<div style='overflow-x:auto'>"
    "<table><thead><tr>"
    "  <th>Tiempo (s)</th><th>PM2.5</th><th>Gas</th>"
    "  <th>Temp</th><th>Hum</th><th>Presión</th><th>Estado</th>"
    "</tr></thead>"
    "<tbody id='hist'><tr><td colspan='7'>Cargando...</td></tr></tbody>"
    "</table></div>"
  );

  // JAVASCRIPT — fetch /datos cada 3 s
  server.sendContent(
    "<script>"
    "const COLORES={'BUENA':'ok','MODERADA':'warn','MALA':'bad','MUY MALA':'crit'};"
    "const CLASES={'BUENA':'BUENA','MODERADA':'MODERADA','MALA':'MALA','MUY MALA':'MUY'};"

    "function actualizar(){"
    "  fetch('/datos').then(r=>r.json()).then(d=>{"
    "    document.getElementById('pm25').textContent = d.pms_ok ? d.pm25.toFixed(1) : '--';"
    "    document.getElementById('gas').textContent  = d.gas.toFixed(1);"
    "    document.getElementById('temp').textContent = d.temp.toFixed(1);"
    "    document.getElementById('hum').textContent  = d.hum >= 0 ? d.hum.toFixed(1) : '--';"
    "    document.getElementById('pres').textContent = d.pres.toFixed(1);"
    "    const b = document.getElementById('badge');"
    "    b.textContent = d.estado;"
    "    b.className = 'estado-badge ' + (CLASES[d.estado]||'');"
    "    let rows = '';"
    "    d.historico.forEach(h=>{"
    "      const cl = COLORES[h.estado]||'';"
    "      rows += `<tr>"
    "        <td>${h.t}</td>"
    "        <td>${h.pm25.toFixed(1)}</td>"
    "        <td>${h.gas.toFixed(1)}</td>"
    "        <td>${h.temp.toFixed(1)}</td>"
    "        <td>${h.hum>=0?h.hum.toFixed(1):'--'}</td>"
    "        <td>${h.pres.toFixed(1)}</td>"
    "        <td class='${cl}'>${h.estado}</td>"
    "      </tr>`;"
    "    });"
    "    document.getElementById('hist').innerHTML = rows||'<tr><td colspan=7>Sin datos aún</td></tr>';"
    "  }).catch(e=>console.error('Error fetch:',e));"
    "}"

    "function apagarAlarma(){"
    "  fetch('/alarma/off',{method:'POST'})"
    "    .then(()=>alert('Alarma silenciada desde el tablero.'))"
    "    .catch(e=>console.error(e));"
    "}"

    "actualizar();"
    "setInterval(actualizar, 3000);"
    "</script></body></html>"
  );

  server.sendContent("");  // finalizar la respuesta chunked
}

// ============================================================
//  WEBSERVER — GET /datos
//  Retorna JSON con estado actual + histórico completo.
//  El dashboard hace fetch a este endpoint cada 3 segundos.
// ============================================================
void handleDatos() {
  // Cabeceras CORS para facilitar pruebas desde el navegador
  server.sendHeader("Access-Control-Allow-Origin", "*");

  String json = "{";
  json += "\"pm25\":"   + String(pm25_ugm3, 2) + ",";
  json += "\"gas\":"    + String(gas_ppm,   2) + ",";
  json += "\"temp\":"   + String(temperatura, 2) + ",";
  json += "\"hum\":"    + String(humedad,   2) + ",";
  json += "\"pres\":"   + String(presion,   2) + ",";
  json += "\"pms_ok\":" + String(pms_ok ? "true" : "false") + ",";
  json += "\"estado\":\"" + String(ESTADOS_STR[estadoActual]) + "\",";
  json += "\"alarmaActiva\":" + String(alarmaActiva ? "true" : "false") + ",";
  json += "\"lecturas\":" + String(lecturas_totales) + ",";

  // Histórico: recorrer el array circular en orden cronológico
  json += "\"historico\":[";
  int start = (historicoCount < HISTORICO_SIZE)
              ? 0
              : historicoIdx;  // entrada más antigua

  for (int i = 0; i < historicoCount; i++) {
    int idx = (start + i) % HISTORICO_SIZE;
    Lectura& l = historico[idx];
    if (i > 0) json += ",";
    json += "{";
    json += "\"t\":"     + String(l.timestamp) + ",";
    json += "\"pm25\":"  + String(l.pm25, 2) + ",";
    json += "\"gas\":"   + String(l.gas,  2) + ",";
    json += "\"temp\":"  + String(l.temp, 2) + ",";
    json += "\"hum\":"   + String(l.hum,  2) + ",";
    json += "\"pres\":"  + String(l.pres, 2) + ",";
    json += "\"estado\":\"" + String(ESTADOS_STR[l.estado]) + "\"";
    json += "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

// ============================================================
//  WEBSERVER — POST /alarma/off
//  Silencia el buzzer físico desde el dashboard.
//  La bandera alarmaActiva se puede reactivar reiniciando
//  el ESP32 o implementando un endpoint /alarma/on.
// ============================================================
void handleAlarmaOff() {
  alarmaActiva = false;
  digitalWrite(BUZZER, LOW);
  Serial.println("⚠ Alarma silenciada remotamente desde el tablero.");
  server.send(200, "application/json", "{\"ok\":true,\"mensaje\":\"Alarma silenciada\"}");
}

// ============================================================
//  WEBSERVER — 404
// ============================================================
void handleNotFound() {
  server.send(404, "text/plain", "Ruta no encontrada. Accede a http://" +
              WiFi.localIP().toString() + "/");
}

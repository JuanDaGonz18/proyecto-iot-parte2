#!/usr/bin/env python3
"""
============================================================
 Bridge IoT — MQTT → SQLite → Ubidots
 Universidad de la Sabana · Challenge #2 · 2026-1

 Flujo:
   ESP32 publica JSON → Mosquitto (RPi)
       → este script suscribe → guarda en SQLite
       → reenvía a Ubidots via HTTP API

 Topic escuchado : sabana/aire/datos
 Topic estado    : sabana/aire/estado (ignorado, info en JSON)

 Uso:
   pip install paho-mqtt requests
   python3 bridge.py

 Variables de entorno opcionales (o editar CONF abajo):
   MQTT_HOST, MQTT_PORT, UBIDOTS_TOKEN, UBIDOTS_DEVICE
============================================================
"""

import os
import json
import time
import logging
import sqlite3
import threading
import requests
import paho.mqtt.client as mqtt
from datetime import datetime

# ============================================================
#  CONFIGURACIÓN — editar o usar variables de entorno
# ============================================================
CONF = {
    # MQTT (Mosquitto en la RPi — usar 127.0.0.1 si corre aquí mismo)
    "mqtt_host"       : os.getenv("MQTT_HOST",        "127.0.0.1"),
    "mqtt_port"       : int(os.getenv("MQTT_PORT",    "1883")),
    "mqtt_topic"      : "sabana/aire/datos",
    "mqtt_client_id"  : "rpi-bridge",

    # SQLite
    "db_path"         : "aire.db",

    # Ubidots
    "ubidots_token"   : os.getenv("UBIDOTS_TOKEN",    "REEMPLAZA_CON_TU_TOKEN"),
    "ubidots_device"  : os.getenv("UBIDOTS_DEVICE",   "esp32-aire"),
    "ubidots_url"     : "https://industrial.api.ubidots.com/api/v1.6/devices/{device}/",

    # Reenvío a Ubidots: acumula lecturas y envía en lote cada N segundos
    # (evita saturar la API con una petición por mensaje)
    "ubidots_intervalo_s" : 10,
}

# ============================================================
#  LOGGING
# ============================================================
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("bridge")

# ============================================================
#  BASE DE DATOS SQLite
# ============================================================
def db_init(path: str) -> sqlite3.Connection:
    """Crea la tabla si no existe y devuelve la conexión."""
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS lecturas (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            ts_unix   INTEGER NOT NULL,          -- epoch segundos (del ESP32)
            ts_local  TEXT    NOT NULL,          -- ISO-8601 hora RPi
            pm25      REAL,
            gas       REAL,
            temp      REAL,
            hum       REAL,
            pres      REAL,
            pms_ok    INTEGER,                   -- 0/1
            estado    TEXT,
            alarma    INTEGER,                   -- 0/1
            lecturas  INTEGER
        )
    """)
    conn.commit()
    log.info("SQLite listo → %s", path)
    return conn


def db_guardar(conn: sqlite3.Connection, data: dict) -> int:
    """Inserta una fila y devuelve el id asignado."""
    ts_unix  = int(time.time())
    ts_local = datetime.now().isoformat(timespec="seconds")
    cur = conn.execute("""
        INSERT INTO lecturas
          (ts_unix, ts_local, pm25, gas, temp, hum, pres, pms_ok, estado, alarma, lecturas)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        ts_unix,
        ts_local,
        data.get("pm25"),
        data.get("gas"),
        data.get("temp"),
        data.get("hum"),
        data.get("pres"),
        1 if data.get("pms_ok") else 0,
        data.get("estado"),
        1 if data.get("alarma") else 0,
        data.get("lecturas"),
    ))
    conn.commit()
    return cur.lastrowid

# ============================================================
#  UBIDOTS
# ============================================================
# Cola thread-safe de payloads pendientes
_ubidots_queue: list[dict] = []
_queue_lock = threading.Lock()


def ubidots_payload_desde(data: dict) -> dict:
    """
    Convierte el JSON del ESP32 al formato de Ubidots:
    { "variable": {"value": X, "timestamp": epoch_ms} }
    """
    ts_ms = int(time.time() * 1000)

    payload = {}
    for campo, var in [
        ("pm25", "pm25"),
        ("gas",  "gas_ppm"),
        ("temp", "temperatura"),
        ("hum",  "humedad"),
        ("pres", "presion"),
    ]:
        val = data.get(campo)
        if val is not None:
            payload[var] = {"value": val, "timestamp": ts_ms}

    # Variables de estado como numéricas (Ubidots no acepta strings en /value)
    estados_num = {"BUENA": 0, "MODERADA": 1, "MALA": 2, "MUY MALA": 3}
    estado_str  = data.get("estado", "BUENA")
    payload["estado_num"] = {
        "value"    : estados_num.get(estado_str, 0),
        "timestamp": ts_ms,
        "context"  : {"estado": estado_str},
    }
    payload["alarma"] = {
        "value"    : 1 if data.get("alarma") else 0,
        "timestamp": ts_ms,
    }
    payload["pms_ok"] = {
        "value"    : 1 if data.get("pms_ok") else 0,
        "timestamp": ts_ms,
    }

    return payload


def ubidots_enviar(payload_acumulado: dict) -> bool:
    """
    POST a Ubidots. Devuelve True si fue exitoso.
    Ubidots acepta múltiples variables en un solo POST.
    """
    url     = CONF["ubidots_url"].format(device=CONF["ubidots_device"])
    headers = {
        "X-Auth-Token": CONF["ubidots_token"],
        "Content-Type": "application/json",
    }
    try:
        r = requests.post(url, json=payload_acumulado, headers=headers, timeout=10)
        if r.status_code in (200, 201):
            log.info("Ubidots ✓ → %d variables enviadas", len(payload_acumulado))
            return True
        else:
            log.warning("Ubidots ✗ HTTP %d: %s", r.status_code, r.text[:120])
            return False
    except requests.RequestException as e:
        log.error("Ubidots error de red: %s", e)
        return False


def hilo_ubidots():
    """
    Hilo separado: cada UBIDOTS_INTERVALO_S segundos toma los
    payloads acumulados, los fusiona (last-wins por variable) y
    hace un único POST a Ubidots.
    """
    while True:
        time.sleep(CONF["ubidots_intervalo_s"])
        with _queue_lock:
            cola_local = _ubidots_queue.copy()
            _ubidots_queue.clear()

        if not cola_local:
            continue

        # Fusionar: la lectura más reciente gana por variable
        fusionado: dict = {}
        for p in cola_local:
            fusionado.update(p)

        ubidots_enviar(fusionado)

# ============================================================
#  MQTT — CALLBACKS
# ============================================================
_db_conn: sqlite3.Connection | None = None


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        log.info("MQTT conectado a %s:%d", CONF["mqtt_host"], CONF["mqtt_port"])
        client.subscribe(CONF["mqtt_topic"])
        log.info("Suscrito a '%s'", CONF["mqtt_topic"])
    else:
        log.error("MQTT conexión rechazada — rc=%d", rc)


def on_disconnect(client, userdata, rc, properties=None, reasonCode=None):
    log.warning("MQTT desconectado (rc=%d). Reconectando automáticamente...", rc)


def on_message(client, userdata, msg):
    """Callback al recibir un mensaje MQTT."""
    global _db_conn
    try:
        raw  = msg.payload.decode("utf-8")
        data = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        log.warning("Payload inválido ignorado: %s", e)
        return

    # 1. Guardar en SQLite
    try:
        row_id = db_guardar(_db_conn, data)
        log.info(
            "DB ✓ fila #%d | PM2.5=%.1f gas=%.1f T=%.1f H=%.1f P=%.1f [%s]",
            row_id,
            data.get("pm25", 0),
            data.get("gas",  0),
            data.get("temp", 0),
            data.get("hum",  0),
            data.get("pres", 0),
            data.get("estado", "?"),
        )
    except sqlite3.Error as e:
        log.error("SQLite error: %s", e)

    # 2. Encolar para Ubidots
    payload = ubidots_payload_desde(data)
    with _queue_lock:
        _ubidots_queue.append(payload)

# ============================================================
#  MAIN
# ============================================================
def main():
    global _db_conn

    log.info("=== Bridge MQTT → SQLite → Ubidots arrancando ===")

    # Inicializar base de datos
    _db_conn = db_init(CONF["db_path"])

    # Hilo de reenvío a Ubidots
    t = threading.Thread(target=hilo_ubidots, daemon=True, name="ubidots-sender")
    t.start()
    log.info("Hilo Ubidots iniciado (intervalo=%ds)", CONF["ubidots_intervalo_s"])

    # Cliente MQTT
    client = mqtt.Client(
        client_id=CONF["mqtt_client_id"],
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    )
    client.on_connect    = on_connect
    client.on_disconnect = on_disconnect
    client.on_message    = on_message

    # Reconexión automática
    client.reconnect_delay_set(min_delay=1, max_delay=30)

    try:
        try:
            client.connect(CONF["mqtt_host"], CONF["mqtt_port"], keepalive=60)
        except (ConnectionRefusedError, TimeoutError, OSError) as e:
            log.error("Error al conectar al broker MQTT en %s:%d - %s", CONF["mqtt_host"], CONF["mqtt_port"], e)
            log.info("Por favor, asegúrate de que Mosquitto esté corriendo o ajusta MQTT_HOST.")
            return

        client.loop_forever()   # bloqueante; Ctrl+C para salir
    except KeyboardInterrupt:
        log.info("Interrupción manual. Cerrando...")
    finally:
        client.disconnect()
        if _db_conn:
            _db_conn.close()
        log.info("Bridge detenido.")


if __name__ == "__main__":
    main()

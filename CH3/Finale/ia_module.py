#!/usr/bin/env python3
"""
============================================================
 Módulo de IA — Predicciones y Recomendaciones
 Universidad de la Sabana · Challenge #3 · 2026-1

 Integra Gemini AI para:
   - Predecir calidad del aire próximas 6 horas
   - Generar recomendaciones para autoridades
   - Analizar tendencias de datos históricos

 Uso:
   pip install google-genai
   export GEMINI_API_KEY="tu_api_key"
   python3 ia_module.py
============================================================
"""

import os
import json
import sqlite3
from datetime import datetime, timedelta
from google import genai

# ============================================================
#  CONFIGURACIÓN
# ============================================================
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY", "REEMPLAZA_CON_TU_KEY")
DB_PATH = "aire.db"

# Configurar Gemini con nueva API
client = genai.Client(api_key=GEMINI_API_KEY)

# ============================================================
#  FUNCIONES DE BASE DE DATOS
# ============================================================
def obtener_ultimas_lecturas(horas=24, limite=100):
    """Obtiene las últimas N lecturas de las últimas X horas."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    
    hace_n_horas = int((datetime.now() - timedelta(hours=horas)).timestamp())
    
    query = """
        SELECT ts_unix, pm25, gas, temp, hum, pres, estado
        FROM lecturas
        WHERE ts_unix >= ?
        ORDER BY ts_unix DESC
        LIMIT ?
    """
    
    filas = conn.execute(query, (hace_n_horas, limite)).fetchall()
    conn.close()
    
    return [dict(f) for f in filas]

def calcular_estadisticas(lecturas):
    """Calcula estadísticas básicas de las lecturas."""
    if not lecturas:
        return {}
    
    pm25_vals = [l['pm25'] for l in lecturas if l['pm25'] is not None]
    gas_vals = [l['gas'] for l in lecturas if l['gas'] is not None]
    
    return {
        "total_lecturas": len(lecturas),
        "pm25_promedio": sum(pm25_vals) / len(pm25_vals) if pm25_vals else 0,
        "pm25_max": max(pm25_vals) if pm25_vals else 0,
        "pm25_min": min(pm25_vals) if pm25_vals else 0,
        "gas_promedio": sum(gas_vals) / len(gas_vals) if gas_vals else 0,
        "estado_predominante": max(set([l['estado'] for l in lecturas]), 
                                   key=[l['estado'] for l in lecturas].count),
        "alertas_criticas": sum(1 for l in lecturas if l['estado'] in ['MALA', 'MUY MALA'])
    }

# ============================================================
#  FUNCIONES DE IA
# ============================================================
def generar_prediccion(lecturas_recientes):
    """
    Usa Gemini para predecir calidad del aire en próximas 6 horas.
    Basado en tendencias de datos históricos.
    """
    # Preparar contexto para el modelo
    stats = calcular_estadisticas(lecturas_recientes)
    
    # Obtener últimas 10 lecturas para tendencia
    ultimas_10 = lecturas_recientes[:10]
    tendencia_pm25 = ", ".join([f"{l['pm25']:.1f}" for l in ultimas_10 if l['pm25']])
    tendencia_gas = ", ".join([f"{l['gas']:.1f}" for l in ultimas_10 if l['gas']])
    
    prompt = f"""
Eres un experto en calidad del aire analizando datos de Chía, Cundinamarca (2560 m s.n.m.).

DATOS ACTUALES (últimas 24 horas):
- Total lecturas: {stats['total_lecturas']}
- PM2.5 promedio: {stats['pm25_promedio']:.1f} µg/m³ (min: {stats['pm25_min']:.1f}, max: {stats['pm25_max']:.1f})
- Gas promedio: {stats['gas_promedio']:.1f} ppm
- Estado predominante: {stats['estado_predominante']}
- Alertas críticas: {stats['alertas_criticas']}

TENDENCIA PM2.5 (últimas 10 lecturas, más reciente primero):
{tendencia_pm25}

TENDENCIA GAS:
{tendencia_gas}

UMBRALES DE REFERENCIA:
- PM2.5: BUENO <12, MODERADO 12-35, MALO >35 µg/m³
- Gas: BUENO <70, MODERADO 70-150, MALO >150 ppm

TAREA:
Predice la calidad del aire para las próximas 6 horas. Responde en JSON con este formato:

{{
  "prediccion_6h": "BUENA|MODERADA|MALA|MUY_MALA",
  "pm25_estimado": float,
  "gas_estimado": float,
  "confianza": "ALTA|MEDIA|BAJA",
  "razonamiento": "Explicación breve de la tendencia observada"
}}

Responde SOLO con el JSON, sin texto adicional.
"""
    
    try:
        response = client.models.generate_content(
            model='models/gemini-2.0-flash',
            contents=prompt
        )
        # Limpiar respuesta (a veces Gemini agrega ```json)
        texto = response.text.strip()
        if texto.startswith("```json"):
            texto = texto[7:]
        if texto.endswith("```"):
            texto = texto[:-3]
        
        return json.loads(texto.strip())
    except Exception as e:
        print(f"Error en predicción: {e}")
        return {
            "prediccion_6h": "DESCONOCIDA",
            "pm25_estimado": 0,
            "gas_estimado": 0,
            "confianza": "BAJA",
            "razonamiento": f"Error al generar predicción: {str(e)}"
        }

def generar_recomendaciones(lecturas_recientes, prediccion):
    """
    Genera recomendaciones para autoridades basadas en datos actuales y predicción.
    """
    stats = calcular_estadisticas(lecturas_recientes)
    estado_actual = stats['estado_predominante']
    estado_predicho = prediccion.get('prediccion_6h', 'DESCONOCIDA')
    
    prompt = f"""
Eres un asesor ambiental para autoridades locales de Chía, Cundinamarca.

SITUACIÓN ACTUAL:
- Estado predominante: {estado_actual}
- PM2.5 promedio: {stats['pm25_promedio']:.1f} µg/m³
- Alertas críticas últimas 24h: {stats['alertas_criticas']}

PREDICCIÓN PRÓXIMAS 6 HORAS:
- Estado predicho: {estado_predicho}
- PM2.5 estimado: {prediccion.get('pm25_estimado', 0):.1f} µg/m³
- Confianza: {prediccion.get('confianza', 'MEDIA')}

TAREA:
Genera recomendaciones concretas para las autoridades. Responde en JSON:

{{
  "prioridad": "URGENTE|ALTA|MEDIA|BAJA",
  "acciones_inmediatas": [
    "Acción 1 específica",
    "Acción 2 específica"
  ],
  "comunicacion_publica": "Mensaje para ciudadanía (máx 280 caracteres)",
  "medidas_preventivas": [
    "Medida 1",
    "Medida 2"
  ]
}}

Responde SOLO con el JSON, sin texto adicional.
"""
    
    try:
        response = client.models.generate_content(
            model='models/gemini-2.0-flash',
            contents=prompt
        )
        texto = response.text.strip()
        if texto.startswith("```json"):
            texto = texto[7:]
        if texto.endswith("```"):
            texto = texto[:-3]
        
        return json.loads(texto.strip())
    except Exception as e:
        print(f"Error en recomendaciones: {e}")
        return {
            "prioridad": "MEDIA",
            "acciones_inmediatas": ["Error al generar recomendaciones"],
            "comunicacion_publica": "Monitoreo continuo en curso",
            "medidas_preventivas": []
        }

def analizar_tendencia_semanal():
    """
    Analiza tendencias de la última semana y genera insights.
    """
    lecturas = obtener_ultimas_lecturas(horas=168, limite=500)  # 7 días
    
    if len(lecturas) < 50:
        return {
            "tendencia": "INSUFICIENTES_DATOS",
            "mensaje": "Se necesitan al menos 50 lecturas para análisis"
        }
    
    # Agrupar por días
    dias = {}
    for l in lecturas:
        fecha = datetime.fromtimestamp(l['ts_unix']).date()
        if fecha not in dias:
            dias[fecha] = []
        dias[fecha].append(l)
    
    # Calcular promedios diarios
    promedios_diarios = []
    for fecha, lecturas_dia in sorted(dias.items()):
        pm25_avg = sum(l['pm25'] for l in lecturas_dia if l['pm25']) / len(lecturas_dia)
        promedios_diarios.append({
            "fecha": str(fecha),
            "pm25": pm25_avg
        })
    
    prompt = f"""
Analiza esta tendencia semanal de PM2.5 en Chía, Cundinamarca:

DATOS DIARIOS:
{json.dumps(promedios_diarios, indent=2)}

TAREA:
Identifica patrones y tendencias. Responde en JSON:

{{
  "tendencia_general": "MEJORANDO|ESTABLE|EMPEORANDO",
  "patron_identificado": "Descripción del patrón (ej: picos fin de semana, deterioro progresivo)",
  "dias_criticos": ["fecha1", "fecha2"],
  "recomendacion_largo_plazo": "Recomendación estratégica para próximas semanas"
}}

Responde SOLO con el JSON.
"""
    
    try:
        response = client.models.generate_content(
            model='models/gemini-2.0-flash',
            contents=prompt
        )
        texto = response.text.strip()
        if texto.startswith("```json"):
            texto = texto[7:]
        if texto.endswith("```"):
            texto = texto[:-3]
        
        return json.loads(texto.strip())
    except Exception as e:
        return {
            "tendencia_general": "ERROR",
            "patron_identificado": str(e),
            "dias_criticos": [],
            "recomendacion_largo_plazo": "Error en análisis"
        }

# ============================================================
#  FUNCIÓN PRINCIPAL (para pruebas)
# ============================================================
def main():
    """Ejecuta todas las funciones de IA y muestra resultados."""
    print("=" * 60)
    print("MÓDULO DE IA - ANÁLISIS Y PREDICCIONES")
    print("=" * 60)
    
    # 1. Obtener datos
    print("\n1. Obteniendo datos históricos...")
    lecturas = obtener_ultimas_lecturas(horas=24, limite=100)
    print(f"   ✓ {len(lecturas)} lecturas obtenidas")
    
    if len(lecturas) < 10:
        print("   ⚠️ Insuficientes datos para análisis")
        return
    
    # 2. Estadísticas
    print("\n2. Calculando estadísticas...")
    stats = calcular_estadisticas(lecturas)
    print(f"   PM2.5 promedio: {stats['pm25_promedio']:.1f} µg/m³")
    print(f"   Estado predominante: {stats['estado_predominante']}")
    print(f"   Alertas críticas: {stats['alertas_criticas']}")
    
    # 3. Predicción
    print("\n3. Generando predicción con IA...")
    prediccion = generar_prediccion(lecturas)
    print(f"   Predicción 6h: {prediccion['prediccion_6h']}")
    print(f"   PM2.5 estimado: {prediccion['pm25_estimado']:.1f} µg/m³")
    print(f"   Confianza: {prediccion['confianza']}")
    print(f"   Razonamiento: {prediccion['razonamiento']}")
    
    # 4. Recomendaciones
    print("\n4. Generando recomendaciones...")
    recs = generar_recomendaciones(lecturas, prediccion)
    print(f"   Prioridad: {recs['prioridad']}")
    print(f"   Acciones: {len(recs['acciones_inmediatas'])}")
    for i, accion in enumerate(recs['acciones_inmediatas'], 1):
        print(f"      {i}. {accion}")
    
    # 5. Tendencia semanal
    print("\n5. Analizando tendencia semanal...")
    tendencia = analizar_tendencia_semanal()
    print(f"   Tendencia: {tendencia.get('tendencia_general', 'N/A')}")
    print(f"   Patrón: {tendencia.get('patron_identificado', 'N/A')}")
    
    print("\n" + "=" * 60)
    print("ANÁLISIS COMPLETADO")
    print("=" * 60)

if __name__ == "__main__":
    main()

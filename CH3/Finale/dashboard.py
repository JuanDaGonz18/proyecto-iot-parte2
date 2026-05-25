#!/usr/bin/env python3
"""
============================================================
 Dashboard Web — Calidad del Aire
 Universidad de la Sabana · Challenge #3 · 2026-1

 Muestra la tabla SQLite con login protegido.
 Incluye predicciones y recomendaciones de IA.

 Uso:
   pip install flask
   python3 dashboard.py

 Acceder en: http://<IP-RPi>:5000
============================================================
"""

import os
import json
import sqlite3
from datetime import datetime
from functools import wraps
from flask import (Flask, render_template_string, request,
                   redirect, url_for, session, flash)

# ─── Configuración ───────────────────────────────────────────
DB_PATH       = os.getenv("DB_PATH",    "aire.db")
SECRET_KEY    = os.getenv("SECRET_KEY", "change_this_secret_key_2026")
DASHBOARD_USER = os.getenv("DASH_USER", "sabana")
DASHBOARD_PASS = os.getenv("DASH_PASS", "sabana2026")
FILAS_POR_PAG  = 20

app = Flask(__name__)
app.secret_key = SECRET_KEY

# ─── Helpers ─────────────────────────────────────────────────
def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def login_required(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        if not session.get("logged_in"):
            return redirect(url_for("login", next=request.url))
        return f(*args, **kwargs)
    return decorated

def estado_badge(estado):
    colores = {
        "BUENA"   : ("#22c55e", "✅"),
        "MODERADA": ("#f59e0b", "⚠️"),
        "MALA"    : ("#f97316", "🔴"),
        "MUY MALA": ("#ef4444", "🚨"),
    }
    color, icon = colores.get(estado, ("#6b7280", "❓"))
    return f'<span style="color:{color};font-weight:600">{icon} {estado}</span>'

# ─── Templates ───────────────────────────────────────────────
BASE_CSS = """
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: 'Segoe UI', sans-serif; background: #f1f5f9; color: #1e293b; }
nav  { background: #0f172a; color: white; padding: 14px 28px;
       display:flex; justify-content:space-between; align-items:center; }
nav h1 { font-size: 1.1rem; letter-spacing:.5px; }
nav a  { color:#94a3b8; text-decoration:none; font-size:.9rem; }
nav a:hover { color:white; }
.container { max-width: 1200px; margin: 32px auto; padding: 0 20px; }
.card { background: white; border-radius: 12px; padding: 24px;
        box-shadow: 0 1px 4px rgba(0,0,0,.08); margin-bottom: 24px; }
.card h2 { font-size: 1rem; color:#64748b; margin-bottom:16px; text-transform:uppercase;
           letter-spacing:.6px; font-weight:600; }
table { width:100%; border-collapse:collapse; font-size:.88rem; }
th { background:#f8fafc; color:#475569; font-weight:600; padding:10px 12px;
     text-align:left; border-bottom:2px solid #e2e8f0; }
td { padding:9px 12px; border-bottom:1px solid #f1f5f9; }
tr:hover td { background:#fafafa; }
.alarma-si { color:#ef4444; font-weight:700; }
.alarma-no { color:#94a3b8; }
.btn { display:inline-block; padding:9px 20px; border-radius:8px; font-size:.9rem;
       cursor:pointer; border:none; font-weight:600; text-decoration:none; }
.btn-primary { background:#3b82f6; color:white; }
.btn-primary:hover { background:#2563eb; }
.btn-danger  { background:#ef4444; color:white; }
.btn-danger:hover { background:#dc2626; }
.pag { display:flex; gap:8px; margin-top:16px; align-items:center; }
.pag a { padding:6px 14px; border-radius:6px; background:#e2e8f0;
         color:#334155; text-decoration:none; font-size:.85rem; }
.pag a:hover { background:#cbd5e1; }
.pag .active { background:#3b82f6; color:white; }
.stats { display:grid; grid-template-columns:repeat(auto-fit,minmax(160px,1fr)); gap:16px; }
.stat  { text-align:center; }
.stat .val { font-size:2rem; font-weight:700; color:#3b82f6; }
.stat .lbl { font-size:.8rem; color:#64748b; margin-top:4px; }
.flash { background:#fef3c7; border:1px solid #fcd34d; border-radius:8px;
         padding:10px 16px; margin-bottom:16px; color:#92400e; font-size:.9rem; }
.label { font-size:.75rem; color:#64748b; margin-bottom:4px; text-transform:uppercase; }
.val { font-size:1.6rem; font-weight:700; margin:4px 0 2px; }
.unit { font-size:.75rem; color:#94a3b8; }
"""

LOGIN_HTML = """
<!DOCTYPE html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Login — Calidad del Aire</title>
<style>
""" + BASE_CSS + """
body { display:flex; justify-content:center; align-items:center; min-height:100vh; }
.login-card { background:white; border-radius:16px; padding:40px 36px; width:100%;
              max-width:380px; box-shadow:0 4px 24px rgba(0,0,0,.10); }
.login-card h2 { text-align:center; margin-bottom:8px; font-size:1.3rem; }
.login-card p  { text-align:center; color:#64748b; margin-bottom:28px; font-size:.9rem; }
label  { display:block; font-size:.85rem; color:#475569; font-weight:600; margin-bottom:6px; }
input  { width:100%; padding:10px 14px; border:1px solid #e2e8f0; border-radius:8px;
         font-size:.95rem; margin-bottom:18px; outline:none; }
input:focus { border-color:#3b82f6; }
.btn { width:100%; padding:12px; font-size:1rem; }
.error { background:#fee2e2; color:#991b1b; border-radius:8px; padding:10px;
         margin-bottom:16px; font-size:.88rem; text-align:center; }
</style></head><body>
<div class="login-card">
  <h2>🌫️ Monitor de Aire</h2>
  <p>Universidad de la Sabana</p>
  {% if error %}<div class="error">{{ error }}</div>{% endif %}
  <form method="POST">
    <label>Usuario</label>
    <input type="text" name="username" autofocus required>
    <label>Contraseña</label>
    <input type="password" name="password" required>
    <button class="btn btn-primary" type="submit">Ingresar</button>
  </form>
</div>
</body></html>
"""

DASHBOARD_HTML = """
<!DOCTYPE html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="30">
<title>Dashboard — Calidad del Aire</title>
<style>""" + BASE_CSS + """</style></head>
<body>
<nav>
  <h1>🌫️ Monitor de Calidad del Aire · U. Sabana</h1>
  <a href="{{ url_for('logout') }}">Cerrar sesión</a>
</nav>
<div class="container">
  {% with messages = get_flashed_messages() %}
    {% if messages %}<div class="flash">{{ messages[0] }}</div>{% endif %}
  {% endwith %}

  <!-- Estadísticas rápidas -->
  <div class="card">
    <h2>Resumen (últimas 24 h)</h2>
    <div class="stats">
      <div class="stat"><div class="val">{{ stats.total }}</div><div class="lbl">Lecturas</div></div>
      <div class="stat"><div class="val">{{ "%.1f"|format(stats.pm25_avg or 0) }}</div><div class="lbl">PM2.5 prom.</div></div>
      <div class="stat"><div class="val">{{ "%.1f"|format(stats.temp_avg or 0) }}°</div><div class="lbl">Temp. prom.</div></div>
      <div class="stat"><div class="val">{{ "%.1f"|format(stats.hum_avg or 0) }}%</div><div class="lbl">Humedad prom.</div></div>
      <div class="stat"><div class="val" style="color:{% if stats.alarmas > 0 %}#ef4444{% else %}#22c55e{% endif %}">
        {{ stats.alarmas }}</div><div class="lbl">Alarmas</div></div>
      <div class="stat"><div class="val">{{ stats.ultima_hora or '—' }}</div><div class="lbl">Última lectura</div></div>
    </div>
  </div>

  <!-- Predicción y Recomendaciones IA -->
  <div class="card">
    <h2>🤖 Análisis de Inteligencia Artificial</h2>
    {% if prediccion %}
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:16px">
      <div>
        <div class="label">Predicción próximas 6h</div>
        <div class="val" style="font-size:1.4rem">{{ prediccion.prediccion_6h }}</div>
        <div class="unit">PM2.5 estimado: {{ "%.1f"|format(prediccion.pm25_estimado) }} µg/m³</div>
        <div class="unit">Confianza: {{ prediccion.confianza }}</div>
      </div>
      <div>
        <div class="label">Prioridad de acción</div>
        <div class="val" style="font-size:1.4rem;color:{% if prediccion.prioridad == 'URGENTE' %}#ef4444{% elif prediccion.prioridad == 'ALTA' %}#f97316{% elif prediccion.prioridad == 'MEDIA' %}#f59e0b{% else %}#22c55e{% endif %}">
          {{ prediccion.prioridad }}
        </div>
      </div>
    </div>
    <div style="background:#f8fafc;padding:12px;border-radius:8px;margin-bottom:12px">
      <div style="font-size:.75rem;color:#64748b;margin-bottom:4px;font-weight:600">RAZONAMIENTO</div>
      <div style="font-size:.85rem">{{ prediccion.razonamiento }}</div>
    </div>
    <div style="background:#f8fafc;padding:12px;border-radius:8px;margin-bottom:12px">
      <div style="font-size:.75rem;color:#64748b;margin-bottom:8px;font-weight:600">ACCIONES RECOMENDADAS</div>
      <ul style="margin-left:20px;font-size:.85rem">
      {% for accion in prediccion.acciones %}
        <li style="margin-bottom:4px">{{ accion }}</li>
      {% endfor %}
      </ul>
    </div>
    <div style="background:#f8fafc;padding:12px;border-radius:8px">
      <div style="font-size:.75rem;color:#64748b;margin-bottom:4px;font-weight:600">COMUNICACIÓN PÚBLICA</div>
      <div style="font-size:.85rem;font-style:italic">"{{ prediccion.comunicacion }}"</div>
    </div>
    <div style="text-align:right;margin-top:8px;font-size:.7rem;color:#94a3b8">
      Última actualización: {{ prediccion.timestamp }}
    </div>
    {% else %}
    <p style="text-align:center;color:#94a3b8;padding:20px">
      Esperando primera predicción... (se genera cada 30 minutos)
    </p>
    {% endif %}
  </div>

  <!-- Filtro por fecha -->
  <div class="card" style="padding:16px 24px">
    <form method="GET" style="display:flex;gap:12px;align-items:flex-end;flex-wrap:wrap">
      <div>
        <label style="font-size:.82rem;color:#64748b;font-weight:600">Desde</label><br>
        <input type="date" name="desde" value="{{ filtro_desde }}"
               style="padding:7px 10px;border:1px solid #e2e8f0;border-radius:7px">
      </div>
      <div>
        <label style="font-size:.82rem;color:#64748b;font-weight:600">Hasta</label><br>
        <input type="date" name="hasta" value="{{ filtro_hasta }}"
               style="padding:7px 10px;border:1px solid #e2e8f0;border-radius:7px">
      </div>
      <button class="btn btn-primary" type="submit">Filtrar</button>
      <a href="{{ url_for('dashboard') }}" class="btn"
         style="background:#e2e8f0;color:#334155">Limpiar</a>
    </form>
  </div>

  <!-- Tabla de lecturas -->
  <div class="card">
    <h2>Historial de lecturas ({{ total_filas }} registros)</h2>
    {% if filas %}
    <div style="overflow-x:auto">
    <table>
      <thead><tr>
        <th>#</th>
        <th>Fecha</th>
        <th>Hora</th>
        <th>PM2.5 (µg/m³)</th>
        <th>Gas (ppm)</th>
        <th>Temp (°C)</th>
        <th>Humedad (%)</th>
        <th>Presión (hPa)</th>
        <th>Estado</th>
        <th>Alarma</th>
        <th>PMS OK</th>
      </tr></thead>
      <tbody>
      {% for f in filas %}
      <tr>
        <td style="color:#94a3b8">{{ f['id'] }}</td>
        <td><strong>{{ f['fecha'] }}</strong></td>
        <td>{{ f['hora'] }}</td>
        <td>{{ "%.2f"|format(f['pm25'] or 0) }}</td>
        <td>{{ "%.2f"|format(f['gas']  or 0) }}</td>
        <td>{{ "%.1f"|format(f['temp'] or 0) }}</td>
        <td>{{ "%.1f"|format(f['hum']  or 0) }}</td>
        <td>{{ "%.1f"|format(f['pres'] or 0) }}</td>
        <td>{{ estado_badge(f['estado']) | safe }}</td>
        <td class="{{ 'alarma-si' if f['alarma'] else 'alarma-no' }}">
          {{ '🚨 SÍ' if f['alarma'] else '—' }}</td>
        <td>{{ '✅' if f['pms_ok'] else '❌' }}</td>
      </tr>
      {% endfor %}
      </tbody>
    </table>
    </div>

    <!-- Paginación -->
    <div class="pag">
      {% if pagina > 1 %}
        <a href="{{ url_for('dashboard', pagina=pagina-1, desde=filtro_desde, hasta=filtro_hasta) }}">← Anterior</a>
      {% endif %}
      <span style="font-size:.85rem;color:#64748b">
        Página {{ pagina }} de {{ total_paginas }}
      </span>
      {% if pagina < total_paginas %}
        <a href="{{ url_for('dashboard', pagina=pagina+1, desde=filtro_desde, hasta=filtro_hasta) }}">Siguiente →</a>
      {% endif %}
    </div>

    {% else %}
    <p style="color:#94a3b8;text-align:center;padding:32px">
      No hay lecturas en este rango de fechas.</p>
    {% endif %}
  </div>
</div>
</body></html>
"""

# ─── Rutas ───────────────────────────────────────────────────
@app.route("/login", methods=["GET", "POST"])
def login():
    error = None
    if request.method == "POST":
        user = request.form.get("username", "").strip()
        pwd  = request.form.get("password", "")
        if user == DASHBOARD_USER and pwd == DASHBOARD_PASS:
            session["logged_in"] = True
            session["user"]      = user
            return redirect(request.args.get("next") or url_for("dashboard"))
        error = "Usuario o contraseña incorrectos."
    return render_template_string(LOGIN_HTML, error=error)


@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))


@app.route("/")
@login_required
def dashboard():
    pagina      = max(1, request.args.get("pagina", 1, type=int))
    filtro_desde = request.args.get("desde", "")
    filtro_hasta = request.args.get("hasta", "")
    offset      = (pagina - 1) * FILAS_POR_PAG

    conn = get_db()

    # Condición de filtro
    where, params = [], []
    if filtro_desde:
        where.append("date(ts_local) >= ?"); params.append(filtro_desde)
    if filtro_hasta:
        where.append("date(ts_local) <= ?"); params.append(filtro_hasta)
    where_sql = ("WHERE " + " AND ".join(where)) if where else ""

    # Total filas
    total_filas  = conn.execute(
        f"SELECT COUNT(*) FROM lecturas {where_sql}", params).fetchone()[0]
    total_paginas = max(1, (total_filas + FILAS_POR_PAG - 1) // FILAS_POR_PAG)

    # Filas paginadas con fecha/hora separadas
    filas_raw = conn.execute(
        f"""SELECT *, date(ts_local) as fecha, time(ts_local) as hora
            FROM lecturas {where_sql}
            ORDER BY id DESC LIMIT ? OFFSET ?""",
        params + [FILAS_POR_PAG, offset]
    ).fetchall()

    # Estadísticas últimas 24 h
    stats_row = conn.execute("""
        SELECT COUNT(*)              AS total,
               AVG(pm25)            AS pm25_avg,
               AVG(temp)            AS temp_avg,
               AVG(hum)             AS hum_avg,
               SUM(alarma)          AS alarmas,
               MAX(time(ts_local))  AS ultima_hora
        FROM lecturas
        WHERE ts_unix >= strftime('%s','now','-24 hours')
    """).fetchone()

    stats = dict(stats_row) if stats_row else {}
    filas = [dict(f) for f in filas_raw]

    # Obtener última predicción IA
    pred_row = conn.execute("""
        SELECT timestamp, prediccion_6h, pm25_estimado, confianza, 
               razonamiento, prioridad, acciones, comunicacion
        FROM predictions
        ORDER BY id DESC LIMIT 1
    """).fetchone()

    prediccion = None
    if pred_row:
        prediccion = {
            'prediccion_6h': pred_row['prediccion_6h'],
            'pm25_estimado': pred_row['pm25_estimado'],
            'confianza': pred_row['confianza'],
            'razonamiento': pred_row['razonamiento'],
            'prioridad': pred_row['prioridad'],
            'acciones': json.loads(pred_row['acciones']) if pred_row['acciones'] else [],
            'comunicacion': pred_row['comunicacion'],
            'timestamp': datetime.fromtimestamp(pred_row['timestamp']).strftime('%Y-%m-%d %H:%M')
        }

    conn.close()

    return render_template_string(
        DASHBOARD_HTML,
        filas=filas,
        total_filas=total_filas,
        total_paginas=total_paginas,
        pagina=pagina,
        filtro_desde=filtro_desde,
        filtro_hasta=filtro_hasta,
        stats=stats,
        prediccion=prediccion,
        estado_badge=estado_badge,
    )


# ─── Entry point ─────────────────────────────────────────────
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)

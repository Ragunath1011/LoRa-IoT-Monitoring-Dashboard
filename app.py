import os
import re
import sqlite3
import threading
import time
from datetime import datetime

from flask import Flask, jsonify, render_template

try:
    import serial
except ImportError:
    serial = None

app = Flask(__name__)

DB_FILE = "lora.db"
SERIAL_PORT = os.getenv("LORA_PORT", "COM5")
BAUD_RATE = 115200
DEMO_MODE = os.getenv("DEMO_MODE", "1") == "1"

last_gateway_data_time = 0
serial_status = "DEMO MODE" if DEMO_MODE else "OFFLINE"


def db():
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = db()
    conn.execute("""
        CREATE TABLE IF NOT EXISTS packets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            packet_number INTEGER NOT NULL,
            packet_type TEXT NOT NULL,
            gateway_rssi INTEGER,
            snr REAL,
            relay_rssi INTEGER,
            rssi_gain INTEGER
        )
    """)
    conn.commit()
    conn.close()


def insert_packet(packet_number, packet_type, gateway_rssi, snr,
                  relay_rssi=None, rssi_gain=None):
    conn = db()
    conn.execute("""
        INSERT INTO packets
        (timestamp, packet_number, packet_type, gateway_rssi,
         snr, relay_rssi, rssi_gain)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (
        datetime.now().isoformat(timespec="seconds"),
        packet_number, packet_type, gateway_rssi, snr,
        relay_rssi, rssi_gain
    ))
    conn.commit()
    conn.close()


# Parses the existing human-readable Gateway output.
# Examples:
# 25 | DIRECT | -91 dBm | | SNR=8.2dB
# 25 | RELAY | -72 dBm | -83 dBm | +19 dB SNR=9.1dB
def parse_gateway_line(line):
    direct = re.search(
        r"(\d+)\s*\|\s*DIRECT\s*\|\s*(-?\d+)\s*dBm.*?SNR=([-+]?\d+(?:\.\d+)?)dB",
        line,
        re.I
    )
    if direct:
        return {
            "packet_number": int(direct.group(1)),
            "packet_type": "DIRECT",
            "gateway_rssi": int(direct.group(2)),
            "snr": float(direct.group(3)),
            "relay_rssi": None,
            "rssi_gain": None,
        }

    relay = re.search(
        r"(\d+)\s*\|\s*RELAY\s*\|\s*(-?\d+)\s*dBm\s*\|\s*(-?\d+)\s*dBm\s*\|\s*([+-]?\d+)\s*dB\s*SNR=([-+]?\d+(?:\.\d+)?)dB",
        line,
        re.I
    )
    if relay:
        return {
            "packet_number": int(relay.group(1)),
            "packet_type": "RELAY",
            "gateway_rssi": int(relay.group(2)),
            "snr": float(relay.group(5)),
            "relay_rssi": int(relay.group(3)),
            "rssi_gain": int(relay.group(4)),
        }

    return None


def serial_worker():
    global last_gateway_data_time, serial_status

    if serial is None:
        serial_status = "PySerial not installed"
        return

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        serial_status = "ONLINE"
        print(f"Connected to {SERIAL_PORT}")

        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            packet = parse_gateway_line(line)
            if packet:
                insert_packet(**packet)
                last_gateway_data_time = time.time()

    except Exception as e:
        serial_status = "OFFLINE"
        print("Serial connection:", e)


def add_demo_data():
    conn = db()
    count = conn.execute("SELECT COUNT(*) FROM packets").fetchone()[0]
    conn.close()

    if count > 0:
        return

    samples = [
        (118, "DIRECT", -88, 7.2, None, None),
        (119, "DIRECT", -91, 7.5, None, None),
        (120, "DIRECT", -89, 8.1, None, None),
        (121, "DIRECT", -92, 7.8, None, None),
        (122, "RELAY", -74, 8.4, -83, 18),
        (123, "RELAY", -72, 8.7, -82, 20),
        (124, "RELAY", -70, 9.0, -80, 19),
        (125, "RELAY", -73, 8.5, -82, 19),
        (126, "RELAY", -70, 8.8, -79, 20),
        (127, "RELAY", -72, 9.1, -83, 19),
    ]
    for row in samples:
        insert_packet(*row)


@app.route("/")
def dashboard():
    return render_template("index.html")


@app.route("/api/latest")
def latest():
    conn = db()
    row = conn.execute(
        "SELECT * FROM packets ORDER BY id DESC LIMIT 1"
    ).fetchone()
    conn.close()
    return jsonify(dict(row) if row else {})


@app.route("/api/history")
def history():
    conn = db()
    rows = conn.execute("""
        SELECT * FROM packets
        ORDER BY id DESC
        LIMIT 50
    """).fetchall()
    conn.close()

    data = [dict(r) for r in reversed(rows)]
    return jsonify(data)


@app.route("/api/stats")
def stats():
    conn = db()
    rows = conn.execute("""
        SELECT packet_number, packet_type
        FROM packets
        ORDER BY packet_number
    """).fetchall()
    conn.close()

    unique_packets = sorted(set(r["packet_number"] for r in rows))

    if unique_packets:
        expected = max(unique_packets)
        received = len(unique_packets)
        lost = max(0, expected - received)
        delivery = round(received / expected * 100, 1) if expected else 0
    else:
        expected = received = lost = 0
        delivery = 0

    return jsonify({
        "total_packets": received,
        "packets_lost": lost,
        "delivery_rate": delivery,
        "gateway_status": serial_status,
        "demo_mode": DEMO_MODE
    })


if __name__ == "__main__":
    init_db()

    if DEMO_MODE:
        add_demo_data()
    else:
        threading.Thread(target=serial_worker, daemon=True).start()

    app.run(host="127.0.0.1", port=5000, debug=False)

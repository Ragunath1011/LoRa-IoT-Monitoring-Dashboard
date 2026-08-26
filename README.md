# LoRa IoT Monitoring Dashboard

Software extension of an existing ESP8266/ESP32 + SX1278 Source–Relay–Gateway LoRa system.

## Features
- Gateway packet monitoring
- Direct vs Relay RSSI comparison
- SNR history
- RSSI gain
- Packet-loss and delivery-rate statistics
- SQLite storage
- Flask REST APIs
- HTML/CSS/JavaScript dashboard
- Optional real USB serial input from the LoRa gateway

## Run the demo
```bash
python -m pip install -r requirements.txt
python app.py
```
Open http://127.0.0.1:5000

The default is DEMO_MODE, so the dashboard works without hardware.

## Use the real Gateway
1. Close Arduino Serial Monitor so Python can use the COM port.
2. Set the COM port:
   Windows PowerShell:
```powershell
$env:DEMO_MODE="0"
$env:LORA_PORT="COM5"
python app.py
```
Replace COM5 with your Gateway port.

The application parses the existing human-readable Gateway lines such as:
`25 | DIRECT | -91 dBm | | SNR=8.2dB`
and
`25 | RELAY | -72 dBm | -83 dBm | +19 dB SNR=9.1dB`

## APIs
- GET /api/latest
- GET /api/history
- GET /api/stats

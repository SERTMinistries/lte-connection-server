# LTE Connection Server

Node.js server that receives BLE/GPS data uploaded over LTE from an ESP32 + SIM7000 board.

## What Was Set Up

### Server (`server.js`)

- Built with **Express** (managed with **pnpm**)
- Runs on **port 3000**
- Receives JSON POST requests at `/upload`
- Saves every upload as a row in `logs/ble_gps_uploads.csv`
- View raw CSV at `/logs`

### Arduino Code (`arduino/`)

| File | Board | Purpose |
|---|---|---|
| `Board-one-BLE-GPS_SD.ino` | ESP32 #1 | Scans BLE, reads GPS, logs to SD card, sends data over serial to Board 2 |
| `Board2-wifi-setup.ino` | ESP32 #2 | WiFi-only dashboard at `http://192.168.4.1` |
| `Board2-wifi-setup-LTE-Hologram.ino` | ESP32 #2 | WiFi dashboard + SIM7000 LTE upload to remote server |

### Wiring

```
Logger ESP32 GPIO25 TX  ->  Dashboard ESP32 GPIO16 RX
Logger GND              ->  Dashboard GND

SIM7000 TX  ->  Dashboard ESP32 GPIO26
SIM7000 RX  ->  Dashboard ESP32 GPIO27
SIM7000 GND ->  Dashboard ESP32 GND
SIM7000 VIN ->  External power (do NOT use ESP32 3.3V)
```

### Hologram SIM Settings

```
APN:      hologram
Username: (blank)
Password: (blank)
```

## Running the Server Locally

```bash
pnpm install
pnpm start
```

Server runs at `http://localhost:3000`

## Tunnel Options

The SIM7000 uses plain HTTP (no SSL). Free tunnels (ngrok, Cloudflare, localhost.run) all force HTTPS and will not work with the SIM7000 directly.

### Recommended: Deploy to Railway

1. Push this repo to GitHub
2. Go to [railway.app](https://railway.app) and deploy from GitHub
3. Generate a domain under Settings → Networking
4. Update `REMOTE_HOST` and `REMOTE_PORT` in `Board2-wifi-setup-LTE-Hologram.ino`:

```cpp
const char REMOTE_HOST[] = "your-app.up.railway.app";
const int  REMOTE_PORT   = 80;
const char REMOTE_PATH[] = "/upload";
```

### ngrok TCP (requires free card on file)

```bash
ngrok tcp 3000
```

Update the ESP32 with the TCP address ngrok gives you.

## Upload Endpoint

```
POST /upload
Content-Type: application/json

{
  "device_id": "esp32-dashboard",
  "timestamp": "...",
  "lat": "33.49",
  "lng": "-117.14",
  "mac": "AA:BB:CC:DD:EE:FF",
  "rssi": -71,
  "name": "BLE Device Name",
  "vendor_hint": "Hologram",
  "gps_status": "FIX",
  "event": "NEW"
}
```

## Test Upload

```bash
curl -X POST http://localhost:3000/upload \
  -H "Content-Type: application/json" \
  -d '{"device_id":"test","timestamp":"2026-05-20T08:00:00Z","lat":33.49,"lng":-117.14,"mac":"AA:BB:CC:DD:EE:FF","rssi":-71,"name":"Test BLE","vendor_hint":"Test"}'
```

## Arduino Libraries Required

Install from Arduino IDE Library Manager:

- **TinyGSM** by Volodymyr Shymanskyy
- **TinyGPSPlus**
- **NimBLE-Arduino**
- SD, SPI, WiFi, WebServer (bundled with ESP32 board package)

## CSV Log Format

```
received_at, device_id, timestamp, lat, lng, mac, rssi, name, vendor_hint, raw
```

Saved to `logs/ble_gps_uploads.csv`

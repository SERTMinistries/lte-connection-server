# ESP32 BLE/GPS Logger + SIM7000 LTE + Hologram + ngrok Setup

This project uses two ESP32 boards:

```text
Board 1: BLE + GPS + SD Logger
Board 2: WiFi Dashboard + SIM7000 LTE Upload
```

The goal is to keep the local dashboard working over WiFi while also allowing the device to upload BLE/GPS logs remotely over LTE using a Hologram SIM card.

## Project Flow

```text
ESP32 Logger Board
  - Scans BLE devices
  - Reads GPS location
  - Saves data to SD card
  - Sends data to Dashboard Board over serial

ESP32 Dashboard Board
  - Hosts local WiFi dashboard
  - Receives logger data from Board 1
  - Sends data over LTE using SIM7000 + Hologram SIM

Mac / Windsurf Server
  - Runs a local Node.js upload server
  - Receives LTE uploads through ngrok
  - Saves uploaded data to a CSV file
```

## Important Concept

You normally do not log into the ESP32 web UI directly over LTE. Most IoT SIM connections are behind carrier networking/NAT, so the ESP32 can send data out, but your phone or computer usually cannot connect directly back into the ESP32.

Use this setup instead:

```text
ESP32 + SIM7000 + Hologram SIM
        ↓ LTE HTTP POST
ngrok public URL
        ↓
Mac local server
        ↓
CSV logs / future dashboard
```

The local dashboard still works nearby through the ESP32 access point:

```text
WiFi SSID: BLE-GPS-LOGGER
URL: http://192.168.4.1
```

## Hardware Used

- ESP32 #1 Logger Board
- ESP32 #2 Dashboard Board
- GPS Module
- MicroSD Card Module
- MicroSD Card
- SIM7000 LTE Board
- Hologram.io SIM Card
- LTE Antenna
- Jumper Wires
- USB Power or Power Bank
- External power source for SIM7000 if needed

## Hologram SIM Settings

Use these cellular settings in the ESP32 code:

```text
APN: hologram
Username: blank
Password: blank
```

The SIM must already be activated in the Hologram dashboard before testing.

## Recommended Wiring

### Logger Board to Dashboard Board

```text
Logger ESP32 GPIO25 TX -> Dashboard ESP32 GPIO16 RX
Logger GND             -> Dashboard GND
```

### SIM7000 to Dashboard ESP32

```text
SIM7000 TX  -> Dashboard ESP32 GPIO26
SIM7000 RX  -> Dashboard ESP32 GPIO27
SIM7000 GND -> Dashboard ESP32 GND
SIM7000 VIN/BAT -> strong external power source
```

Important: Do not power the SIM7000 only from the ESP32 3.3V pin. LTE modules can pull high current bursts and may reset or fail to connect if underpowered.

## Arduino Libraries Needed

Install these from the Arduino IDE Library Manager:

```text
TinyGSM
```

Your existing project may also use:

```text
TinyGPSPlus
NimBLE-Arduino
SD
SPI
WiFi
WebServer
```

## ESP32 Code Files

Use these files:

```text
Board-one-BLE-GPS_SD.ino
Board2-wifi-setup-LTE-Hologram.ino
```

Upload:

```text
Board-one-BLE-GPS_SD.ino -> Logger ESP32
Board2-wifi-setup-LTE-Hologram.ino -> Dashboard ESP32
```

## Create the Local Server in Windsurf

Create a new folder on your Mac:

```bash
mkdir esp32-ble-gps-lte-server
cd esp32-ble-gps-lte-server
```

Open that folder in Windsurf.

Create a file named:

```text
server.js
```

Add this code:

```js
const express = require("express");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = 3000;

app.use(express.json({ limit: "5mb" }));
app.use(express.urlencoded({ extended: true }));
app.use(express.text({ type: "*/*", limit: "5mb" }));

const logsDir = path.join(__dirname, "logs");
const csvFile = path.join(logsDir, "ble_gps_uploads.csv");

if (!fs.existsSync(logsDir)) {
  fs.mkdirSync(logsDir);
}

if (!fs.existsSync(csvFile)) {
  fs.writeFileSync(
    csvFile,
    "received_at,device_id,timestamp,lat,lng,mac,rssi,name,vendor_hint,raw\n"
  );
}

function csvSafe(value) {
  if (value === undefined || value === null) return "";
  return `"${String(value).replace(/"/g, '""')}"`;
}

app.get("/", (req, res) => {
  res.send(`
    <h1>ESP32 BLE/GPS LTE Server</h1>
    <p>Server is running.</p>
    <p>POST ESP32 uploads to <code>/upload</code></p>
    <p>View logs at <code>/logs</code></p>
  `);
});

app.post("/upload", (req, res) => {
  const receivedAt = new Date().toISOString();

  let data = req.body;

  if (typeof data === "string") {
    try {
      data = JSON.parse(data);
    } catch {
      data = { raw: req.body };
    }
  }

  console.log("Upload received:");
  console.log(data);

  const row = [
    receivedAt,
    data.device_id,
    data.timestamp,
    data.lat,
    data.lng,
    data.mac,
    data.rssi,
    data.name,
    data.vendor_hint,
    JSON.stringify(data),
  ]
    .map(csvSafe)
    .join(",");

  fs.appendFileSync(csvFile, row + "\n");

  res.status(200).json({
    success: true,
    message: "Upload received",
    received_at: receivedAt,
  });
});

app.get("/logs", (req, res) => {
  res.sendFile(csvFile);
});

app.listen(PORT, () => {
  console.log(`ESP32 BLE/GPS LTE server running at http://localhost:${PORT}`);
  console.log(`Upload endpoint: http://localhost:${PORT}/upload`);
});
```

## Install and Run the Server

In the Windsurf terminal, run:

```bash
npm init -y
npm install express
node server.js
```

You should see:

```text
ESP32 BLE/GPS LTE server running at http://localhost:3000
Upload endpoint: http://localhost:3000/upload
```

Open this in your browser:

```text
http://localhost:3000
```

You can view uploaded logs here:

```text
http://localhost:3000/logs
```

## Start ngrok

Open a second terminal and run:

```bash
ngrok http 3000
```

ngrok will show a forwarding URL like:

```text
https://abc123.ngrok-free.app -> http://localhost:3000
```

Your ESP32 upload endpoint becomes:

```text
https://abc123.ngrok-free.app/upload
```

For the first SIM7000 test, HTTP is usually easier than HTTPS. If ngrok gives you an HTTP forwarding URL, use the HTTP version:

```text
http://abc123.ngrok-free.app/upload
```

## Update the ESP32 Dashboard Code

In `Board2-wifi-setup-LTE-Hologram.ino`, find:

```cpp
const char REMOTE_HOST[] = "webhook.site";
const int  REMOTE_PORT   = 80;
const char REMOTE_PATH[] = "/REPLACE_WITH_YOUR_WEBHOOK_PATH";
```

Change it to your ngrok domain:

```cpp
const char REMOTE_HOST[] = "abc123.ngrok-free.app";
const int  REMOTE_PORT   = 80;
const char REMOTE_PATH[] = "/upload";
```

Do not include `http://` or `/upload` in `REMOTE_HOST`.

Correct:

```cpp
const char REMOTE_HOST[] = "abc123.ngrok-free.app";
const char REMOTE_PATH[] = "/upload";
```

Wrong:

```cpp
const char REMOTE_HOST[] = "http://abc123.ngrok-free.app/upload";
```

## Test the Server Before Using the ESP32

Test local upload:

```bash
curl -X POST http://localhost:3000/upload \
  -H "Content-Type: application/json" \
  -d '{"device_id":"test","timestamp":"2026-05-20T08:00:00Z","lat":33.49,"lng":-117.14,"mac":"AA:BB:CC:DD:EE:FF","rssi":-71,"name":"Test BLE","vendor_hint":"Test"}'
```

Then check:

```text
http://localhost:3000/logs
```

Test through ngrok:

```bash
curl -X POST http://abc123.ngrok-free.app/upload \
  -H "Content-Type: application/json" \
  -d '{"device_id":"ngrok-test","timestamp":"2026-05-20T08:00:00Z","lat":33.49,"lng":-117.14,"mac":"AA:BB:CC:DD:EE:FF","rssi":-71,"name":"LTE Test","vendor_hint":"Hologram"}'
```

If the ngrok test shows up in the Windsurf terminal and in `/logs`, the Mac/ngrok side is working.

## Test the SIM7000 with AT Commands

Before relying on the full project, confirm the modem sees the SIM and network:

```text
AT
AT+CPIN?
AT+CSQ
AT+CEREG?
AT+CGDCONT=1,"IP","hologram"
```

Good signs:

```text
+CPIN: READY
```

Signal should not be:

```text
+CSQ: 99,99
```

If signal is weak, connect the LTE antenna and move near a window or outside.

## Serial Monitor Checklist

Open Serial Monitor on the Dashboard ESP32 at:

```text
115200 baud
```

Good messages may look like:

```text
SIM7000 serial started
Starting SIM7000 modem
LTE connected through Hologram
Upload OK
```

Common problems:

```text
Modem restart failed
```

Check SIM7000 power, TX/RX wiring, and whether the board needs the PWRKEY button pressed.

```text
No network
```

Check antenna, SIM activation, signal strength, and APN.

```text
Set REMOTE_PATH first
```

Update the ngrok path in the ESP32 code.

## Free ngrok Warning

Free ngrok URLs usually change when ngrok restarts.

Every time the ngrok URL changes, update this line in the ESP32 code:

```cpp
const char REMOTE_HOST[] = "your-new-url.ngrok-free.app";
```

For long-term use, replace ngrok with a hosted API endpoint so the ESP32 can upload to a stable domain.

## Recommended Development Order

1. Confirm Hologram SIM is activated.
2. Confirm SIM7000 has antenna and stable power.
3. Test SIM7000 with AT commands.
4. Run the Windsurf Node server locally.
5. Test local upload with curl.
6. Start ngrok.
7. Test ngrok upload with curl.
8. Add ngrok host to ESP32 dashboard code.
9. Upload ESP32 code.
10. Watch Serial Monitor for LTE connection and upload status.
11. Confirm data appears in Windsurf terminal and `/logs`.

## Future Improvement

Once testing works, move from ngrok to a real hosted dashboard/API:

```text
ESP32 + SIM7000
   ↓
Laravel / Node / Supabase / Firebase API
   ↓
Web dashboard with map, filters, CSV export, and device history
```

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

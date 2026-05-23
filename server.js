const express = require("express");
const fs = require("fs");
const path = require("path");
const http = require("http");
const { Server } = require("socket.io");

const app = express();
const httpServer = http.createServer(app);
const io = new Server(httpServer, { cors: { origin: "*" } });
const PORT = 4000;

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

// In-memory watchlist (persisted to file)
const watchlistFile = path.join(__dirname, "logs", "watchlist.json");
let watchlist = [];
try {
  if (fs.existsSync(watchlistFile)) {
    watchlist = JSON.parse(fs.readFileSync(watchlistFile, "utf8"));
  }
} catch {}

function saveWatchlist() {
  fs.writeFileSync(watchlistFile, JSON.stringify(watchlist, null, 2));
}

// Socket.IO
io.on("connection", (socket) => {
  console.log("UI connected via WebSocket");
  socket.emit("watchlist", watchlist);
});

// Watchlist API
app.get("/watchlist", (req, res) => res.json(watchlist));

app.post("/watchlist", (req, res) => {
  const mac = (req.body.mac || "").trim().toLowerCase();
  if (!mac || watchlist.includes(mac)) return res.status(400).json({ error: "Invalid or duplicate MAC" });
  watchlist.push(mac);
  saveWatchlist();
  io.emit("watchlist", watchlist);
  res.json({ success: true, watchlist });
});

app.delete("/watchlist/:mac", (req, res) => {
  const mac = decodeURIComponent(req.params.mac).toLowerCase();
  watchlist = watchlist.filter((m) => m !== mac);
  saveWatchlist();
  io.emit("watchlist", watchlist);
  res.json({ success: true, watchlist });
});

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

  // Check if this MAC is in the watchlist
  const mac = (data.mac || "").trim().toLowerCase();
  if (mac && watchlist.includes(mac)) {
    console.log(`WATCHLIST HIT: ${mac} at ${data.lat},${data.lng} RSSI:${data.rssi}`);
    io.emit("watchlistHit", {
      mac: data.mac,
      rssi: data.rssi,
      name: data.name,
      lat: data.lat,
      lng: data.lng,
      timestamp: data.timestamp || receivedAt,
      device_id: data.device_id,
    });
  }

  res.status(200).json({
    success: true,
    message: "Upload received",
    received_at: receivedAt,
  });
});

app.get("/logs", (req, res) => {
  res.sendFile(csvFile);
});

httpServer.listen(PORT, () => {
  console.log(`ESP32 BLE/GPS LTE server running at http://localhost:${PORT}`);
  console.log(`Upload endpoint: http://localhost:${PORT}/upload`);
  console.log(`WebSocket: ws://localhost:${PORT}`);
  console.log(`Watchlist API: http://localhost:${PORT}/watchlist`);
});

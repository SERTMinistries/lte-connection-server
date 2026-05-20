#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

#define TINY_GSM_MODEM_SIM7000
#include <TinyGsmClient.h>

// =====================================================
// Wi-Fi AP
// =====================================================
const char* ssid = "BLE-GPS-LOGGER";
const char* password = "12345678";

WebServer server(80);

// =====================================================
// LOGGER SERIAL (from Board 1)
// Logger GPIO25 TX -> Dashboard GPIO16 RX
// =====================================================
HardwareSerial LOGGER(1);
#define LOGGER_RX 16
#define LOGGER_TX 17

// =====================================================
// SIM7000 SERIAL
// SIM7000 TX -> Dashboard ESP32 GPIO26
// SIM7000 RX -> Dashboard ESP32 GPIO27
// =====================================================
HardwareSerial SIM7000_SERIAL(2);
#define SIM_RX 26
#define SIM_TX 27

TinyGsm modem(SIM7000_SERIAL);
TinyGsmClient gsmClient(modem);

// =====================================================
// REMOTE SERVER (Railway)
// Do not include http:// or https:// in REMOTE_HOST.
// =====================================================
const char REMOTE_HOST[] = "lte-connection-server-production.up.railway.app";
const int  REMOTE_PORT   = 80;
const char REMOTE_PATH[] = "/upload";

// =====================================================
// APN
// =====================================================
const char APN[]      = "hologram";
const char APN_USER[] = "";
const char APN_PASS[] = "";

// =====================================================
// UPLOAD INTERVAL
// =====================================================
const unsigned long UPLOAD_INTERVAL = 60000;
unsigned long lastUpload = 0;

bool lteReady = false;

// =====================================================
// LATEST VALUES
// =====================================================
String latestEvent      = "Waiting";
String latestLat        = "NO_FIX";
String latestLng        = "NO_FIX";
String latestGpsStatus  = "NO_FIX";
String latestSatellites = "0";
String latestBleName    = "None";
String latestBleAddress = "None";
String latestBleRssi    = "None";
String latestBleSignal  = "None";

int totalEvents = 0;
String lastRawLine = "Waiting for logger data...";

// =====================================================
// STRONGEST BLE DEVICE
// =====================================================
String strongestEvent      = "None";
String strongestName       = "None";
String strongestAddress    = "None";
String strongestRssi       = "None";
String strongestSignal     = "None";
String strongestLat        = "NO_FIX";
String strongestLng        = "NO_FIX";
String strongestGpsStatus  = "NO_FIX";
String strongestSatellites = "0";

int strongestRssiValue = -999;

// =====================================================
// EVENT HISTORY TABLE
// =====================================================
const int MAX_EVENTS    = 100;
const int ROWS_PER_PAGE = 10;

String eventType[MAX_EVENTS];
String eventLat[MAX_EVENTS];
String eventLng[MAX_EVENTS];
String eventGpsStatus[MAX_EVENTS];
String eventSatellites[MAX_EVENTS];
String eventBleName[MAX_EVENTS];
String eventBleAddress[MAX_EVENTS];
String eventRssi[MAX_EVENTS];
String eventSignal[MAX_EVENTS];
String eventRaw[MAX_EVENTS];

int eventCount = 0;

// =====================================================
// HELPERS
// =====================================================
String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String urlEncode(String value) {
  String encoded = "";
  for (int i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "+";
    } else {
      encoded += "%";
      if (c < 16) encoded += "0";
      encoded += String(c, HEX);
    }
  }
  return encoded;
}

bool containsIgnoreCase(String haystack, String needle) {
  haystack.toLowerCase();
  needle.toLowerCase();
  return haystack.indexOf(needle) != -1;
}

String getField(String data, int index) {
  int commaCount = 0;
  int startIndex = 0;
  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) == ',') {
      if (commaCount == index) return data.substring(startIndex, i);
      commaCount++;
      startIndex = i + 1;
    }
  }
  if (commaCount == index) return data.substring(startIndex);
  return "";
}

void updateStrongestDevice() {
  int currentRssi = latestBleRssi.toInt();
  if (currentRssi > strongestRssiValue) {
    strongestRssiValue  = currentRssi;
    strongestEvent      = latestEvent;
    strongestName       = latestBleName;
    strongestAddress    = latestBleAddress;
    strongestRssi       = latestBleRssi;
    strongestSignal     = latestBleSignal;
    strongestLat        = latestLat;
    strongestLng        = latestLng;
    strongestGpsStatus  = latestGpsStatus;
    strongestSatellites = latestSatellites;
    Serial.println("New strongest BLE device: " + strongestName);
  }
}

void addEventToTable(
  String e, String lat, String lng, String gps, String sats,
  String name, String address, String rssi, String signal, String raw
) {
  if (eventCount >= MAX_EVENTS) {
    for (int i = 1; i < MAX_EVENTS; i++) {
      eventType[i-1]       = eventType[i];
      eventLat[i-1]        = eventLat[i];
      eventLng[i-1]        = eventLng[i];
      eventGpsStatus[i-1]  = eventGpsStatus[i];
      eventSatellites[i-1] = eventSatellites[i];
      eventBleName[i-1]    = eventBleName[i];
      eventBleAddress[i-1] = eventBleAddress[i];
      eventRssi[i-1]       = eventRssi[i];
      eventSignal[i-1]     = eventSignal[i];
      eventRaw[i-1]        = eventRaw[i];
    }
    eventCount = MAX_EVENTS - 1;
  }
  int index = eventCount;
  eventType[index]       = e;
  eventLat[index]        = lat;
  eventLng[index]        = lng;
  eventGpsStatus[index]  = gps;
  eventSatellites[index] = sats;
  eventBleName[index]    = name;
  eventBleAddress[index] = address;
  eventRssi[index]       = rssi;
  eventSignal[index]     = signal;
  eventRaw[index]        = raw;
  eventCount++;
}

void parseLoggerLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  lastRawLine      = line;
  latestEvent      = getField(line, 0);
  latestLat        = getField(line, 1);
  latestLng        = getField(line, 2);
  latestGpsStatus  = getField(line, 3);
  latestSatellites = getField(line, 4);
  latestBleName    = getField(line, 5);
  latestBleAddress = getField(line, 6);
  latestBleRssi    = getField(line, 7);
  latestBleSignal  = getField(line, 8);

  totalEvents++;
  updateStrongestDevice();
  addEventToTable(
    latestEvent, latestLat, latestLng, latestGpsStatus, latestSatellites,
    latestBleName, latestBleAddress, latestBleRssi, latestBleSignal, lastRawLine
  );
  Serial.println("Received from logger: " + line);
}

// =====================================================
// LTE UPLOAD
// =====================================================
void uploadOverLTE() {
  if (!lteReady) {
    Serial.println("LTE not ready, skipping upload.");
    return;
  }

  String body = "{";
  body += "\"device_id\":\"esp32-dashboard\",";
  body += "\"timestamp\":\"" + String(millis()) + "\",";
  body += "\"lat\":\"" + latestLat + "\",";
  body += "\"lng\":\"" + latestLng + "\",";
  body += "\"mac\":\"" + latestBleAddress + "\",";
  body += "\"rssi\":" + (latestBleRssi.length() > 0 ? latestBleRssi : "0") + ",";
  body += "\"name\":\"" + latestBleName + "\",";
  body += "\"vendor_hint\":\"Hologram\",";
  body += "\"gps_status\":\"" + latestGpsStatus + "\",";
  body += "\"event\":\"" + latestEvent + "\"";
  body += "}";

  Serial.println("Uploading over LTE...");

  if (!gsmClient.connect(REMOTE_HOST, REMOTE_PORT)) {
    Serial.println("LTE connect failed.");
    return;
  }

  gsmClient.print(String("POST ") + REMOTE_PATH + " HTTP/1.1\r\n");
  gsmClient.print(String("Host: ") + REMOTE_HOST + "\r\n");
  gsmClient.print("Content-Type: application/json\r\n");
  gsmClient.print("ngrok-skip-browser-warning: true\r\n");
  gsmClient.print("Content-Length: " + String(body.length()) + "\r\n");
  gsmClient.print("Connection: close\r\n\r\n");
  gsmClient.print(body);

  unsigned long timeout = millis();
  while (gsmClient.connected() && millis() - timeout < 10000) {
    while (gsmClient.available()) {
      String line = gsmClient.readStringUntil('\n');
      Serial.println(line);
      timeout = millis();
    }
  }

  gsmClient.stop();
  Serial.println("Upload done.");
}

// =====================================================
// LTE INIT
// =====================================================
void initLTE() {
  Serial.println("Starting SIM7000 modem...");

  SIM7000_SERIAL.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
  delay(3000);

  if (!modem.restart()) {
    Serial.println("Modem restart failed. Check wiring and power.");
    lteReady = false;
    return;
  }

  Serial.println("Modem started.");
  Serial.print("Modem info: ");
  Serial.println(modem.getModemInfo());

  modem.gprsConnect(APN, APN_USER, APN_PASS);
  delay(5000);

  if (!modem.isGprsConnected()) {
    Serial.println("GPRS/LTE connect failed. Check SIM, antenna, and APN.");
    lteReady = false;
    return;
  }

  Serial.println("LTE connected through Hologram.");
  lteReady = true;
}

// =====================================================
// DASHBOARD HTML
// =====================================================
String getEventBadge(String eventName) {
  if (eventName == "NEW")      return "<span class='badge new'>NEW</span>";
  if (eventName == "LEFT")     return "<span class='badge left'>LEFT</span>";
  if (eventName == "RETURNED") return "<span class='badge returned'>RETURNED</span>";
  return "<span class='badge'>" + htmlEscape(eventName) + "</span>";
}

bool eventMatchesFilters(int index, String filterEvent, String searchText) {
  if (filterEvent != "ALL" && eventType[index] != filterEvent) return false;
  searchText.trim();
  if (searchText.length() > 0) {
    String combined = eventBleName[index] + " " + eventBleAddress[index] + " " + eventSignal[index] + " " + eventType[index];
    if (!containsIgnoreCase(combined, searchText)) return false;
  }
  return true;
}

bool shouldSwap(int a, int b, String sortBy) {
  if (sortBy == "newest")    return a < b;
  if (sortBy == "oldest")    return a > b;
  if (sortBy == "rssi_desc") return eventRssi[a].toInt() < eventRssi[b].toInt();
  if (sortBy == "rssi_asc")  return eventRssi[a].toInt() > eventRssi[b].toInt();
  if (sortBy == "name_az") {
    String nameA = eventBleName[a]; nameA.toLowerCase();
    String nameB = eventBleName[b]; nameB.toLowerCase();
    return nameA > nameB;
  }
  return a < b;
}

String selectedOption(String current, String value) {
  return (current == value) ? " selected" : "";
}

String getFilterControlsHTML(String filterEvent, String sortBy, String searchText) {
  String html = "";
  html += "<div><b>Table Filters</b><br><form method='GET' action='/'>";
  html += "<label>Event Filter</label><br><select name='event'>";
  html += "<option value='ALL'" + selectedOption(filterEvent, "ALL") + ">All Events</option>";
  html += "<option value='NEW'" + selectedOption(filterEvent, "NEW") + ">NEW</option>";
  html += "<option value='LEFT'" + selectedOption(filterEvent, "LEFT") + ">LEFT</option>";
  html += "<option value='RETURNED'" + selectedOption(filterEvent, "RETURNED") + ">RETURNED</option>";
  html += "</select><br><br><label>Sort By</label><br><select name='sort'>";
  html += "<option value='newest'" + selectedOption(sortBy, "newest") + ">Newest First</option>";
  html += "<option value='oldest'" + selectedOption(sortBy, "oldest") + ">Oldest First</option>";
  html += "<option value='rssi_desc'" + selectedOption(sortBy, "rssi_desc") + ">Strongest RSSI First</option>";
  html += "<option value='rssi_asc'" + selectedOption(sortBy, "rssi_asc") + ">Weakest RSSI First</option>";
  html += "<option value='name_az'" + selectedOption(sortBy, "name_az") + ">Name A-Z</option>";
  html += "</select><br><br><label>Search Name or Address</label><br>";
  html += "<input type='text' name='search' value='" + htmlEscape(searchText) + "' placeholder='Search BLE name or address'>";
  html += "<input type='hidden' name='page' value='1'><br><br>";
  html += "<button type='submit'>Apply Filters</button> <a class='clearBtn' href='/'>Clear</a>";
  html += "</form></div>";
  return html;
}

String getTableHTML(int page, String filterEvent, String sortBy, String searchText) {
  String html = "";
  int filteredIndexes[MAX_EVENTS];
  int filteredCount = 0;

  for (int i = 0; i < eventCount; i++) {
    if (eventMatchesFilters(i, filterEvent, searchText)) {
      filteredIndexes[filteredCount++] = i;
    }
  }

  for (int i = 0; i < filteredCount - 1; i++) {
    for (int j = i + 1; j < filteredCount; j++) {
      if (shouldSwap(filteredIndexes[i], filteredIndexes[j], sortBy)) {
        int temp = filteredIndexes[i];
        filteredIndexes[i] = filteredIndexes[j];
        filteredIndexes[j] = temp;
      }
    }
  }

  int totalPages = (filteredCount > 0) ? (filteredCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE : 1;
  if (page < 1) page = 1;
  if (page > totalPages) page = totalPages;

  int startPosition = (page - 1) * ROWS_PER_PAGE;
  int endPosition = startPosition + ROWS_PER_PAGE;
  if (endPosition > filteredCount) endPosition = filteredCount;

  html += "<div><b>Event Table</b><br>Page " + String(page) + " of " + String(totalPages);
  html += " | Matching: " + String(filteredCount) + " | Total: " + String(eventCount) + "</div>";

  html += "<div class='tableWrap'><table>";
  html += "<tr><th>#</th><th>Event</th><th>Name</th><th>Address</th><th>RSSI</th><th>Signal</th><th>GPS</th><th>Lat</th><th>Lng</th><th>Sats</th></tr>";

  if (filteredCount == 0) {
    html += "<tr><td colspan='10'>No matching BLE/GPS events found.</td></tr>";
  } else {
    for (int pos = startPosition; pos < endPosition; pos++) {
      int i = filteredIndexes[pos];
      html += "<tr>";
      html += "<td>" + String(i + 1) + "</td>";
      html += "<td>" + getEventBadge(eventType[i]) + "</td>";
      html += "<td>" + htmlEscape(eventBleName[i]) + "</td>";
      html += "<td class='mono'>" + htmlEscape(eventBleAddress[i]) + "</td>";
      html += "<td>" + htmlEscape(eventRssi[i]) + "</td>";
      html += "<td>" + htmlEscape(eventSignal[i]) + "</td>";
      html += (eventGpsStatus[i] == "FIX") ? "<td><span class='good'>FIX</span></td>" : "<td><span class='warn'>NO_FIX</span></td>";
      html += "<td>" + htmlEscape(eventLat[i]) + "</td>";
      html += "<td>" + htmlEscape(eventLng[i]) + "</td>";
      html += "<td>" + htmlEscape(eventSatellites[i]) + "</td>";
      html += "</tr>";
    }
  }

  html += "</table></div>";

  String baseQuery = "&event=" + urlEncode(filterEvent) + "&sort=" + urlEncode(sortBy) + "&search=" + urlEncode(searchText);
  html += "<div class='pager'>";
  html += (page > 1) ? "<a href='/?page=" + String(page - 1) + baseQuery + "'>Previous</a>" : "<span>Previous</span>";
  html += " ";
  html += (page < totalPages) ? "<a href='/?page=" + String(page + 1) + baseQuery + "'>Next</a>" : "<span>Next</span>";
  html += "</div>";

  return html;
}

String getDashboardHTML(int page, String filterEvent, String sortBy, String searchText) {
  String html = "";
  html += "<html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='15'>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:#eee;padding:16px;}";
  html += "div{background:#222;margin:10px 0;padding:12px;border-radius:8px;}";
  html += "b{color:#7dd3fc;}";
  html += ".good{color:#22c55e;} .bad{color:#ef4444;} .warn{color:#facc15;}";
  html += ".mono{font-family:monospace;font-size:12px;}";
  html += ".tableWrap{overflow-x:auto;padding:0;background:#111;}";
  html += "table{border-collapse:collapse;width:100%;min-width:950px;background:#181818;}";
  html += "th,td{border:1px solid #333;padding:8px;text-align:left;font-size:13px;}";
  html += "th{background:#222;color:#7dd3fc;position:sticky;top:0;}";
  html += "tr:nth-child(even){background:#1f1f1f;}";
  html += ".badge{display:inline-block;padding:3px 6px;border-radius:6px;background:#555;color:#fff;font-size:11px;}";
  html += ".new{background:#2563eb;} .left{background:#dc2626;} .returned{background:#16a34a;}";
  html += ".strongCard{border:1px solid #22c55e;box-shadow:0 0 12px rgba(34,197,94,.25);}";
  html += ".bigRssi{font-size:30px;font-weight:bold;color:#22c55e;}";
  html += ".smallNote{font-size:12px;color:#aaa;}";
  html += ".pager a,.pager span,.clearBtn,button{display:inline-block;margin:4px;padding:8px 12px;border-radius:6px;background:#333;color:#fff;text-decoration:none;border:0;}";
  html += ".pager span{opacity:.4;}";
  html += "select,input{width:100%;max-width:420px;padding:10px;border-radius:6px;border:1px solid #444;background:#111;color:#eee;}";
  html += "label{font-size:13px;color:#7dd3fc;} button{cursor:pointer;background:#2563eb;} .clearBtn{background:#555;}";
  html += "</style></head><body>";

  html += "<h2>BLE + GPS Dashboard</h2>";
  html += "<div><b>Wi-Fi Login</b><br>Network: BLE-GPS-LOGGER<br>Password: 12345678<br>URL: http://192.168.4.1</div>";

  html += "<div><b>System</b><br>";
  html += "Dashboard ESP32: <span class='good'>ONLINE</span>";
  html += "<br>LTE Status: ";
  html += lteReady ? "<span class='good'>CONNECTED</span>" : "<span class='warn'>NOT CONNECTED</span>";
  html += "<br>LTE Host: " + String(REMOTE_HOST);
  html += "<br>Total Events: " + String(totalEvents);
  html += "<br>Stored Rows: " + String(eventCount) + " / " + String(MAX_EVENTS);
  html += "</div>";

  html += "<div class='strongCard'><b>Strongest BLE Device</b><br>";
  html += "<span class='smallNote'>RSSI closest to zero wins.</span><br><br>";
  html += "Name: " + htmlEscape(strongestName);
  html += "<br>Address: <span class='mono'>" + htmlEscape(strongestAddress) + "</span>";
  html += "<br>RSSI: <span class='bigRssi'>" + htmlEscape(strongestRssi) + "</span>";
  html += "<br>Signal: " + htmlEscape(strongestSignal);
  html += "<br>Event: " + getEventBadge(strongestEvent);
  html += "<br>GPS: ";
  html += (strongestGpsStatus == "FIX") ? "<span class='good'>FIX</span>" : "<span class='warn'>NO_FIX</span>";
  html += "<br>Lat: " + htmlEscape(strongestLat);
  html += "<br>Lng: " + htmlEscape(strongestLng);
  html += "<br>Satellites: " + htmlEscape(strongestSatellites);
  html += "</div>";

  html += "<div><b>Latest BLE Event</b><br>";
  html += "Event: " + getEventBadge(latestEvent);
  html += "<br>Name: " + htmlEscape(latestBleName);
  html += "<br>Address: <span class='mono'>" + htmlEscape(latestBleAddress) + "</span>";
  html += "<br>RSSI: " + htmlEscape(latestBleRssi);
  html += "<br>Signal: " + htmlEscape(latestBleSignal);
  html += "</div>";

  html += "<div><b>GPS</b><br>";
  html += "Status: ";
  html += (latestGpsStatus == "FIX") ? "<span class='good'>FIX</span>" : "<span class='warn'>NO_FIX</span>";
  html += "<br>Lat: " + htmlEscape(latestLat);
  html += "<br>Lng: " + htmlEscape(latestLng);
  html += "<br>Satellites: " + htmlEscape(latestSatellites);
  html += "</div>";

  html += getFilterControlsHTML(filterEvent, sortBy, searchText);
  html += getTableHTML(page, filterEvent, sortBy, searchText);

  html += "<div><b>Raw Logger Line</b><br>" + htmlEscape(lastRawLine) + "</div>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  int page = 1;
  String filterEvent = "ALL";
  String sortBy = "newest";
  String searchText = "";

  if (server.hasArg("page")) {
    page = server.arg("page").toInt();
    if (page < 1) page = 1;
  }
  if (server.hasArg("event")) {
    filterEvent = server.arg("event");
    filterEvent.toUpperCase();
    if (filterEvent != "ALL" && filterEvent != "NEW" && filterEvent != "LEFT" && filterEvent != "RETURNED") {
      filterEvent = "ALL";
    }
  }
  if (server.hasArg("sort")) {
    sortBy = server.arg("sort");
    if (sortBy != "newest" && sortBy != "oldest" && sortBy != "rssi_desc" && sortBy != "rssi_asc" && sortBy != "name_az") {
      sortBy = "newest";
    }
  }
  if (server.hasArg("search")) {
    searchText = server.arg("search");
    searchText.trim();
  }

  server.send(200, "text/html", getDashboardHTML(page, filterEvent, sortBy, searchText));
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32 DASHBOARD + LTE");
  Serial.println("---------------------");

  LOGGER.begin(115200, SERIAL_8N1, LOGGER_RX, LOGGER_TX);
  Serial.println("Logger serial started.");

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  bool started = WiFi.softAP(ssid, password, 1);
  Serial.println(started ? "Wi-Fi dashboard started." : "Wi-Fi dashboard failed.");

  server.on("/", handleRoot);
  server.begin();

  Serial.print("Wi-Fi: "); Serial.println(ssid);
  Serial.print("Dashboard: http://"); Serial.println(WiFi.softAPIP());

  initLTE();

  Serial.println("Dashboard ready.");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  server.handleClient();

  while (LOGGER.available()) {
    String line = LOGGER.readStringUntil('\n');
    parseLoggerLine(line);
  }

  if (millis() - lastUpload > UPLOAD_INTERVAL) {
    lastUpload = millis();
    uploadOverLTE();
  }
}

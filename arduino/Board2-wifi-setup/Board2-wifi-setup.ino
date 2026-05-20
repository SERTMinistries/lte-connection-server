#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

// Wi-Fi login
const char* ssid = "BLE-GPS-LOGGER";
const char* password = "12345678";

WebServer server(80);

// Logger GPIO25 TX -> Dashboard GPIO16 RX
HardwareSerial LOGGER(1);

#define LOGGER_RX 16
#define LOGGER_TX 17

// =====================================================
// LATEST VALUES
// =====================================================
String latestEvent = "Waiting";
String latestLat = "NO_FIX";
String latestLng = "NO_FIX";
String latestGpsStatus = "NO_FIX";
String latestSatellites = "0";
String latestBleName = "None";
String latestBleAddress = "None";
String latestBleRssi = "None";
String latestBleSignal = "None";

int totalEvents = 0;
String lastRawLine = "Waiting for logger data...";

// =====================================================
// STRONGEST BLE DEVICE
// =====================================================
String strongestEvent = "None";
String strongestName = "None";
String strongestAddress = "None";
String strongestRssi = "None";
String strongestSignal = "None";
String strongestLat = "NO_FIX";
String strongestLng = "NO_FIX";
String strongestGpsStatus = "NO_FIX";
String strongestSatellites = "0";

int strongestRssiValue = -999;

// =====================================================
// EVENT HISTORY TABLE
// =====================================================
const int MAX_EVENTS = 100;
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

String lowerText(String value) {
  value.toLowerCase();
  return value;
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
      if (commaCount == index) {
        return data.substring(startIndex, i);
      }

      commaCount++;
      startIndex = i + 1;
    }
  }

  if (commaCount == index) {
    return data.substring(startIndex);
  }

  return "";
}

void updateStrongestDevice() {
  int currentRssi = latestBleRssi.toInt();

  // RSSI closer to zero is stronger.
  // Example: -35 is stronger than -80.
  if (currentRssi > strongestRssiValue) {
    strongestRssiValue = currentRssi;

    strongestEvent = latestEvent;
    strongestName = latestBleName;
    strongestAddress = latestBleAddress;
    strongestRssi = latestBleRssi;
    strongestSignal = latestBleSignal;
    strongestLat = latestLat;
    strongestLng = latestLng;
    strongestGpsStatus = latestGpsStatus;
    strongestSatellites = latestSatellites;

    Serial.println("New strongest BLE device:");
    Serial.println(strongestName);
    Serial.println(strongestAddress);
    Serial.println(strongestRssi);
  }
}

void addEventToTable(
  String e,
  String lat,
  String lng,
  String gps,
  String sats,
  String name,
  String address,
  String rssi,
  String signal,
  String raw
) {
  if (eventCount >= MAX_EVENTS) {
    for (int i = 1; i < MAX_EVENTS; i++) {
      eventType[i - 1] = eventType[i];
      eventLat[i - 1] = eventLat[i];
      eventLng[i - 1] = eventLng[i];
      eventGpsStatus[i - 1] = eventGpsStatus[i];
      eventSatellites[i - 1] = eventSatellites[i];
      eventBleName[i - 1] = eventBleName[i];
      eventBleAddress[i - 1] = eventBleAddress[i];
      eventRssi[i - 1] = eventRssi[i];
      eventSignal[i - 1] = eventSignal[i];
      eventRaw[i - 1] = eventRaw[i];
    }

    eventCount = MAX_EVENTS - 1;
  }

  int index = eventCount;

  eventType[index] = e;
  eventLat[index] = lat;
  eventLng[index] = lng;
  eventGpsStatus[index] = gps;
  eventSatellites[index] = sats;
  eventBleName[index] = name;
  eventBleAddress[index] = address;
  eventRssi[index] = rssi;
  eventSignal[index] = signal;
  eventRaw[index] = raw;

  eventCount++;
}

void parseLoggerLine(String line) {
  line.trim();

  if (line.length() == 0) return;

  lastRawLine = line;

  latestEvent = getField(line, 0);
  latestLat = getField(line, 1);
  latestLng = getField(line, 2);
  latestGpsStatus = getField(line, 3);
  latestSatellites = getField(line, 4);
  latestBleName = getField(line, 5);
  latestBleAddress = getField(line, 6);
  latestBleRssi = getField(line, 7);
  latestBleSignal = getField(line, 8);

  totalEvents++;

  updateStrongestDevice();

  addEventToTable(
    latestEvent,
    latestLat,
    latestLng,
    latestGpsStatus,
    latestSatellites,
    latestBleName,
    latestBleAddress,
    latestBleRssi,
    latestBleSignal,
    lastRawLine
  );

  Serial.println("Received from logger:");
  Serial.println(line);
}

// =====================================================
// TABLE / FILTER HELPERS
// =====================================================
String getEventBadge(String eventName) {
  if (eventName == "NEW") {
    return "<span class='badge new'>NEW</span>";
  }

  if (eventName == "LEFT") {
    return "<span class='badge left'>LEFT</span>";
  }

  if (eventName == "RETURNED") {
    return "<span class='badge returned'>RETURNED</span>";
  }

  return "<span class='badge'>" + htmlEscape(eventName) + "</span>";
}

bool eventMatchesFilters(int index, String filterEvent, String searchText) {
  if (filterEvent != "ALL" && eventType[index] != filterEvent) {
    return false;
  }

  searchText.trim();

  if (searchText.length() > 0) {
    String combined = eventBleName[index] + " " + eventBleAddress[index] + " " + eventSignal[index] + " " + eventType[index];

    if (!containsIgnoreCase(combined, searchText)) {
      return false;
    }
  }

  return true;
}

bool shouldSwap(int a, int b, String sortBy) {
  if (sortBy == "newest") {
    return a < b;
  }

  if (sortBy == "oldest") {
    return a > b;
  }

  if (sortBy == "rssi_desc") {
    return eventRssi[a].toInt() < eventRssi[b].toInt();
  }

  if (sortBy == "rssi_asc") {
    return eventRssi[a].toInt() > eventRssi[b].toInt();
  }

  if (sortBy == "name_az") {
    String nameA = eventBleName[a];
    String nameB = eventBleName[b];

    nameA.toLowerCase();
    nameB.toLowerCase();

    return nameA > nameB;
  }

  return a < b;
}

String selectedOption(String current, String value) {
  if (current == value) return " selected";
  return "";
}

String getFilterControlsHTML(String filterEvent, String sortBy, String searchText) {
  String html = "";

  html += "<div><b>Table Filters</b><br>";
  html += "<form method='GET' action='/'>";

  html += "<label>Event Filter</label><br>";
  html += "<select name='event'>";
  html += "<option value='ALL'" + selectedOption(filterEvent, "ALL") + ">All Events</option>";
  html += "<option value='NEW'" + selectedOption(filterEvent, "NEW") + ">NEW</option>";
  html += "<option value='LEFT'" + selectedOption(filterEvent, "LEFT") + ">LEFT</option>";
  html += "<option value='RETURNED'" + selectedOption(filterEvent, "RETURNED") + ">RETURNED</option>";
  html += "</select>";

  html += "<br><br>";

  html += "<label>Sort By</label><br>";
  html += "<select name='sort'>";
  html += "<option value='newest'" + selectedOption(sortBy, "newest") + ">Newest First</option>";
  html += "<option value='oldest'" + selectedOption(sortBy, "oldest") + ">Oldest First</option>";
  html += "<option value='rssi_desc'" + selectedOption(sortBy, "rssi_desc") + ">Strongest RSSI First</option>";
  html += "<option value='rssi_asc'" + selectedOption(sortBy, "rssi_asc") + ">Weakest RSSI First</option>";
  html += "<option value='name_az'" + selectedOption(sortBy, "name_az") + ">Name A-Z</option>";
  html += "</select>";

  html += "<br><br>";

  html += "<label>Search Name or Address</label><br>";
  html += "<input type='text' name='search' value='" + htmlEscape(searchText) + "' placeholder='Search BLE name or address'>";

  html += "<input type='hidden' name='page' value='1'>";

  html += "<br><br>";
  html += "<button type='submit'>Apply Filters</button>";
  html += " <a class='clearBtn' href='/'>Clear</a>";

  html += "</form>";
  html += "</div>";

  return html;
}

String getTableHTML(int page, String filterEvent, String sortBy, String searchText) {
  String html = "";

  int filteredIndexes[MAX_EVENTS];
  int filteredCount = 0;

  for (int i = 0; i < eventCount; i++) {
    if (eventMatchesFilters(i, filterEvent, searchText)) {
      filteredIndexes[filteredCount] = i;
      filteredCount++;
    }
  }

  // Simple sort for filtered indexes
  for (int i = 0; i < filteredCount - 1; i++) {
    for (int j = i + 1; j < filteredCount; j++) {
      if (shouldSwap(filteredIndexes[i], filteredIndexes[j], sortBy)) {
        int temp = filteredIndexes[i];
        filteredIndexes[i] = filteredIndexes[j];
        filteredIndexes[j] = temp;
      }
    }
  }

  int totalPages = 1;

  if (filteredCount > 0) {
    totalPages = (filteredCount + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
  }

  if (page < 1) page = 1;
  if (page > totalPages) page = totalPages;

  int startPosition = (page - 1) * ROWS_PER_PAGE;
  int endPosition = startPosition + ROWS_PER_PAGE;

  if (endPosition > filteredCount) {
    endPosition = filteredCount;
  }

  html += "<div><b>Event Table</b><br>";
  html += "Filter: " + htmlEscape(filterEvent);
  html += "<br>Sort: " + htmlEscape(sortBy);
  html += "<br>Search: " + htmlEscape(searchText);
  html += "<br>Page " + String(page) + " of " + String(totalPages);
  html += "<br>Matching Rows: " + String(filteredCount);
  html += "<br>Total Stored Rows: " + String(eventCount);
  html += "</div>";

  html += "<div class='tableWrap'>";
  html += "<table>";
  html += "<tr>";
  html += "<th>#</th>";
  html += "<th>Event</th>";
  html += "<th>Name</th>";
  html += "<th>Address</th>";
  html += "<th>RSSI</th>";
  html += "<th>Signal</th>";
  html += "<th>GPS</th>";
  html += "<th>Lat</th>";
  html += "<th>Lng</th>";
  html += "<th>Sats</th>";
  html += "</tr>";

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

      if (eventGpsStatus[i] == "FIX") {
        html += "<td><span class='good'>FIX</span></td>";
      } else {
        html += "<td><span class='warn'>NO_FIX</span></td>";
      }

      html += "<td>" + htmlEscape(eventLat[i]) + "</td>";
      html += "<td>" + htmlEscape(eventLng[i]) + "</td>";
      html += "<td>" + htmlEscape(eventSatellites[i]) + "</td>";
      html += "</tr>";
    }
  }

  html += "</table>";
  html += "</div>";

  html += "<div class='pager'>";

  String baseQuery = "&event=" + urlEncode(filterEvent) + "&sort=" + urlEncode(sortBy) + "&search=" + urlEncode(searchText);

  if (page > 1) {
    html += "<a href='/?page=" + String(page - 1) + baseQuery + "'>Previous</a>";
  } else {
    html += "<span>Previous</span>";
  }

  html += " ";

  if (page < totalPages) {
    html += "<a href='/?page=" + String(page + 1) + baseQuery + "'>Next</a>";
  } else {
    html += "<span>Next</span>";
  }

  html += "</div>";

  return html;
}

// =====================================================
// DASHBOARD HTML
// =====================================================
String getDashboardHTML(int page, String filterEvent, String sortBy, String searchText) {
  String html = "";

  html += "<html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='15'>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:#eee;padding:16px;}";
  html += "div{background:#222;margin:10px 0;padding:12px;border-radius:8px;}";
  html += "b{color:#7dd3fc;}";
  html += ".good{color:#22c55e;}";
  html += ".bad{color:#ef4444;}";
  html += ".warn{color:#facc15;}";
  html += ".mono{font-family:monospace;font-size:12px;}";
  html += ".tableWrap{overflow-x:auto;padding:0;background:#111;}";
  html += "table{border-collapse:collapse;width:100%;min-width:950px;background:#181818;}";
  html += "th,td{border:1px solid #333;padding:8px;text-align:left;font-size:13px;}";
  html += "th{background:#222;color:#7dd3fc;position:sticky;top:0;}";
  html += "tr:nth-child(even){background:#1f1f1f;}";
  html += ".badge{display:inline-block;padding:3px 6px;border-radius:6px;background:#555;color:#fff;font-size:11px;}";
  html += ".new{background:#2563eb;}";
  html += ".left{background:#dc2626;}";
  html += ".returned{background:#16a34a;}";
  html += ".strongCard{border:1px solid #22c55e;box-shadow:0 0 12px rgba(34,197,94,.25);}";
  html += ".bigRssi{font-size:30px;font-weight:bold;color:#22c55e;}";
  html += ".smallNote{font-size:12px;color:#aaa;}";
  html += ".pager a,.pager span,.clearBtn,button{display:inline-block;margin:4px;padding:8px 12px;border-radius:6px;background:#333;color:#fff;text-decoration:none;border:0;}";
  html += ".pager span{opacity:.4;}";
  html += "select,input{width:100%;max-width:420px;padding:10px;border-radius:6px;border:1px solid #444;background:#111;color:#eee;}";
  html += "label{font-size:13px;color:#7dd3fc;}";
  html += "button{cursor:pointer;background:#2563eb;}";
  html += ".clearBtn{background:#555;}";
  html += "</style></head><body>";

  html += "<h2>BLE + GPS Dashboard</h2>";

  html += "<div><b>Wi-Fi Login</b><br>";
  html += "Network: BLE-GPS-LOGGER";
  html += "<br>Password: 12345678";
  html += "<br>URL: http://192.168.4.1";
  html += "</div>";

  html += "<div><b>System</b><br>";
  html += "Dashboard ESP32: <span class='good'>ONLINE</span>";
  html += "<br>Total Events Received: " + String(totalEvents);
  html += "<br>Stored Table Rows: " + String(eventCount);
  html += "<br>Max Stored Rows: " + String(MAX_EVENTS);
  html += "</div>";

  html += "<div class='strongCard'><b>Strongest BLE Device</b><br>";
  html += "<span class='smallNote'>The dashboard saves whichever device has the RSSI closest to zero.</span>";
  html += "<br><br>Name: " + htmlEscape(strongestName);
  html += "<br>Address: <span class='mono'>" + htmlEscape(strongestAddress) + "</span>";
  html += "<br>RSSI: <span class='bigRssi'>" + htmlEscape(strongestRssi) + "</span>";
  html += "<br>Signal: " + htmlEscape(strongestSignal);
  html += "<br>Event Seen: " + getEventBadge(strongestEvent);
  html += "<br>GPS: ";

  if (strongestGpsStatus == "FIX") {
    html += "<span class='good'>FIX</span>";
  } else {
    html += "<span class='warn'>NO_FIX</span>";
  }

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
  html += latestGpsStatus == "FIX" ? "<span class='good'>FIX</span>" : "<span class='warn'>NO_FIX</span>";
  html += "<br>Lat: " + htmlEscape(latestLat);
  html += "<br>Lng: " + htmlEscape(latestLng);
  html += "<br>Satellites: " + htmlEscape(latestSatellites);
  html += "</div>";

  html += getFilterControlsHTML(filterEvent, sortBy, searchText);

  html += getTableHTML(page, filterEvent, sortBy, searchText);

  html += "<div><b>Raw Logger Line</b><br>";
  html += htmlEscape(lastRawLine);
  html += "</div>";

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

    if (
      filterEvent != "ALL" &&
      filterEvent != "NEW" &&
      filterEvent != "LEFT" &&
      filterEvent != "RETURNED"
    ) {
      filterEvent = "ALL";
    }
  }

  if (server.hasArg("sort")) {
    sortBy = server.arg("sort");

    if (
      sortBy != "newest" &&
      sortBy != "oldest" &&
      sortBy != "rssi_desc" &&
      sortBy != "rssi_asc" &&
      sortBy != "name_az"
    ) {
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
  Serial.println("ESP32 DASHBOARD");
  Serial.println("---------------");

  LOGGER.begin(115200, SERIAL_8N1, LOGGER_RX, LOGGER_TX);
  Serial.println("Logger serial started.");
  Serial.println("Dashboard GPIO16 RX <- Logger GPIO25 TX");

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  bool started = WiFi.softAP(ssid, password, 1);

  if (started) {
    Serial.println("Wi-Fi dashboard started.");
  } else {
    Serial.println("Wi-Fi dashboard failed.");
  }

  server.on("/", handleRoot);
  server.begin();

  Serial.print("Wi-Fi: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("Dashboard: http://");
  Serial.println(WiFi.softAPIP());

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
}

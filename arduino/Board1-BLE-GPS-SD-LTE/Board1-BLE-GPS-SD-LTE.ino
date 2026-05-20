#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define TINY_GSM_MODEM_SIM7000
#include <TinyGsmClient.h>

// =====================================================
// GPS WIRING
// GPS TX -> ESP32 GPIO16
// GPS RX -> ESP32 GPIO17
// =====================================================
TinyGPSPlus gps;
HardwareSerial GPS_SERIAL(1);

#define GPS_RX 16
#define GPS_TX 17

// =====================================================
// SD CARD WIRING
// SD CS   -> GPIO5
// SD SCK  -> GPIO18
// SD MOSI -> GPIO23
// SD MISO -> GPIO19
// =====================================================
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

bool sdReady = false;
String logFile   = "/ble_events.csv";
String aliasFile = "/alias_map.csv";

// =====================================================
// SIM7000 WIRING
// SIM7000 TX -> ESP32 GPIO26
// SIM7000 RX -> ESP32 GPIO27
// SIM7000 GND -> ESP32 GND
// SIM7000 VIN -> External power (NOT ESP32 3.3V)
// =====================================================
HardwareSerial SIM_SERIAL(2);

#define SIM_RX 26
#define SIM_TX 27

TinyGsm       modem(SIM_SERIAL);
TinyGsmClient gsmClient(modem);

// =====================================================
// REMOTE SERVER
// Update REMOTE_HOST to your Railway or hosted URL.
// Do not include http:// in REMOTE_HOST.
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
// UPLOAD INTERVAL (ms)
// =====================================================
const unsigned long UPLOAD_INTERVAL = 60000;
unsigned long lastUpload = 0;

bool lteReady = false;

// =====================================================
// BLE
// =====================================================
BLEScan* pBLEScan;

const int scanTime              = 5;
const unsigned long scanInterval = 60000;

unsigned long lastScan        = 0;
unsigned long lastStatusPrint = 0;

int scanNumber      = 0;
int lastBleCount    = 0;
int totalEventsLogged = 0;

// =====================================================
// DEVICE TRACKING
// =====================================================
const int MAX_DEVICES = 100;

String knownAddress[MAX_DEVICES];
String knownName[MAX_DEVICES];
String knownStableName[MAX_DEVICES];
int    knownRssi[MAX_DEVICES];
bool   knownPresent[MAX_DEVICES];
bool   seenThisScan[MAX_DEVICES];

int knownCount = 0;

// =====================================================
// STABLE NAME TRACKING
// =====================================================
const int MAX_ALIASES = 150;

String aliasKey[MAX_ALIASES];
String aliasLabel[MAX_ALIASES];

int aliasCount      = 0;
int nextDeviceNumber = 1;

// =====================================================
// GPS VALUES
// =====================================================
String gpsStatus  = "NO_FIX";
String latitude   = "NO_FIX";
String longitude  = "NO_FIX";
String satellites = "0";
String altitude   = "0";
String gpsChars   = "0";

// =====================================================
// LAST BLE EVENT (for LTE upload)
// =====================================================
String lastEventType    = "";
String lastStableName   = "";
String lastBleAddress   = "";
int    lastRssi         = 0;

// =====================================================
// HELPERS
// =====================================================
String cleanCSV(String value) {
  value.replace(",", " ");
  value.replace("\n", " ");
  value.replace("\r", " ");
  value.replace("\"", "'");
  return value;
}

String getSignalLabel(int rssi) {
  if (rssi >= -50) return "Very Strong";
  if (rssi >= -70) return "Good";
  if (rssi >= -85) return "Weak";
  return "Very Weak";
}

String padDeviceNumber(int number) {
  if (number < 10)  return "DEV-00" + String(number);
  if (number < 100) return "DEV-0"  + String(number);
  return "DEV-" + String(number);
}

bool hasRealName(String name) {
  if (name.length() == 0) return false;
  if (name == "Unknown BLE Device") return false;
  return true;
}

String getAliasKey(String name, String address) {
  if (hasRealName(name)) return "NAME:" + name;
  return "ADDR:" + address;
}

int findAliasIndex(String key) {
  for (int i = 0; i < aliasCount; i++) {
    if (aliasKey[i] == key) return i;
  }
  return -1;
}

void saveAlias(String key, String label) {
  if (!sdReady) return;
  File file = SD.open(aliasFile, FILE_APPEND);
  if (file) {
    file.print(cleanCSV(key));
    file.print(",");
    file.println(cleanCSV(label));
    file.close();
  }
}

String getStableName(String name, String address) {
  String key   = getAliasKey(name, address);
  int    index = findAliasIndex(key);

  if (index != -1) return aliasLabel[index];

  if (aliasCount >= MAX_ALIASES) {
    if (hasRealName(name)) return name;
    return "UNKNOWN-LIMIT";
  }

  String label = hasRealName(name) ? name : padDeviceNumber(nextDeviceNumber++);

  aliasKey[aliasCount]   = key;
  aliasLabel[aliasCount] = label;
  aliasCount++;

  saveAlias(key, label);

  Serial.print("New stable alias: ");
  Serial.print(label);
  Serial.print(" = ");
  Serial.println(key);

  return label;
}

void loadAliasesFromSD() {
  if (!sdReady) return;

  if (!SD.exists(aliasFile)) {
    File file = SD.open(aliasFile, FILE_WRITE);
    if (file) {
      file.println("key,label");
      file.close();
      Serial.println("Created alias_map.csv");
    }
    return;
  }

  File file = SD.open(aliasFile, FILE_READ);
  if (!file) {
    Serial.println("Could not open alias_map.csv");
    return;
  }

  bool firstLine   = true;
  int  maxDevNumber = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (firstLine) {
      firstLine = false;
      if (line.startsWith("key,label")) continue;
    }

    int commaIndex = line.indexOf(',');
    if (commaIndex == -1) continue;

    String key   = line.substring(0, commaIndex);
    String label = line.substring(commaIndex + 1);
    key.trim();
    label.trim();

    if (key.length() == 0 || label.length() == 0) continue;
    if (aliasCount >= MAX_ALIASES) break;

    aliasKey[aliasCount]   = key;
    aliasLabel[aliasCount] = label;
    aliasCount++;

    if (label.startsWith("DEV-")) {
      int devNum = label.substring(4).toInt();
      if (devNum > maxDevNumber) maxDevNumber = devNum;
    }
  }

  file.close();
  nextDeviceNumber = maxDevNumber + 1;

  Serial.print("Aliases loaded: ");
  Serial.println(aliasCount);
  Serial.print("Next device number: ");
  Serial.println(nextDeviceNumber);
}

int findDeviceIndex(String address) {
  for (int i = 0; i < knownCount; i++) {
    if (knownAddress[i] == address) return i;
  }
  return -1;
}

int addDevice(String address, String name, String stableName, int rssi) {
  if (knownCount >= MAX_DEVICES) return -1;
  int index = knownCount;
  knownAddress[index]    = address;
  knownName[index]       = name;
  knownStableName[index] = stableName;
  knownRssi[index]       = rssi;
  knownPresent[index]    = true;
  seenThisScan[index]    = true;
  knownCount++;
  return index;
}

// =====================================================
// GPS
// =====================================================
void readGPS() {
  while (GPS_SERIAL.available() > 0) {
    gps.encode(GPS_SERIAL.read());
  }
}

void updateGPSValues() {
  gpsChars = String(gps.charsProcessed());

  if (gps.location.isValid()) {
    gpsStatus = "FIX";
    latitude  = String(gps.location.lat(), 6);
    longitude = String(gps.location.lng(), 6);
  } else {
    gpsStatus = "NO_FIX";
    latitude  = "NO_FIX";
    longitude = "NO_FIX";
  }

  satellites = gps.satellites.isValid() ? String(gps.satellites.value()) : "0";
  altitude   = gps.altitude.isValid()   ? String(gps.altitude.meters())  : "0";
}

// =====================================================
// SD LOGGING
// =====================================================
void writeHeaderIfNeeded() {
  if (!sdReady) return;
  if (!SD.exists(logFile)) {
    File file = SD.open(logFile, FILE_WRITE);
    if (file) {
      file.println("timestamp_ms,scan_number,event,lat,lng,gps_status,satellites,altitude_m,stable_name,ble_name,ble_address,rssi,signal_label");
      file.close();
      Serial.println("Created ble_events.csv");
    }
  }
}

void logEvent(String eventType, String stableName, String realName, String address, int rssi) {
  readGPS();
  updateGPSValues();

  String signal  = getSignalLabel(rssi);
  String csvLine = "";
  csvLine += String(millis()); csvLine += ",";
  csvLine += String(scanNumber); csvLine += ",";
  csvLine += eventType; csvLine += ",";
  csvLine += latitude; csvLine += ",";
  csvLine += longitude; csvLine += ",";
  csvLine += gpsStatus; csvLine += ",";
  csvLine += satellites; csvLine += ",";
  csvLine += altitude; csvLine += ",";
  csvLine += cleanCSV(stableName); csvLine += ",";
  csvLine += cleanCSV(realName); csvLine += ",";
  csvLine += address; csvLine += ",";
  csvLine += String(rssi); csvLine += ",";
  csvLine += signal;

  Serial.println(csvLine);

  if (sdReady) {
    File file = SD.open(logFile, FILE_APPEND);
    if (file) {
      file.println(csvLine);
      file.close();
    } else {
      Serial.println("SD write failed.");
    }
  }

  lastEventType  = eventType;
  lastStableName = stableName;
  lastBleAddress = address;
  lastRssi       = rssi;

  totalEventsLogged++;
}

// =====================================================
// LTE UPLOAD
// =====================================================
void initLTE() {
  Serial.println("Starting SIM7000 modem...");

  SIM_SERIAL.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
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

void uploadOverLTE() {
  if (!lteReady) {
    Serial.println("LTE not ready, skipping upload.");
    return;
  }

  String body = "{";
  body += "\"device_id\":\"esp32-board1\",";
  body += "\"timestamp\":\"" + String(millis()) + "\",";
  body += "\"lat\":\"" + latitude + "\",";
  body += "\"lng\":\"" + longitude + "\",";
  body += "\"mac\":\"" + lastBleAddress + "\",";
  body += "\"rssi\":" + String(lastRssi) + ",";
  body += "\"name\":\"" + lastStableName + "\",";
  body += "\"vendor_hint\":\"Hologram\",";
  body += "\"gps_status\":\"" + gpsStatus + "\",";
  body += "\"event\":\"" + lastEventType + "\"";
  body += "}";

  Serial.println("Uploading over LTE...");

  if (!gsmClient.connect(REMOTE_HOST, REMOTE_PORT)) {
    Serial.println("LTE connect failed.");
    return;
  }

  gsmClient.print(String("POST ") + REMOTE_PATH + " HTTP/1.1\r\n");
  gsmClient.print(String("Host: ") + REMOTE_HOST + "\r\n");
  gsmClient.print("Content-Type: application/json\r\n");
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
// BLE SCAN
// =====================================================
void scanBLEAndLog() {
  scanNumber++;

  Serial.println();
  Serial.println("===== BLE SCAN START =====");
  Serial.print("Scan number: ");
  Serial.println(scanNumber);

  for (int i = 0; i < knownCount; i++) seenThisScan[i] = false;

  readGPS();
  updateGPSValues();

  Serial.println("Scanning BLE...");

  BLEScanResults* results = pBLEScan->start(scanTime, false);
  int count = results->getCount();
  lastBleCount = count;

  Serial.print("BLE devices found: ");
  Serial.println(count);

  for (int i = 0; i < count; i++) {
    readGPS();
    updateGPSValues();

    BLEAdvertisedDevice device = results->getDevice(i);

    String realName   = device.haveName() ? device.getName().c_str() : "Unknown BLE Device";
    String address    = device.getAddress().toString().c_str();
    int    rssi       = device.getRSSI();
    String stableName = getStableName(realName, address);
    int    index      = findDeviceIndex(address);

    if (index == -1) {
      index = addDevice(address, realName, stableName, rssi);
      if (index != -1) logEvent("NEW", stableName, realName, address, rssi);
    } else {
      seenThisScan[index]      = true;
      knownName[index]         = realName;
      knownStableName[index]   = stableName;
      knownRssi[index]         = rssi;
      if (!knownPresent[index]) {
        knownPresent[index] = true;
        logEvent("RETURNED", stableName, realName, address, rssi);
      }
    }
  }

  for (int i = 0; i < knownCount; i++) {
    if (knownPresent[i] && !seenThisScan[i]) {
      knownPresent[i] = false;
      logEvent("LEFT", knownStableName[i], knownName[i], knownAddress[i], knownRssi[i]);
    }
  }

  pBLEScan->clearResults();
  Serial.println("===== BLE SCAN END =====");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32 BOARD 1: BLE + GPS + SD + LTE");
  Serial.println("------------------------------------");

  GPS_SERIAL.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS started.");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, SPI, 4000000)) {
    sdReady = false;
    Serial.println("SD card failed or not detected.");
  } else {
    sdReady = true;
    Serial.println("SD card initialized.");
    writeHeaderIfNeeded();
    loadAliasesFromSD();
  }

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  Serial.println("BLE scanner started.");

  initLTE();

  Serial.println("Board 1 ready.");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  readGPS();
  updateGPSValues();

  if (millis() - lastStatusPrint > 10000) {
    lastStatusPrint = millis();
    Serial.println();
    Serial.println("----- STATUS -----");
    Serial.print("GPS: ");       Serial.println(gpsStatus);
    Serial.print("Lat: ");       Serial.println(latitude);
    Serial.print("Lng: ");       Serial.println(longitude);
    Serial.print("Satellites: "); Serial.println(satellites);
    Serial.print("SD Ready: ");  Serial.println(sdReady ? "YES" : "NO");
    Serial.print("LTE Ready: "); Serial.println(lteReady ? "YES" : "NO");
    Serial.print("Known Devices: "); Serial.println(knownCount);
    Serial.print("Events Logged: ");  Serial.println(totalEventsLogged);
    Serial.println("------------------");
  }

  if (millis() - lastScan > scanInterval) {
    lastScan = millis();
    scanBLEAndLog();
  }

  if (millis() - lastUpload > UPLOAD_INTERVAL) {
    lastUpload = millis();
    uploadOverLTE();
  }
}

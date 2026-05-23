#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// =====================================================
// GPS WIRING
// GPS TX -> ESP32 GPIO16
// GPS RX -> ESP32 GPIO17
// =====================================================
TinyGPSPlus gps;
HardwareSerial GPS(1);

#define GPS_RX 16
#define GPS_TX 17

// =====================================================
// SD CARD WIRING
// SD VCC  -> 5V
// SD GND  -> GND
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
String logFile = "/ble_events.csv";
String aliasFile = "/alias_map.csv";

// =====================================================
// DASHBOARD SERIAL
// Logger GPIO25 -> Dashboard GPIO16
// Logger GND    -> Dashboard GND
// =====================================================
HardwareSerial DASHBOARD(2);

#define DASHBOARD_RX 26
#define DASHBOARD_TX 25

// =====================================================
// BLE
// =====================================================
BLEScan* pBLEScan;

const int scanTime = 30;  // 30 seconds to catch infrequent BLE advertisers
const unsigned long scanInterval = 31000;  // Run nearly continuously

unsigned long lastScan = 0;
unsigned long lastStatusPrint = 0;

int scanNumber = 0;
int lastBleCount = 0;
int totalEventsLogged = 0;

// =====================================================
// DEVICE TRACKING
// =====================================================
const int MAX_DEVICES = 100;

String knownAddress[MAX_DEVICES];
String knownName[MAX_DEVICES];
String knownStableName[MAX_DEVICES];
int knownRssi[MAX_DEVICES];
bool knownPresent[MAX_DEVICES];
bool seenThisScan[MAX_DEVICES];
bool knownLogged[MAX_DEVICES];

int knownCount = 0;

// =====================================================
// TARGET MAC ADDRESSES TO PRIORITIZE (PrimeAudio devices)
// =====================================================
const int TARGET_MAC_COUNT = 12;
const char* targetMacs[TARGET_MAC_COUNT] = {
  "41:42:4a:55:0c:fa",
  "41:42:d2:00:6b:09",
  "41:42:bf:56:72:36",
  "41:42:ef:ad:83:96",
  "41:42:f8:a2:8f:8f",
  "41:42:84:ca:20:16",
  "41:42:1b:70:67:e0",
  "41:42:81:69:16:3d",
  "41:42:69:bf:a4:49",
  "41:42:49:ad:71:73",
  "41:42:c5:31:2e:e9",
  "40:00:00:ee:5e:89"
};

bool isTargetMac(String address) {
  address.toLowerCase();
  for (int i = 0; i < TARGET_MAC_COUNT; i++) {
    if (address.equals(targetMacs[i])) return true;
  }
  return false;
}

// =====================================================
// STABLE NAME TRACKING
// =====================================================
const int MAX_ALIASES = 300;

String aliasKey[MAX_ALIASES];
String aliasLabel[MAX_ALIASES];

int aliasCount = 0;
int nextDeviceNumber = 1;

// =====================================================
// GPS VALUES
// =====================================================
String gpsStatus = "NO_FIX";
String latitude = "NO_FIX";
String longitude = "NO_FIX";
String satellites = "0";
String altitude = "0";
String gpsChars = "0";

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
  if (number < 10) return "DEV-00" + String(number);
  if (number < 100) return "DEV-0" + String(number);
  return "DEV-" + String(number);
}

bool hasRealName(String name) {
  if (name.length() == 0) return false;
  if (name == "Unknown BLE Device") return false;
  return true;
}

String getAliasKey(String name, String address) {
  if (hasRealName(name)) {
    return "NAME:" + name;
  }

  return "ADDR:" + address;
}

int findAliasIndex(String key) {
  for (int i = 0; i < aliasCount; i++) {
    if (aliasKey[i] == key) {
      return i;
    }
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
  String key = getAliasKey(name, address);

  int index = findAliasIndex(key);

  if (index != -1) {
    return aliasLabel[index];
  }

  if (aliasCount >= MAX_ALIASES) {
    if (hasRealName(name)) return name;
    return "UNKNOWN-LIMIT";
  }

  String label = "";

  if (hasRealName(name)) {
    label = name;
  } else {
    label = padDeviceNumber(nextDeviceNumber);
    nextDeviceNumber++;
  }

  aliasKey[aliasCount] = key;
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

  bool firstLine = true;
  int maxDevNumber = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    if (firstLine) {
      firstLine = false;

      if (line.startsWith("key,label")) {
        continue;
      }
    }

    int commaIndex = line.indexOf(',');

    if (commaIndex == -1) continue;

    String key = line.substring(0, commaIndex);
    String label = line.substring(commaIndex + 1);

    key.trim();
    label.trim();

    if (key.length() == 0 || label.length() == 0) continue;
    if (aliasCount >= MAX_ALIASES) break;

    aliasKey[aliasCount] = key;
    aliasLabel[aliasCount] = label;
    aliasCount++;

    if (label.startsWith("DEV-")) {
      int devNum = label.substring(4).toInt();

      if (devNum > maxDevNumber) {
        maxDevNumber = devNum;
      }
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
    if (knownAddress[i] == address) {
      return i;
    }
  }

  return -1;
}

int addDevice(String address, String name, String stableName, int rssi) {
  if (knownCount >= MAX_DEVICES) {
    return -1;
  }

  int index = knownCount;

  knownAddress[index] = address;
  knownName[index] = name;
  knownStableName[index] = stableName;
  knownRssi[index] = rssi;
  knownPresent[index] = true;
  seenThisScan[index] = true;
  knownLogged[index] = false;

  knownCount++;

  return index;
}

// =====================================================
// GPS
// =====================================================
void readGPS() {
  while (GPS.available() > 0) {
    gps.encode(GPS.read());
  }
}

void updateGPSValues() {
  gpsChars = String(gps.charsProcessed());

  if (gps.location.isValid()) {
    gpsStatus = "FIX";
    latitude = String(gps.location.lat(), 6);
    longitude = String(gps.location.lng(), 6);
  } else {
    gpsStatus = "NO_FIX";
    latitude = "NO_FIX";
    longitude = "NO_FIX";
  }

  satellites = gps.satellites.isValid() ? String(gps.satellites.value()) : "0";
  altitude = gps.altitude.isValid() ? String(gps.altitude.meters()) : "0";
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

void sendToDashboard(String eventType, String stableName, String realName, String address, int rssi) {
  String signal = getSignalLabel(rssi);

  String dashboardLine = "";
  dashboardLine += eventType;
  dashboardLine += ",";
  dashboardLine += latitude;
  dashboardLine += ",";
  dashboardLine += longitude;
  dashboardLine += ",";
  dashboardLine += gpsStatus;
  dashboardLine += ",";
  dashboardLine += satellites;
  dashboardLine += ",";
  dashboardLine += cleanCSV(stableName);
  dashboardLine += ",";
  dashboardLine += cleanCSV(realName);
  dashboardLine += ",";
  dashboardLine += address;
  dashboardLine += ",";
  dashboardLine += String(rssi);
  dashboardLine += ",";
  dashboardLine += signal;

  DASHBOARD.println(dashboardLine);
}

void logEvent(String eventType, String stableName, String realName, String address, int rssi) {
  readGPS();
  updateGPSValues();

  String signal = getSignalLabel(rssi);

  String csvLine = "";
  csvLine += String(millis());
  csvLine += ",";
  csvLine += String(scanNumber);
  csvLine += ",";
  csvLine += eventType;
  csvLine += ",";
  csvLine += latitude;
  csvLine += ",";
  csvLine += longitude;
  csvLine += ",";
  csvLine += gpsStatus;
  csvLine += ",";
  csvLine += satellites;
  csvLine += ",";
  csvLine += altitude;
  csvLine += ",";
  csvLine += cleanCSV(stableName);
  csvLine += ",";
  csvLine += cleanCSV(realName);
  csvLine += ",";
  csvLine += address;
  csvLine += ",";
  csvLine += String(rssi);
  csvLine += ",";
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

  sendToDashboard(eventType, stableName, realName, address, rssi);

  totalEventsLogged++;
}

// =====================================================
// BLE SCAN
// =====================================================
// =====================================================
// BLE CALLBACK - fires instantly when device detected
// =====================================================
class BLECallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    String realName = device.haveName() ? device.getName().c_str() : "Unknown BLE Device";
    String address = device.getAddress().toString().c_str();
    int rssi = device.getRSSI();

    bool isMacTarget = isTargetMac(address);
    bool isNameTarget = (realName.indexOf("Prime") >= 0 || realName.indexOf("prime") >= 0 ||
                         realName.indexOf("PRIME") >= 0 || realName.indexOf("Audio") >= 0);

    // Detect Google Fast Pair service UUID 0xFE2C (used by PrimeAudio/headsets)
    bool isFastPair = false;
    if (device.haveServiceUUID()) {
      String uuid = device.getServiceUUID().toString().c_str();
      if (uuid.indexOf("fe2c") >= 0 || uuid.indexOf("FE2C") >= 0) {
        isFastPair = true;
      }
    }
    // Also check service data for FE2C
    if (!isFastPair && device.haveServiceData()) {
      String svcData = device.getServiceDataUUID().toString().c_str();
      if (svcData.indexOf("fe2c") >= 0 || svcData.indexOf("FE2C") >= 0) isFastPair = true;
    }

    Serial.print("  " + address + " | " + realName + " | RSSI:" + String(rssi));
    if (isMacTarget) {
      Serial.println("  <<<<< TARGET MAC!");
    } else if (isNameTarget) {
      Serial.println("  <<<<< TARGET NAME! New MAC: " + address);
    } else if (isFastPair) {
      Serial.println("  <<<<< FAST PAIR DEVICE! Possible PrimeAudio - MAC: " + address);
    } else {
      Serial.println();
    }

    // Alert and log if MAC, name, or Fast Pair service matches
    if (isMacTarget || isNameTarget || isFastPair) {
      String label = isMacTarget ? getStableName(realName, address) : "PrimeAudio-TARGET";
      int index = findDeviceIndex(address);
      if (index == -1) {
        index = addDevice(address, realName, label, rssi);
        if (index != -1 && !knownLogged[index]) {
          logEvent("NEW", label, realName, address, rssi);
          sendToDashboard("NEW", label, realName, address, rssi);
          knownLogged[index] = true;
        }
      }
      lastBleCount++;
    }
  }
};

void scanBLEAndLog() {
  scanNumber++;
  lastBleCount = 0;

  Serial.println();
  Serial.println("===== BLE SCAN START =====");
  Serial.print("Scan number: ");
  Serial.println(scanNumber);

  for (int i = 0; i < knownCount; i++) {
    seenThisScan[i] = false;
  }
  // Note: knownLogged[] persists across scans (not reset)

  readGPS();
  updateGPSValues();

  Serial.println("Scanning BLE (callback mode)...");
  pBLEScan->start(scanTime, false);
  pBLEScan->clearResults();

  for (int i = 0; i < knownCount; i++) {
    if (knownPresent[i] == true && seenThisScan[i] == false) {
      knownPresent[i] = false;
      // No LEFT logging - only first detection is recorded
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
  Serial.println("ESP32 LOGGER: BLE + GPS + SD + STABLE NAMES");
  Serial.println("------------------------------------------");

  GPS.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS started.");

  DASHBOARD.begin(115200, SERIAL_8N1, DASHBOARD_RX, DASHBOARD_TX);
  Serial.println("Dashboard serial started.");
  Serial.println("Logger GPIO25 TX -> Dashboard GPIO16 RX");

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
  pBLEScan->setAdvertisedDeviceCallbacks(new BLECallback());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("BLE scanner started.");
  Serial.println("Logger ready.");
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
    Serial.println("----- LOGGER STATUS -----");
    Serial.print("GPS: ");
    Serial.println(gpsStatus);
    Serial.print("Lat: ");
    Serial.println(latitude);
    Serial.print("Lng: ");
    Serial.println(longitude);
    Serial.print("Satellites: ");
    Serial.println(satellites);
    Serial.print("GPS Chars: ");
    Serial.println(gpsChars);
    Serial.print("SD Ready: ");
    Serial.println(sdReady ? "YES" : "NO");
    Serial.print("Known Devices This Boot: ");
    Serial.println(knownCount);
    Serial.print("Stable Aliases: ");
    Serial.println(aliasCount);
    Serial.print("Last Scan Count: ");
    Serial.println(lastBleCount);
    Serial.print("Events Logged: ");
    Serial.println(totalEventsLogged);
    Serial.println("-------------------------");
  }

  if (millis() - lastScan > scanInterval) {
    lastScan = millis();
    scanBLEAndLog();
  }
}

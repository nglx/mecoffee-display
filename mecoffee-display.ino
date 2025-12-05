#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <U8g2lib.h>
#include <Wire.h>

// External OLED display (SH1107, 64x128, I2C address 0x3C on Port A)
U8G2_SH1107_64X128_F_HW_I2C oled(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// BLE
static BLEUUID meCoffeeServiceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");
BLEScan* pBLEScan = nullptr;
static BLEClient* pClient = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr;

// State
static bool connected = false;
static bool doConnect = false;
static bool brewing = false;
static bool showShotTime = false;

// Timing
static unsigned long shotStarted = 0;
static unsigned long lastShotActivity = 0;
static unsigned long lastDataReceived = 0;
static unsigned long lastShiftTime = 0;
static unsigned long lastLoopTime = 0;
static int tempYOffset = 0;

// Display values
static String currentTemperature = "";
static String currentShotTime = "";

// Constants
const int SCAN_TIME = 5;
const unsigned long SHOT_DISPLAY_TIMEOUT_MS = 60000;   // Hide shot time after 1 min
const unsigned long PIXEL_SHIFT_INTERVAL_MS = 5000;    // Shift every 5 sec
const unsigned long CONNECTION_TIMEOUT_MS = 120000;   // Watchdog: 2 min no data
const unsigned long LOOP_WATCHDOG_MS = 30000;         // Watchdog: 30 sec no loop

// Forward declarations
void sleepDisplay();
void wakeDisplay();
void updateOLED();
void fullReset();

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() &&
        advertisedDevice.isAdvertisingService(meCoffeeServiceUUID)) {
      Serial.println("Found meCoffee!");
      BLEDevice::getScan()->stop();

      if (myDevice != nullptr) delete myDevice;
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("BLE Connected");
    currentTemperature = "";
    currentShotTime = "";
    showShotTime = false;
    lastDataReceived = millis();
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("BLE Disconnected");
    connected = false;
    brewing = false;
    sleepDisplay();
  }
};

static MyClientCallback clientCallback;

void initOLED() {
  Wire.begin(32, 33);
  oled.setI2CAddress(0x3C << 1);

  if (oled.begin()) {
    Serial.println("OLED initialized");
    oled.setContrast(255);
    oled.setDrawColor(1);

    // Keep OLED off until connected (burn-in protection)
    oled.clearBuffer();
    oled.sendBuffer();
  } else {
    Serial.println("OLED init failed!");
  }
}

void setup() {
  M5.begin();
  Serial.begin(9600);
  Serial.println("meCoffee Display starting...");

  // Turn off M5StickC LCD
  M5.Axp.ScreenBreath(0);
  M5.Lcd.fillScreen(TFT_BLACK);

  initOLED();

  // Initialize BLE
  BLEDevice::init("meCoffeeDisplay");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  lastLoopTime = millis();
}

void updateOLED() {
  // Check if shot time should be hidden
  if (showShotTime && !brewing && (millis() - lastShotActivity > SHOT_DISPLAY_TIMEOUT_MS)) {
    showShotTime = false;
    currentShotTime = "";
  }

  // Pixel shifting when shot time not shown
  int tempY = 45;

  if (!showShotTime) {
    if (millis() - lastShiftTime > PIXEL_SHIFT_INTERVAL_MS) {
      lastShiftTime = millis();
      tempYOffset = (tempYOffset + 30) % 120;
    }
    tempY = 28 + tempYOffset;
  } else {
    tempYOffset = 0;
  }

  oled.clearBuffer();
  oled.setFont(u8g2_font_logisoso22_tr);

  int displayWidth = oled.getDisplayWidth();

  if (currentTemperature.length() > 0) {
    int tempWidth = oled.getStrWidth(currentTemperature.c_str());
    oled.drawStr(displayWidth - tempWidth - 3, tempY, currentTemperature.c_str());
  }

  if (showShotTime && currentShotTime.length() > 0) {
    int shotWidth = oled.getStrWidth(currentShotTime.c_str());
    oled.drawStr(displayWidth - shotWidth - 3, 105, currentShotTime.c_str());
  }

  oled.sendBuffer();
}

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {

  lastDataReceived = millis();
  String sData = (char*)pData;

  if (sData.startsWith("tmp")) {
    int i, reqTemp, curTemp;
    sscanf((char*)pData, "tmp %d %d %d", &i, &reqTemp, &curTemp);
    currentTemperature = String(curTemp / 100) + "C";
    updateOLED();

  } else if (sData.startsWith("sht")) {
    int i, ms;
    sscanf((char*)pData, "sht %d %d", &i, &ms);

    if (ms == 0) {
      brewing = true;
      shotStarted = millis();
    } else {
      brewing = false;
    }

    showShotTime = true;
    lastShotActivity = millis();
    currentShotTime = String(ms / 1000) + "s";
    updateOLED();
  }

  if (!sData.startsWith("sht") && brewing) {
    lastShotActivity = millis();
    currentShotTime = String((millis() - shotStarted) / 1000) + "s";
    updateOLED();
  }
}

bool connectToServer() {
  Serial.println("Connecting to meCoffee...");

  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&clientCallback);
  }

  if (pClient->isConnected()) {
    pClient->disconnect();
    delay(100);
  }

  if (!pClient->connect(myDevice)) {
    Serial.println("Connection failed");
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(meCoffeeServiceUUID);
  if (pRemoteService == nullptr) {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Characteristic not found");
    pClient->disconnect();
    return false;
  }

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);
  }

  connected = true;
  lastDataReceived = millis();
  Serial.println("Connected!");
  wakeDisplay();

  return true;
}

void disconnectAndReset() {
  Serial.println("Soft reset...");

  if (pClient != nullptr && pClient->isConnected()) {
    pClient->disconnect();
  }

  connected = false;
  brewing = false;
  doConnect = false;
  currentTemperature = "";
  currentShotTime = "";
  showShotTime = false;

  sleepDisplay();
}

void fullReset() {
  Serial.println("Full system reset!");
  delay(100);
  ESP.restart();
}

void sleepDisplay() {
  oled.clearBuffer();
  oled.sendBuffer();
}

void wakeDisplay() {
  // Display will be updated by notifyCallback
}

void loop() {
  M5.update();
  lastLoopTime = millis();

  // Button A (big button): Force full restart
  if (M5.BtnA.wasPressed()) {
    Serial.println("Button pressed - restarting...");
    fullReset();
  }

  // Watchdog: no data for too long while "connected"
  if (connected && (millis() - lastDataReceived > CONNECTION_TIMEOUT_MS)) {
    Serial.println("Watchdog: no data - resetting connection");
    disconnectAndReset();
  }

  // Scan when not connected
  if (!connected && !doConnect) {
    BLEScanResults* foundDevices = pBLEScan->start(SCAN_TIME, false);
    Serial.printf("Scan: %d devices\n", foundDevices->getCount());
    pBLEScan->clearResults();
  }

  // Connect when device found
  if (doConnect) {
    if (!connectToServer()) {
      Serial.println("Connect failed - will retry");
      delay(1000);
    }
    doConnect = false;
  }

  // Update OLED
  if (connected) {
    updateOLED();
  }

  delay(1000);
}

#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <U8g2lib.h>
#include <Wire.h>

// External OLED display (SH1107, 64x128, I2C address 0x3C on Port A)
// Using R3 rotation for landscape mode, flipped 180° (for upside-down mounting)
U8G2_SH1107_64X128_F_HW_I2C oled(U8G2_R3, /* reset=*/ U8X8_PIN_NONE);
bool oledConnected = false;

int scanTime = 5; //In seconds
BLEScan* pBLEScan;

static BLEUUID meCoffeeServiceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean brewing = false;
static unsigned long shotStarted = -1;

static BLEAdvertisedDevice* myDevice;
static BLERemoteCharacteristic* pRemoteCharacteristic;

static String currentShotTime = "";
static String currentTemperature = "";

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());

      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(meCoffeeServiceUUID)) {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
      }
    }
};

void initOLED() {
  Wire.begin(32, 33);  // M5StickC Plus Port A: SDA=32, SCL=33

  // Try to initialize OLED
  oled.setI2CAddress(0x3C << 1);  // U8g2 needs address shifted
  oledConnected = oled.begin();

  if (oledConnected) {
    Serial.println("OLED connected!");
    oled.clearBuffer();
    oled.setFont(u8g2_font_helvB12_tr);  // Smaller font for "Scanning..."
    oled.drawStr(25, 38, "Scanning...");
    oled.sendBuffer();

    // Disable main LCD when external OLED is connected
    M5.Axp.ScreenBreath(0);
  } else {
    Serial.println("OLED not found");
  }
}

void updateOLED() {
  if (!oledConnected) return;

  oled.clearBuffer();
  oled.setFont(u8g2_font_logisoso28_tr);  // Clean digital-style font

  int displayWidth = oled.getDisplayWidth();

  // Draw temperature (top, right-aligned)
  if (currentTemperature.length() > 0) {
    int tempWidth = oled.getStrWidth(currentTemperature.c_str());
    oled.drawStr(displayWidth - tempWidth - 5, 32, currentTemperature.c_str());
  }

  // Draw shot time (bottom, right-aligned)
  if (currentShotTime.length() > 0) {
    int shotWidth = oled.getStrWidth(currentShotTime.c_str());
    oled.drawStr(displayWidth - shotWidth - 5, 62, currentShotTime.c_str());
  }

  oled.sendBuffer();
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(1);  // Landscape mode

  Serial.begin(115200);
  Serial.println("Scanning...");

  // Initialize external OLED
  initOLED();

  // Show "Scanning..." on main LCD only if OLED not connected
  if (!oledConnected) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("Scanning...", M5.Lcd.width() / 2, M5.Lcd.height() / 2);
    M5.Lcd.setTextDatum(MR_DATUM);
    delay(2000);
  }

  M5.Lcd.setTextSize(5);
  sleepDisplay();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    currentShotTime = "";
    currentTemperature = "";
    drawShotTime("0s", TFT_LIGHTGREY);
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    brewing = false;
    Serial.println("onDisconnect");
    sleepDisplay();
  }
};

void drawTemperature(String temperature, uint16_t color) {
  if (currentTemperature != temperature) {
    currentTemperature = temperature;

    // Draw to main LCD only if OLED not connected
    if (!oledConnected) {
      if (currentTemperature.length() == 4 && temperature.length() == 3) {
        M5.Lcd.setTextColor(TFT_BLACK, TFT_BLACK);
        M5.Lcd.drawString(currentTemperature, M5.Lcd.width() - 7, M5.Lcd.height() / 4 + 10);
      }
      M5.Lcd.setTextColor(color, TFT_BLACK);
      M5.Lcd.drawString(currentTemperature, M5.Lcd.width() - 7, M5.Lcd.height() / 4 + 10);
    }

    updateOLED();  // Update external OLED
  }
}

void drawShotTime(String shotTime, uint16_t color) {
  if (currentShotTime != shotTime) {
    currentShotTime = shotTime;

    // Draw to main LCD only if OLED not connected
    if (!oledConnected) {
      M5.Lcd.setTextColor(color, TFT_BLACK);
      M5.Lcd.drawString(" " + currentShotTime, M5.Lcd.width() - 7, 3 * (M5.Lcd.height() / 4) - 10);
    }

    updateOLED();  // Update external OLED
  }
}

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    String sData = (char*)pData;

    if (sData.startsWith("tmp")) {
      int i, reqTemp, curTemp;

      sscanf((char*)pData, "tmp %d %d %d", &i, &reqTemp, &curTemp);

      uint16_t color = (curTemp > (reqTemp - 100)) ? TFT_GREEN : TFT_ORANGE;

      drawTemperature(String(curTemp / 100) + "C", color);
    } else if (sData.startsWith("sht")) {
      int i, ms;
      uint16_t color;

      sscanf((char*)pData, "sht %d %d", &i, &ms);
      if (ms == 0) {
        brewing = true;

        shotStarted = millis();
        color = TFT_ORANGE;
      } else {
        brewing = false;
        color = TFT_GREEN;
      }

      drawShotTime(String(ms / 1000) + "s", color);
    }

    if (!sData.startsWith("sht") && brewing) {
      drawShotTime(String((millis() - shotStarted) / 1000) + "s", TFT_ORANGE);
    }
}

void connectToServer() {
  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pClient->connect(myDevice);

  BLERemoteService* pRemoteService = pClient->getService(meCoffeeServiceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(meCoffeeServiceUUID.toString().c_str());
    pClient->disconnect();

    connected = false;
    sleepDisplay();
    return;
  }

  Serial.println(" - Found our service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();

    connected = false;
    sleepDisplay();
    return;
  }
  Serial.println(" - Found our characteristic");

  if(pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  Serial.println("Connected to meCoffee");
  wakeDisplay();
}

void sleepDisplay() {
  // Always turn off main LCD
  M5.Axp.ScreenBreath(0);
  M5.Lcd.fillScreen(TFT_BLACK);

  // Clear external OLED
  if (oledConnected) {
    oled.clearBuffer();
    oled.sendBuffer();
  }
}

void wakeDisplay() {
  M5.Lcd.fillScreen(TFT_BLACK);

  // Only turn on main LCD if external OLED is NOT connected
  if (!oledConnected) {
    M5.Axp.ScreenBreath(100);  // Turn on backlight via AXP192 (0-100, 100=max)
  }
}

void loop() {
  M5.update();  // Update button states

  if (!connected) {
    BLEScanResults* foundDevices = pBLEScan->start(scanTime, false);
    Serial.print("Devices found: ");
    Serial.println(foundDevices->getCount());

    Serial.println("Scan done!");
    pBLEScan->clearResults();
    delay(200);
  }

  if (doConnect == true) {
    Serial.println("Found meCoffee");
    connectToServer();
    doConnect = false;
  }

  delay(2000);
}

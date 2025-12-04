#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

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

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");

  pinMode(TFT_BL, OUTPUT);
  tft.init();

  tft.fillScreen(TFT_BLACK); // Clear Screen
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Scanning...",  tft.width() / 2, tft.height() / 2 );
  tft.setTextDatum(MR_DATUM);

  delay(2000);
  tft.setTextSize(5);
  sleepDisplay();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
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
    if (currentTemperature.length() == 4 && temperature.length() == 3) {
      tft.setTextColor(TFT_BLACK, TFT_BLACK);
      tft.drawString(currentTemperature, tft.width() - 7, tft.height() / 4 + 10 );
    }

    currentTemperature = temperature;

    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(currentTemperature, tft.width() - 7, tft.height() / 4 + 10 );
  }
}

void drawShotTime(String shotTime, uint16_t color) {
  if (currentShotTime != shotTime) {
//    tft.setTextColor(TFT_BLACK, TFT_BLACK);
//    tft.drawString(currentShotTime, tft.width() - 5, 3 * (tft.height() / 4) );

    currentShotTime = shotTime;

    tft.setTextColor(color, TFT_BLACK);
    tft.drawString(" " + currentShotTime, tft.width() - 7, 3 * (tft.height() / 4) - 10 );
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
  BLEClient*  pClient  = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)


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
  digitalWrite(TFT_BL, LOW);  // Backlight OFF
  tft.fillScreen(TFT_BLACK);
}

void wakeDisplay() {
  tft.fillScreen(TFT_BLACK);  // Clear any stale content
  digitalWrite(TFT_BL, HIGH); // Backlight ON
}

void loop() {
  if (!connected) {
    BLEScanResults* foundDevices = pBLEScan->start(scanTime, false);
    Serial.print("Devices found: ");
    Serial.println(foundDevices->getCount());


    Serial.println("Scan done!");
    pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
    delay(200);
  }

  if (doConnect == true) {
    Serial.println("Found meCoffee");
    connectToServer();
    doConnect = false;
  }

  delay(2000);
}

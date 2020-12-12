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
static BLEAdvertisedDevice* myDevice;
static BLERemoteCharacteristic* pRemoteCharacteristic;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());

      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(meCoffeeServiceUUID)) {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
      } // Found our server
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");
  
  tft.init();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
//  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
    tft.fillScreen(TFT_BLACK);
  }
};


static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    String sData = (char*)pData;

    if (sData.startsWith("tmp")) {

      int i;
      int reqTemp;
      int curTemp;

      sscanf((char*)pData, "tmp %d %d %d", &i, &reqTemp, &curTemp);

      if (abs(curTemp - reqTemp) < 100) {
        tft.fillScreen(TFT_GREEN);
      } else {
        tft.fillScreen(TFT_BLACK);
      }
      float temp = curTemp / 100.0;
      Serial.printf("%.2f\n", temp);
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
      return;
    }

    Serial.println(" - Found our service");

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();

      connected = false;
      return;
    }
    Serial.println(" - Found our characteristic");

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    Serial.println("Connected to meCoffee");
}

void loop() {
  if (!connected) {
    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
    Serial.print("Devices found: ");
    Serial.println(foundDevices.getCount());


    Serial.println("Scan done!");
    pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
    
    tft.fillScreen(TFT_GREEN);
    delay(200);
    tft.fillScreen(TFT_BLACK);
  }

  if (doConnect == true) {
    Serial.println("Found meCoffee");
    connectToServer();
    doConnect = false;
  }


  delay(2000);
}

/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

// Advertised Device: Name: , Address: 18:93:d7:0d:10:17, manufacturer data: 484d1893d70d1017, serviceUUID: 0000ffe0-0000-1000-8000-00805f9b34fb, txPower: 0 

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define LED 2

int scanTime = 5; //In seconds
BLEScan* pBLEScan;

static BLEUUID meCoffeeServiceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID    charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");


char str[] = "Hello";

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

  pinMode(LED,OUTPUT);
  
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
    digitalWrite(LED,LOW);
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
        digitalWrite(LED,HIGH);
      } else {
        digitalWrite(LED,LOW);
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
    digitalWrite(LED,HIGH);
    delay(200);
    digitalWrite(LED,LOW);
  }
  
  if (doConnect == true) {
    Serial.println("Found meCoffee");
    connectToServer();
    doConnect = false;
  }  


  delay(2000);
}

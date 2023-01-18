#include <list>
#include "BLEDevice.h"
#include <Adafruit_GFX.h>
#include <TFT_eSPI.h>
//#include "free_fonts.h"

#define BUTTON1PIN 35
#define BUTTON2PIN 0

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

#define GOVEE_BT_mac_OUI_PREFIX "a4:c1:38"
#define H5075_UPDATE_UUID16  "88:ec"

#define FINE_SCAN true

bool DEBUG = false;

// Create object "tft"
TFT_eSPI display = TFT_eSPI();

//Declare BLEScanner
BLEScan* pBLEScan;

int num = 1;

// Sensor
struct tempSensor {
  std::string mac;
  std::string type;
  double temp;
  double hum;
  double bat;
  int num;
  int rssi;
  int battype = 0; // 0=Volts, 1=Percent
};

#define BAT_VOLT 0
#define BAT_PERCENT 1

std::list<tempSensor> sensors;

std::__cxx11::string string_to_hex(const std::__cxx11::string& input)
{
    static const char hex_digits[] = "0123456789ABCDEF:";

    std::string output;
    output.reserve(input.length() * 3);
    for (unsigned char c : input)
    {
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
        output.push_back(hex_digits[16]);
    }
    return output;
}

tempSensor getSensor(std::string mac) {
  for (tempSensor t : sensors) {
    if (t.mac == mac) return t;
  }
  tempSensor t1;
  t1.mac = mac;
  t1.type = "new";
  t1.num = sensors.size() + 1;
  sensors.push_back(t1);
  return sensors.back();
}

tempSensor getSensor(int n) {
  for (tempSensor t : sensors) {
    if (t.num == n) return t;
  }
}

void setSensor(std::string mac, std::string type, double temp, double hum, double bat, int rssi, int battype) {
  for (auto it = sensors.rbegin(); it != sensors.rend(); it++) {
    if (it->mac == mac) {
      it->type = type;
      it->temp = temp;
      it->hum = hum;
      it->bat = bat;
      it->rssi = rssi;
      it->battype = battype;
      return;
    }
  }
}

//function that prints the latest sensor readings in the OLED display
void displayScreen(tempSensor t) {
  display.fillScreen(TFT_BLACK);
  uint8_t y = 0;
  uint8_t d = 34;
  uint8_t x = 0;

  // display sensor numbers
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  for (int i=1; i <= sensors.size(); i++) {
    display.setCursor(x + (i-1)*20, y);
    if (t.num == i) {
      display.print("o");
    } else {
      display.print(".");
    }
  }

  // display device
  x=0;
  y=22;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  display.printf("Device %02u (%s)",t.num ,t.type.c_str());
  display.setTextFont(0);
  display.setTextSize(2);
  display.setCursor(x,y+18);
  display.print(t.mac.c_str());

  // display temperature
  y=y+d+7;  
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.setCursor(x,y);
  display.setTextSize(1);
  display.setTextFont(6);
  display.print(int(t.temp));
  display.setTextFont(4);
  display.printf(".%02u°C",int((t.temp - int(t.temp))*100));
  //display.write(167);

  //display humidity 
  x=x+130;
  display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  display.setCursor(x, y);
  display.setTextFont(6);
  display.print(int(t.hum));
  display.setTextFont(4);
  display.printf(".%02u%%",int((t.hum - int(t.hum))*100));

  //display Battery
  x=0;
  y=y+d+5;
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x, y+10);
  display.printf("Bat: ");
  display.setTextFont(4); 
  switch (t.battype)
  {
  case BAT_VOLT:
    display.printf("%.2f", t.bat);
    display.print("V");
    break;
  case BAT_PERCENT:
    display.printf("%.0f", t.bat);
    display.print("%");
    break;
  default:
    break;
  }

  //display RSSI
  x=130;
  display.setTextColor(TFT_GREEN, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x, y+10);
  display.printf("RSSI: ");
  display.setTextFont(4);
  display.printf("%02ddb", t.rssi);

  display.setTextFont(0);
  display.setTextSize(1);
}

void displaySensor(std::string mac) {
  displayScreen(getSensor(mac));
}

void displaySensor(int i ){
  displayScreen(getSensor(i));
}

void printReadings(double temp, double hum, double bat, int rssi, int battype) {
  Serial.print("Temperature:");
  Serial.print(temp);
  Serial.print("C");
  Serial.print(" Humidity:");
  Serial.print(hum); 
  Serial.print("%");
  Serial.print(" Battery:");
  switch (battype) {
  case BAT_VOLT:
    Serial.printf("%.2f", bat);
    Serial.print("V");
    break;
  case BAT_PERCENT:
    Serial.printf("%.0f", bat);
    Serial.print("%");
    break;
  default:
    break;
  }
  Serial.print(" RSSI:");
  Serial.print(rssi);
  Serial.println("db");
}

//Callback function that gets called, when another device's advertisement has been received
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    std::string mac;
    tempSensor t;
    double temp;
    double hum;
    double bat;
    int rssi;
    int battype;

    rssi = advertisedDevice.getRSSI();
    mac = advertisedDevice.getAddress().toString();
    std::__cxx11::string data = advertisedDevice.getManufacturerData();
    if (DEBUG) {
      Serial.print("Found Device Advertisement. Manufacturer=");
      Serial.print(mac.substr(0,8).c_str());
      Serial.print(" - Device Name: ");
      Serial.println(advertisedDevice.getName().c_str());
      Serial.print("Raw Data, length=");
      Serial.println(data.size());
      Serial.println(string_to_hex(data).c_str());
    }

    if (advertisedDevice.getName() == "ThermoBeacon") { //Check if the name of the advertiser matches   

      if (data.length() == 20) {
        Serial.print("ThermoBeacon Data Received: ");
        Serial.println(mac.c_str());

        t = getSensor(mac);
        if (t.type == "new") {
          Serial.print("Found New Sensor! Type=ThermoBeacon count=");
          Serial.println(sensors.size());
        }

        temp = ((double)data[12] + 256 * (double)data[13]) / 16;
        hum = ((double)data[14] + 256 * (double)data[15]) / 16;
        bat = ((double)data[10] + 256 *(double)data[11]) / 1000;
        battype = BAT_VOLT;

        setSensor(mac,"ThermoBeacon",temp,hum,bat,rssi,battype);
        printReadings(temp,hum,bat,rssi,battype);

        if (t.type == "new" || t.num == num) {
          displaySensor(mac);
          num = t.num;
        }
      }
      if (data.length() == 22) {
 
      }
    }
    if (mac.substr(0,8) == GOVEE_BT_mac_OUI_PREFIX && data.length() > 15 ) { //data.lengh() == 17 ??
      Serial.print("Govee H5075 Data Received: ");
      Serial.println(mac.c_str());
      Serial.print("Manufacturer Key: ");
      Serial.println(string_to_hex(data.substr(2,2)).c_str());

      // Ist das Paket, die payload 10 Byte lang oder 17 byte ??
      //                      09:ff:88:ec:00:03:32:9c:64:00 (value = 5,6,7 und bat = 8)
      // 03:03:88:ec:02:01:05:09:ff:88:ec:00:03:32:9c:64:00 (value = 12,13,14 und bat = 15)

      u_int32_t value = data[12] * 65535 + data[13] * 256 + data[14];
      
      if (value & 0x800000) {
        temp = double((value ^ 0x800000) / -10000);
      } else {
        temp = double(value / 10000);
      }
      hum = double(value % 1000) / 10;
      bat = double(data[15]);
      battype = BAT_PERCENT;

      t = getSensor(mac);
      if (t.type == "new") {
        Serial.print("Found New Sensor! Type=H5075 count=");
        Serial.println(sensors.size());
      }

      setSensor(mac,"Govee H5075",temp,hum,bat,rssi,battype);
      printReadings(temp,hum,bat,rssi,battype);

      if (t.type == "new" || t.num == num) {
        displaySensor(mac);
        num = t.num;
      }
    }
    if (DEBUG) Serial.println();
  }
};

void IRAM_ATTR toggleButton1() {
  if (num < sensors.size()) {
    num++;
  } else {
    num = 1;
  }
  displaySensor(num);
}

void IRAM_ATTR toggleButton2() {
  if ( num > 1 ) {
    num--;
  } else {
    num = sensors.size();
  }
  displaySensor(num);
}

void setup() {
  pinMode(BUTTON1PIN, INPUT);
  pinMode(BUTTON2PIN, INPUT);
  attachInterrupt(BUTTON1PIN, toggleButton1, RISING);
  attachInterrupt(BUTTON2PIN, toggleButton2, RISING);

  display.init();
  display.setRotation(1);
  display.fillScreen(TFT_BLACK);

  display.setTextSize(1);
  display.setTextFont(4);
  display.setTextColor(TFT_WHITE,0);
  display.setCursor(0,25);
  display.print("BLE2MQTT starting...");
  
  //Start serial communication
  Serial.begin(115200);
  Serial.println("BLE2MQTT starting...");

  //Init BLE device
  BLEDevice::init("");
 
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setInterval(80);
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), FINE_SCAN);
  pBLEScan->setWindow(30);
  pBLEScan->setActiveScan(true);
  display.println("Searching Sensors");
}

void loop() {
  Serial.println("Start Scanning...");

  /* Async Scanning
  pBLEScan->start(0, nullptr, false); // Non Blocking Scan
  
  unsigned long start = millis();
  while (millis() - start < 60000) {

  }
  pBLEScan->stop();
  */

  pBLEScan->start(60); // Blocking Scab
  Serial.println("Stop Scanning...");
  delay(10);
  
  // Push Data to Data Logging Service

}

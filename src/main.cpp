#include <list>
#include "BLEDevice.h"
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <aes/esp_aes.h>
#include <array>

#define S3
#define WIFI
#define MQTT
bool DEBUG = false;
String version = "V2.3";

#ifdef WIFI
#include <WiFi.h>
#endif

#ifdef MQTT
#include <PubSubClient.h>
#endif
#ifdef S3
#define BUTTON1PIN 14
#define BUTTON2PIN 0
#else
#define BUTTON1PIN 35
#define BUTTON2PIN 0
#endif

#ifdef S3
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 170
#else
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135
#endif

#define GOVEE_BT_mac_OUI_PREFIX "a4:c1:38"
#define H5075_UPDATE_UUID16  "88:ec"
#define VICTRON_BT_mac_OUI_PREFIX "60:a4:23"
#define SMARTSOLAR_ENCRYPTION_KEY 0x0df4d0395b7d1a876c0c33ecb9e70aea
#define VICTRON_ENCRYPTED_DATA_MAX_SIZE 16

#define FINE_SCAN true

// WiFi Icon
const unsigned char wifiicon[] PROGMEM  = {
	0b00000000, 0b00000000, //                 
	0b00000111, 0b11100000, //      ######     
	0b00011111, 0b11111000, //    ##########   
	0b00111111, 0b11111100, //   ############  
	0b01110000, 0b00001110, //  ###        ### 
	0b01100111, 0b11100110, //  ##  ######  ## 
	0b00001111, 0b11110000, //     ########    
	0b00011000, 0b00011000, //    ##      ##   
	0b00000011, 0b11000000, //       ####      
	0b00000111, 0b11100000, //      ######     
	0b00000100, 0b00100000, //      #    #     
	0b00000001, 0b10000000, //        ##       
	0b00000001, 0b10000000, //        ##       
	0b00000000, 0b00000000, //                 
	0b00000000, 0b00000000, //                 
	0b00000000, 0b00000000, //                 
};

#ifdef MQTT
String basetopic = "/openhab/in/";
String conftopic = "/openhab/configuration/";
String getconftopic = "/openhab/configuration";
String cmdtopic;
String timetopic = "/openhab/Daytime";
String datetopic = "/openhab/DayDate";
String debugtopic = "/openhab/debug/";

IPAddress broker_int(192,168,20,17);          // Address of the MQTT broker SSID=UPC4E87B2D
IPAddress broker_ext(82,165,176,152);         
IPAddress broker_openhab(192,168,1,1);        // Address of the MQTT broker SSID=OpenHAB
IPAddress broker;
String ssid;
IPAddress ip;

WiFiClient wificlient;
PubSubClient client(wificlient);
#endif

String mqttdate = "Mo,00.00.0000";
String mqtttime = "00:00";

int MQ_COLOR = TFT_WHITE;

u_int64_t espID;
String client_id;

// Create object "tft"
TFT_eSPI display = TFT_eSPI();
bool displayON = true;

//Declare BLEScanner
BLEScan* pBLEScan;

int num = 0;

#define BAT_VOLT 0
#define BAT_PERCENT 1

// Sensor
struct tempSensor {
  std::string mac;
  std::string type;
  std::string name;
  std::string fullname = "none";
  std::string device;
  double temp;
  double hum;
  double bat;
  int num;
  int rssi;
  int battype = 0; // 0=Volts, 1=Percent
  String lastupdate = "";
};

std::list<tempSensor> sensors;

std::__cxx11::string string_to_hex(const std::__cxx11::string& input, int length = 0)
{
    static const char hex_digits[] = "0123456789ABCDEF:";

    std::string output;
    if (length == 0) length = input.length();
    output.reserve(length * 3);

    for (int i=0; i < length; i++)
    {
        unsigned char c = input[i];
        output.push_back(hex_digits[c >> 4]);
        output.push_back(hex_digits[c & 15]);
        output.push_back(hex_digits[16]);
    }
    return output;
}

/**
 * Print Debug Output to Serial and/or MQTT
**/
void debugPrintln(std::__cxx11::string msg) {
  if (client.connected()) {
    client.publish(debugtopic.c_str(), msg.c_str());
  }
  Serial.println(msg.c_str());
}

tempSensor getSensor(std::string mac, std::string name) {
  for (tempSensor t : sensors) {
    if (t.mac == mac) return t;
  }
  tempSensor t1;
  t1.mac = mac;
  t1.device = (mac.substr(0,2) + mac.substr(3,2) + mac.substr(6,2) + mac.substr(9,2) + mac.substr(12,2) + mac.substr(15,2)).c_str();
  t1.type = "new";
  t1.name = name;
  t1.num = sensors.size() + 1;
  sensors.push_back(t1);
  #ifdef MQTT
  String msg = "getconfig:"+client_id;
  client.publish(getconftopic.c_str(),msg.c_str());
  #endif
  return sensors.back();
}

tempSensor getSensor(std::string mac) {
  tempSensor t1;
  for (tempSensor t : sensors) {
    if (t.mac == mac) return t;
  }
  return t1;
}

tempSensor getSensor(int n) {
  tempSensor t1;
  for (tempSensor t : sensors) {
    if (t.num == n) return t;
  }
  return t1;
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
      it->lastupdate = mqttdate + " " + mqtttime;
      return;
    }
  }
}

void setSensorName(std::string device, std::string fullname) {
  for (auto it = sensors.rbegin(); it != sensors.rend(); it++) {
    if (it->device == device) {
      it->fullname = fullname;
      return;
    }
  }
}

void display_indicators() {
  //display WiFI & MQTT Connection
  u_int16_t col = TFT_WHITE;
  #ifdef WIFI
  if (WiFi.status() == WL_CONNECTED) {
    col = TFT_GREEN;
  } else {
    col = TFT_RED;
  }
  #endif 
  display.drawBitmap(240 - 50, 2, wifiicon,16,16,col);
  display.setTextColor(MQ_COLOR, TFT_BLACK);
  display.setTextFont(2);
  display.setCursor(240 - 20, 0);
  display.print("MQ");
  display.setTextColor(TFT_WHITE);
}

void display_indicators(int col) {
  MQ_COLOR = col;
  display_indicators();
}

void displayDateTime() {
  display.fillScreen(TFT_BLACK);
  
  if (!displayON) {
    display_indicators();
    return;
  }
    
  uint8_t y = 0;
  uint8_t d = 34;
  uint8_t x = 0;

  // display sensor numbers
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  for (int i=0; i <= sensors.size(); i++) {
    display.setCursor(x + i*20, y);
    if (i == 0) {
      display.print("o");
    } else {
      display.print(".");
    }
  }

  display_indicators();

  // display Time
  x=0;
  y=24;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  display.printf("Device: %s",client_id.c_str());

  x = 50;
  y=y+22;
  display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(6);
  display.setCursor(x,y);
  display.printf(mqtttime.c_str());

  // display Date
  x = 30;
  y=y+d+12;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(4);
  display.setCursor(x,y);
  display.printf(mqttdate.c_str());

    // display WiFi
  x = 0;
  y=y+26;
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.setTextFont(2);
  display.setCursor(x,y);
  //wifi_power_t tx = WiFi.getTxPower();
  display.printf("%s - %s",ssid.c_str(),ip.toString().c_str());
}

//function that prints the latest sensor readings in the OLED display
void displayScreen(tempSensor t) {
  display.fillScreen(TFT_BLACK);

  if (!displayON) {
    display_indicators();
    return;
  }

  uint8_t y = 0;
  uint8_t d = 34;
  uint8_t x = 0;

  // display sensor numbers
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  for (int i=0; i <= sensors.size(); i++) {
    display.setCursor(x + i*20, y);
    if (t.num == i) {
      display.print("o");
    } else {
      display.print(".");
    }
  }

  display_indicators();

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
  if (t.fullname == "none") {
    display.print(t.mac.c_str());
  } else {
    display.print(t.fullname.c_str());
  }
  // display temperature
  y=y+d+9;  
  display.setTextColor(TFT_ORANGE, TFT_BLACK);
  display.setCursor(x,y);
  display.setTextSize(1);
  display.setTextFont(6);
  int sign = 1;
  if (t.temp < 0.0 ) sign = -1;
  display.printf("%d", (int)t.temp);
  display.setTextFont(4);
  // Set Precision
  double value = (t.temp - int(t.temp)) * sign;
  if ( t.type == "ThermoBeacon") {
    display.printf(".%02dC",(int)(value*100.0));
  } else {
    display.printf(".%01dC",(int)(value*10.0));
  }
  
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

void displaySensor(int i){
  if ( i == 0 ) { 
    displayDateTime();
  } else {
    displayScreen(getSensor(i));
  }
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

bool decrypt_message_(const u_int8_t *crypted_data, const u_int8_t crypted_len,
                                  u_int8_t encrypted_data[VICTRON_ENCRYPTED_DATA_MAX_SIZE],
                                  const u_int8_t data_counter_lsb, const u_int8_t data_counter_msb) {
  esp_aes_context ctx;
  esp_aes_init(&ctx);
  unsigned char bindkey = (unsigned char) SMARTSOLAR_ENCRYPTION_KEY;
  //std::array<uint8_t, 16> key;
  //key = (std::array<uint8_t, 16>)(unsigned char) SMARTSOLAR_ENCRYPTION_KEY;
  //bindkey = (std::array<uint8_t, 16>)SMARTSOLAR_ENCRYPTION_KEY;
  auto status = esp_aes_setkey(&ctx, &bindkey, 16 * 8);
  if (status != 0) {
    Serial.printf("Error during esp_aes_setkey operation (%i).", status);
    esp_aes_free(&ctx);
    return false;
  }

  size_t nc_offset = 0;
  u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
  u_int8_t stream_block[16] = {0};

  status = esp_aes_crypt_ctr(&ctx, crypted_len, &nc_offset, nonce_counter, stream_block, crypted_data, encrypted_data);
  if (status != 0) {
    //ESP_LOGE(TAG, "[%s] Error during esp_aes_crypt_ctr operation (%i).", this->address_str().c_str(), status);
    esp_aes_free(&ctx);
    return false;
  }

  esp_aes_free(&ctx);
  Serial.printf("Enrypted message: %s",
           string_to_hex((char *)encrypted_data, crypted_len));
  return true;
}

//Callback function that gets called, when another device's advertisement has been received
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    tempSensor t;
    double temp;
    double hum;
    double bat;
    int battype;

    int rssi = advertisedDevice.getRSSI();
    std::__cxx11::string mac = advertisedDevice.getAddress().toString();
    std::__cxx11::string name = advertisedDevice.getName();
    std::__cxx11::string data = advertisedDevice.getManufacturerData();

    u_int8_t *payload = advertisedDevice.getPayload();
    int plength = advertisedDevice.getPayloadLength();
    char spayload[plength];
    memcpy(spayload, payload, plength);

    if (DEBUG) {
      debugPrintln("Found Device Advertisement. Mac-Address=" + mac);
      debugPrintln("Manufacturer=" + mac.substr(0,8) + " - Device Name: " + advertisedDevice.getName());
      debugPrintln("Manufacturer Data, length=" + std::to_string(data.size()));
      debugPrintln(string_to_hex(data.c_str(), data.length()));
      
      spayload[plength] = '\0';

      debugPrintln("Payload Raw: length=" + std::to_string(plength));
      Serial.println(spayload);
      //debugPrintln("Payload Hex:");
      debugPrintln(string_to_hex(spayload, plength));
      //Serial.print("Manufacturer Key: ");
      //Serial.println(string_to_hex(spayload, plength).substr(69,5).c_str());
    }
    /*
    *     ThermoBeacon
    *
    */
    if (name == "ThermoBeacon") { //Check if the name of the advertiser matches   

      if (data.length() == 20) {
        Serial.print("ThermoBeacon Data Received: ");
        Serial.println(mac.c_str());

        t = getSensor(mac,name);
        if (t.type == "new") {
          Serial.printf("Found New Sensor: %s Type=ThermoBeacon count=",name.c_str());
          Serial.println(sensors.size());
        }
        /* 
        // New Method for Temperature
        int16_t btemp = data[12] + 256 * data[13];
        double ntemp = (double)btemp / 16;

        Serial.print("New Temperature=");
        Serial.println(ntemp);
        */
        temp = ((double)data[12] + 256 * (double)data[13]) / 16;
        if (temp > 4000) temp = temp - 4096;         
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
    /*
    *     Govee H5075
    *
    */
    if (mac.substr(0,8) == GOVEE_BT_mac_OUI_PREFIX && data.length() > 15 ) { //data.lengh() == 17 ??
      Serial.print("Govee H5075 Data Received: ");
      Serial.println(mac.c_str());

      // Ist das Paket, die payload 10 Byte lang oder 17 byte ??
      //                                                                09:ff:88:ec:00:03:32:9c:64:00 (value = 5,6,7 und bat = 8)
      //                                           03:03:88:ec:02:01:05:09:ff:88:ec:00:03:32:9c:64:00 (value = 12,13,14 und bat = 15)
      // 0D:09:47:56:48:35:30:37:35:5F:33:32:37:37:03:03:88:EC:02:01:05:09:FF:88:EC:38:03:46:0B:64: (58)
      // 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30

      u_int32_t value = spayload[26] * 65535 + spayload[27] * 256 + spayload[28];

      // Precision für %.1f für temp und hum
      if (value & 0x800000) {
        temp = (double)(value ^ 0x800000) / -10000.0;
      } else {
        temp = (double)value / 10000.0;
      }
      hum = (double)(value % 1000) / 10.0;
      bat = (double)spayload[29];
      battype = BAT_PERCENT;

      t = getSensor(mac,name);
      if (t.type == "new") {
        Serial.printf("Found New Sensor: %s Type=H5075 count=", name.c_str());
        Serial.println(sensors.size());
      }

      setSensor(mac,"Govee H5075",temp,hum,bat,rssi,battype);
      printReadings(temp,hum,bat,rssi,battype);

      if (t.type == "new" || t.num == num) {
        displaySensor(mac);
        num = t.num;
      }
    }
    /*
    *     Victron SmartSolar? 
    *     VICTRON_MANUFACTURER_ID = 0x02E1;
    *
    */
    if (mac.substr(0,8) == VICTRON_BT_mac_OUI_PREFIX) {
      Serial.print("Victron Data Received: ");
      Serial.println(mac.c_str());
      char recordtype = data[0];
      u_int16_t noonce = data[1] + 256 * data[2];
      char byte0 = data[3];

      debugPrintln("Received Victron Data: Recordtype=" + std::to_string(recordtype));
      debugPrintln("  noonce=" + std::to_string(noonce) + " byte0=" + std::to_string(byte0));

      if (recordtype == 0x01) { // Solar Charger
        u_int8_t *data;

        
      }
    }

    if (DEBUG) Serial.println("--------------------------------------------------------------------------");
  }
};

void IRAM_ATTR toggleButton1() {
  if (!displayON) {
    displayON = true;
  } else {
    if (num < sensors.size()) {
      num++;
    } else {
      num = 0;
    }
  }
  displaySensor(num);
}

void IRAM_ATTR toggleButton2() {
  if (!displayON) {
    displayON = true;
  } else {
    if ( num > 0 ) {
      num--;
    } else {
      num = sensors.size();
    }
  }
  displaySensor(num);
}

#ifdef MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char spayload[length];
  memcpy(spayload, payload, length);
  spayload[length] = '\0';
  String msg;
  char delimiter[] = ":";
  char *ptr;
  char temp[50];
  std::string device;
  std::string fullname;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(spayload);

  if (strcmp(topic,conftopic.c_str()) == 0) {
    ptr = strtok(spayload, delimiter);
    if (ptr != NULL) {
      if (strcmp(ptr,"name") == 0) { // Set Sensor Fullname
        ptr = strtok(NULL, delimiter);
        strcpy(temp,ptr);
        device = temp;
        ptr = strtok(NULL, delimiter);
        strcpy(temp,ptr);
        fullname = temp;
        setSensorName(device,fullname);
      }
    }   
  }

  if (strcmp(topic,cmdtopic.c_str()) == 0) {
    if ( strcmp(spayload,"restart") == 0) {
      ESP.restart();
    }

    if ( strcmp(spayload,"getVersion") == 0 ) {
      msg = "Version " + version;
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    if ( strcmp(spayload,"getIP") == 0 ) {
      msg = "IP=" + ip.toString() + " SSID=" + ssid + " Broker=" + broker.toString();
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    if ( strcmp(spayload,"reconfig") == 0 ) {
      msg = "getconfig:"+client_id;
      client.publish(getconftopic.c_str(),msg.c_str());
    }

    // setScreen+
    if ( strcmp(spayload,"setScreen+") == 0 ) {
      toggleButton1();
    }

    // setScreen-
    if ( strcmp(spayload,"setScreen-") == 0 ) {
      toggleButton2();
    }

    // getMaxSensor
    if ( strcmp(spayload,"getMaxSensor") == 0 ) {
      msg = "maxScreen:"+sensors.size();
      client.publish(conftopic.c_str() ,msg.c_str(), true);
    }

    // setScreen: X
    
    // BlinkScreen
    
    // Turn Off Screen
    if ( strcmp(spayload,"blankScreen") == 0 ) {
      displayON = false;
      displaySensor(num);
    }
    // MQTT Debug On
    if ( strcmp(spayload,"debug") == 0 ) {
      if (!DEBUG) {
        DEBUG = true;
        debugPrintln("DEBUG=ON");
      } else {
        debugPrintln("DEBUG=OFF");
        DEBUG = false;
      }
      displaySensor(num);
    }

  } // End Commands

  if (strcmp(topic,timetopic.c_str()) == 0) {
    mqtttime = spayload;
    if (num == 0) {
      displaySensor(0);
    }
  }

  if (strcmp(topic,datetopic.c_str()) == 0) {
    mqttdate = spayload;
    if (num == 0) {
      displaySensor(0);
    }
  }
}

void mqttReconnect() {
  // Set MQ Indikator
  display_indicators(TFT_RED);

  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(client_id.c_str())) {
      display_indicators(TFT_GREEN);
      Serial.println("connected..");
      client.subscribe(conftopic.c_str());
      client.subscribe(cmdtopic.c_str());
      client.subscribe(timetopic.c_str());
      client.subscribe(datetopic.c_str());
      String msg = "Online " + version + ": SSID=" + ssid + " IP=" + WiFi.localIP().toString() + " Broker=" + broker.toString();
      client.publish(debugtopic.c_str(),msg.c_str(),true);
      display_indicators(TFT_DARKGREY);
    } else {
      display_indicators(TFT_RED);
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}


#endif

String mac2String(byte ar[]) {
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%02X", ar[i]); // J-M-L: slight modification, added the 0 in the format for padding 
    s += buf;
    //if (i < 5) s += ':';
  }
  return s;
}

void setup() {
  //Start serial communication
  Serial.begin(115200);
  Serial.println("BLE2MQTT starting...");
  Serial.println(version);

  // Setup Buttons
  pinMode(BUTTON1PIN, INPUT);
  pinMode(BUTTON2PIN, INPUT);

  // Setup Display
  Serial.println("Setup Display...");
  display.init();
  display.setRotation(1);
  display.setTextSize(1);
  display.setTextFont(4);

  //Welcome Messager
  display.fillScreen(TFT_WHITE);
  display.setTextColor(TFT_BLACK,TFT_WHITE);
  display.setCursor(0,25);
  display.println(" BLE2MQTT starting");
  display.printf("  Version: %s", version);
  display.println();
  display.println();
  display.println(" ..Nihil fit sine causa..");
  delay(2000);

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE,TFT_BLACK);
  display.setTextFont(2);
  display.setCursor(0,16);

  Serial.println("Init BLE Device...");
  //Init BLE device
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setInterval(80);
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), FINE_SCAN);
  pBLEScan->setWindow(30);
  pBLEScan->setActiveScan(true);

  espID = ESP.getEfuseMac();
  client_id = "ble2mqtt-" + mac2String((byte*) &espID);
  conftopic = conftopic + client_id;
  cmdtopic = conftopic + "/cmd";
  debugtopic = debugtopic + client_id;

  #ifdef WIFI
  
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  Serial.println("Creating WiFi Manager...");
  WiFiManager wm;  
  WiFi.mode(WIFI_STA);   

  // Button2 Press on Startup Resets WiFi Settings and starts AP Mode
  if ( digitalRead(BUTTON1PIN) == LOW ) {
    Serial.println("Button1Pin is low - Reset WiFi Settings - Starting in AP Mode.");
    display.println("Reset WiFi Settings...");
    display.println("Starting AP Mode.");
    wm.resetSettings();
  } else {
    Serial.println("Connect to WiFi...");
    display.println("Connect to WIFI...");
  }

  bool res;
  wm.setConnectTimeout(10);
  res = wm.autoConnect("BLE2MQTT","password");
  if (!res) {
    Serial.println("Failed to connect to WiFi!");
  }
 
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    display.print("Cconnected! IP=");
    display.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Connection Failed!");
  }
  #endif

  display.println("Searching Sensors");

  #ifdef MQTT
  /* Prepare MQTT client */
  ssid = WiFi.SSID();
  ip = WiFi.localIP();

  if (ssid == "UPC4E87B2D") {
    broker = broker_int;
  } else if (ssid == "OpenHAB") {
    broker = broker_openhab;
  } else {
    broker = broker_ext;
  }
  
  Serial.printf("Connect to MQTT at %s", broker.toString().c_str());
  Serial.println();
  client.setServer(broker, 1883);
  client.setCallback(mqttCallback);
  mqttReconnect();
  #endif

  // Attach Button Callbacks
  attachInterrupt(BUTTON1PIN, toggleButton1, RISING);
  attachInterrupt(BUTTON2PIN, toggleButton2, RISING);
}

void loop() {
  String topic = "";
  String msg = "";
  String dev = "";
  
  // Scan for Sensors
  Serial.println("Start Scanning...");
  // non Blocking Scan
  pBLEScan->start(0,nullptr,false);
  u_long startmillis = millis();
  while (millis() - startmillis < 60000 && millis() >= startmillis) {
    client.loop();
  }
  pBLEScan->stop();
  Serial.println("Stop Scanning...");

  // Publsh Sensor Values to MQTT
  #ifdef MQTT
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      mqttReconnect();
      if (client.connected()) {
        msg = "getconfig:"+client_id;
        client.publish(getconftopic.c_str(),msg.c_str());
      }
    } 
    // MQ Indikator anhand der Aktivität ändern
    
    if (client.connected()) {
     
      display_indicators(TFT_GREEN);

      for (tempSensor t : sensors) {
        Serial.print("Publish Sensor: ");
        Serial.print(t.mac.c_str());
        
        dev = t.device.c_str();
        Serial.print(" => ");
        Serial.println(basetopic + dev);

        topic = basetopic + dev + "_temp/state";
        msg = (String)t.temp;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_hum/state";
        msg = (String)t.hum;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_bat/state";
        msg = (String)t.bat;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_battype/state";
        msg = (String)t.battype;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_type/state";
        msg = t.type.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_rssi/state";
        msg = (String)t.rssi;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_name/state";
        msg = t.name.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_fullname/state";
        msg = t.fullname.c_str();
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_gateway/state";
        msg = client_id;
        client.publish(topic.c_str(),msg.c_str(),true);
        topic = basetopic + dev + "_lastupdate/state";
        msg = t.lastupdate;
        client.publish(topic.c_str(),msg.c_str(),true);
      }
      display_indicators(TFT_DARKGREY);
    }
  }

  #endif

}

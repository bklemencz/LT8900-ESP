
/* Connections:

 D1 Mini -> LT8900

 GND        GND
 3v3        VCC
 D0         PKT
 D8         CS
 D4         RST
 D7         MOSI
 D6         MISO
 D5         SCK

*/

#include <arduino.h>
#include <SPI.h>
#include "LT8900.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <FS.h>


const uint8_t PIN_NRF_RST = D9;
const uint8_t PIN_NRF_CS = D10;
const uint8_t PIN_NRF_PKT = D2;


bool rem_key_hold;
uint8_t rem_group_act;
int lastCounter;
uint16_t RemContr_Add;
uint8_t LearnCnt;
String ssid,password,mqtt_server;

LT8900 lt(PIN_NRF_CS, PIN_NRF_PKT, PIN_NRF_RST);

WiFiClient espClient;
PubSubClient client(espClient);

void FlashLed(int Number, int Del)
{
  for (int i=0;i<Number;i++)
  {
    digitalWrite (BUILTIN_LED,1);
    delay(Del);
    digitalWrite (BUILTIN_LED,0);
    delay(Del);
  }
  pinMode(PIN_NRF_PKT,INPUT);
}

void Flash_Light(int Nr, int del)
{
  for (int i=0;i<Nr;i++)
  {
    client.publish("Remote/AllON","1");
    client.loop();
    delay(del);
    client.publish("Remote/AllON","0");
    client.loop();
    delay(del);
  }
}

void setup_FS(void)
{
  bool ok = SPIFFS.begin();
  if (ok) {
    Serial.println("ok");
  }
  File f = SPIFFS.open("/wconf.txt", "r");
  if (!f) {
      Serial.println("file open failed");
  }
  ssid = f.readStringUntil('\n');
  password = f.readStringUntil('\n');
  mqtt_server = f.readStringUntil('\n');
  ssid.remove((ssid.length()-1));
  password.remove((password.length()-1));
  mqtt_server.remove((mqtt_server.length()-1));
  /////////// NEED TO RUN ONLY ONCE ///////////
  //  Serial.println("Spiffs formating...");
  //  SPIFFS.format();
  //  Serial.println("Spiffs formatted");
}

void callback(char* topic, byte* payload, unsigned int length)
{
 // FlashLed(2, 500);
//  TimeOut = atoi((char*)payload);
//  Serial.print("New TimeOut: ");
//  Serial.println(TimeOut);
}

void setup_wifi()
{
  //  Wifi.setHostname("Milight_Gateway");
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("Connecting to: "); Serial.println(ssid.c_str());}
  FlashLed(3, 200);
  Serial.println("Wifi Connected!");
  delay(1000);
  client.setServer(mqtt_server.c_str(), 1883);
  client.setCallback(callback);


}

void setup_OTA()
{
  ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
      {
        type = "filesystem";
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        SPIFFS.end();
      }

      Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.setHostname("MiLight_Gateway");
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println(ArduinoOTA.getHostname());
}

void setup_spi()
{
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE1);
  SPI.setClockDivider(SPI_CLOCK_DIV4);
}

void setup_Lt8900()
{
  lt.begin();
  lt.setDataRate(LT8900::LT8900_1MBPS);
  lt.setChannel(0x04);
  lt.startListening();
  lt.whatsUp(Serial);
}

void EEprom_Write2Bytes(int Address,uint16_t Data)
{
  EEPROM.write(Address, Data / 256);
  EEPROM.write(Address+1, Data % 256);
  EEPROM.end();
}

uint16_t EEprom_Read2Bytes(int Address)
{
  return EEPROM.read(Address)*256 + EEPROM.read(Address+1);
}


void ParseRemoteComm(uint8_t buf[])
{
  uint16_t LastRemAdd;
  if (lastCounter != buf[5])
  {
    lastCounter = buf[5];
    LastRemAdd = buf[1] * 256 + buf[2];
    if (buf[4] == 0x15)
    {                                            // Remote Address Learning Mode
      LearnCnt++;
      if(LearnCnt >= 20)
      {                                                // If keep pressed for 5 Sec
        RemContr_Add = buf[1] * 256 + buf[2];
        EEprom_Write2Bytes(0,RemContr_Add);           // Save Remote Address to EEprom at adr0
        Serial.print("New Remote Learned Adress: ");
        Serial.println(RemContr_Add);
        Flash_Light(5, 300);

      }
    }  else LearnCnt = 0;
    if (LastRemAdd == RemContr_Add)
    {

      String Raw = String(buf[1],HEX) + " " + String(buf[2],HEX) + " " + String(buf[3],HEX) + " " + String(buf[4],HEX) + " " + String(buf[5],HEX);
      client.publish("Remote/RAW",Raw.c_str());

      rem_group_act = buf[3];                     //Group 1,2,3,4 All-0
      if (buf[4]/16) rem_key_hold = true;
                else rem_key_hold = false;
      int actComm = buf[4] % 16;                  //AllON-0x05, AllOFF-0x09,
                                                  //UP-0x0C, DOWN-0x04,
                                                  //LEFT-0x0E, RIGHT-0x0F,
                                                  //GR1ON-0x08, GR1OFF-0x0B,
                                                  //GR2ON-0x0D, GR2OFF-0x03,
                                                  //GR3ON-0x07, GR3OFF-0x0A,
                                                  //GR4ON-0x02, GR4OFF-0x06,
      if (actComm == 0x05)
      {
        client.publish("Remote/AllON","1");
      }
      if (actComm == 0x09)
      {
        client.publish("Remote/AllON","0");
      }
      client.loop();                // Update MQTT client
    }
  }
}

void setup()
{
  Serial.begin(9600);
  setup_FS();
  EEPROM.begin(512);
  RemContr_Add = EEprom_Read2Bytes(0);
  setup_spi();

  setup_wifi();
  delay(500);

  setup_OTA();
  setup_Lt8900();

  Serial.println(F("Boot completed."));
}

void loop()
{
   while (!client.connected())
   {
     if (client.connect("ESP8266Client"))
     {
       FlashLed(5, 100);
       Serial.println("MQTT Online!");
       client.publish("Remote/Online","1");
       lt.begin();
       lt.setChannel(4);
       lt.startListening();
     } else
     {
       delay(1000);
     }
   }
  if (lt.available())
    {
      uint8_t buf[8];

      int packetSize = lt.read(buf, 8);
      if (packetSize > 0)
      {
        ParseRemoteComm(buf);
      }
      lt.startListening();      // LT8900 Rx Enable
    }
  client.loop();                // Update MQTT client
  ArduinoOTA.handle();          // OTA Updates
}

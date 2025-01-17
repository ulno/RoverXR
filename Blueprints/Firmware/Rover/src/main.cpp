/*
    Description: RoverC will display "M5AP+2-byte mac address" hotspot name
   after booting up, waiting for JoyC to pair.
*/

#include "config.h"
#ifndef CONNECT_SSID
  #define USE_AP // run your own accesspoint?
#endif
#ifndef SYSNUM
  #define SYSNUM 3 // which system are we on?
#endif

#include <Arduino.h>
#include <M5StickC.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>

#ifdef USE_AP
  const char *ssid = "M5AP";
  const char *password = "77777777";
#else
  const char *ssid = CONNECT_SSID;
  const char *password = CONNECT_PW;
  #define SYSNUM_STRINGIFY2(x) #x
  #define SYSNUM_STRINGIFY(x) SYSNUM_STRINGIFY2(x)
  #define SYSNUM_CONCAT(a, b) a b
  #define REMOTENAME SYSNUM_CONCAT("m5-remote-", SYSNUM_STRINGIFY(SYSNUM))
  #define ROVERNAME SYSNUM_CONCAT("m5-rover-", SYSNUM_STRINGIFY(SYSNUM))
#endif  

//TFT_eSprite Disbuff = TFT_eSprite(&M5.Lcd);
WiFiServer server(80);

WiFiUDP Udp1;
// M5_RoverC roverc;

void SetChargingCurrent(uint8_t CurrentLevel) {
  Wire1.beginTransmission(0x34);
  Wire1.write(0x33);
  Wire1.write(0xC0 | (CurrentLevel & 0x0f));
  Wire1.endTransmission();
}

void setup() {
  M5.begin();
  M5.update();
  // Turn off screen
  M5.Axp.SetLDO2(false);
  M5.Axp.SetLDO3(false);
  // roverc.begin();
  Wire.begin(0, 26, 100000UL);

  uint64_t chipid = ESP.getEfuseMac();
  String str = ssid + String((uint32_t)(chipid >> 32), HEX);
  
  // M5.Lcd.setRotation(1);
  // M5.Lcd.setSwapBytes(false);
  // Disbuff.createSprite(160, 80);
  // Disbuff.setSwapBytes(true);
  // Disbuff.fillRect(0, 0, 160, 20, Disbuff.color565(50, 50, 50));
  // Disbuff.setTextSize(2);
  // Disbuff.setTextColor(WHITE);
  // Disbuff.setCursor(15, 35);
  // Disbuff.print(str);
  // Disbuff.pushSprite(0, 0);

  SetChargingCurrent(4);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Serial initialized.");
  #ifdef USE_AP
    // Set device in STA mode to begin with
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(str.c_str(), password);
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
  #else
    Serial.print("Hostname: ");
    const char* hostname = ROVERNAME;
    Serial.println(hostname);
    WiFi.setHostname(hostname);
    WiFi.begin(CONNECT_SSID, CONNECT_PW);
    Serial.println(F("Starting MDNS."));
    MDNS.begin(hostname);
    Serial.println(F("MDNS Ready."));
    for(int i=0; i<40; i++) {
      if(WiFi.isConnected()) {
        Serial.println();
        Serial.print(F("Connected to Wi-Fi with IP: "));
        Serial.println(WiFi.localIP());
        break;
      }
      else {
        Serial.print(".");
        delay(500);
      }
    }
  #endif

  server.begin();
  Udp1.begin(1000 + SYSNUM);
}

uint8_t SendBuff[9] = { 0xAA, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xee };

uint8_t I2CWrite1Byte(uint8_t Addr, uint8_t Data) {
  Wire.beginTransmission(0x38);
  Wire.write(Addr);
  Wire.write(Data);
  return Wire.endTransmission();
}

uint8_t I2CWritebuff(uint8_t Addr, uint8_t *Data, uint16_t Length) {
  Wire.beginTransmission(0x38);
  Wire.write(Addr);
  for (int i = 0; i < Length; i++) {
    Wire.write(Data[i]);
  }
  return Wire.endTransmission();
}

int16_t speed_buff[4] = { 0 };
int8_t speed_sendbuff[4] = { 0 };
uint32_t count = 0;
uint8_t IIC_ReState = ESP_OK;

uint8_t Setspeed(int16_t Vtx, int16_t Vty, int16_t Wt) {
  Wt = (Wt > 100) ? 100 : Wt;
  Wt = (Wt < -100) ? -100 : Wt;

  Vtx = (Vtx > 100) ? 100 : Vtx;
  Vtx = (Vtx < -100) ? -100 : Vtx;
  Vty = (Vty > 100) ? 100 : Vty;
  Vty = (Vty < -100) ? -100 : Vty;

  Vtx = (Wt != 0) ? Vtx * (100 - abs(Wt)) / 100 : Vtx;
  Vty = (Wt != 0) ? Vty * (100 - abs(Wt)) / 100 : Vty;

  speed_buff[0] = Vty - Vtx - Wt;
  speed_buff[1] = Vty + Vtx + Wt;
  speed_buff[3] = Vty - Vtx + Wt;
  speed_buff[2] = Vty + Vtx - Wt;

  for (int i = 0; i < 4; i++) {
    speed_buff[i] = (speed_buff[i] > 100) ? 100 : speed_buff[i];
    speed_buff[i] = (speed_buff[i] < -100) ? -100 : speed_buff[i];
    speed_sendbuff[i] = speed_buff[i];
  }
  // Serial.println("Set speed...");
  return I2CWritebuff(0x00, (uint8_t *)speed_sendbuff, 4);
}

void setServoAngle(uint8_t pos, uint8_t angle) {
    uint8_t reg = 0x10 + pos;
    I2CWritebuff(reg, &angle, 1);
}

void loop() {
  static bool wifi_connected = false;
  int udplength = Udp1.parsePacket();
  if (udplength) {
    char udpdata[udplength];
    Udp1.read(udpdata, udplength);
    IPAddress udp_client = Udp1.remoteIP();
    if ((udpdata[0] == 0xAA) && (udpdata[1] == 0x55) && (udpdata[7] == 0xee)) {
      for (int i = 0; i < 8; i++) {
        Serial.printf("%02X ", udpdata[i]);
      }
      Serial.println();
      if (udpdata[6] == 0x01) {
        IIC_ReState = Setspeed(udpdata[3] - 100, udpdata[4] - 100,
                               udpdata[5] - 100);
      } else {
        IIC_ReState = Setspeed(0, 0, 0);
      }
      if (udpdata[2] != 0x00) {
        switch (udpdata[2]) {
          case 0x01:
            setServoAngle(0, 75); // Serial.println("RIGHT Trigger Pressed");
            break;
          case 0x10:
            setServoAngle(0, 20); // Serial.println("LEFT Trigger Pressed");
            break;
          case 0x11:
            setServoAngle(0, 2); // Serial.println("BOTH Triggers Pressed");
            break;
        }
      }
    } else {
      IIC_ReState = Setspeed(0, 0, 0);
    }
  }
  if(WiFi.status() != WL_CONNECTED) { /// No WiFi connection
    if(wifi_connected) {
      Serial.println("Wifi disconnected.");
      wifi_connected = false;
    }
  } else { // WiFi connection exists
    if(!wifi_connected) {
      Serial.println("Wifi connected.");
      wifi_connected = true;
    }
  }
 
  count++;
  if (count > 100) {
    count = 0;

    // Disbuff.fillRect(0, 0, 160, 20, Disbuff.color565(50, 50, 50));
    // Disbuff.setTextSize(1);
    // Disbuff.setTextColor(WHITE);
    // Disbuff.setCursor(5, 5);
    // Disbuff.printf("%.2fV,%.2fmA,%d", M5.Axp.GetBatVoltage(),
    //                M5.Axp.GetBatCurrent(), IIC_ReState);
    // Disbuff.pushSprite(0, 0);
  }
}

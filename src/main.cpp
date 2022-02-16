#include <Adafruit_AHTX0.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "iot_iconset_16x16.h"
#include "wifi_password.h"  // STASSID and STAPSK

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define SCREEN_ADDRESS \
  0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define HUMIDITY_LOW_THRESHOLD 40
#define HUMIDITY_HIGH_THRESHOLD 50

IPAddress local_IP(192, 168, 86, 42);
IPAddress gateway(192, 168, 86, 1);
IPAddress subnet(255, 255, 255, 0);

const char *ssid = STASSID;
const char *password = STAPSK;

TwoWire i2c;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT);
Adafruit_AHTX0 aht;
WiFiUDP udp;
bool humidifierOn = false;
int initialTx = 10;  // On init transmit this many times.
int temperature_C = 0;
int temperature = 0;
int humidity = 100;
int loopCount = 0;

void getTempHumidity();
void sendUDPMessage();

void displayData(bool wifi = false, bool tx = false, bool sense = false) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  if (wifi) {
    display.drawBitmap(0, 0, wifi1_icon16x16, 16, 16, 1);
  }
  if (initialTx > 0) {
    display.drawBitmap(80, 0, timer_icon16x16, 16, 16, 1);
  }
  if (sense) {
    display.drawBitmap(98, 0, home_icon16x16, 16, 16, 1);
  }
  if (tx) {
    display.setTextSize(1);  // Normal 1:1 pixel scale
    display.setCursor(116, 0);
    display.print("Tx");
  }
  display.setTextSize(2);
  display.drawBitmap(32, 16, humidity2_icon16x16, 16, 16, 1);
  display.setCursor(56, 16);
  if (humidity == 100) {
    display.print("-");
  } else {
    display.print(humidity);
    display.print("%");
  }
  display.drawBitmap(32, 48, temperature_icon16x16, 16, 16, 1);
  display.setCursor(56, 48);
  if (temperature == 0) {
    display.print("-");
  } else {
    display.print(temperature);
    display.print("F");
  }
  display.display();
}

void setup() {
  Serial.begin(74880);
  Serial.println("Booting");
  i2c.begin(D3, D2);
  while (!aht.begin(&i2c)) {
    Serial.println("Could not find AHT? Check wiring");
    delay(1000);
  }
  // Setup humidity sensor
  Serial.println("AHT10 or AHT20 found");

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.dim(true);
  display.display();
  delay(1000);

  // don't store the connection each time to save wear on the flash
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet);
  displayData(true, false);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  displayData(false, false);
}

void getTempHumidity() {
  sensors_event_t humidity_sensor, temp_sensor;
  // populate temp and humidity objects with fresh data
  if (!aht.getEvent(&humidity_sensor, &temp_sensor)) {
    Serial.println("Error getting humidity/temperature");
    Serial.println(aht.getStatus());
    return;
  }
  temperature_C = int(temp_sensor.temperature);
  temperature = int(temperature_C * 9 / 5 + 32);
  humidity = int(humidity_sensor.relative_humidity);
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" degrees F");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println("% rH");
}

void sendUDPMessage() {
  if (udp.beginPacketMulticast(IPAddress(239, 0, 10, 1), 10000, WiFi.localIP(),
                               5) != 1) {
    Serial.println("Error udp.beginPacketMulticast()");
    return;
  }
  String msg = "{ \"id\": \"1\", \"epoch\": \"" + String(millis() / 1000) +
               "\", \"type\": \"humidity\", \"data\": \"" + humidity + "\"}";

  // Temp: " + String(temp.temperature) + " Humidity: " +
  // humidity.relative_humidity + "\r\n";
  udp.write(msg.c_str(), msg.length());
  if (udp.endPacket() != 1) {
    Serial.println("Error udp.endPacket()");
    return;
  }

  if (udp.beginPacketMulticast(IPAddress(239, 0, 10, 1), 10000, WiFi.localIP(),
                               5) != 1) {
    Serial.println("Error udp.beginPacketMulticast()");
    return;
  }
  msg = "{ \"id\": \"1\", \"epoch\": \"" + String(millis() / 1000) +
        "\", \"type\": \"temperature\", \"data\": \"" + temperature_C + "\"}";

  udp.write(msg.c_str(), msg.length());
  if (udp.endPacket() != 1) {
    Serial.println("Error udp.endPacket()");
    return;
  }
}

bool shouldSendMessage() {
  loopCount++;
  if (initialTx > 0) {
    --initialTx;
    return true;
  }
  if (loopCount > 20) {
    loopCount = 0;
    return true;
  }
  if (humidity < HUMIDITY_LOW_THRESHOLD) {
    humidifierOn = true;
    return true;
  } else if (humidity > HUMIDITY_HIGH_THRESHOLD) {
    humidifierOn = false;
    // send few more times to make sure control server
    // gets the message
    initialTx = 5;
    return true;
  } else {
    return humidifierOn;
  }
}

void loop() {
  displayData(false, false, true);
  getTempHumidity();
  displayData(false, false, false);
  if (shouldSendMessage()) {
    WiFi.mode(WIFI_STA);
    displayData(true, false);
    WiFi.reconnect();
    Serial.println("Wokeup");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    displayData(true, true);
    sendUDPMessage();
    displayData(true, false);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    displayData(false, false);
  }
  delay(60 * 1000);
}
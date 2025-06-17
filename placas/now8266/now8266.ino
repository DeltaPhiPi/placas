#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
/* Copyright 2019 David Conran
*
* This example code demonstrates how to use the "Common" IRac class to control
* various air conditions. The IRac class does not support all the features
* for every protocol. Some have more detailed support that what the "Common"
* interface offers, and some only have a limited subset of the "Common" options.
*
* This example code will:
* o Try to turn on, then off every fully supported A/C protocol we know of.
* o It will try to put the A/C unit into Cooling mode at 25C, with a medium
*   fan speed, and no fan swinging.
* Note: Some protocols support multiple models, only the first model is tried.
*
*/
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* ssid = "wifing";
const char* password = "wifing-pub";
JsonDocument doc;
//Your Domain name with URL path or IP address with path
String serverName = "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/aircon";
String serverName2 = "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/oled";
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastTime = 0;
// Timer set to 10 minutes (600000)
//unsigned long timerDelay = 600000;
// Set timer to 5 seconds (5000)
unsigned long timerDelay = 10000;

const uint16_t kIrLed = 12;  // The ESP GPIO pin to use that controls the IR LED.
IRac ac(kIrLed);  // Create a A/C object using GPIO to sending messages with.

void setup() {
  Serial.begin(115200);
  delay(200);
  ac.next.protocol = decode_type_t::KELVINATOR;  // Set a protocol to use.
  ac.next.model = 1;  // Some A/Cs have different models. Try just the first.
  ac.next.mode = stdAc::opmode_t::kHeat;  // Run in cool mode initially.
  ac.next.celsius = true;  // Use Celsius for temp units. False = Fahrenheit
  ac.next.degrees = 25;  // 25 degrees.
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium;  // Start the fan at medium.
  ac.next.swingv = stdAc::swingv_t::kOff;  // Don't swing the fan up or down.
  ac.next.swingh = stdAc::swingh_t::kOff;  // Don't swing the fan left or right.
  ac.next.light = false;  // Turn off any LED/Lights/Display that we can.
  ac.next.beep = false;  // Turn off any beep from the A/C if we can.
  ac.next.econo = false;  // Turn off any economy modes if we can.
  ac.next.filter = false;  // Turn off any Ion/Mold/Health filters if we can.
  ac.next.turbo = false;  // Don't use any turbo/powerful/etc modes.
  ac.next.quiet = false;  // Don't use any quiet/silent/etc modes.
  ac.next.sleep = -1;  // Don't set any sleep time or modes.
  ac.next.clean = false;  // Turn off any Cleaning options if we can.
  ac.next.clock = -1;  // Don't set any current time if we can avoid it.
  ac.next.power = false;  // Initially start with the unit off.
  

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
 
  Serial.println("Timer set to 5 seconds (timerDelay variable), it will take 5 seconds before publishing the first reading.");
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
    delay(2000);
  display.clearDisplay();

  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  // Display static text
  // display.println("Hello, world!");
  // display.display(); 

}


struct ACData {
    int protocol;
    int temperature;
    bool power;
    stdAc::opmode_t mode;
};

ACData acdata = ACData {20, false};
stdAc::opmode_t to_mode(String mode) {
  if (mode == "cool") {
    return stdAc::opmode_t::kCool;
  }
  if (mode == "heat") {
    return stdAc::opmode_t::kHeat;
  }
  return stdAc::opmode_t::kHeat;
}
int ledtime = 0;
int leddelay = 5000;
void loop() {
  // return;
  //For every protocol the library has ...
    if (millis() - ledtime > leddelay) {
      ledtime = millis();
      if(WiFi.status()== WL_CONNECTED){
        WiFiClient client;
        HTTPClient http;
        String serverPath2 = serverName2;
        http.begin(client, serverPath2.c_str());
        http.setTimeout(10000);
        int httpResponseCode = http.GET();
        auto s = http.getString();
        Serial.println(httpResponseCode);
        Serial.println(s);
        display.setTextSize(3);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.clearDisplay();
        // Display static text
        
        display.display();
        display.println(s);
        display.display();
      }
    }
    if ((millis() - lastTime) > timerDelay) {
        //Check WiFi connection status
        if(WiFi.status()== WL_CONNECTED){
        WiFiClient client;
        HTTPClient http;
        String serverPath = serverName;
        http.begin(client, serverPath.c_str());
        int httpResponseCode = http.GET();
        Serial.println(http.getString());
        bool do_update = false;
        if (httpResponseCode>0) {
            deserializeJson(doc, http.getString());
            do_update = doc["force_update"];
            // if (!do_update) {
            //   if (acdata.protocol != doc["protocol"] ||
            //       acdata.power != doc["power"] ||
            //       acdata.temperature != doc["temperature"] ||
            //       acdata.mode != to_mode(doc["mode"])
            //       ) {

            //       do_update = true;
            //   }
            // }
            acdata.protocol = doc["protocol"];
            acdata.power = doc["power"];
            acdata.temperature = doc["temperature"];
            acdata.mode = to_mode(doc["mode"]);
        }
        else {
            Serial.print("Error code: ");
            Serial.println(httpResponseCode);
        }
        // Free resources
        http.end();

        ac.next.power = acdata.power;
        ac.next.degrees = acdata.temperature;
        Serial.println(acdata.protocol);
        ac.next.protocol = (decode_type_t)acdata.protocol;
        if (do_update)
          ac.sendAc();
        delay(1000);

    }
    else {
        Serial.println("WiFi Disconnected");
    }
    lastTime = millis();
  }
  
}

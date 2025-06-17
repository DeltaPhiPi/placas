#include <HTTPClient.h>


#include <WiFi.h>
#include "ThingSpeak.h"
#include "Adafruit_VL53L0X.h"
#include <Wire.h>

const char* ssid = "wifing";
const char* password = "wifing-pub";
WiFiClient  client;

unsigned long myChannelNumber = 1;
const char * myWriteAPIKey = "P5NTS5EHZ07HTO6O";


const int THRESHOLD = 100;
const int MIN_TIME = 250;
const int SPEED = 350;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
Adafruit_VL53L0X lox2 = Adafruit_VL53L0X();

void setup() {
    Serial.begin(115200);
    Wire.begin(21,22);
    while (! Serial) {
      delay(1);
    }
    pinMode(32, OUTPUT);
    pinMode(33, OUTPUT);
    digitalWrite(32, LOW);
    digitalWrite(33, LOW);
    delay(10);
    digitalWrite(32, HIGH);
    digitalWrite(33, HIGH);
    delay(10);
    digitalWrite(33, LOW);
    lox.begin(0x30, false);
    delay(10);
    digitalWrite(33, HIGH);
    lox2.begin(0x31);
    delay(100);
    WiFi.mode(WIFI_STA);   
    ThingSpeak.begin(client); 
    Serial.println(F("VL53L0X API Simple Ranging example\n\n")); 
}
VL53L0X_RangingMeasurementData_t measure;
VL53L0X_RangingMeasurementData_t measure2;
const int TH1 = 1300;
const int TH2 = 1300;

int state = 0;
float count = 0;
int pseudocount = 0;
int polarity = 0;
float estimator(int c, int p) {
  if (c < 5) {
    return 1.0*p;
  }
  else {
    return (1.0 + (float)(c-5)/5.0)*p;
  }
}
int last_time = 0;
short samples[4096];
int length = 0;

void send_data() {
    if(WiFi.status() != WL_CONNECTED){
        Serial.print("Attempting to connect");
        while(WiFi.status() != WL_CONNECTED){
            WiFi.begin(ssid, password); 
            delay(5000);     
        } 
        Serial.println("\nConnected.");
    }
    WiFiClient client;
    HTTPClient http;
    String httpRequestData = "[";
    http.begin(client, "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/laser");
    http.addHeader("Content-Type", "application/json");

    for (int i = 0; i < length; i++) {
        httpRequestData += samples[i];
        if (i < length-1) {
            httpRequestData += ",";
        }
    }
    httpRequestData += "]";
    int x = http.POST(httpRequestData);
    if(x == 200){
        Serial.println("Channel update successful.");
    }
    else{
        Serial.println("Problem updating channel. HTTP error code " + String(x));
    }
    delay(1000);
}

void loop() { 
    lox.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
    lox2.rangingTest(&measure2, false); // pass in 'true' to get debug data printout!
    int m1 = measure.RangeMilliMeter;
    int m2 = measure2.RangeMilliMeter;
    samples[length++] = m1;
    samples[length++] = m2;
    delay(50);
    if (length % 100 == 0) {
      Serial.println(length);
    }
    if (length == 4096) {
        send_data();
        length = 0;
    }
    else if (millis() - last_time > 30000) {
        send_data();
        length = 0;
        last_time = millis();
    }
}

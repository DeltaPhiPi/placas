#include "Adafruit_BME680.h"
#include <Adafruit_Sensor.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include "ESP32OTAPull.h"

#define JSON_URL   "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/ota.json" // this is where you'll post your JSON filter file
#define SSID 	   "wifing"
#define PASS       "wifing-pub"
#define VERSION    "1.6.0" // The current version of this program


float frame[32*24]; // buffer for full frame of temperatures
Adafruit_MLX90640 mlx;

WiFiClient wclient;
Adafruit_BME680 bme(&Wire); // I2C
unsigned long myChannelNumber = 1;
#define SEALEVELPRESSURE_HPA (1013.25)

int bme_log = 200;
int camera_log = 200;
int ota_log = 0;



void callback(int offset, int totallength);
void init_serial() {
	Serial.begin(115200);
	delay(2000); // wait for ESP32 Serial to stabilize
}
void init_wifi() {
  WiFi.begin(SSID, PASS);
	while (!WiFi.isConnected())
	{
		Serial.print(".");
		delay(250);
	}
	Serial.printf("\n\n");

}
bool do_bme;
void init_bme() {
  if (!bme.begin()) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    do_bme = false;
    return;
  }
  do_bme = true;

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
}
void init_camera() {
  Serial.println("Camera initialization");
  if (! mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("MLX90640 not found!");
  }
  Serial.println("Found Adafruit MLX90640");
  mlx.setMode(MLX90640_CHESS);
  mlx.setResolution(MLX90640_ADC_19BIT);
  mlx.setRefreshRate(MLX90640_2_HZ);
  Serial.println("Mode: chess; Resolution: 19bit, FPS: 2hz");
}
ESP32OTAPull ota;


void setup()
{
// #if defined(LED_BUILTIN)
// 	pinMode(LED_BUILTIN, OUTPUT);
// #endif
  init_serial();
  init_wifi();
  init_bme();
  init_camera();
	Serial.printf("Connecting to WiFi '%s'...", SSID);
	delay(3000);
	// First example: update should NOT occur, because Version string in JSON matches local VERSION value.
	ota.SetCallback(callback);
	Serial.printf("We are running version %s of the sketch, Board='%s', Device='%s'.\n", VERSION, ARDUINO_BOARD, WiFi.macAddress().c_str());
	Serial.printf("Checking %s to see if an update is available...\n", JSON_URL);
	int ret = ota.CheckForOTAUpdate(JSON_URL, VERSION);
	Serial.printf("CheckForOTAUpdate returned %d (%s)\n\n", ret, errtext(ret));
  }

int send_frame() {
  int c_ = mlx.getFrame(frame);
  if (c_ != 0) {
    return c_;
  }
  WiFiClient client;
  HTTPClient http;
  Serial.flush();
  http.setTimeout(10000);
  http.begin(client, "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/img");
  http.addHeader("Content-Type", "application/json");
  Serial.flush();
  float ta = mlx.getTa(false);

  String httpRequestData = "{\"width\": 32, \"height\": 24, \"data\": [";
  for (uint8_t h=0; h<24; h++) {
      for (uint8_t w=0; w<32; w++) {
          float t = frame[h*32 + w];
          t = t * 100.0;
          int ti = (int) t;
          float t2 = float (ti) / 100.0;
          httpRequestData += t2;
          if (h*32 + w < 767) {
              httpRequestData += ",";
          }
      }
  }
  httpRequestData += "], \"temp\":";
  httpRequestData += ta;
  httpRequestData += "}";
  int code = http.POST(httpRequestData);
  Serial.print("Img sent ");
  Serial.println(code);
  http.end();
  return code;
}

int bme_timer = 0;
int ota_timer = 0;
int log_timer = 0;
int camera_timer = 0;

int send_bme() {
  if (!bme.performReading()) {
      return 3000;
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/weather");
  http.addHeader("Content-Type", "application/json");
  float t = bme.temperature;
  float p = bme.pressure;
  float h = bme.humidity;
  float aq = bme.gas_resistance;
  String body = "{\"temperature\":";
  body += t;
  body += ",\"humidity\":";
  body += h;
  body += ",\"pressure\":";
  body += p;
  body += ",\"gas_resistance\":";
  body += aq;
  body += "}";
  
  Serial.print("Temperature: "); Serial.println(t);
  Serial.print("Pressure: "); Serial.println(p);
  Serial.print("Humidity: "); Serial.println(h);
  Serial.print("Air quality: "); Serial.println(aq);
  int code = http.PUT(body);
  Serial.print("BME sent ");
  Serial.println(code);
  http.end();
  return code;
}


void send_log_s(String log) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, "http://fing-bot.brazilsouth.cloudapp.azure.com:5000/log");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(log);
  if (code == 418) {
    ESP.restart();
  }
}
void send_log() {
  String httpRequestData = "";
  httpRequestData += bme_log;
  httpRequestData += ",";
  httpRequestData += camera_log;
  httpRequestData += ",";
  httpRequestData += errtext(ota_log);
  httpRequestData += ",";
  httpRequestData += ota_timer;
  httpRequestData += "\n";
  send_log_s(httpRequestData);
  bme_log = 200;
  camera_log = 200;
}
void loop() {
  int m = millis();
  if (m - ota_timer > 60000) {
    ota_timer = millis();
    int ret = ota.CheckForOTAUpdate(JSON_URL, VERSION);
    Serial.printf("CheckForOTAUpdate returned %d (%s)\n\n", ret, errtext(ret));
    ota_log = ret;
  }
  else if (m - log_timer > 30000) {
    send_log();
    log_timer = millis();
  }
  else if (do_bme && bme_log == 200 && m - bme_timer > 60000) {
    bme_log = send_bme();
    bme_timer = millis();
  }
  else if (m - camera_timer > 500 && camera_log == 200) {
    camera_log = send_frame();
    camera_log = 200;
    camera_timer = millis();
  }
}

const char *errtext(int code)
{
	switch(code)
	{
		case ESP32OTAPull::UPDATE_AVAILABLE:
			return "An update is available but wasn't installed";
		case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
			return "No profile matches";
		case ESP32OTAPull::NO_UPDATE_AVAILABLE:
			return "Profile matched, but update not applicable";
		case ESP32OTAPull::UPDATE_OK:
			return "An update was done, but no reboot";
		case ESP32OTAPull::HTTP_FAILED:
			return "HTTP GET failure";
		case ESP32OTAPull::WRITE_ERROR:
			return "Write error";
		case ESP32OTAPull::JSON_PROBLEM:
			return "Invalid JSON";
		case ESP32OTAPull::OTA_UPDATE_FAIL:
			return "Update fail (no OTA partition?)";
		default:
			if (code > 0)
				return "Unexpected HTTP response code";
			break;
	}
	return "Unknown error";
}


void callback(int offset, int totallength)
{
	Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
// #if defined(LED_BUILTIN) // flicker LED on update
// 	static int status = LOW;
// 	status = status == LOW && offset < totallength ? HIGH : LOW;
// 	digitalWrite(LED_BUILTIN, status);
// #endif
}

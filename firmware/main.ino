#include <Wire.h>
#include <Adafruit_BME680.h>
#include <Adafruit_PM25AQI.h>
#include <Adafruit_AS7341.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <time.h>

// WiFi
#define WIFI_SSID "WIFISSID"
#define WIFI_PASS "WIFI_PASS"

// API
#define API_URL "https://digital-twin-donkerbos.onrender.com/api/sensor"
#define API_KEY "API_KEY"

// PCA9548A
#define PCA_ADDR  0x70
#define CH_PMSA   3
#define CH_AS7341 6
#define CH_BME    7
#define CH_RTC    5

// LM393 Microphone pins — wired directly to Feather
#define MIC_LOUD_AO  A3   
#define MIC_LOUD_DO  A4   
#define MIC_QUIET_AO 15   
#define MIC_QUIET_DO 32   

// Number of samples averaged per mic reading
#define MIC_SAMPLES 32

// Sensors
Adafruit_BME680  bme;
Adafruit_PM25AQI aqi;
PM25_AQI_Data    pmsaData;
Adafruit_AS7341  as7341;
RTC_DS3231       rtc;

float temperature = -1, pressure = -1, humidity = -1, gas = -1, altitude = -1;

uint16_t pm10 = 0, pm25 = 0, pm100 = 0;
uint16_t particles_03 = 0, particles_05 = 0;
uint16_t particles_10 = 0, particles_25 = 0, particles_50 = 0, particles_100 = 0;

float    asCounts[10] = {0};
uint16_t flicker = 0;

uint16_t mic_loud_analog  = 0;
uint8_t  mic_loud_digital = 0;
uint16_t mic_low_analog   = 0;
uint8_t  mic_low_digital  = 0;


void selectChannel(uint8_t ch) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
  delay(5);
}

uint32_t getTimestamp() {
  selectChannel(CH_RTC);
  if (rtc.begin()) {
    DateTime now = rtc.now();
    Serial.println("[TIME] Using RTC");
    return now.unixtime();
  }
  Serial.println("[TIME] RTC failed → using NTP");
  uint32_t ts = time(nullptr);
  if (ts < 100000) {
    configTime(0, 0, "pool.ntp.org");
    delay(2000);
    ts = time(nullptr);
  }
  return ts;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();

  selectChannel(CH_BME);
  bme.begin(0x77) ? Serial.println("[OK] BME680") : Serial.println("[ERROR] BME680");

  selectChannel(CH_AS7341);
  as7341.begin() ? Serial.println("[OK] AS7341") : Serial.println("[ERROR] AS7341");

  selectChannel(CH_PMSA);
  Serial.println("[ESP] Warming PMSA...");
  aqi.begin_I2C() ? Serial.println("[OK] PMSA") : Serial.println("[ERROR] PMSA");
  delay(30000);

  selectChannel(CH_RTC);
  rtc.begin() ? Serial.println("[OK] RTC") : Serial.println("[WARN] RTC missing");

  pinMode(MIC_LOUD_AO,  INPUT);
  pinMode(MIC_LOUD_DO,  INPUT);
  pinMode(MIC_QUIET_AO, INPUT);
  pinMode(MIC_QUIET_DO, INPUT);
  Serial.println("[OK] Microphones (LM393 x2)");

  Serial.println("[ESP] Ready");
}

#define TIME_SLEEPING 1800000
void loop() {
  uint32_t timestamp = getTimestamp();

  Serial.println("\n========== NEW MEASURE ==========");

  readBME();
  readAS();
  readPMSA();
  readMicrophones();

  char json[1200];
  buildJSON(json, sizeof(json), timestamp);

  Serial.println("\n[JSON]");
  Serial.println(json);

  sendToAPI(json);

  Serial.println("[ESP] Sleep 60s...\n");
  delay(TIME_SLEEPING);
}

void readBME() {
  selectChannel(CH_BME);
  if (bme.performReading()) {
    temperature = bme.temperature;
    pressure    = bme.pressure / 100.0;
    humidity    = bme.humidity;
    gas         = bme.gas_resistance / 1000.0;
    altitude    = bme.readAltitude(1013.25);
    Serial.printf("[BME680] T=%.2f H=%.2f P=%.2f G=%.2f\n",
                  temperature, humidity, pressure, gas);
  } else {
    Serial.println("[ERROR] BME680 read failed");
  }
}

void readAS() {
  selectChannel(CH_AS7341);
  if (as7341.readAllChannels()) {
    flicker     = as7341.detectFlickerHz();
    asCounts[0] = as7341.getChannel(AS7341_CHANNEL_415nm_F1);
    asCounts[1] = as7341.getChannel(AS7341_CHANNEL_445nm_F2);
    asCounts[2] = as7341.getChannel(AS7341_CHANNEL_480nm_F3);
    asCounts[3] = as7341.getChannel(AS7341_CHANNEL_515nm_F4);
    asCounts[4] = as7341.getChannel(AS7341_CHANNEL_555nm_F5);
    asCounts[5] = as7341.getChannel(AS7341_CHANNEL_590nm_F6);
    asCounts[6] = as7341.getChannel(AS7341_CHANNEL_630nm_F7);
    asCounts[7] = as7341.getChannel(AS7341_CHANNEL_680nm_F8);
    asCounts[8] = as7341.getChannel(AS7341_CHANNEL_CLEAR);
    asCounts[9] = as7341.getChannel(AS7341_CHANNEL_NIR);
    Serial.println("[AS7341] OK");
  } else {
    Serial.println("[ERROR] AS7341 failed");
  }
}

void readPMSA() {
  selectChannel(CH_PMSA);
  if (aqi.read(&pmsaData)) {
    pm10         = pmsaData.pm10_env;
    pm25         = pmsaData.pm25_env;
    pm100        = pmsaData.pm100_env;
    particles_03 = pmsaData.particles_03um;
    particles_05 = pmsaData.particles_05um;
    particles_10 = pmsaData.particles_10um;
    particles_25 = pmsaData.particles_25um;
    particles_50 = pmsaData.particles_50um;
    particles_100 = pmsaData.particles_100um;
    Serial.printf("[PMSA003I] PM2.5=%d PM10=%d\n", pm25, pm100);
  } else {
    Serial.println("[WARN] PMSA003I read failed");
  }
}

void readMicrophones() {
  uint32_t loudSum = 0, quietSum = 0;
  for (int i = 0; i < MIC_SAMPLES; i++) {
    loudSum  += analogRead(MIC_LOUD_AO);
    quietSum += analogRead(MIC_QUIET_AO);
    delay(2);
  }
  mic_loud_analog = loudSum  / MIC_SAMPLES;
  mic_low_analog  = quietSum / MIC_SAMPLES;

  mic_loud_digital = (digitalRead(MIC_LOUD_DO) == LOW) ? 1 : 0;
  mic_low_digital  = (digitalRead(MIC_QUIET_DO) == LOW) ? 1 : 0;

  Serial.printf("[MIC] Loud: analog=%d digital=%d | Quiet: analog=%d digital=%d\n",
                mic_loud_analog, mic_loud_digital,
                mic_low_analog,  mic_low_digital);
}


// ================= JSON =================
void buildJSON(char* buf, size_t size, uint32_t ts) {
  snprintf(buf, size,
    "{"
    "\"timestamp\":%lu,"
    "\"temperature_c\":%.2f,"
    "\"pressure_hpa\":%.2f,"
    "\"humidity_pct\":%.2f,"
    "\"gas_kohms\":%.2f,"
    "\"altitude_m\":%.2f,"
    "\"pm10_env\":%u,"
    "\"pm25_env\":%u,"
    "\"pm100_env\":%u,"
    "\"particles_03um\":%u,"
    "\"particles_05um\":%u,"
    "\"particles_10um\":%u,"
    "\"particles_25um\":%u,"
    "\"particles_50um\":%u,"
    "\"particles_100um\":%u,"
    "\"f1_415nm\":%.0f,"
    "\"f2_445nm\":%.0f,"
    "\"f3_480nm\":%.0f,"
    "\"f4_515nm\":%.0f,"
    "\"f5_555nm\":%.0f,"
    "\"f6_590nm\":%.0f,"
    "\"f7_630nm\":%.0f,"
    "\"f8_680nm\":%.0f,"
    "\"clear\":%.0f,"
    "\"nir\":%.0f,"
    "\"flicker_hz\":%u,"
    "\"mic_loud_digital\":%u,"
    "\"mic_loud_analog\":%u,"
    "\"mic_low_digital\":%u,"
    "\"mic_low_analog\":%u"
    "}",
    ts,
    temperature, pressure, humidity, gas, altitude,
    pm10, pm25, pm100,
    particles_03, particles_05, particles_10,
    particles_25, particles_50, particles_100,
    asCounts[0], asCounts[1], asCounts[2], asCounts[3],
    asCounts[4], asCounts[5], asCounts[6], asCounts[7],
    asCounts[8], asCounts[9], flicker,
    mic_loud_digital, mic_loud_analog,
    mic_low_digital,  mic_low_analog
  );
}

bool sendToAPI(const char* json) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting...");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(" FAILED");
      return false;
    }
    Serial.println(" OK");
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);

  int code = http.POST((uint8_t*)json, strlen(json));
  Serial.printf("[HTTP] Code: %d\n", code);
  http.end();

  return code == 200;
}
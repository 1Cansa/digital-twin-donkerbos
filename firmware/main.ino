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
#define WIFI_PASS "WIFIPASSWORD"

// API
#define API_URL "https://digital-twin-donkerbos.onrender.com/api/sensor"
#define API_KEY "put your API KEY"

// Timing
#define SEND_AT_HOUR 11
#define SEND_EVERYTIME false
#define SLEEP_DURATION 5 * 60 * 1000000
#define CONNECTION_TIMEOUT 10000

// Multiplexer
#define PCA_ADDR 0x70

// Channels
#define CH_PMSA 3
#define CH_AS7341 6
#define CH_BME 7

// Microphones
#define MIC_LOUD_DIGITAL 32
#define MIC_LOUD_ANALOG  34
#define MIC_LOW_DIGITAL  33
#define MIC_LOW_ANALOG   35

// Sensors
Adafruit_BME680 bme;
Adafruit_PM25AQI aqi;
PM25_AQI_Data pmsaData;

Adafruit_AS7341 as7341;
float asCounts[10];
uint16_t flicker = 0;

RTC_DS3231 rtc;

// Microphone values
int micLoudDigital = 0;
int micLoudAnalog  = 0;
int micLowDigital  = 0;
int micLowAnalog   = 0;

RTC_DATA_ATTR bool sentToday = false;
RTC_DATA_ATTR uint32_t lastSentDay = 0;

#define SEALEVELPRESSURE_HPA (1013.25)

void selectChannel(uint8_t channel) {
  Wire.beginTransmission(PCA_ADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delay(5);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(MIC_LOUD_DIGITAL, INPUT);
  pinMode(MIC_LOW_DIGITAL, INPUT);

  Wire.begin();
  Wire.setTimeout(2000);

  // BME680
  selectChannel(CH_BME);
  if (!bme.begin(0x77)) {
    Serial.println("[ERROR] BME680 init failed");
  } else {
    Serial.println("[OK] BME680 ready");
  }

  // AS7341
  selectChannel(CH_AS7341);
  if (!as7341.begin()) {
    Serial.println("[ERROR] AS7341 init failed");
  } else {
    Serial.println("[OK] AS7341 ready");
  }

  // PMSA
  selectChannel(CH_PMSA);
  Serial.println("[ESP] Warming up PMSA...");
  if (!aqi.begin_I2C()) {
    Serial.println("[ERROR] PMSA init failed");
  }
  delay(30000);
  Serial.println("[OK] PMSA ready");

  // RTC
  if (!rtc.begin()) {
    Serial.println("[ERROR] RTC not found");
  }

  // Config sensors
  as7341.setATIME(100);
  as7341.setASTEP(999);
  as7341.setGain(AS7341_GAIN_256X);

  bme.setTemperatureOversampling(BME680_OS_16X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  Serial.println("[ESP] Station ready");
}

void loop() {
  uint32_t timestamp = getTimestampSafe();

  uint32_t currentDay = timestamp / 86400;
  if (currentDay != lastSentDay) {
    sentToday = false;
    lastSentDay = currentDay;
  }

  if (!readPMSA()) Serial.println("[ERROR] PMSA read failed");
  if (!readBME())  Serial.println("[ERROR] BME680 read failed");
  if (!readAS())   Serial.println("[ERROR] AS7341 read failed");

  readMicrophones();

  char json[900];
  buildJSON(json, sizeof(json), timestamp);

  uint32_t hourOfDay = (timestamp % 86400) / 3600;
  if ((hourOfDay == SEND_AT_HOUR && !sentToday) || SEND_EVERYTIME) {
    if (sendToAPI(json)) {
      sentToday = true;
    }
  }

  goToSleep();
}

bool readPMSA() {
  selectChannel(CH_PMSA);
  return aqi.read(&pmsaData);
}

bool readBME() {
  selectChannel(CH_BME);
  return bme.performReading();
}

bool readAS() {
  selectChannel(CH_AS7341);

  if (!as7341.readAllChannels()) return false;

  flicker = as7341.detectFlickerHz();

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

  return true;
}

void readMicrophones() {
  micLoudDigital = digitalRead(MIC_LOUD_DIGITAL);
  micLoudAnalog  = analogRead(MIC_LOUD_ANALOG);
  micLowDigital  = digitalRead(MIC_LOW_DIGITAL);
  micLowAnalog   = analogRead(MIC_LOW_ANALOG);
}

uint32_t getTimestampSafe() {
  if (rtc.lostPower()) {
    Serial.println("[WARN] RTC lost power, syncing...");
    if (!syncRTC()) return 0;
  }
  return rtc.now().unixtime();
}

bool syncRTC() {
  if (!connectWiFi()) return false;

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) delay(500);

  rtc.adjust(DateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  ));

  WiFi.disconnect(true);
  esp_wifi_stop();
  return true;
}


bool sendToAPI(const char* json) {
  if (!connectWiFi()) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);

  int code = http.POST((uint8_t*)json, strlen(json));
  Serial.printf("[HTTP] Code: %d\n", code);

  http.end();
  WiFi.disconnect(true);
  esp_wifi_stop();

  return code == 200;
}

bool connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECTION_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected");
    return true;
  }

  Serial.println("\n[ERROR] WiFi failed");
  return false;
}

void goToSleep() {
  Serial.println("[ESP] Deep sleep...");
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
  esp_deep_sleep_start();
}

void buildJSON(char* buf, size_t bufSize, uint32_t timestamp) {
  snprintf(buf, bufSize,
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
    "\"mic_loud_digital\":%d,"
    "\"mic_loud_analog\":%d,"
    "\"mic_low_digital\":%d,"
    "\"mic_low_analog\":%d"
    "}",
    timestamp,
    bme.temperature, bme.pressure / 100.0, bme.humidity,
    bme.gas_resistance / 1000.0, bme.readAltitude(SEALEVELPRESSURE_HPA),
    pmsaData.pm10_env, pmsaData.pm25_env, pmsaData.pm100_env,
    pmsaData.particles_03um, pmsaData.particles_05um, pmsaData.particles_10um,
    pmsaData.particles_25um, pmsaData.particles_50um, pmsaData.particles_100um,
    asCounts[0], asCounts[1], asCounts[2], asCounts[3],
    asCounts[4], asCounts[5], asCounts[6], asCounts[7],
    asCounts[8], asCounts[9], flicker,
    micLoudDigital, micLoudAnalog,
    micLowDigital, micLowAnalog);
}
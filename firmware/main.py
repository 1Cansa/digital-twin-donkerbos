import time
import ssl
import wifi
import socketpool
import adafruit_requests
import board
import busio
import digitalio
import analogio

import adafruit_bme680
import adafruit_pm25.i2c
import adafruit_as7341


WIFI_SSID = "WIFISSID"
WIFI_PASS = "WIFIPASSWORD"

API_URL = "https://digital-twin-donkerbos.onrender.com/api/sensor"
API_KEY = "your_api_key"

SEND_INTERVAL = 300  # seconds


i2c = busio.I2C(board.GP1, board.GP0)

bme = adafruit_bme680.Adafruit_BME680_I2C(i2c)
pm25 = adafruit_pm25.i2c.PM25_I2C(i2c)
as7341 = adafruit_as7341.AS7341(i2c)

mic_loud_digital = digitalio.DigitalInOut(board.GP16)
mic_loud_digital.direction = digitalio.Direction.INPUT

mic_low_digital = digitalio.DigitalInOut(board.GP17)
mic_low_digital.direction = digitalio.Direction.INPUT

mic_loud_analog = analogio.AnalogIn(board.GP26)
mic_low_analog  = analogio.AnalogIn(board.GP27)


print("[WiFi] Connecting...")
wifi.radio.connect(WIFI_SSID, WIFI_PASS)
print("[WiFi] Connected!")

pool = socketpool.SocketPool(wifi.radio)
requests = adafruit_requests.Session(pool, ssl.create_default_context())

while True:
    try:
        # Timestamp
        timestamp = int(time.time())

        # BME680
        temperature = bme.temperature
        pressure = bme.pressure
        humidity = bme.humidity
        gas = bme.gas

        # PM2.5
        aqi = pm25.read()
        
        # AS7341
        channels = as7341.all_channels
        flicker = as7341.flicker_detected

        # Microphones
        mic_ld = mic_loud_digital.value
        mic_la = mic_loud_analog.value
        mic_sd = mic_low_digital.value
        mic_sa = mic_low_analog.value

        # JSON payload
        data = {
            "timestamp": timestamp,
            "temperature_c": temperature,
            "pressure_hpa": pressure,
            "humidity_pct": humidity,
            "gas_kohms": gas / 1000,
            "altitude_m": 0,

            "pm10_env": aqi["pm10 env"],
            "pm25_env": aqi["pm25 env"],
            "pm100_env": aqi["pm100 env"],

            "particles_03um": aqi["particles 03um"],
            "particles_05um": aqi["particles 05um"],
            "particles_10um": aqi["particles 10um"],
            "particles_25um": aqi["particles 25um"],
            "particles_50um": aqi["particles 50um"],
            "particles_100um": aqi["particles 100um"],

            "f1_415nm": channels[0],
            "f2_445nm": channels[1],
            "f3_480nm": channels[2],
            "f4_515nm": channels[3],
            "f5_555nm": channels[4],
            "f6_590nm": channels[5],
            "f7_630nm": channels[6],
            "f8_680nm": channels[7],
            "clear": channels[8],
            "nir": channels[9],

            "flicker_hz": flicker,

            "mic_loud_digital": int(mic_ld),
            "mic_loud_analog": mic_la,
            "mic_low_digital": int(mic_sd),
            "mic_low_analog": mic_sa
        }

        print("[HTTP] Sending data...")

        headers = {
            "Content-Type": "application/json",
            "x-api-key": API_KEY
        }

        response = requests.post(API_URL, json=data, headers=headers)
        print("[HTTP] Status:", response.status_code)
        response.close()

    except Exception as e:
        print("[ERROR]", e)

    print("[SLEEP]")
    time.sleep(SEND_INTERVAL)
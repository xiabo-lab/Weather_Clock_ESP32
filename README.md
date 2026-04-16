# ESP32 Weather Clock with Cuckoo Chime

A feature-rich desk clock built on the **DOIT ESP32 DevKit V1** that combines an analog + digital clock, live weather forecast, cuckoo chime, and configurable alarm — all controlled with three physical buttons and displayed on a 128×64 OLED screen.

No credentials are hardcoded. WiFi and location are configured at first boot via a phone browser.

---

## Features

- **Analog + digital clock** — analog face with hour/minute/second hands, plus digital HH:MM:SS and MM/DD/YY
- **Live weather forecast** — today and tomorrow, fetched by GPS coordinates
  - Condition, temperature min/max, wind speed/direction, UV index, sunrise/sunset
  - Primary API: [Open-Meteo](https://open-meteo.com) — automatic fallback to [wttr.in](https://wttr.in)
  - 6-hour cache, auto-refreshes when stale
- **Cuckoo chime** — plays at the top of every hour from 7 AM to 9 PM
  - Chimes N times matching the current hour (12-hour format)
  - DAC-generated sine wave audio through speaker/amp
- **Alarm clock** — daily alarm with Minuet in G melody, toggle on/off without losing the set time
- **WiFi captive portal** — first boot creates a hotspot for phone-based WiFi setup, no code editing needed
- **Auto location detection** — IP-based geolocation on first boot, saved to flash
- **Manual location override** — enter precise latitude, longitude, and UTC offset via buttons
- **City name lookup** — reverse geocoded automatically after location entry, retried silently in background

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | DOIT ESP32 DevKit V1 |
| Display | SSD1306 128×64 OLED, I2C address 0x3C |
| Audio | HXJ8002 audio amplifier module + speaker (4Ω–8Ω) |
| Blue button | GPIO27 — momentary, active HIGH |
| Red button | GPIO14 — momentary, active HIGH |
| Yellow button | GPIO12 — momentary, active HIGH |

### Wiring

```
SSD1306 OLED
  VCC  → ESP32 3V3
  GND  → GND
  SDA  → GPIO21
  SCL  → GPIO22

HXJ8002 Amplifier (5 pins)
  VCC  → ESP32 5V
  GND  → GND
  R    → Not connected
  GND  → GND
  L    → GPIO25  (DAC output)

HXJ8002 → Speaker
  OUT+ → Speaker +
  OUT− → Speaker −

Each button (×3)
  One leg  → 5V rail
  Other leg → Signal pin + 10kΩ resistor to GND
  GPIO27 = Blue | GPIO14 = Red | GPIO12 = Yellow
```

**Button logic:** pull-down resistor circuit. When released the pin reads LOW (0V). When pressed it reads HIGH (5V).

---

## Libraries Required

Install all via **Arduino Library Manager** (Sketch → Include Library → Manage Libraries):

| Library | Author |
|---|---|
| ArduinoJson | Benoit Blanchon |
| Adafruit GFX Library | Adafruit |
| Adafruit SSD1306 | Adafruit |
| WiFiManager | tzapu |

The following are included with the ESP32 board package and need no separate install:
`WiFi`, `Preferences`, `time.h`, `driver/dac.h`

**Board:** ESP32 Dev Module (Espressif Systems package)
Install via Arduino IDE → Preferences → Additional Board URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

---

## First Boot — WiFi Setup

1. Power on the device
2. It creates a hotspot named **`WeatherClock-Setup`**
3. Connect your phone or laptop to that hotspot
4. A browser page opens automatically at `192.168.4.1`
   (if it doesn't, open your browser and go there manually)
5. Select your WiFi network from the list, enter the password, tap Save
6. The device reboots, connects to WiFi, auto-detects your location from your IP address, and starts the clock

WiFi credentials are saved to flash memory and survive reboots and power cuts.

---

## Button Reference

### Blue button (GPIO27)

| Press | Action |
|---|---|
| Single press | Return to clock face |
| Double press | Enter **Alarm Setup** mode |
| Hold 3 seconds | Toggle alarm **ON / OFF** |
| Hold 10 seconds | Wipe WiFi credentials and reboot into setup mode |

**Alarm Setup mode:**
- Yellow = increase hour or minute
- Blue = decrease hour or minute
- Red = confirm and advance to next field

### Red button (GPIO14)

| Press | Action |
|---|---|
| Single press | Today's weather card (condition, temp min/max) |
| Double press | Today's detail card (wind, UV index, sunrise, sunset) |
| Hold 3 seconds | Enter **Location Setup** mode |

**Location Setup mode (3 pages):**

| Page | Field | Yellow | Blue | Tap step | Short hold | Long hold |
|---|---|---|---|---|---|---|
| 1 | Latitude | Increase | Decrease | ±0.01 | ±0.10 | ±1.00 continuous |
| 2 | Longitude | Increase | Decrease | ±0.01 | ±0.10 | ±1.00 continuous |
| 3 | UTC offset | +1 hour | −1 hour | — | — | — |

Red = confirm each page and advance. After UTC is confirmed, the device reverse-geocodes the coordinates to find the city name, then reboots with the new location active.

### Yellow button (GPIO12)

| Press | Action |
|---|---|
| Single press | Tomorrow's weather card |
| Double press | Tomorrow's detail card |
| Hold 3 seconds | Force immediate weather re-fetch (any screen) |

---

## Clock Face Layout

```
┌────────────────────────────────┐
│  ╭──────╮   12:34:56           │
│  │ (( )) │   04/15/25           │
│  │  ╲   │                      │
│  ╰──────╯   🔔 06:50           │
└────────────────────────────────┘
```

- Left half — analog clock face with second, minute, and hour hands
- Top right — digital time HH:MM:SS
- Mid right — date MM/DD/YY
- Bottom right — alarm icon and alarm time (shows `NoAlrm` when alarm is disabled)

---

## Cuckoo Chime

- Plays automatically at the **top of every hour** from **7 AM to 9 PM**
- Number of chimes = current hour in 12-hour format (e.g. 8 PM → 8 chimes)
- Sound: two-note "koo koo" pattern, D5 → B4, synthesised via GPIO25 DAC
- If alarm is enabled and set for that exact hour, the alarm melody plays instead
- If alarm is disabled, cuckoo plays at that hour regardless
- Double press Blue while in cuckoo hours → manual chime trigger (removed in current build; use Yellow 3s to re-fetch)

---

## Weather Services

| Service | URL | Notes |
|---|---|---|
| Open-Meteo | api.open-meteo.com | Primary. Free, no API key, lat/lon based |
| wttr.in | wttr.in | Backup. Free, no API key |

Both services are free with no API key required. Weather is always fetched by latitude and longitude — the city name is for display only and does not affect accuracy.

Weather data is cached for 6 hours. The cache is automatically refreshed when stale. You can force an immediate re-fetch by holding Yellow for 3 seconds.

---

## Location System

| Method | When used |
|---|---|
| Flash (saved from previous boot) | Every boot after the first |
| IP geolocation (ip-api.com) | First boot, or after WiFi wipe |
| Manual entry via buttons | Red hold 3s → Location Setup |
| San Jose, CA fallback | If all detection methods fail |

City name is looked up via `api.bigdatacloud.net` with `geocode.maps.co` as fallback. If both fail, the city name is retried silently in the background every 30 seconds until found.

Location is saved to flash and reused on every subsequent boot. To force re-detection, hold Blue for 10 seconds to wipe all saved data (WiFi credentials and location are both cleared).

---

## Customisation

Open `Weather_Forecast_ESP32_V2.ino` and adjust these constants near the top:

```cpp
// Cuckoo chime window (24-hour)
const int CUCKOO_START = 7;   // first chime hour
const int CUCKOO_END   = 21;  // last chime hour

// Default alarm time
int ALARM_HOUR   = 6;
int ALARM_MINUTE = 50;
```

UTC offset, city, latitude, and longitude are all set at runtime via the button interface and saved to flash — no code changes needed.

---

## File Structure

```
Weather_Forecast_ESP32_V2/
├── Weather_Forecast_ESP32_V2.ino   — main sketch
├── pitches.h                        — musical note frequency definitions
└── README.md                        — this file
```

---

## APIs Used

All APIs are **free** and require **no API key**:

| API | Purpose | URL |
|---|---|---|
| Open-Meteo | Weather forecast | api.open-meteo.com |
| wttr.in | Weather backup | wttr.in |
| ip-api.com | IP geolocation | ip-api.com |
| BigDataCloud | Reverse geocoding | api.bigdatacloud.net |
| geocode.maps.co | Reverse geocoding backup | geocode.maps.co |
| NTP | Time sync | pool.ntp.org |

---

## License

MIT License — free to use, modify, and distribute for personal or commercial projects.

---

## Acknowledgements

- [Open-Meteo](https://open-meteo.com) — free open-source weather API
- [wttr.in](https://wttr.in) — terminal weather service by Igor Chubin
- [WiFiManager](https://github.com/tzapu/WiFiManager) by tzapu — captive portal library
- [ArduinoJson](https://arduinojson.org) by Benoit Blanchon — JSON parsing
- [Adafruit GFX + SSD1306](https://github.com/adafruit/Adafruit_SSD1306) — OLED display driver
- Musical note definitions from [robsoncouto/arduino-songs](https://github.com/robsoncouto/arduino-songs)

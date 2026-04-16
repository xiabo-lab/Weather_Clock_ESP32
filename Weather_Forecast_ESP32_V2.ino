/*
 * ESP32 DevKit V1 — Weather Clock with Cuckoo Chime
 * Revision 8 — Full feature set
 *
 * ═══════════════════════════════════════════════════
 *  HARDWARE
 * ═══════════════════════════════════════════════════
 *  - SSD1306 128×64 OLED   I2C 0x3C  SDA=GPIO21  SCL=GPIO22
 *  - Speaker/buzzer         GPIO25    DAC (cuckoo) + PWM (alarm) shared
 *  - Blue   button          GPIO27
 *  - Red    button          GPIO14
 *  - Yellow button          GPIO12
 *  Buttons: active HIGH with 10kΩ pull-down resistors to GND
 *
 * ═══════════════════════════════════════════════════
 *  FIRST BOOT — WiFi Setup
 * ═══════════════════════════════════════════════════
 *  1. Device starts hotspot:  WeatherClock-Setup
 *  2. Connect phone to that hotspot
 *  3. Browser opens 192.168.4.1 automatically
 *     (or open manually if it doesn't)
 *  4. Select your WiFi network, enter password, Save
 *  5. Device reboots, connects, detects location
 *     from IP and fetches weather automatically
 *  Credentials are saved to flash — survive reboots.
 *
 * ═══════════════════════════════════════════════════
 *  BUTTON GUIDE
 * ═══════════════════════════════════════════════════
 *
 *  🔵 BLUE BUTTON
 *  ─────────────────────────────────────────────────
 *  Single press    → Return to clock face
 *  Double press    → Enter Alarm Setup mode
 *                    • Yellow = hour/minute up
 *                    • Blue   = hour/minute down
 *                    • Red    = confirm and advance
 *  Hold 3s         → Toggle alarm ON / OFF
 *                    OLED shows "ON" or "OFF" briefly
 *  Hold 10s        → Wipe WiFi credentials and reboot
 *                    into WiFi setup mode
 *                    (use when changing router)
 *
 *  🔴 RED BUTTON
 *  ─────────────────────────────────────────────────
 *  Single press    → Today's weather card
 *                    (date, condition, temp min/max)
 *  Double press    → Today's detail card
 *                    (wind speed/dir, UV, sunrise/sunset)
 *  Hold 3s         → Enter Location Setup mode
 *                    Page 1 — Latitude
 *                    • Yellow = increase  Blue = decrease
 *                    • Tap = ±0.01  Short hold = ±0.10
 *                    • Long hold = ±1.00 continuously
 *                    • Red = confirm, go to next page
 *                    Page 2 — Longitude (same controls)
 *                    Page 3 — UTC offset (Yellow/Blue ±1hr)
 *                    • After confirming UTC, device looks
 *                      up city name then reboots
 *
 *  🟡 YELLOW BUTTON
 *  ─────────────────────────────────────────────────
 *  Single press    → Tomorrow's weather card
 *  Double press    → Tomorrow's detail card
 *  Hold 3s         → Force weather re-fetch immediately
 *                    (works from any screen)
 *                    Shows today's weather after fetch
 *
 * ═══════════════════════════════════════════════════
 *  CLOCK FACE
 * ═══════════════════════════════════════════════════
 *  Left half   — Analog clock (hour/min/sec hands)
 *  Right top   — Digital time  HH:MM:SS
 *  Right mid   — Date          MM/DD/YY
 *  Right bot   — Alarm icon + time  (or "NoAlrm" if OFF)
 *
 * ═══════════════════════════════════════════════════
 *  CUCKOO CHIME
 * ═══════════════════════════════════════════════════
 *  Plays automatically at the top of every hour
 *  from 7AM to 9PM. Chimes N times = current hour
 *  (12-hour format). e.g. 8PM → 8 "koo koo" calls.
 *  If the alarm is set for that exact hour, the alarm
 *  melody plays instead of the cuckoo.
 *  If alarm is disabled, cuckoo plays regardless.
 *
 * ═══════════════════════════════════════════════════
 *  WEATHER SERVICES
 * ═══════════════════════════════════════════════════
 *  Primary:  Open-Meteo  (api.open-meteo.com)
 *  Backup:   wttr.in
 *  Weather is fetched by latitude/longitude — city
 *  name does not affect weather accuracy.
 *  Cache: 6 hours. Auto-refreshes when stale.
 *
 * ═══════════════════════════════════════════════════
 *  LOCATION
 * ═══════════════════════════════════════════════════
 *  Auto-detected from IP on first boot.
 *  Saved to flash — reused on every subsequent boot.
 *  Override anytime: Red hold 3s → Location Setup.
 *  City name looked up from coordinates automatically.
 *  If city shows "Unknown", it retries silently every
 *  30 seconds in the background.
 *
 * ═══════════════════════════════════════════════════
 *  LIBRARIES (install via Arduino Library Manager)
 * ═══════════════════════════════════════════════════
 *  - ArduinoJson
 *  - Adafruit GFX Library
 *  - Adafruit SSD1306
 *  - WiFiManager  (by tzapu)
 *  WiFi, Preferences, time.h — included with ESP32
 *  Board: "ESP32 Dev Module" (Espressif Systems)
 */

#include <WiFi.h>
#include <WiFiManager.h>       // tzapu/WiFiManager — captive portal provisioning
#include <Preferences.h>       // ESP32 NVS — persist WiFi reset flag
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <time.h>
#include <driver/dac.h>
#include <initializer_list>    // for firstNonEmpty helper
#include "pitches.h"

// ─────────────────────────────────────────────
//  ESP32 tone — uses LEDC PWM (core v3.x API)
// ─────────────────────────────────────────────
#define TONE_RESOLUTION 8
void esp32Tone(int pin, int freq) {
  if (freq > 0) {
    ledcAttach(pin, freq, TONE_RESOLUTION);
    ledcWriteTone(pin, freq);
  }
}
void esp32NoTone(int pin) {
  ledcWriteTone(pin, 0);
  ledcDetach(pin);
}

// ─────────────────────────────────────────────
//  *** CONFIGURE THESE ***
// ─────────────────────────────────────────────
// WiFi credentials are no longer hardcoded.
// On first boot the device starts a hotspot called
// "WeatherClock-Setup". Connect your phone to it,
// enter your WiFi SSID + password on the page that
// opens, and the credentials are saved to flash.
// Long press Blue button 10 s to wipe and re-enter
// setup mode at any time.
//
// Location is auto-detected from your IP address.
// You can optionally override it in the WiFi setup
// portal by filling in the location fields.

// Cuckoo chime window (24-hour)
const int CUCKOO_START = 7;   // 7 AM — first chime
const int CUCKOO_END   = 21;  // 9 PM — last chime
// ─────────────────────────────────────────────

// ── Location globals ──────────────────────────
// Populated at boot from IP geolocation or flash.
// Override via WiFiManager portal fields.
float  g_lat        =  0.0f;
float  g_lon        =  0.0f;
long   g_utcOffset  =  0L;    // seconds, e.g. -25200 = UTC-7
String g_city       = "";
String g_country    = "";
String g_timezone   = "";     // e.g. "America/Los_Angeles"
String g_tzEncoded  = "";     // URL-encoded for Open-Meteo API

// ── Alarm time ────────────────────────────────
// Mutable — updated via alarm setup mode
int  ALARM_HOUR    =  6;
int  ALARM_MINUTE  = 50;
bool g_alarmEnabled = true;   // false = alarm silenced, shows "No Alarm"

// ── Button pins ───────────────────────────────
#define BTN_BLUE   27   // return to clock
#define BTN_RED    14   // today's weather
#define BTN_YELLOW 12   // tomorrow's weather

// ── Buzzer / DAC pin — shared GPIO25 ─────────
// PWM (LEDC) drives it for alarm melody.
// DAC drives it for cuckoo chime.
// The two are NEVER active at the same time.
const int buzzer = 25;

// DAC channel for GPIO25
#define DAC_CH  DAC_CHANNEL_1   // GPIO25

// ── OLED ──────────────────────────────────────
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Alarm melody (Minuet in G) ────────────────
int tempo    = 144;
int melody[] = {
  NOTE_E5, 4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_C5,8,  NOTE_B4,8,
  NOTE_A4, 4,  NOTE_A4,8,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
  NOTE_B4,-4,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
  NOTE_C5, 4,  NOTE_A4,4,  NOTE_A4,4,  REST,4,

  REST,8,  NOTE_D5,4,  NOTE_F5,8,  NOTE_A5,4,  NOTE_G5,8,  NOTE_F5,8,
  NOTE_E5,-4,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
  NOTE_B4, 4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
  NOTE_C5, 4,  NOTE_A4,4,  NOTE_A4,4,  REST,4,

  NOTE_E5,2, NOTE_C5,2,
  NOTE_D5,2, NOTE_B4,2,
  NOTE_C5,2, NOTE_A4,2,
  NOTE_B4,1,

  NOTE_E5,2, NOTE_C5,2,
  NOTE_D5,2, NOTE_B4,2,
  NOTE_C5,4, NOTE_E5,4, NOTE_A5,2,
  NOTE_GS5,1,

  NOTE_E5, 4,  NOTE_B4,8,  NOTE_C5,8,  NOTE_D5,4,  NOTE_C5,8,  NOTE_B4,8,
  NOTE_A4, 4,  NOTE_A4,8,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
  NOTE_B4,-4,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
  NOTE_C5, 4,  NOTE_A4,4,  NOTE_A4,4,  REST,4,

  REST,8, NOTE_D5,4,  NOTE_F5,8,  NOTE_A5,4,  NOTE_G5,8,  NOTE_F5,8,
  REST,8, NOTE_E5,4,  NOTE_C5,8,  NOTE_E5,4,  NOTE_D5,8,  NOTE_C5,8,
  REST,8, NOTE_B4,4,  NOTE_C5,8,  NOTE_D5,4,  NOTE_E5,4,
  REST,8, NOTE_C5,4,  NOTE_A4,8,  NOTE_A4,4,  REST,4,
};
int notes     = sizeof(melody) / sizeof(melody[0]) / 2;
int wholenote = (60000 * 4) / tempo;
int divider   = 0, noteDuration = 0;

// ── NTP ───────────────────────────────────────
const char* NTP_SERVER = "pool.ntp.org";

// ── Weather API ───────────────────────────────
const char* WEATHER_HOST = "api.open-meteo.com";
WiFiClient  client;

// ── Time globals ──────────────────────────────
int  g_hour   = -1;
int  g_minute = -1;
int  g_second =  0;
int  g_day    =  1;
int  g_month  =  1;
int  g_year   =  2000;

// ── Display state ─────────────────────────────
enum DisplayMode { MODE_CLOCK, MODE_TODAY, MODE_TOMORROW, MODE_ALARM_SETUP };
DisplayMode g_displayMode = MODE_CLOCK;

// ── Alarm flag ────────────────────────────────
bool g_alarmFiredToday = false;

// ── Cuckoo flag ───────────────────────────────
// Tracks the last hour we chimed so we only chime once per hour.
int  g_lastChimeHour = -1;

// ── Weather cache ─────────────────────────────
const unsigned long WEATHER_CACHE_MS = 21600000UL;
unsigned long g_weatherFetchedAt = 0;

// Basic card data (today + tomorrow)
String g_todayDate = ""; int g_todayWMO = 0;
float  g_todayTMin = 0,      g_todayTMax = 0;
String g_tmrDate   = ""; int g_tmrWMO   = 0;
float  g_tmrTMin   = 0,      g_tmrTMax   = 0;

// Detail data (today + tomorrow)
float  g_todayWindSpd = 0;  int g_todayWindDir = 0;
float  g_todayUV      = 0;
String g_todaySunrise = "";  String g_todaySunset = "";

float  g_tmrWindSpd   = 0;  int g_tmrWindDir   = 0;
float  g_tmrUV        = 0;
String g_tmrSunrise   = "";  String g_tmrSunset   = "";

// ── Loop timing ───────────────────────────────
unsigned long g_lastResyncMs   = 0;
unsigned long g_lastSecondVal  = 99999UL;
unsigned long g_lastGeoRetryMs = 0;   // set in setup() after boot

// ─────────────────────────────────────────────
//  Button helper — active HIGH, debounced
//  Single press: press + release detected.
// ─────────────────────────────────────────────
bool buttonPressed(int pin) {
  if (digitalRead(pin) == HIGH) {
    delay(50);
    if (digitalRead(pin) == HIGH) {
      while (digitalRead(pin) == HIGH);
      delay(50);
      return true;
    }
  }
  return false;
}

// ─────────────────────────────────────────────
//  OLED — WiFi setup mode splash
//  Shown while the captive portal is active.
// ─────────────────────────────────────────────
void drawWiFiSetupScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 0);
  display.println("* WiFi Setup *");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
  display.setCursor(0, 13);
  display.println("Connect phone to:");
  display.setCursor(0, 23);
  display.println("WeatherClock-Setup");
  display.drawLine(0, 33, SCREEN_WIDTH - 1, 33, SSD1306_WHITE);
  display.setCursor(0, 36);
  display.println("Browser auto-opens");
  display.setCursor(0, 46);
  display.println("192.168.4.1");
  display.display();
}

// ─────────────────────────────────────────────
//  OLED — WiFi reset confirmation splash
//  Shown for 2 s before rebooting.
// ─────────────────────────────────────────────
void drawWiFiResetScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 8);
  display.println("* WiFi Reset *");
  display.drawLine(0, 18, SCREEN_WIDTH - 1, 18, SSD1306_WHITE);
  display.setCursor(10, 26);
  display.println("Credentials wiped.");
  display.setCursor(10, 38);
  display.println("Rebooting into");
  display.setCursor(10, 48);
  display.println("setup mode...");
  display.display();
}

// ─────────────────────────────────────────────
//  URL-encode a timezone string for Open-Meteo
//  Only "/" needs encoding → "%2F"
// ─────────────────────────────────────────────
String encodeTZ(String tz) {
  tz.replace("/", "%2F");
  return tz;
}

// ─────────────────────────────────────────────
//  HTTP body reader — inline, reusable
//  Used by geocode functions before readHttpBody
//  is defined in the file.
// ─────────────────────────────────────────────
String httpGet(const char* host, int port, const char* path) {
  WiFiClient c;
  c.setTimeout(15000);
  if (!c.connect(host, port)) {
    Serial.print("[HTTP] Cannot connect to "); Serial.println(host);
    return "";
  }
  c.print(String("GET ") + path + " HTTP/1.0\r\nHost: " +
          host + "\r\nConnection: close\r\n\r\n");
  unsigned long t = millis();
  while (!c.available()) {
    if (millis() - t > 15000) { c.stop(); return ""; }
    delay(10);
  }
  // Skip headers
  while (c.connected() || c.available()) {
    String line = c.readStringUntil('\n');
    if (line == "\r" || line == "") break;
  }
  String body = "";
  t = millis();
  while ((c.connected() || c.available()) && millis() - t < 15000) {
    if (c.available()) { body += (char)c.read(); t = millis(); }
  }
  c.stop();
  return body;
}

// ─────────────────────────────────────────────
//  Pick best non-empty string from candidates
// ─────────────────────────────────────────────
String firstNonEmpty(std::initializer_list<String> candidates) {
  for (const String& s : candidates)
    if (s.length() > 0 && s != "null") return s;
  return "";
}

// ─────────────────────────────────────────────
//  Reverse geocode lat/lon → city name
//
//  Service 1: api.bigdatacloud.net  (plain HTTP)
//  Service 2: geocode.maps.co       (plain HTTP)
//  Fallback:  keep previous g_city  (don't clobber
//             a good name with "Unknown")
// ─────────────────────────────────────────────
String reverseGeocode(float lat, float lon) {
  Serial.println("[GEO] Reverse geocoding...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 24); display.print("Finding city...");
  display.display();

  // ── Service 1: bigdatacloud ───────────────
  {
    char path[120];
    sprintf(path,
      "/data/reverse-geocode-client?latitude=%.4f&longitude=%.4f&localityLanguage=en",
      lat, lon);
    String body = httpGet("api.bigdatacloud.net", 80, path);
    Serial.print("[GEO] bigdatacloud: "); Serial.println(body.substring(0, 120));

    if (body.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, body)) {
        String city = firstNonEmpty({
          doc["city"].as<String>(),
          doc["locality"].as<String>(),
          doc["localityInfo"]["administrative"][2]["name"].as<String>(),
          doc["localityInfo"]["administrative"][1]["name"].as<String>(),
          doc["principalSubdivision"].as<String>(),
          doc["countryName"].as<String>()
        });
        if (city.length() > 0) {
          Serial.print("[GEO] City (bigdatacloud): "); Serial.println(city);
          return city;
        }
      }
    }
  }

  // ── Service 2: geocode.maps.co ────────────
  {
    char path[80];
    sprintf(path, "/reverse?lat=%.4f&lon=%.4f", lat, lon);
    String body = httpGet("geocode.maps.co", 80, path);
    Serial.print("[GEO] geocode.maps.co: "); Serial.println(body.substring(0, 120));

    if (body.length() > 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, body)) {
        JsonObject addr = doc["address"];
        String city = firstNonEmpty({
          addr["city"].as<String>(),
          addr["town"].as<String>(),
          addr["village"].as<String>(),
          addr["hamlet"].as<String>(),
          addr["county"].as<String>(),
          addr["state"].as<String>()
        });
        if (city.length() > 0) {
          Serial.print("[GEO] City (geocode.maps.co): "); Serial.println(city);
          return city;
        }
      }
    }
  }

  // ── Both failed — keep previous city ─────
  Serial.println("[GEO] All services failed — keeping previous city name.");
  if (g_city.length() > 0 && g_city != "Unknown")
    return g_city;
  return "Unknown";
}
void saveLocation() {
  Preferences prefs;
  prefs.begin("location", false);
  prefs.putFloat ("lat",      g_lat);
  prefs.putFloat ("lon",      g_lon);
  prefs.putLong  ("utcOff",   g_utcOffset);
  prefs.putString("city",     g_city);
  prefs.putString("country",  g_country);
  prefs.putString("timezone", g_timezone);
  prefs.end();
  Serial.println("[LOC] Saved to flash.");
}

// ─────────────────────────────────────────────
//  Load location from flash.
//  Returns true if valid saved data was found.
// ─────────────────────────────────────────────
bool loadSavedLocation() {
  Preferences prefs;
  prefs.begin("location", true);   // read-only
  float lat = prefs.getFloat("lat", 999.0f);
  if (lat == 999.0f) {
    prefs.end();
    Serial.println("[LOC] No saved location in flash.");
    return false;
  }
  g_lat       = lat;
  g_lon       = prefs.getFloat ("lon",      0.0f);
  g_utcOffset = prefs.getLong  ("utcOff",   0L);
  g_city      = prefs.getString("city",     "");
  g_country   = prefs.getString("country",  "");
  g_timezone  = prefs.getString("timezone", "");
  g_tzEncoded = encodeTZ(g_timezone);
  prefs.end();
  Serial.print("[LOC] Loaded from flash: ");
  Serial.print(g_city); Serial.print(", "); Serial.print(g_country);
  Serial.print(" | lat="); Serial.print(g_lat, 4);
  Serial.print(" lon="); Serial.print(g_lon, 4);
  Serial.print(" tz="); Serial.println(g_timezone);
  return true;
}

// ─────────────────────────────────────────────
//  Auto-detect location from IP address
//  Uses ip-api.com free JSON API — no key needed.
//  Returns true on success.
// ─────────────────────────────────────────────
bool fetchLocationByIP() {
  Serial.println("\n[LOC] Auto-detecting location from IP...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Detecting location");
  display.setCursor(10, 32);
  display.println("from IP address...");
  display.display();

  WiFiClient locClient;
  if (!locClient.connect("ip-api.com", 80)) {
    Serial.println("[LOC] Connection to ip-api.com failed.");
    return false;
  }

  locClient.print(
    "GET /json/?fields=status,city,countryCode,lat,lon,timezone,offset"
    " HTTP/1.0\r\nHost: ip-api.com\r\nConnection: close\r\n\r\n"
  );

  // Inline body reader — skip headers then read body
  unsigned long t = millis();
  while (!locClient.available()) {
    if (millis() - t > 8000) {
      Serial.println("[LOC] Timeout waiting for ip-api.com.");
      locClient.stop(); return false;
    }
    delay(10);
  }
  while (locClient.connected() || locClient.available()) {
    String line = locClient.readStringUntil('\n');
    if (line == "\r" || line == "") break;
  }
  String body = "";
  t = millis();
  while ((locClient.connected() || locClient.available()) && millis() - t < 8000) {
    if (locClient.available()) { body += (char)locClient.read(); t = millis(); }
  }
  locClient.stop();

  Serial.print("[LOC] ip-api response: "); Serial.println(body);

  if (body.length() == 0) {
    Serial.println("[LOC] Empty response.");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[LOC] JSON parse error: "); Serial.println(err.c_str());
    return false;
  }
  if (String(doc["status"].as<const char*>()) != "success") {
    Serial.println("[LOC] ip-api status != success.");
    return false;
  }

  g_lat       = doc["lat"].as<float>();
  g_lon       = doc["lon"].as<float>();
  g_utcOffset = doc["offset"].as<long>();
  g_city      = doc["city"].as<String>();
  g_country   = doc["countryCode"].as<String>();
  g_timezone  = doc["timezone"].as<String>();
  g_tzEncoded = encodeTZ(g_timezone);

  Serial.print("[LOC] Detected: ");
  Serial.print(g_city); Serial.print(", "); Serial.print(g_country);
  Serial.print(" lat="); Serial.print(g_lat, 4);
  Serial.print(" lon="); Serial.print(g_lon, 4);
  Serial.print(" tz="); Serial.print(g_timezone);
  Serial.print(" offset="); Serial.println(g_utcOffset);
  return true;
}

// ─────────────────────────────────────────────
//  connectWiFi — WiFiManager captive portal
//  with optional location override fields.
//
//  Portal fields (all optional — leave blank to
//  use auto-detected values):
//    • City name   (display only)
//    • Latitude    (decimal, e.g. 31.2222)
//    • Longitude   (decimal, e.g. 121.4581)
//    • Timezone    (e.g. Asia/Shanghai)
//    • UTC offset  (seconds, e.g. 28800)
// ─────────────────────────────────────────────
void connectWiFi() {
  WiFiManager wm;

  // Show setup screen on OLED as soon as portal opens
  wm.setAPCallback([](WiFiManager* wm) {
    Serial.println("\n[WiFi] No credentials — starting captive portal.");
    Serial.println("[WiFi] Hotspot: WeatherClock-Setup");
    Serial.println("[WiFi] Open browser → 192.168.4.1");
    drawWiFiSetupScreen();
  });

  wm.setTitle("Weather Clock — WiFi Setup");
  wm.setConfigPortalTimeout(300);

  Serial.println("\n[WiFi] Starting WiFiManager...");
  bool connected = wm.autoConnect("WeatherClock-Setup");

  if (!connected) {
    Serial.println("[WiFi] Portal timed out — rebooting.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 24);
    display.println("WiFi timeout.");
    display.setCursor(10, 36);
    display.println("Rebooting...");
    display.display();
    delay(3000);
    ESP.restart();
  }

  Serial.println("[WiFi] Connected!");
  Serial.print("[WiFi] IP:   "); Serial.println(WiFi.localIP());
  Serial.print("[WiFi] SSID: "); Serial.println(WiFi.SSID());
  Serial.print("[WiFi] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
}

// ─────────────────────────────────────────────
//  HTTP body reader
// ─────────────────────────────────────────────
String readHttpBody(WiFiClient& c) {
  unsigned long t = millis();
  while (!c.available()) {
    if (millis() - t > 15000) {
      Serial.println("[HTTP] Timeout."); c.stop(); return "";
    }
    delay(10);
  }
  while (c.connected() || c.available()) {
    String line = c.readStringUntil('\n');
    if (line == "\r" || line == "") break;
  }
  String body = "";
  t = millis();
  while ((c.connected() || c.available()) && millis() - t < 15000) {
    if (c.available()) { body += (char)c.read(); t = millis(); }
  }
  c.stop();
  Serial.print("[HTTP] Body: "); Serial.print(body.length()); Serial.println(" bytes");
  return body;
}

// ─────────────────────────────────────────────
//  NTP time sync
// ─────────────────────────────────────────────
void fetchCurrentTime() {
  Serial.println("\n[NTP] Syncing via configTime ...");
  configTime(g_utcOffset, 0, NTP_SERVER);

  struct tm t;
  unsigned long start = millis();
  while (!getLocalTime(&t, 1000)) {
    if (millis() - start > 10000) {
      Serial.println("[NTP] Timeout."); return;
    }
    delay(200);
  }

  g_hour   = t.tm_hour;
  g_minute = t.tm_min;
  g_second = t.tm_sec;
  g_day    = t.tm_mday;
  g_month  = t.tm_mon  + 1;
  g_year   = t.tm_year + 1900;

  Serial.print("[NTP] Synced: ");
  Serial.print(g_year);  Serial.print("-");
  if (g_month  < 10) Serial.print("0"); Serial.print(g_month);  Serial.print("-");
  if (g_day    < 10) Serial.print("0"); Serial.print(g_day);    Serial.print(" ");
  if (g_hour   < 10) Serial.print("0"); Serial.print(g_hour);   Serial.print(":");
  if (g_minute < 10) Serial.print("0"); Serial.print(g_minute); Serial.print(":");
  if (g_second < 10) Serial.print("0"); Serial.println(g_second);
}

// ─────────────────────────────────────────────
//  Update local clock from ESP32 RTC
// ─────────────────────────────────────────────
void updateLocalClock() {
  struct tm t;
  if (getLocalTime(&t, 0)) {
    g_hour   = t.tm_hour;
    g_minute = t.tm_min;
    g_second = t.tm_sec;
    g_day    = t.tm_mday;
    g_month  = t.tm_mon  + 1;
    g_year   = t.tm_year + 1900;
  }
}

// ─────────────────────────────────────────────
//  WMO code → weather description
// ─────────────────────────────────────────────
String wmoDescription(int code, float tempMax) {
  if (tempMax > 3.0f) {
    switch (code) {
      case 71: code = 61; break;
      case 73: code = 63; break;
      case 75: code = 65; break;
      case 77: code = 61; break;
      case 85: code = 80; break;
      case 86: code = 81; break;
    }
  }
  switch (code) {
    case 0:  return "Clear sky";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: return "Foggy";
    case 48: return "Icy fog";
    case 51: return "Light drizzle";
    case 53: return "Moderate drizzle";
    case 55: return "Dense drizzle";
    case 61: return "Slight rain";
    case 63: return "Moderate rain";
    case 65: return "Heavy rain";
    case 71: return "Slight snow";
    case 73: return "Moderate snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Slight showers";
    case 81: return "Moderate showers";
    case 82: return "Violent showers";
    case 85: return "Slight snow shwrs";
    case 86: return "Heavy snow shwrs";
    case 95: return "Thunderstorm";
    case 96: return "Thunderstorm+hail";
    case 99: return "Thunderstorm+hail";
    default: return "WMO " + String(code);
  }
}

// ─────────────────────────────────────────────
//  OLED — analog clock frame
// ─────────────────────────────────────────────
void drawClockFrame() {
  display.clearDisplay();

  if (g_hour < 0 || g_year < 2020) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 24);
    display.println("Syncing time...");
    display.display();
    return;
  }

  const int cx = 31, cy = 31, R = 30;

  display.drawCircle(cx, cy, R, SSD1306_WHITE);

  for (int i = 0; i < 360; i += 30) {
    float rad = i / 57.2958f;
    display.drawLine(cx + (int)(R     * sin(rad)), cy - (int)(R     * cos(rad)),
                     cx + (int)((R-4) * sin(rad)), cy - (int)((R-4) * cos(rad)),
                     SSD1306_WHITE);
  }

  { float rad = g_second * 6 / 57.2958f;
    display.drawLine(cx, cy,
                     cx + (int)((R-2) * sin(rad)),
                     cy - (int)((R-2) * cos(rad)), SSD1306_WHITE); }

  { float rad = g_minute * 6 / 57.2958f;
    display.drawTriangle(
      cx + (int)(2  * cos(rad)), cy + (int)(2  * sin(rad)),
      cx - (int)(2  * cos(rad)), cy - (int)(2  * sin(rad)),
      cx + (int)(24 * sin(rad)), cy - (int)(24 * cos(rad)),
      SSD1306_WHITE); }

  { float rad = (g_hour % 12 * 30 + g_minute / 2) / 57.2958f;
    display.fillTriangle(
      cx + (int)(2  * cos(rad)), cy + (int)(2  * sin(rad)),
      cx - (int)(2  * cos(rad)), cy - (int)(2  * sin(rad)),
      cx + (int)(17 * sin(rad)), cy - (int)(17 * cos(rad)),
      SSD1306_WHITE); }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  char buf[16];

  sprintf(buf, "%02d:%02d:%02d", g_hour, g_minute, g_second);
  display.setCursor(66, 8);
  display.print(buf);

  sprintf(buf, "%02d/%02d/%02d", g_month, g_day, g_year % 100);
  display.setCursor(66, 28);
  display.print(buf);

  {
    const int bx  = 67, by  = 48;
    const int cx2 = bx + 5, cy2 = by + 8;
    if (g_alarmEnabled) {
      // Draw alarm clock icon
      display.fillCircle(cx2 - 3, by + 2, 2, SSD1306_WHITE);
      display.fillCircle(cx2 + 3, by + 2, 2, SSD1306_WHITE);
      display.fillCircle(cx2, cy2, 5, SSD1306_WHITE);
      display.fillCircle(cx2, cy2, 3, SSD1306_BLACK);
      display.drawPixel(cx2,     cy2 - 2, SSD1306_WHITE);
      display.drawPixel(cx2,     cy2 - 1, SSD1306_WHITE);
      display.drawPixel(cx2 + 1, cy2,     SSD1306_WHITE);
      display.drawPixel(cx2 + 2, cy2,     SSD1306_WHITE);
      sprintf(buf, "%02d:%02d", ALARM_HOUR, ALARM_MINUTE);
      display.setCursor(83, 51);
      display.print(buf);
    } else {
      // Draw a crossed-out bell icon (X through the bell body)
      display.fillCircle(cx2 - 3, by + 2, 2, SSD1306_WHITE);
      display.fillCircle(cx2 + 3, by + 2, 2, SSD1306_WHITE);
      display.fillCircle(cx2, cy2, 5, SSD1306_WHITE);
      display.fillCircle(cx2, cy2, 3, SSD1306_BLACK);
      // X lines through the bell
      display.drawLine(cx2 - 3, cy2 - 3, cx2 + 3, cy2 + 3, SSD1306_WHITE);
      display.drawLine(cx2 + 3, cy2 - 3, cx2 - 3, cy2 + 3, SSD1306_WHITE);
      // "No Alarm" text
      display.setCursor(80, 51);
      display.print("NoAlrm");
    }
  }

  display.display();
}

// ─────────────────────────────────────────────
//  OLED — cuckoo announcement screen
//  Shows while the chime is playing.
//  Example:  "* Cuckoo Clock *"
//            "  It is now"
//            "    8:00 PM"
//            "  o o o o o o o o"  (dots = chime count)
// ─────────────────────────────────────────────
void drawCuckooScreen(int hour24, int chimeCount) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  display.setCursor(14, 0);
  display.print("* Cuckoo Clock *");

  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  // "It is now"
  display.setCursor(28, 15);
  display.print("It is now");

  // Format hour as 12-hour + AM/PM
  int  h12  = hour24 % 12;
  if (h12 == 0) h12 = 12;
  bool isPM = (hour24 >= 12);
  char buf[12];
  sprintf(buf, "%d:00 %s", h12, isPM ? "PM" : "AM");

  // Centre the time string (each char ~6 px wide at size 1, use size 2 here)
  display.setTextSize(2);
  int strW = strlen(buf) * 12;  // size-2 char ≈ 12 px wide
  display.setCursor((SCREEN_WIDTH - strW) / 2, 26);
  display.print(buf);

  // Dots row — one dot per chime, centred
  display.setTextSize(1);
  int dotSpacing = 8;
  int totalW     = chimeCount * dotSpacing - 2;
  int dotX       = (SCREEN_WIDTH - totalW) / 2;
  for (int i = 0; i < chimeCount; i++) {
    display.fillCircle(dotX + i * dotSpacing, 56, 2, SSD1306_WHITE);
  }

  display.display();
}

// ─────────────────────────────────────────────
//  Pin ownership helpers
//  GPIO25 is shared between LEDC (PWM) and DAC.
//  Always call the matching release before switching modes.
//  releaseForDAC()  — detaches LEDC so DAC can drive the pin
//  releaseForPWM()  — disables DAC output so LEDC can drive the pin
// ─────────────────────────────────────────────
void releaseForDAC() {
  // Make sure LEDC is not holding the pin
  ledcWriteTone(buzzer, 0);
  ledcDetach(buzzer);
  delay(5);
}

void releaseForPWM() {
  // Disable DAC output so LEDC can take over
  dac_output_disable(DAC_CH);
  delay(5);
}

// ─────────────────────────────────────────────
//  DAC helper — write 8-bit sample to GPIO25
// ─────────────────────────────────────────────
inline void dacWrite8(uint8_t val) {
  dac_output_voltage(DAC_CH, val);
}

// ─────────────────────────────────────────────
//  DAC sine tone — plays a pure sine for durationMs.
//  Enables DAC before playing, leaves DAC enabled
//  (caller must call releaseForPWM() when done with DAC).
// ─────────────────────────────────────────────
void dacSineTone(float freq, uint32_t durationMs, uint8_t amplitude = 100) {
  dac_output_enable(DAC_CH);   // ensure DAC is active
  const float    twoPi  = 2.0f * M_PI;
  const uint32_t stepUs = 100;   // ~10 000 samples/sec
  float    phase  = 0.0f;
  uint32_t lastUs = micros();
  uint32_t start  = millis();

  while (millis() - start < durationMs) {
    uint32_t now = micros();
    if (now - lastUs >= stepUs) {
      float dt = (float)(now - lastUs) / 1000000.0f;
      phase += twoPi * freq * dt;
      if (phase > twoPi) phase -= twoPi;
      dacWrite8((uint8_t)(128 + amplitude * sinf(phase)));
      lastUs = now;
    }
  }
  dacWrite8(128);  // return to mid-rail (silence)
}

// ─────────────────────────────────────────────
//  Single cuckoo call — "koo koo"
//  Two notes of equal length, slight pitch drop.
//  D5 (587 Hz) → B4 (494 Hz), with a short gap
//  between the two syllables.
//  The 1 second gap between repeats is handled
//  by the caller (playCuckooChime).
// ─────────────────────────────────────────────
void singleCuckoo() {
  dacSineTone(587.0f, 250, 95);   // "koo" — D5
  dacWrite8(128); delay(120);     // gap between syllables
  dacSineTone(494.0f, 250, 95);   // "koo" — B4
  dacWrite8(128);
}

// ─────────────────────────────────────────────
//  Full cuckoo chime sequence
//  1. Short two-note fanfare intro (like a clock winding up)
//  2. Chime N times where N = 12-hour count
//  3. OLED shows announcement the whole time
//  4. Returns to previous display mode
// ─────────────────────────────────────────────
void playCuckooChime(int hour24) {
  int chimeCount = hour24 % 12;
  if (chimeCount == 0) chimeCount = 12;

  Serial.print("\n[CUCKOO] Chiming ");
  Serial.print(chimeCount);
  Serial.print(" time(s) for ");
  Serial.print(hour24 % 12 == 0 ? 12 : hour24 % 12);
  Serial.println(hour24 >= 12 ? " PM" : " AM");

  // Show announcement on OLED
  drawCuckooScreen(hour24, chimeCount);

  // ── Take ownership of GPIO25 for DAC ──────
  releaseForDAC();
  dac_output_enable(DAC_CH);
  dacWrite8(128);   // mid-rail = silence
  delay(100);

  // Chime N times — 1 second gap between each "koo koo"
  for (int i = 0; i < chimeCount; i++) {
    singleCuckoo();
    delay(1000);
  }
  dacWrite8(128);

  // ── Release GPIO25 back for PWM (alarm) ───
  releaseForPWM();

  Serial.println("[CUCKOO] Chime complete.");

  delay(800);
  g_displayMode   = MODE_CLOCK;
  g_lastSecondVal = 99999UL;
  drawClockFrame();
}

// ─────────────────────────────────────────────
//  Check and trigger cuckoo chime
//  Called every loop iteration.
//  Fires at minute==0 and second<5, once per hour.
// ─────────────────────────────────────────────
void checkCuckooChime() {
  if (g_hour < CUCKOO_START || g_hour > CUCKOO_END) return;
  if (g_minute != 0) return;
  if (g_second >= 5) return;
  if (g_lastChimeHour == g_hour) return;

  // If alarm is enabled and set for this exact hour,
  // skip cuckoo — alarm melody plays instead.
  // If alarm is disabled, cuckoo plays normally.
  if (g_alarmEnabled && g_hour == ALARM_HOUR && g_minute == ALARM_MINUTE) {
    Serial.println("[CUCKOO] Skipped — alarm melody takes priority this hour.");
    g_lastChimeHour = g_hour;
    return;
  }

  g_lastChimeHour = g_hour;
  playCuckooChime(g_hour);
}

// ─────────────────────────────────────────────
//  OLED — compact weather card
// ─────────────────────────────────────────────
void drawWeatherCard(const char* title, String date,
                     String condition, float tempMin, float tempMax) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 10, SCREEN_WIDTH-1, 10, SSD1306_WHITE);

  display.setCursor(0, 13);
  display.print("Date: "); display.println(date);

  display.setCursor(0, 25);
  display.print("Cond: ");
  if (condition.length() > 15) display.println(condition.substring(0, 15));
  else                          display.println(condition);

  display.drawLine(0, 36, SCREEN_WIDTH-1, 36, SSD1306_WHITE);

  display.setCursor(0, 40);
  display.print("Min: "); display.print(tempMin, 1);
  display.print((char)247); display.println("C");

  display.setCursor(0, 52);
  display.print("Max: "); display.print(tempMax, 1);
  display.print((char)247); display.println("C");

  display.display();
}

// ─────────────────────────────────────────────
//  OLED — detail card (double-press)
//  Shows: location, wind speed+direction,
//         UV index, sunrise, sunset.
//  128×64 px, text size 1 (6×8 px per char).
// ─────────────────────────────────────────────
void drawDetailCard(const char* title,
                    float windSpd, int windDir, float uv,
                    String sunrise, String sunset) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title row
  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  // Location
  display.setCursor(0, 13);
  display.print("Loc: ");
  display.print(g_city.c_str()); display.print(", "); display.print(g_country.c_str());

  // Wind speed + direction
  char buf[24];
  display.setCursor(0, 24);
  snprintf(buf, sizeof(buf), "Wind:%4.1f km/h %s",
           windSpd, windDirName(windDir).c_str());
  display.print(buf);

  // UV index
  display.setCursor(0, 35);
  snprintf(buf, sizeof(buf), "UV Index: %.1f", uv);
  display.print(buf);

  display.drawLine(0, 45, SCREEN_WIDTH - 1, 45, SSD1306_WHITE);

  // Sunrise / Sunset
  display.setCursor(0, 49);
  display.print("\x18 "); display.print(sunrise);   // ↑ sunrise arrow
  display.print("   ");
  display.print("\x19 "); display.print(sunset);    // ↓ sunset arrow

  display.display();
}
void drawFetchingSplash(const char* label) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 20);
  display.print("Fetching ");
  display.print(label);
  display.setCursor(28, 34);
  display.print("Please Wait");
  display.display();
}

// ─────────────────────────────────────────────
//  Play one alarm note — interruptible by button.
//  Assumes GPIO25 is already released for PWM
//  (releaseForPWM called before playAlarm).
// ─────────────────────────────────────────────
bool playNote(int pitch, int duration) {
  esp32Tone(buzzer, pitch);
  int elapsed = 0;
  while (elapsed < duration) {
    delay(20);
    elapsed += 20;
    if (digitalRead(BTN_BLUE)   == HIGH ||
        digitalRead(BTN_RED)    == HIGH ||
        digitalRead(BTN_YELLOW) == HIGH) {
      esp32NoTone(buzzer);
      while (digitalRead(BTN_BLUE)   == HIGH ||
             digitalRead(BTN_RED)    == HIGH ||
             digitalRead(BTN_YELLOW) == HIGH);
      delay(50);
      return true;
    }
  }
  esp32NoTone(buzzer);
  return false;
}

// ─────────────────────────────────────────────
//  Alarm — loops melody until button or 5 min
// ─────────────────────────────────────────────
void playAlarm() {
  Serial.println("[ALARM] Started. Press any button to stop.");

  // ── Take GPIO25 for PWM ───────────────────
  releaseForDAC();   // ensure DAC is detached first
  releaseForPWM();   // disable DAC so LEDC can drive pin

  const unsigned long TIMEOUT = 300000UL;
  unsigned long start = millis();

  while (millis() - start < TIMEOUT) {
    for (int i = 0; i < notes * 2; i += 2) {
      divider = melody[i + 1];
      if      (divider > 0) noteDuration = wholenote / divider;
      else if (divider < 0) { noteDuration = wholenote / abs(divider); noteDuration *= 1.5; }

      if (millis() - start >= TIMEOUT) {
        esp32NoTone(buzzer);
        Serial.println("[ALARM] Timed out.");
        releaseForPWM();   // leave pin clean for next DAC use
        return;
      }
      if (playNote(melody[i], noteDuration)) {
        Serial.println("[ALARM] Stopped by button.");
        releaseForPWM();
        return;
      }
    }
    delay(500);
  }
  esp32NoTone(buzzer);
  releaseForPWM();   // leave pin clean for next DAC use
  Serial.println("[ALARM] Timed out.");
}

// ─────────────────────────────────────────────
//  Wind degrees → compass direction string
// ─────────────────────────────────────────────
String windDirName(int deg) {
  // 8-point compass, each sector = 45°, offset by 22.5°
  const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
  return dirs[((deg + 22) / 45) % 8];
}

// ─────────────────────────────────────────────
//  Parse ISO 8601 time string "2025-04-14T06:12"
//  and return just "HH:MM" portion.
// ─────────────────────────────────────────────
String isoToHHMM(String iso) {
  // Format: "YYYY-MM-DDTHH:MM"  (Open-Meteo daily sunrise/sunset)
  int tIdx = iso.indexOf('T');
  if (tIdx < 0) return iso;            // fallback: return as-is
  return iso.substring(tIdx + 1, tIdx + 6);  // "HH:MM"
}

// ─────────────────────────────────────────────
//  Parse weather globals from Open-Meteo JSON body
// ─────────────────────────────────────────────
bool parseOpenMeteo(String& body) {
  JsonDocument filter;
  for (int i = 0; i < 2; i++) {
    filter["daily"]["time"][i]                          = true;
    filter["daily"]["weather_code"][i]                  = true;
    filter["daily"]["temperature_2m_max"][i]            = true;
    filter["daily"]["temperature_2m_min"][i]            = true;
    filter["daily"]["wind_speed_10m_max"][i]            = true;
    filter["daily"]["wind_direction_10m_dominant"][i]   = true;
    filter["daily"]["uv_index_max"][i]                  = true;
    filter["daily"]["sunrise"][i]                       = true;
    filter["daily"]["sunset"][i]                        = true;
  }
  JsonDocument doc;
  deserializeJson(doc, body, DeserializationOption::Filter(filter));
  JsonObject daily = doc["daily"];

  g_todayDate    = daily["time"][0].as<String>();
  g_todayWMO     = daily["weather_code"][0].as<int>();
  g_todayTMax    = daily["temperature_2m_max"][0].as<float>();
  g_todayTMin    = daily["temperature_2m_min"][0].as<float>();
  g_todayWindSpd = daily["wind_speed_10m_max"][0].as<float>();
  g_todayWindDir = daily["wind_direction_10m_dominant"][0].as<int>();
  g_todayUV      = daily["uv_index_max"][0].as<float>();
  g_todaySunrise = isoToHHMM(daily["sunrise"][0].as<String>());
  g_todaySunset  = isoToHHMM(daily["sunset"][0].as<String>());

  g_tmrDate      = daily["time"][1].as<String>();
  g_tmrWMO       = daily["weather_code"][1].as<int>();
  g_tmrTMax      = daily["temperature_2m_max"][1].as<float>();
  g_tmrTMin      = daily["temperature_2m_min"][1].as<float>();
  g_tmrWindSpd   = daily["wind_speed_10m_max"][1].as<float>();
  g_tmrWindDir   = daily["wind_direction_10m_dominant"][1].as<int>();
  g_tmrUV        = daily["uv_index_max"][1].as<float>();
  g_tmrSunrise   = isoToHHMM(daily["sunrise"][1].as<String>());
  g_tmrSunset    = isoToHHMM(daily["sunset"][1].as<String>());

  return (g_todayDate.length() > 0 && g_tmrDate.length() > 0);
}

// ─────────────────────────────────────────────
//  Backup weather: wttr.in JSON API
//  Returns temp min/max, condition description,
//  wind speed/dir, sunrise/sunset for today+tmr.
//  UV index not available — set to 0.
//  WMO code mapped from wttr condition code.
// ─────────────────────────────────────────────

// Map wttr.in weatherCode to nearest WMO code
int wttrToWMO(int wc) {
  if (wc == 113) return 0;   // Sunny/Clear
  if (wc == 116) return 2;   // Partly cloudy
  if (wc == 119) return 3;   // Cloudy/Overcast
  if (wc == 122) return 3;   // Overcast
  if (wc == 143) return 45;  // Mist/Fog
  if (wc == 176) return 80;  // Patchy rain
  if (wc == 185) return 56;  // Patchy freezing drizzle
  if (wc == 200) return 95;  // Thundery outbreaks
  if (wc == 227) return 71;  // Blowing snow
  if (wc == 230) return 75;  // Blizzard
  if (wc == 248) return 45;  // Fog
  if (wc == 260) return 48;  // Freezing fog
  if (wc == 263) return 51;  // Light drizzle
  if (wc == 266) return 51;  // Light drizzle
  if (wc == 281) return 56;  // Freezing drizzle
  if (wc == 284) return 57;  // Heavy freezing drizzle
  if (wc == 293) return 61;  // Light rain
  if (wc == 296) return 61;  // Light rain
  if (wc == 299) return 63;  // Moderate rain
  if (wc == 302) return 63;  // Moderate rain
  if (wc == 305) return 65;  // Heavy rain
  if (wc == 308) return 65;  // Very heavy rain
  if (wc == 311) return 56;  // Light freezing rain
  if (wc == 314) return 57;  // Moderate/heavy freezing rain
  if (wc == 317) return 68;  // Light sleet
  if (wc == 320) return 69;  // Moderate/heavy sleet
  if (wc == 323) return 71;  // Light snow
  if (wc == 326) return 71;  // Light snow
  if (wc == 329) return 73;  // Moderate snow
  if (wc == 332) return 73;  // Moderate snow
  if (wc == 335) return 75;  // Heavy snow
  if (wc == 338) return 75;  // Heavy snow
  if (wc == 350) return 77;  // Ice pellets
  if (wc == 353) return 80;  // Light showers
  if (wc == 356) return 81;  // Moderate showers
  if (wc == 359) return 82;  // Heavy showers
  if (wc == 362) return 85;  // Light sleet showers
  if (wc == 365) return 86;  // Moderate/heavy sleet showers
  if (wc == 368) return 85;  // Light snow showers
  if (wc == 371) return 86;  // Moderate/heavy snow showers
  if (wc == 374) return 85;  // Light ice pellet showers
  if (wc == 377) return 86;  // Moderate/heavy ice pellet showers
  if (wc == 386) return 95;  // Thundery with light rain
  if (wc == 389) return 96;  // Thundery with heavy rain
  if (wc == 392) return 95;  // Thundery with light snow
  if (wc == 395) return 96;  // Thundery with heavy snow
  return 2;                   // default: partly cloudy
}

// Wind direction degrees from 16-point compass string
int wttrWindDir(const char* dir) {
  const char* dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                        "S","SSW","SW","WSW","W","WNW","NW","NNW"};
  for (int i = 0; i < 16; i++)
    if (strcmp(dir, dirs[i]) == 0) return i * 22;
  return 0;
}

bool fetchWeatherFromWttr() {
  Serial.println("[WEATHER] Trying backup: wttr.in ...");

  // wttr.in ?format=j1 returns JSON with today + 2 days forecast
  char path[80];
  sprintf(path, "/%s?format=j1", g_city.length() > 0 && g_city != "Unknown"
          ? g_city.c_str() : (String(g_lat, 2) + "," + String(g_lon, 2)).c_str());

  String body = httpGet("wttr.in", 80, path);
  if (body.length() == 0) {
    Serial.println("[WEATHER] wttr.in empty response."); return false;
  }
  Serial.print("[WEATHER] wttr.in body length: "); Serial.println(body.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[WEATHER] wttr.in parse error: "); Serial.println(err.c_str());
    return false;
  }

  // wttr returns "weather" array: [0]=today, [1]=tomorrow
  JsonArray weather = doc["weather"].as<JsonArray>();
  if (weather.size() < 2) {
    Serial.println("[WEATHER] wttr.in insufficient data."); return false;
  }

  auto parseDay = [&](JsonObject day, int idx) {
    float tMaxC = day["maxtempC"].as<float>();
    float tMinC = day["mintempC"].as<float>();
    int   wCode = day["hourly"][4]["weatherCode"].as<int>();  // noon hour
    float wSpd  = day["hourly"][4]["windspeedKmph"].as<float>();
    const char* wDir = day["hourly"][4]["winddir16Point"].as<const char*>();
    String sunr = day["astronomy"][0]["sunrise"].as<String>();
    String suns = day["astronomy"][0]["sunset"].as<String>();
    // Build date string YYYY-MM-DD from wttr "date" field
    String date = day["date"].as<String>();  // already YYYY-MM-DD

    if (idx == 0) {
      g_todayDate    = date;
      g_todayTMax    = tMaxC;
      g_todayTMin    = tMinC;
      g_todayWMO     = wttrToWMO(wCode);
      g_todayWindSpd = wSpd;
      g_todayWindDir = wttrWindDir(wDir);
      g_todayUV      = 0;
      g_todaySunrise = sunr;
      g_todaySunset  = suns;
    } else {
      g_tmrDate      = date;
      g_tmrTMax      = tMaxC;
      g_tmrTMin      = tMinC;
      g_tmrWMO       = wttrToWMO(wCode);
      g_tmrWindSpd   = wSpd;
      g_tmrWindDir   = wttrWindDir(wDir);
      g_tmrUV        = 0;
      g_tmrSunrise   = sunr;
      g_tmrSunset    = suns;
    }
  };

  parseDay(weather[0].as<JsonObject>(), 0);
  parseDay(weather[1].as<JsonObject>(), 1);

  if (g_todayDate.length() == 0 || g_tmrDate.length() == 0) {
    Serial.println("[WEATHER] wttr.in parse incomplete."); return false;
  }

  g_weatherFetchedAt = millis();
  Serial.println("[WEATHER] wttr.in success.");
  Serial.print("[WEATHER] Today: "); Serial.print(g_todayDate);
  Serial.print(" max="); Serial.print(g_todayTMax,1);
  Serial.print(" min="); Serial.println(g_todayTMin,1);
  Serial.print("[WEATHER] Tomorrow: "); Serial.print(g_tmrDate);
  Serial.print(" max="); Serial.print(g_tmrTMax,1);
  Serial.print(" min="); Serial.println(g_tmrTMin,1);
  return true;
}

// ─────────────────────────────────────────────
//  Fetch today + tomorrow weather
//  Primary:  Open-Meteo  (api.open-meteo.com)
//  Backup:   wttr.in
// ─────────────────────────────────────────────
bool fetchWeatherCache() {
  // ── Primary: Open-Meteo ───────────────────
  Serial.println("\n[WEATHER] Fetching from Open-Meteo...");
  String path = "/v1/forecast?latitude=" + String(g_lat, 4) +
                "&longitude=" + String(g_lon, 4) +
                "&daily=weather_code"
                ",temperature_2m_max,temperature_2m_min"
                ",wind_speed_10m_max,wind_direction_10m_dominant"
                ",uv_index_max"
                ",sunrise,sunset" +
                "&forecast_days=2" +
                "&timezone=" + g_tzEncoded;

  if (client.connect(WEATHER_HOST, 80)) {
    client.setTimeout(15000);
    client.print(String("GET ") + path + " HTTP/1.0\r\n" +
                 "Host: " + WEATHER_HOST + "\r\nConnection: close\r\n\r\n");
    String body = readHttpBody(client);
    if (body.length() > 0 && parseOpenMeteo(body)) {
      g_weatherFetchedAt = millis();
      Serial.print("[WEATHER] Open-Meteo OK. Today:"); Serial.print(g_todayDate);
      Serial.print(" max="); Serial.print(g_todayTMax,1);
      Serial.print(" min="); Serial.print(g_todayTMin,1);
      Serial.print(" | Tomorrow:"); Serial.print(g_tmrDate);
      Serial.print(" max="); Serial.print(g_tmrTMax,1);
      Serial.print(" min="); Serial.println(g_tmrTMin,1);
      return true;
    }
    Serial.println("[WEATHER] Open-Meteo failed — trying backup...");
  } else {
    Serial.println("[WEATHER] Open-Meteo connection failed — trying backup...");
  }

  // ── Backup: wttr.in ───────────────────────
  return fetchWeatherFromWttr();
}

// ─────────────────────────────────────────────
//  Ensure weather cache is fresh (max 6 hours)
// ─────────────────────────────────────────────
bool ensureWeatherCache() {
  bool empty = (g_weatherFetchedAt == 0 || g_todayDate.length() == 0);
  bool stale = (millis() - g_weatherFetchedAt >= WEATHER_CACHE_MS);
  if (empty || stale) {
    drawFetchingSplash("Weather");
    return fetchWeatherCache();
  }
  Serial.println("[WEATHER] Using cached data.");
  return true;
}

// ─────────────────────────────────────────────
//  Weather fetch waiting screen — shared helper
//  Shows attempt count, Blue=back to clock,
//  Yellow hold 3s = force re-fetch.
//  Returns true if data ready, false if Blue pressed.
// ─────────────────────────────────────────────
bool waitForWeather() {
  int attempts = 0;
  while (true) {
    if (ensureWeatherCache()) return true;   // got data

    attempts++;
    Serial.print("[WEATHER] Attempt "); Serial.print(attempts);
    Serial.println(" failed. Retrying...");

    // Show waiting screen with attempt count
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(14, 8);  display.print("Fetching weather");
    display.drawLine(0, 18, SCREEN_WIDTH - 1, 18, SSD1306_WHITE);
    display.setCursor(10, 24); display.print("Attempt: "); display.print(attempts);
    display.setCursor(0,  36); display.print("BLU = back to clock");
    display.setCursor(0,  48); display.print("YLW hold 3s = retry");
    display.display();

    // Wait up to 5s — check Blue (escape) and Yellow (force retry)
    unsigned long waitStart = millis();
    while (millis() - waitStart < 5000) {
      // Blue = abort, go back to clock
      if (digitalRead(BTN_BLUE) == HIGH) {
        delay(50);
        if (digitalRead(BTN_BLUE) == HIGH) {
          while (digitalRead(BTN_BLUE) == HIGH);
          Serial.println("[WEATHER] User pressed Blue — returning to clock.");
          g_displayMode   = MODE_CLOCK;
          g_lastSecondVal = 99999UL;
          drawClockFrame();
          return false;
        }
      }
      // Yellow held 3s = force immediate re-fetch
      if (digitalRead(BTN_YELLOW) == HIGH) {
        unsigned long yStart = millis();
        while (digitalRead(BTN_YELLOW) == HIGH) {
          if (millis() - yStart >= 3000) {
            while (digitalRead(BTN_YELLOW) == HIGH);
            Serial.println("[WEATHER] Force re-fetch triggered.");
            g_weatherFetchedAt = 0;   // mark cache as empty
            break;
          }
        }
        break;   // exit wait loop, retry fetch immediately
      }
      delay(50);
    }
  }
}

// ─────────────────────────────────────────────
//  Show today's weather (Red button)
// ─────────────────────────────────────────────
void showTodayWeather() {
  Serial.println("\n[BTN RED] Today's weather requested.");
  if (!waitForWeather()) return;
  String cond = wmoDescription(g_todayWMO, g_todayTMax);
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║          TODAY'S WEATHER FORECAST            ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.print  ("║  Date      : "); Serial.println(g_todayDate);
  Serial.print  ("║  Location  : "); Serial.print(g_city.c_str()); Serial.print(", "); Serial.println(g_country.c_str());
  Serial.print  ("║  Condition : "); Serial.println(cond);
  Serial.print  ("║  Temp Max  : "); Serial.print(g_todayTMax, 1); Serial.println(" °C");
  Serial.print  ("║  Temp Min  : "); Serial.print(g_todayTMin, 1); Serial.println(" °C");
  Serial.println("╚══════════════════════════════════════════════╝");
  drawWeatherCard("   Today's Weather  ", g_todayDate, cond, g_todayTMin, g_todayTMax);
  g_displayMode = MODE_TODAY;
}

// ─────────────────────────────────────────────
//  Show tomorrow's weather (Yellow button)
// ─────────────────────────────────────────────
void showTomorrowWeather() {
  Serial.println("\n[BTN YLW] Tomorrow's weather requested.");
  if (!waitForWeather()) return;
  String cond = wmoDescription(g_tmrWMO, g_tmrTMax);
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║        TOMORROW'S WEATHER FORECAST           ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.print  ("║  Date      : "); Serial.println(g_tmrDate);
  Serial.print  ("║  Location  : "); Serial.print(g_city.c_str()); Serial.print(", "); Serial.println(g_country.c_str());
  Serial.print  ("║  Condition : "); Serial.println(cond);
  Serial.print  ("║  Temp Max  : "); Serial.print(g_tmrTMax, 1); Serial.println(" °C");
  Serial.print  ("║  Temp Min  : "); Serial.print(g_tmrTMin, 1); Serial.println(" °C");
  Serial.println("╚══════════════════════════════════════════════╝");
  drawWeatherCard(" Tomorrow's Weather ", g_tmrDate, cond, g_tmrTMin, g_tmrTMax);
  g_displayMode = MODE_TOMORROW;
}

// ─────────────────────────────────────────────
//  Show today detail (Red double-press)
// ─────────────────────────────────────────────
void showTodayDetail() {
  Serial.println("\n[BTN RED x2] Today detail requested.");
  if (!waitForWeather()) return;
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║           TODAY DETAIL                       ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.print  ("║  Location  : "); Serial.print(g_city.c_str()); Serial.print(", "); Serial.println(g_country.c_str());
  Serial.print  ("║  Wind      : "); Serial.print(g_todayWindSpd, 1); Serial.print(" km/h "); Serial.println(windDirName(g_todayWindDir));
  Serial.print  ("║  UV Index  : "); Serial.println(g_todayUV, 1);
  Serial.print  ("║  Sunrise   : "); Serial.println(g_todaySunrise);
  Serial.print  ("║  Sunset    : "); Serial.println(g_todaySunset);
  Serial.println("╚══════════════════════════════════════════════╝");
  drawDetailCard(" Today Detail", g_todayWindSpd, g_todayWindDir,
                 g_todayUV, g_todaySunrise, g_todaySunset);
  g_displayMode = MODE_TODAY;
}

// ─────────────────────────────────────────────
//  Show tomorrow detail (Yellow double-press)
// ─────────────────────────────────────────────
void showTomorrowDetail() {
  Serial.println("\n[BTN YLW x2] Tomorrow detail requested.");
  if (!waitForWeather()) return;
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║           TOMORROW DETAIL                    ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.print  ("║  Location  : "); Serial.print(g_city.c_str()); Serial.print(", "); Serial.println(g_country.c_str());
  Serial.print  ("║  Wind      : "); Serial.print(g_tmrWindSpd, 1); Serial.print(" km/h "); Serial.println(windDirName(g_tmrWindDir));
  Serial.print  ("║  UV Index  : "); Serial.println(g_tmrUV, 1);
  Serial.print  ("║  Sunrise   : "); Serial.println(g_tmrSunrise);
  Serial.print  ("║  Sunset    : "); Serial.println(g_tmrSunset);
  Serial.println("╚══════════════════════════════════════════════╝");
  drawDetailCard(" Tomorrow Detail", g_tmrWindSpd, g_tmrWindDir,
                 g_tmrUV, g_tmrSunrise, g_tmrSunset);
  g_displayMode = MODE_TOMORROW;
}

// ─────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────
//  OLED — alarm setup screen
//  stage 0 = selecting hour
//  stage 1 = selecting minute
// ─────────────────────────────────────────────
void drawAlarmSetupScreen(int stage, int selHour, int selMinute) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setCursor(14, 0);
  display.print("* Alarm Setup *");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  char buf[12];

  if (stage == 0) {
    // ── Hour selection ──────────────────────
    display.setCursor(38, 14);
    display.print("Set Hour");

    // Up/down arrows
    display.setCursor(8, 38);  display.print("\x18");   // up arrow
    display.setCursor(114, 38); display.print("\x19");  // down arrow

    // Large highlighted hour value
    display.setTextSize(2);
    sprintf(buf, "%02d", selHour);
    display.setCursor(46, 30);
    display.print(buf);
    display.setTextSize(1);

    display.setCursor(10, 57);
    display.print("YLW=up BLU=dn RED=ok");

  } else {
    // ── Minute selection ────────────────────

    // Locked hour shown dimly at top
    display.setCursor(20, 14);
    display.print("Hour locked:");
    display.setTextSize(2);
    sprintf(buf, "%02d :", selHour);
    display.setCursor(34, 22);
    display.print(buf);
    display.setTextSize(1);

    // Up/down arrows
    display.setCursor(8, 50);  display.print("\x18");
    display.setCursor(114, 50); display.print("\x19");

    // Minute value
    display.setTextSize(2);
    sprintf(buf, "%02d", selMinute);
    display.setCursor(74, 38);
    display.print(buf);
    display.setTextSize(1);

    display.setCursor(10, 57);
    display.print("YLW=up BLU=dn RED=ok");
  }

  display.display();
}

// ─────────────────────────────────────────────
//  OLED — alarm set confirmation splash
// ─────────────────────────────────────────────
void drawAlarmConfirmedScreen(int h, int m) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(14, 0);
  display.print("* Alarm Setup *");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  display.setCursor(26, 20);
  display.print("Alarm set for");

  char buf[8];
  sprintf(buf, "%02d:%02d", h, m);
  display.setTextSize(2);
  display.setCursor(32, 32);
  display.print(buf);
  display.setTextSize(1);

  display.setCursor(14, 55);
  display.print("Returning to clock");
  display.display();
}

// ─────────────────────────────────────────────
//  Alarm setup mode — full interactive flow
//  Called on blue button double-press.
//  Yellow = up, Blue = down, Red = confirm.
//  Stage 0: pick hour (00-23)
//  Stage 1: pick minute (00-59)
//  After confirmation: updates ALARM_HOUR and
//  ALARM_MINUTE and returns to clock mode.
// ─────────────────────────────────────────────
void enterAlarmSetup() {
  Serial.println("\n[ALARM SETUP] Entered.");
  g_displayMode = MODE_ALARM_SETUP;

  // Drain blue button — it was double-pressed to get here,
  // wait until fully released before reading buttons.
  while (digitalRead(BTN_BLUE) == HIGH);
  delay(100);

  int selHour   = ALARM_HOUR;
  int selMinute = ALARM_MINUTE;

  // ── Stage 0: select hour ─────────────────
  drawAlarmSetupScreen(0, selHour, selMinute);

  while (true) {
    // Yellow = hour up
    if (buttonPressed(BTN_YELLOW)) {
      selHour = (selHour + 1) % 24;
      drawAlarmSetupScreen(0, selHour, selMinute);
      Serial.print("[ALARM SETUP] Hour → "); Serial.println(selHour);
    }
    // Blue = hour down
    if (buttonPressed(BTN_BLUE)) {
      selHour = (selHour + 23) % 24;  // +23 mod 24 = subtract 1 with wrap
      drawAlarmSetupScreen(0, selHour, selMinute);
      Serial.print("[ALARM SETUP] Hour → "); Serial.println(selHour);
    }
    // Red = confirm hour, advance to minute
    if (buttonPressed(BTN_RED)) {
      Serial.print("[ALARM SETUP] Hour confirmed: "); Serial.println(selHour);
      break;
    }
  }

  // ── Stage 1: select minute ───────────────
  drawAlarmSetupScreen(1, selHour, selMinute);

  while (true) {
    // Yellow = minute up
    if (buttonPressed(BTN_YELLOW)) {
      selMinute = (selMinute + 1) % 60;
      drawAlarmSetupScreen(1, selHour, selMinute);
      Serial.print("[ALARM SETUP] Minute → "); Serial.println(selMinute);
    }
    // Blue = minute down
    if (buttonPressed(BTN_BLUE)) {
      selMinute = (selMinute + 59) % 60;  // +59 mod 60 = subtract 1 with wrap
      drawAlarmSetupScreen(1, selHour, selMinute);
      Serial.print("[ALARM SETUP] Minute → "); Serial.println(selMinute);
    }
    // Red = confirm minute, save and exit
    if (buttonPressed(BTN_RED)) {
      Serial.print("[ALARM SETUP] Minute confirmed: "); Serial.println(selMinute);
      break;
    }
  }

  // ── Save and show confirmation ────────────
  ALARM_HOUR   = selHour;
  ALARM_MINUTE = selMinute;
  g_alarmFiredToday = false;   // reset so new alarm can fire today if applicable

  Serial.print("[ALARM SETUP] New alarm saved: ");
  Serial.print(ALARM_HOUR); Serial.print(":");
  if (ALARM_MINUTE < 10) Serial.print("0");
  Serial.println(ALARM_MINUTE);

  drawAlarmConfirmedScreen(ALARM_HOUR, ALARM_MINUTE);
  delay(2000);   // show confirmation for 2 seconds

  // Return to clock
  g_displayMode   = MODE_CLOCK;
  g_lastSecondVal = 99999UL;
  drawClockFrame();
}

// ─────────────────────────────────────────────
//  OLED — location setup page
//  page 0 = latitude   (-90.00 to +90.00)
//  page 1 = longitude  (-180.00 to +180.00)
//  page 2 = UTC offset (-12 to +14)
// ─────────────────────────────────────────────
void drawLocationSetupScreen(int page, float lat, float lon, int utcHr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 0);
  display.print("* Location Setup *");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  char buf[24];

  if (page == 0) {
    display.setCursor(30, 14);
    display.print("Latitude");
    display.setCursor(8, 28);  display.print("\x18");
    display.setCursor(114, 28); display.print("\x19");
    display.setTextSize(2);
    // Show as e.g. "34.05 N" or "34.05 S"
    sprintf(buf, "%.2f %c", fabsf(lat), lat >= 0 ? 'N' : 'S');
    int strW = strlen(buf) * 12;
    display.setCursor((SCREEN_WIDTH - strW) / 2, 22);
    display.print(buf);
    display.setTextSize(1);
    display.setCursor(2, 46);
    display.print("tap=.01 hold=.10/.1+");
    display.setCursor(22, 56);
    display.print("RED = confirm");

  } else if (page == 1) {
    display.setCursor(26, 14);
    display.print("Longitude");
    display.setCursor(8, 28);  display.print("\x18");
    display.setCursor(114, 28); display.print("\x19");
    display.setTextSize(2);
    // Show as e.g. "118.24 E" or "118.24 W"
    sprintf(buf, "%.2f %c", fabsf(lon), lon >= 0 ? 'E' : 'W');
    int strW = strlen(buf) * 12;
    display.setCursor((SCREEN_WIDTH - strW) / 2, 22);
    display.print(buf);
    display.setTextSize(1);
    display.setCursor(2, 46);
    display.print("tap=.01 hold=.10/.1+");
    display.setCursor(22, 56);
    display.print("RED = confirm");

  } else {
    display.setCursor(22, 14);
    display.print("UTC Offset (hours)");
    display.setCursor(8, 28);  display.print("\x18");
    display.setCursor(114, 28); display.print("\x19");
    display.setTextSize(2);
    sprintf(buf, "UTC%+d", utcHr);
    int strW = strlen(buf) * 12;
    display.setCursor((SCREEN_WIDTH - strW) / 2, 22);
    display.print(buf);
    display.setTextSize(1);
    display.setCursor(10, 46);
    display.print("YLW=+1hr BLU=-1hr");
    display.setCursor(22, 56);
    display.print("RED = confirm");
  }

  display.display();
}

// ─────────────────────────────────────────────
//  OLED — location confirmed screen
// ─────────────────────────────────────────────
void drawLocationConfirmedScreen(float lat, float lon, int utcHr, String city) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.print("* Location Saved *");
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
  char buf[24];
  display.setCursor(0, 14); display.print("City: "); display.print(city.substring(0, 14));
  sprintf(buf, "Lat: %.2f %c", fabsf(lat), lat >= 0 ? 'N' : 'S');
  display.setCursor(0, 26); display.print(buf);
  sprintf(buf, "Lon: %.2f %c", fabsf(lon), lon >= 0 ? 'E' : 'W');
  display.setCursor(0, 38); display.print(buf);
  sprintf(buf, "UTC%+d", utcHr);
  display.setCursor(0, 50); display.print(buf);
  display.display();
}

// ─────────────────────────────────────────────
//  Location button step — 3 speeds, live display
//
//  tap  (< 150ms hold)  → ±0.01
//  short hold (150–600ms) → ±0.10
//  long hold  (> 600ms)   → ±1.00 continuously
//                           while held, screen
//                           updates every step
//
//  pageNum, pLat, pLon, pUtc — pointers to the
//  values being edited so the screen can refresh
//  live during a long press.
// ─────────────────────────────────────────────
float locButtonStep(int pin, int dir,
                    int pageNum,
                    float* pLat, float* pLon, int* pUtc) {
  if (digitalRead(pin) != HIGH) return 0.0f;
  delay(30);
  if (digitalRead(pin) != HIGH) return 0.0f;

  unsigned long holdStart = millis();

  // Wait for release or threshold
  while (digitalRead(pin) == HIGH) {
    unsigned long held = millis() - holdStart;

    if (held > 600) {
      // ── Long hold → continuous 1.00 steps ──
      float step = dir * 1.0f;
      while (digitalRead(pin) == HIGH) {
        if (pageNum == 0) {
          *pLat = constrain(*pLat + step, -90.0f, 90.0f);
          *pLat = roundf(*pLat * 100) / 100.0f;
        } else if (pageNum == 1) {
          // For longitude, step increases the absolute value
          // in the current hemisphere (preserve sign)
          float absStep = (*pLon >= 0) ? step : -step;
          float newLon = *pLon + absStep;
          // If crossing zero, clamp to 0
          if (*pLon >= 0 && newLon < 0) newLon = 0;
          if (*pLon < 0  && newLon > 0) newLon = 0;
          *pLon = constrain(newLon, -180.0f, 180.0f);
          *pLon = roundf(*pLon * 100) / 100.0f;
        }
        drawLocationSetupScreen(pageNum, *pLat, *pLon, *pUtc);
        delay(150);
      }
      delay(30);
      return 0.0f;   // already applied directly
    }
  }
  delay(30);

  unsigned long held = millis() - holdStart - 30;
  if (held > 150) return dir * 0.10f;   // short hold → 0.10
  return dir * 0.01f;                    // tap → 0.01
}

void enterLocationSetup() {
  Serial.println("\n[LOC SETUP] Entered.");

  // Drain the red button — held 3s to get here
  while (digitalRead(BTN_RED) == HIGH);
  delay(100);

  float selLat = roundf(g_lat * 100) / 100.0f;
  float selLon = roundf(g_lon * 100) / 100.0f;
  int   selUtc = (int)(g_utcOffset / 3600L);

  // ── Page 0: Latitude ─────────────────────
  drawLocationSetupScreen(0, selLat, selLon, selUtc);
  while (true) {
    float step = locButtonStep(BTN_YELLOW, +1, 0, &selLat, &selLon, &selUtc);
    if (step == 0.0f)
      step = locButtonStep(BTN_BLUE,   -1, 0, &selLat, &selLon, &selUtc);
    if (step != 0.0f) {
      selLat = constrain(selLat + step, -90.0f, 90.0f);
      selLat = roundf(selLat * 100) / 100.0f;
      drawLocationSetupScreen(0, selLat, selLon, selUtc);
      Serial.print("[LOC] Lat="); Serial.println(selLat, 2);
    }
    if (buttonPressed(BTN_RED)) {
      Serial.print("[LOC] Lat confirmed: "); Serial.println(selLat, 2);
      break;
    }
  }

  // ── Page 1: Longitude ────────────────────
  // Yellow = increase absolute value (more E or more W)
  // Blue   = decrease absolute value (less E or less W)
  // Sign is preserved — hemisphere doesn't flip on tap.
  drawLocationSetupScreen(1, selLat, selLon, selUtc);
  while (true) {
    float step = locButtonStep(BTN_YELLOW, +1, 1, &selLat, &selLon, &selUtc);
    if (step == 0.0f)
      step = locButtonStep(BTN_BLUE,   -1, 1, &selLat, &selLon, &selUtc);
    if (step != 0.0f) {
      // Apply step in the direction of current sign
      float absStep = (selLon >= 0) ? step : -step;
      selLon = constrain(selLon + absStep, -180.0f, 180.0f);
      selLon = roundf(selLon * 100) / 100.0f;
      drawLocationSetupScreen(1, selLat, selLon, selUtc);
      Serial.print("[LOC] Lon="); Serial.println(selLon, 2);
    }
    if (buttonPressed(BTN_RED)) {
      Serial.print("[LOC] Lon confirmed: "); Serial.println(selLon, 2);
      break;
    }
  }

  // ── Page 2: UTC offset ───────────────────
  drawLocationSetupScreen(2, selLat, selLon, selUtc);
  while (true) {
    if (buttonPressed(BTN_YELLOW)) {
      selUtc = min(selUtc + 1, 14);
      drawLocationSetupScreen(2, selLat, selLon, selUtc);
      Serial.print("[LOC] UTC="); Serial.println(selUtc);
    }
    if (buttonPressed(BTN_BLUE)) {
      selUtc = max(selUtc - 1, -12);
      drawLocationSetupScreen(2, selLat, selLon, selUtc);
      Serial.print("[LOC] UTC="); Serial.println(selUtc);
    }
    if (buttonPressed(BTN_RED)) {
      Serial.print("[LOC] UTC confirmed: "); Serial.println(selUtc);
      break;
    }
  }

  // ── Reverse geocode to get city name ─────
  String detectedCity = reverseGeocode(selLat, selLon);

  // ── Save ─────────────────────────────────
  char tzBuf[20];
  if (selUtc >= 0) sprintf(tzBuf, "Etc/GMT-%d", selUtc);
  else             sprintf(tzBuf, "Etc/GMT+%d", -selUtc);

  g_lat       = selLat;
  g_lon       = selLon;
  g_utcOffset = (long)selUtc * 3600L;
  g_city      = detectedCity;
  g_country   = "";
  g_timezone  = String(tzBuf);
  g_tzEncoded = encodeTZ(g_timezone);

  Serial.print("[LOC SETUP] Saved: lat="); Serial.print(g_lat, 2);
  Serial.print(" lon="); Serial.print(g_lon, 2);
  Serial.print(" UTC"); Serial.print(selUtc);
  Serial.print(" city="); Serial.println(g_city);

  saveLocation();
  drawLocationConfirmedScreen(selLat, selLon, selUtc, detectedCity);
  delay(2500);
  ESP.restart();
}

// ─────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);

  // Buttons
  pinMode(BTN_BLUE,   INPUT);
  pinMode(BTN_RED,    INPUT);
  pinMode(BTN_YELLOW, INPUT);

  // GPIO25 starts idle — DAC and PWM both inactive.
  // Each function (cuckoo / alarm) enables its mode before use
  // and releases the pin cleanly when done.

  // I2C + OLED
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("[OLED] Init failed. Check wiring.");
    delay(2000);
  } else {
    Serial.println("[OLED] Initialized.");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(16, 20);
    display.println("Weather Clock");
    display.setCursor(16, 34);
    display.println("Starting up...");
    display.display();
  }

  // WiFiManager — connects using saved credentials or
  // opens captive portal if none are saved yet.
  // OLED switches to setup instructions automatically
  // via the setAPCallback above if portal is needed.
  connectWiFi();

  // ── Location setup ────────────────────────
  // Priority:
  //  1. Flash (saved from previous boot)
  //  2. IP geolocation (auto-detect, then save)
  //  3. Hardcoded fallback (San Jose, CA 95129)
  // To force re-detection: long press Blue 10s.
  bool locOK = loadSavedLocation();

  // If flash had the old "London" default from a
  // previous firmware version, wipe it and re-detect.
  if (locOK && g_city == "London" && g_country == "GB") {
    Serial.println("[LOC] Stale London default detected — re-detecting.");
    locOK = false;
  }

  if (!locOK) {
    locOK = fetchLocationByIP();
    if (locOK) {
      saveLocation();
    } else {
      Serial.println("[LOC] All detection failed — using default (San Jose, CA).");
      g_lat       =  37.3118f;
      g_lon       = -122.0266f;
      g_utcOffset = -25200L;
      g_city      = "San Jose";
      g_country   = "US";
      g_timezone  = "America/Los_Angeles";
      g_tzEncoded = "America%2FLos_Angeles";
      saveLocation();
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.print("Location: ");
  display.println(g_city.substring(0, 11));   // truncate long city names
  display.setCursor(10, 34);
  display.println("Syncing time...");
  display.display();

  Serial.print("[INFO] Location: "); Serial.print(g_city.c_str());
  Serial.print(", "); Serial.print(g_country.c_str());
  Serial.print(" | TZ: "); Serial.println(g_timezone.c_str());

  fetchCurrentTime();

  g_displayMode = MODE_CLOCK;
  drawClockFrame();

  // Silently fetch weather in background — no splash shown.
  // Data will be ready when user first presses Red/Yellow.
  Serial.println("[WEATHER] Background fetch on startup...");
  fetchWeatherCache();
  Serial.println("[WEATHER] Startup fetch done.");

  // Start geo retry timer from now — first background retry
  // will fire 30 seconds after boot completes, not immediately.
  g_lastGeoRetryMs = millis();

  Serial.println("\n[READY] Clock running.");
  Serial.print  ("[INFO]  WiFi SSID : "); Serial.println(WiFi.SSID());
  Serial.println("[INFO]  Red btn       = Today's weather card");
  Serial.println("[INFO]  Red btn x2    = Today's detail (wind/UV/sun)");
  Serial.println("[INFO]  Red btn 3s    = Location setup mode");
  Serial.println("[INFO]  Yellow btn    = Tomorrow's weather card");
  Serial.println("[INFO]  Yellow btn x2 = Tomorrow's detail (wind/UV/sun)");
  Serial.println("[INFO]  Blue btn      = Return to clock");
  Serial.println("[INFO]  Blue btn x2   = Alarm setup mode");
  Serial.println("[INFO]  Blue btn 3s   = Toggle alarm on/off");
  Serial.println("[INFO]  Blue btn 10s  = Wipe WiFi credentials + reboot");
  Serial.print  ("[INFO]  Auto cuckoo chime: ");
  Serial.print  (CUCKOO_START); Serial.print(":00 — ");
  Serial.print  (CUCKOO_END);   Serial.println(":00 daily");
}

// ─────────────────────────────────────────────
//  loop
// ─────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Update time from ESP32 RTC
  updateLocalClock();

  // NTP re-sync every hour
  if (now - g_lastResyncMs >= 3600000UL) {
    fetchCurrentTime();
    g_lastResyncMs = millis();
  }

  // ── Silent background geocode retry ──────
  // If city is still Unknown, retry every 30s
  // silently until a name is found.
  if ((g_city == "Unknown" || g_city.length() == 0) &&
      (now - g_lastGeoRetryMs >= 30000UL)) {
    g_lastGeoRetryMs = millis();
    Serial.println("[GEO] Background retry...");
    String found = reverseGeocode(g_lat, g_lon);
    if (found != "Unknown" && found.length() > 0) {
      g_city = found;
      saveLocation();
      Serial.print("[GEO] City found: "); Serial.println(g_city);
      // Force clock redraw so city shows if displayed anywhere
      g_lastSecondVal = 99999UL;
    }
  }

  // ── Cuckoo chime check ────────────────────
  checkCuckooChime();

  // ── Daily alarm ───────────────────────────
  if (g_alarmEnabled &&
      g_hour == ALARM_HOUR && g_minute == ALARM_MINUTE && g_second < 5) {
    if (!g_alarmFiredToday) {
      Serial.print("\n[ALARM] ");
      Serial.print(ALARM_HOUR); Serial.print(":");
      if (ALARM_MINUTE < 10) Serial.print("0");
      Serial.print(ALARM_MINUTE); Serial.println(" — starting alarm.");
      g_alarmFiredToday = true;
      g_displayMode = MODE_CLOCK;
      drawClockFrame();
      playAlarm();
    }
  } else {
    if (g_hour != ALARM_HOUR || g_minute != ALARM_MINUTE) g_alarmFiredToday = false;
  }

  // ── Blue button ───────────────────────────
  //  Single press   → return to clock
  //  Double press   → alarm setup mode
  //  Hold 3–9s      → toggle alarm on/off
  //  Hold 10s+      → wipe WiFi + reboot
  if (digitalRead(BTN_BLUE) == HIGH) {
    unsigned long pressStart = millis();

    // Wait while held — break when released
    while (digitalRead(BTN_BLUE) == HIGH) {
      // 10s reached while still held → WiFi reset immediately
      if (millis() - pressStart >= 10000) {
        Serial.println("\n[BTN BLUE 10s] WiFi reset triggered.");
        drawWiFiResetScreen();
        delay(2000);
        WiFiManager wm;
        wm.resetSettings();
        Preferences prefs;
        prefs.begin("location", false);
        prefs.clear();
        prefs.end();
        ESP.restart();
      }
    }

    // Button released — measure total hold time
    unsigned long held = millis() - pressStart;
    delay(50);

    if (held >= 3000) {
      // ── 3–9s: toggle alarm ───────────────
      g_alarmEnabled    = !g_alarmEnabled;
      g_alarmFiredToday = false;
      Serial.print("[BTN BLUE 3s] Alarm ");
      Serial.println(g_alarmEnabled ? "ENABLED" : "DISABLED");

      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(22, 16); display.print("* Alarm *");
      display.drawLine(0, 26, SCREEN_WIDTH - 1, 26, SSD1306_WHITE);
      display.setTextSize(2);
      if (g_alarmEnabled) {
        display.setCursor(26, 34); display.print("ON");
      } else {
        display.setCursor(14, 34); display.print("OFF");
      }
      display.setTextSize(1);
      display.display();
      delay(1500);

      g_displayMode   = MODE_CLOCK;
      g_lastSecondVal = 99999UL;
      drawClockFrame();

    } else {
      // ── < 3s: single or double press ─────
      bool isDouble = false;
      unsigned long t = millis();
      while (millis() - t < 400) {
        if (digitalRead(BTN_BLUE) == HIGH) {
          delay(50);
          if (digitalRead(BTN_BLUE) == HIGH) {
            while (digitalRead(BTN_BLUE) == HIGH);
            delay(50);
            isDouble = true;
          }
          break;
        }
      }

      if (isDouble) {
        Serial.println("[BTN BLUE x2] Entering alarm setup.");
        enterAlarmSetup();
      } else {
        Serial.println("[BTN BLUE] Return to clock.");
        g_displayMode   = MODE_CLOCK;
        g_lastSecondVal = 99999UL;
      }
    }
  }
  // ── Red button ────────────────────────────
  //  Hold 3s          → location setup mode
  //  Single press      → today's weather card
  //  Double press      → today's detail card
  if (digitalRead(BTN_RED) == HIGH) {
    unsigned long pressStart = millis();
    while (digitalRead(BTN_RED) == HIGH) {
      if (millis() - pressStart >= 3000) {
        Serial.println("\n[BTN RED 3s] Location setup triggered.");
        enterLocationSetup();   // reboots after saving
      }
    }
    delay(50);
    // Single / double detection
    bool isDouble = false;
    unsigned long t = millis();
    while (millis() - t < 400) {
      if (digitalRead(BTN_RED) == HIGH) {
        delay(50);
        if (digitalRead(BTN_RED) == HIGH) {
          while (digitalRead(BTN_RED) == HIGH);
          delay(50);
          isDouble = true;
        }
        break;
      }
    }
    if (isDouble) showTodayDetail();
    else          showTodayWeather();
  }

  // ── Yellow button ─────────────────────────
  //  Hold 3s (any weather page) → force re-fetch
  //  Single press               → tomorrow weather
  //  Double press               → tomorrow detail
  if (digitalRead(BTN_YELLOW) == HIGH) {
    unsigned long pressStart = millis();
    bool longPress = false;

    // Measure hold duration — same pattern as blue button
    while (digitalRead(BTN_YELLOW) == HIGH) {
      if (millis() - pressStart >= 3000) {
        longPress = true;
        // Wait for release before acting
        while (digitalRead(BTN_YELLOW) == HIGH);
        delay(50);
        break;
      }
    }

    if (longPress) {
      // ── 3s: force weather re-fetch ─────────
      Serial.println("[BTN YLW 3s] Force weather re-fetch.");
      g_weatherFetchedAt = 0;   // invalidate cache — force fresh fetch
      drawFetchingSplash("Weather");
      bool ok = fetchWeatherCache();
      if (ok) {
        // Redraw whichever weather page was active, or today by default
        if (g_displayMode == MODE_TOMORROW) {
          String cond = wmoDescription(g_tmrWMO, g_tmrTMax);
          drawWeatherCard(" Tomorrow's Weather ", g_tmrDate, cond,
                          g_tmrTMin, g_tmrTMax);
        } else if (g_displayMode == MODE_TODAY ||
                   g_displayMode == MODE_CLOCK) {
          String cond = wmoDescription(g_todayWMO, g_todayTMax);
          drawWeatherCard("   Today's Weather  ", g_todayDate, cond,
                          g_todayTMin, g_todayTMax);
          g_displayMode = MODE_TODAY;
        }
      } else {
        // Fetch failed — show error briefly then return to clock
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(10, 24); display.println("Weather fetch");
        display.setCursor(10, 36); display.println("failed. Try again.");
        display.display();
        delay(2000);
        g_displayMode   = MODE_CLOCK;
        g_lastSecondVal = 99999UL;
        drawClockFrame();
      }
    } else {
      // Released before 3s — single / double press
      delay(50);
      bool isDouble = false;
      unsigned long t = millis();
      while (millis() - t < 400) {
        if (digitalRead(BTN_YELLOW) == HIGH) {
          delay(50);
          if (digitalRead(BTN_YELLOW) == HIGH) {
            while (digitalRead(BTN_YELLOW) == HIGH);
            delay(50);
            isDouble = true;
          }
          break;
        }
      }
      if (isDouble) showTomorrowDetail();
      else          showTomorrowWeather();
    }
  }

  // ── Redraw clock when second changes ──────
  unsigned long currentSecond = (unsigned long)g_hour   * 3600UL
                              + (unsigned long)g_minute  *   60UL
                              + (unsigned long)g_second;
  if (g_displayMode == MODE_CLOCK && currentSecond != g_lastSecondVal) {
    g_lastSecondVal = currentSecond;
    drawClockFrame();
  }

  delay(20);
}

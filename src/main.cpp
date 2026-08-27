/**
 * =======================================================================================
 * @file main.cpp
 * @brief ESP32-C3 Global Weather Radar (RainViewer) + ADS-B Aircraft Tracker
 * @author hackra76 / Antigravity AI
 * @version 2.0.0
 * 
 * @details 
 * Multi-functional global radar application for GC9A01 240x240 round display and ESP32-C3 SuperMini.
 * 
 * Core Features:
 * 1. 🌍 GLOBAL WEATHER RADAR (RainViewer API):
 *    - Slippy Map / Web Mercator tile projection for any coordinate on Earth.
 *    - High-speed downloading and decoding of 256x256 px weather radar tiles.
 * 
 * 2. ✈️ ADS-B AIRCRAFT TRACKER (Live Global Flights):
 *    - Real-time aircraft tracking with track vector, speed, altitude, emergency status (SQ 7700).
 *    - Automatic route lookup (VRS standing-data).
 * 
 * 3. ⛈️ APPROACHING RAIN / STORM ALERTS:
 *    - Proximity & storm tracking detection using wind vectors from Open-Meteo API.
 *    - Tactical Threat Sector Beam & pulsating perimeter beacon.
 * =======================================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <PNGdec.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <math.h>

#include "config.h"
#include "display_gc9a01.h"
#include "ui_font.h"

static const char* CURRENT_VERSION = "v2.0.0";

// =======================================================================================
// 1. GLOBAL INSTANCES & DATA STRUCTURES
// =======================================================================================

LGFX tft;                 ///< LovyanGFX display driver
LGFX_Sprite canvas(&tft); ///< Dynamic offscreen buffer (Double Buffering)
bool canvasReady = false;
PNG png;                  ///< PNG decoder for radar images
File pngFile;             ///< File descriptor for SPIFFS
Preferences prefs;        ///< Persistent NVS storage for configuration
WebServer server(80);     ///< Local embedded web server

static const char* RADAR_FILE = "/radar.png";
static const char* RADAR_RAW_CACHE_FILE = "/radar_cache.raw";
bool radarRawCacheValid = false;
File fRawOut;

// RainViewer State
String rainViewerHost = RAINVIEWER_DEFAULT_HOST;
String radarPath = "";
uint32_t radarTimestamp = 0;
int currentTileX = 0;
int currentTileY = 0;
float currentTileOffsetPx = 0.0f;
float currentTileOffsetPy = 0.0f;

uint16_t currLine565[RADAR_TILE_SIZE];
static uint8_t* tileChunks[4] = {nullptr, nullptr, nullptr, nullptr};

void ensureCanvas() {
  if (!canvasReady) {
    canvas.setColorDepth(8);
    if (canvas.createSprite(TFT_W, TFT_H)) {
      canvas.loadFont(ui_font_vlw, lgfx::IFont::font_type_t::ft_vlw);
      canvas.setTextSize(0.80f);
      canvasReady = true;
    }
  }
}

void releaseCanvas() {
  if (canvasReady) {
    canvas.deleteSprite();
    canvasReady = false;
  }
}

// Precipitation Detection (Rain / Storm / Hail / Snow) & Heading with Open-Meteo Wind
float minPrecipDistSqPx = 999999.0f;
float closestPrecipDistKm = -1.0f;
bool rainAlertActive = false;
bool isIncomingAlert = false;
float rainAlertThresholdKm = 15.0f; ///< Alert trigger radius in km (0 = disabled)
int rainAlertMinPixels = 25;        ///< Noise filter threshold: 10=high, 25=medium, 50=low

// Incoming precipitation (in wind cone)
int alertIncomingPixelCount = 0;
float alertIncomingMinSqPx = 999999.0f;
float alertIncomingTargetDx = 0.0f;
float alertIncomingTargetDy = 0.0f;

// General proximity precipitation
int alertAnyPixelCount = 0;
float alertAnyMinSqPx = 999999.0f;
float alertAnyTargetDx = 0.0f;
float alertAnyTargetDy = 0.0f;

float alertBearingDeg = -1.0f;
float alertTargetDistPx = 0.0f;
char alertBearingDir[8] = "";

// Wind data from Open-Meteo API
float windDir700hPa = -1.0f;     ///< Wind direction at 700 hPa (~3000m ASL, 0-359°)
float windSpeed700hPa = 0.0f;    ///< Wind speed at 700 hPa (km/h)
float windDirSurface = -1.0f;    ///< Surface wind direction (0-359°)
float windSpeedSurface = 0.0f;   ///< Surface wind speed (km/h)
uint32_t lastWindFetchMs = 0;
static constexpr uint32_t WIND_FETCH_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes

inline const char* getCompassDirText(float deg) {
  if (deg < 0.0f) return "--";
  if (deg >= 337.5f || deg < 22.5f) return "N";
  if (deg >= 22.5f && deg < 67.5f) return "NE";
  if (deg >= 67.5f && deg < 112.5f) return "E";
  if (deg >= 112.5f && deg < 157.5f) return "SE";
  if (deg >= 157.5f && deg < 202.5f) return "S";
  if (deg >= 202.5f && deg < 247.5f) return "SW";
  if (deg >= 247.5f && deg < 292.5f) return "W";
  if (deg >= 292.5f && deg < 337.5f) return "NW";
  return "--";
}

#include "world_borders.h"

// Landmark Cities
struct City { const char* name; float lat; float lon; bool isMajor; };
static const City CITIES[] = {
  {"BA", 48.1486, 17.1077, true},  {"TT", 48.3775, 17.5883, false},
  {"NR", 48.3061, 18.0864, true},  {"TN", 48.8945, 18.0444, false},
  {"ZA", 49.2231, 18.7397, true},  {"BB", 48.7363, 19.1462, true},
  {"PO", 48.9984, 21.2393, true},  {"KE", 48.7164, 21.2611, true},
  {"PP", 49.0595, 20.2978, false}, {"VIE", 48.1103, 16.5697, true},
  {"BUD", 47.4369, 19.2556, true}, {"PRG", 50.1008, 14.2600, true},
  {"KRK", 50.0777, 19.7848, true}
};
static constexpr size_t CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

// Configuration & Runtime Variables
float centerLat = atof(DEFAULT_CENTER_LAT);
float centerLon = atof(DEFAULT_CENTER_LON);
int timeOffsetHours = DEFAULT_TIME_OFFSET_HOURS;
int carouselIntervalSec = 30;
uint32_t carouselIntervalMs = 30000;
bool carouselEnabled = true;

// Night Mode (Astronomical sunset/sunrise)
bool nightModeEnabled = true;
bool isNightActive = false;

// Zoom Levels (Radius in km & RainViewer Tile Zoom <= 7)
static const float ZOOM_LEVELS_KM[] = {10.0f, 25.0f, 50.0f, 100.0f, 250.0f};
static const int ZOOM_LEVELS_Z[]    = {7,     7,     7,     7,      6};
static constexpr int ZOOM_LEVEL_COUNT = sizeof(ZOOM_LEVELS_KM) / sizeof(ZOOM_LEVELS_KM[0]);
int zoomIndex = 2; // Default 50 km
float currentRadiusKm = atof(DEFAULT_RADIUS_KM_TEXT);

// Application State Machine
enum AppMode { MODE_COMBINED = 0, MODE_WEATHER = 1, MODE_PLANES = 2 };
AppMode currentMode = MODE_COMBINED;

uint32_t lastWeatherUpdateMs = 0;
uint32_t lastCarouselSwitchMs = 0;
uint32_t lastPlaneFetchMs = 0;
uint32_t lastPlaneRedrawMs = 0;
uint32_t lastPlaneFetchFixMs = 0;
static constexpr uint32_t PLANE_FETCH_INTERVAL_MS = 10000;
static constexpr uint32_t PLANE_REDRAW_INTERVAL_MS = 1000;

// Aircraft Data Model
struct AircraftData {
  float lat;
  float lon;
  float track;
  float nose_deg;
  float gs_knots;
  float vrate_fpm;
  bool is_mil;
  bool is_emergency;
  char squawk[5];
  char route[10];    ///< Format e.g. "VIE>AMS"
  char callsign[9];  ///< Callsign e.g. "KLM1902"
  char type[5];      ///< ICAO Type e.g. "A21N"
  char alt[12];      ///< Altitude in meters e.g. "10250m"
};

static constexpr size_t MAX_AIRCRAFT = 32;
AircraftData aircraftList[MAX_AIRCRAFT];
size_t aircraftCount = 0;

// Forward Declarations
bool downloadLatestRadar();
void decodeRadarImage();
void fetchWindData();
void renderScreen();
void fetchPlanesData();
void drawPlanesOverlay(LovyanGFX& target);
void drawWeatherOverlay(LovyanGFX& target, bool showTime);
void drawPlaneRadarGrid(LovyanGFX& target);
void setupWebServer();


// =======================================================================================
// 2. GEOGRAPHIC & PROJECTION FUNCTIONS (PHYSICALLY ACCURATE SPHERICAL MAPPING)
// =======================================================================================

inline float gpsToScreenX(float lat, float lon) {
  float cosLat = cosf(centerLat * 0.017453292519943295f);
  if (fabsf(cosLat) < 0.01f) cosLat = 0.01f;
  float dLon = lon - centerLon;
  float dX_km = dLon * 111.320f * cosLat;
  return 120.0f + (dX_km / currentRadiusKm) * 120.0f;
}

inline float gpsToScreenY(float lat, float lon) {
  float dLat = centerLat - lat;
  float dY_km = dLat * 111.320f;
  return 120.0f + (dY_km / currentRadiusKm) * 120.0f;
}


// =======================================================================================
// 3. UI, SETTINGS & NIGHT MODE
// =======================================================================================

void showStatus(const String& text) {
  tft.setTextSize(0.75f);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int y = 75;
  int start = 0;
  while (true) {
    int pos = text.indexOf('\n', start);
    String line = (pos == -1) ? text.substring(start) : text.substring(start, pos);
    tft.setTextDatum(textdatum_t::middle_center);
    tft.drawString(line, TFT_W / 2, y);
    y += 18;
    if (pos == -1) break;
    start = pos + 1;
  }
}

String getRadarTimeText(uint32_t ts) {
  if (ts == 0) return "--:--";
  time_t localTs = (time_t)(ts + (time_t)(timeOffsetHours * 3600));
  struct tm* t = gmtime(&localTs);
  if (!t) return "--:--";
  char out[6];
  snprintf(out, sizeof(out), "%02d:%02d", t->tm_hour, t->tm_min);
  return String(out);
}

String getCurrentSystemTimeText() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (t->tm_year < 100) return "--:--";
  char out[6];
  snprintf(out, sizeof(out), "%02d:%02d", t->tm_hour, t->tm_min);
  return String(out);
}

void resetSettingsAndRestart() {
  showStatus("Resetting settings...");
  WiFiManager wm; 
  wm.resetSettings();
  prefs.begin("radar", false); 
  prefs.clear(); 
  prefs.end();
  delay(1000); 
  ESP.restart();
}

void checkResetButtonAtBoot() {
  if (digitalRead(ZOOM_BUTTON_PIN) != LOW) return;
  showStatus("Hold for factory reset");
  uint32_t start = millis();
  while (digitalRead(ZOOM_BUTTON_PIN) == LOW) {
    if (millis() - start >= RESET_HOLD_MS) resetSettingsAndRestart();
    delay(20);
  }
}

void calculateSunTimes(float lat, float lon, int offsetHours, int& sunriseMin, int& sunsetMin) {
  time_t rawtime = time(nullptr);
  struct tm* t = localtime(&rawtime);
  int yday = (t->tm_yday >= 0 && t->tm_yday < 366) ? t->tm_yday : 180;

  float gamma = 2.0f * (float)M_PI * (float)yday / 365.0f;
  float decl = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma)
               - 0.006758f * cosf(2.0f * gamma) + 0.000907f * sinf(2.0f * gamma);

  float latRad = lat * DEG_TO_RAD;
  float cosH0 = (cosf(90.833f * DEG_TO_RAD) - sinf(latRad) * sinf(decl)) / (cosf(latRad) * cosf(decl));
  
  if (cosH0 > 1.0f) {
    sunriseMin = -1; sunsetMin = -1; return;
  } else if (cosH0 < -1.0f) {
    sunriseMin = 0; sunsetMin = 1440; return;
  }

  float H0_deg = acosf(cosH0) * RAD_TO_DEG;
  float eqtime = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma)
                 - 0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));

  float solarNoonUtc = 720.0f - (4.0f * lon) - eqtime;
  float riseUtc = solarNoonUtc - (H0_deg * 4.0f);
  float setUtc = solarNoonUtc + (H0_deg * 4.0f);

  sunriseMin = (int)(riseUtc + (offsetHours * 60.0f) + 0.5f);
  sunsetMin = (int)(setUtc + (offsetHours * 60.0f) + 0.5f);

  while (sunriseMin < 0) sunriseMin += 1440;
  while (sunriseMin >= 1440) sunriseMin -= 1440;
  while (sunsetMin < 0) sunsetMin += 1440;
  while (sunsetMin >= 1440) sunsetMin -= 1440;
}

void updateNightMode() {
  if (!nightModeEnabled) {
    if (isNightActive) {
      isNightActive = false;
      tft.setBrightness(180);
    }
    return;
  }
  
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (t->tm_year < 100) return;

  int currentMin = t->tm_hour * 60 + t->tm_min;
  int sunriseMin = 6 * 60;
  int sunsetMin = 21 * 60;

  calculateSunTimes(centerLat, centerLon, timeOffsetHours, sunriseMin, sunsetMin);

  bool shouldBeNight = false;
  if (sunriseMin < sunsetMin) {
    shouldBeNight = (currentMin < sunriseMin || currentMin >= sunsetMin);
  } else {
    shouldBeNight = (currentMin >= sunsetMin && currentMin < sunriseMin);
  }

  if (shouldBeNight != isNightActive) {
    isNightActive = shouldBeNight;
    tft.setBrightness(isNightActive ? 30 : 180);
    Serial.printf("[NIGHT] Night mode: %s (Brightness %d, Sunrise: %02d:%02d, Sunset: %02d:%02d)\n", 
                  isNightActive ? "ACTIVE" : "OFF", isNightActive ? 30 : 180,
                  sunriseMin / 60, sunriseMin % 60, sunsetMin / 60, sunsetMin % 60);
  }
}

void setZoomIndex(int newIndex) {
  zoomIndex = (newIndex >= 0 && newIndex < ZOOM_LEVEL_COUNT) ? newIndex : 2;
  currentRadiusKm = ZOOM_LEVELS_KM[zoomIndex];
  
  prefs.begin("radar", false);
  prefs.putInt("zoom_idx", zoomIndex);
  prefs.putFloat("radius", currentRadiusKm);
  prefs.end();

  if (SPIFFS.exists(RADAR_FILE)) {
    decodeRadarImage();
    renderScreen();
  }

  if (currentMode == MODE_WEATHER || currentMode == MODE_COMBINED) {
    downloadLatestRadar();
  }
  if (currentMode == MODE_PLANES || currentMode == MODE_COMBINED) {
    lastPlaneFetchMs = millis();
    fetchPlanesData();
  }
  renderScreen();
}

void setAppMode(AppMode newMode, bool saveToPrefs = true) {
  currentMode = newMode;
  lastCarouselSwitchMs = millis();

  if (saveToPrefs) {
    prefs.begin("radar", false);
    prefs.putInt("mode", (int)currentMode);
    prefs.end();
  }

  if (currentMode == MODE_WEATHER) {
    renderScreen();
  } else {
    lastPlaneFetchMs = millis();
    fetchPlanesData();
    renderScreen();
  }
}

void handleButton() {
  static bool lastBtnState = HIGH;
  static uint32_t pressStartMs = 0;
  static uint32_t lastReleaseMs = 0;
  static int pendingClicks = 0;
  static constexpr uint32_t DOUBLE_CLICK_GAP_MS = 350;

  bool btnState = digitalRead(ZOOM_BUTTON_PIN);
  uint32_t now = millis();

  if (lastBtnState == HIGH && btnState == LOW) {
    pressStartMs = now;
  } else if (btnState == LOW) {
    if (now - pressStartMs >= RESET_HOLD_MS) {
      resetSettingsAndRestart();
      return;
    }
  } else if (lastBtnState == LOW && btnState == HIGH) {
    uint32_t duration = now - pressStartMs;
    if (duration >= 30 && duration < RESET_HOLD_MS) {
      pendingClicks++;
      lastReleaseMs = now;
    }
  }
  lastBtnState = btnState;

  if (pendingClicks > 0 && (now - lastReleaseMs >= DOUBLE_CLICK_GAP_MS)) {
    if (pendingClicks == 1) {
      setZoomIndex((zoomIndex + 1) % ZOOM_LEVEL_COUNT);
    } else if (pendingClicks >= 2) {
      setAppMode((AppMode)((currentMode + 1) % 3), true);
    }
    pendingClicks = 0;
  }
}

static bool shouldSaveWifiConfig = false;
static void saveWifiConfigCallback() {
  shouldSaveWifiConfig = true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA); 
  delay(100);
  showStatus("ESP-GlobalRadar " + String(CURRENT_VERSION) + "\nConnecting WiFi...");

  prefs.begin("radar", true);
  String curLat = String(prefs.getFloat("lat", atof(DEFAULT_CENTER_LAT)), 4);
  String curLon = String(prefs.getFloat("lon", atof(DEFAULT_CENTER_LON)), 4);
  String curRad = String((int)prefs.getFloat("radius", atof(DEFAULT_RADIUS_KM_TEXT)));
  String curOff = String(prefs.getInt("offset", DEFAULT_TIME_OFFSET_HOURS));
  String curCar = String(prefs.getInt("car_int", 30));
  prefs.end();

  WiFiManagerParameter custom_lat("lat", "Latitude", curLat.c_str(), 10);
  WiFiManagerParameter custom_lon("lon", "Longitude", curLon.c_str(), 10);
  WiFiManagerParameter custom_rad("radius", "Default Radius (km)", curRad.c_str(), 5);
  WiFiManagerParameter custom_off("offset", "Timezone UTC Offset (hours)", curOff.c_str(), 3);
  WiFiManagerParameter custom_car("car_int", "Carousel Interval (seconds)", curCar.c_str(), 4);

  WiFiManager wm;
  shouldSaveWifiConfig = false;
  wm.setSaveConfigCallback(saveWifiConfigCallback);
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(15);
  wm.setBreakAfterConfig(true);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  wm.addParameter(&custom_lat);
  wm.addParameter(&custom_lon);
  wm.addParameter(&custom_rad);
  wm.addParameter(&custom_off);
  wm.addParameter(&custom_car);

  if (!wm.autoConnect("ESP-GlobalRadar-Setup")) {
    showStatus("WiFi Config Portal\nSSID: ESP-GlobalRadar-Setup\nIP: 192.168.4.1");
    return;
  }

  if (shouldSaveWifiConfig) {
    prefs.begin("radar", false);
    if (strlen(custom_lat.getValue()) > 0) {
      centerLat = atof(custom_lat.getValue());
      prefs.putFloat("lat", centerLat);
    }
    if (strlen(custom_lon.getValue()) > 0) {
      centerLon = atof(custom_lon.getValue());
      prefs.putFloat("lon", centerLon);
    }
    if (strlen(custom_rad.getValue()) > 0) {
      currentRadiusKm = atof(custom_rad.getValue());
      prefs.putFloat("radius", currentRadiusKm);
    }
    if (strlen(custom_off.getValue()) > 0) {
      timeOffsetHours = atoi(custom_off.getValue());
      prefs.putInt("offset", timeOffsetHours);
    }
    if (strlen(custom_car.getValue()) > 0) {
      carouselIntervalSec = atoi(custom_car.getValue());
      if (carouselIntervalSec < 5) carouselIntervalSec = 5;
      prefs.putInt("car_int", carouselIntervalSec);
    }
    prefs.end();
  }

  carouselIntervalMs = (uint32_t)carouselIntervalSec * 1000;
  configTime(timeOffsetHours * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  uint32_t ntpStart = millis();
  while (time(nullptr) < 100000 && millis() - ntpStart < 3500) {
    delay(100);
  }

  String ipStr = WiFi.localIP().toString();
  Serial.println("\n=======================================================");
  Serial.printf("WiFi Connected! IP Address: %s\n", ipStr.c_str());
  if (MDNS.begin("espglobalradar")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("Web Dashboard: http://espglobalradar.local or http://" + ipStr);
  }
  Serial.println("=======================================================\n");

  showStatus("WiFi Connected!\n\nIP: " + ipStr + "\nespglobalradar.local");
  delay(2000);
}


// =======================================================================================
// 4. LOCAL WEB DASHBOARD (EMBEDDED RESPONSIVE WEB SERVER)
// =======================================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP GlobalRadar & Aircraft Dashboard</title>
  <style>
    :root {
      --bg: #0d1117;
      --card-bg: rgba(22, 27, 34, 0.85);
      --border: #30363d;
      --accent: #58a6ff;
      --accent-green: #3fb950;
      --accent-magenta: #d2a8ff;
      --accent-orange: #f0883e;
      --text: #c9d1d9;
      --text-bright: #f0f6fc;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 16px; min-height: 100vh; display: flex; flex-direction: column; align-items: center; }
    .container { width: 100%; max-width: 680px; }
    header { text-align: center; margin-bottom: 20px; }
    header h1 { color: var(--text-bright); font-size: 1.6rem; display: flex; align-items: center; justify-content: center; gap: 8px; }
    header p { color: #8b949e; font-size: 0.9rem; margin-top: 4px; }
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.3); backdrop-filter: blur(8px); }
    .card h2 { font-size: 1.1rem; color: var(--text-bright); margin-bottom: 12px; display: flex; align-items: center; justify-content: space-between; }
    .grid-stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 10px; }
    .stat-box { background: rgba(0,0,0,0.3); padding: 10px; border-radius: 8px; border: 1px solid var(--border); text-align: center; }
    .stat-box .label { font-size: 0.75rem; color: #8b949e; text-transform: uppercase; letter-spacing: 0.5px; }
    .stat-box .val { font-size: 1.15rem; font-weight: bold; color: var(--accent); margin-top: 4px; }
    .stat-box .sub { font-size: 0.75rem; color: #8b949e; margin-top: 3px; }
    .btn-group { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
    button, input[type="submit"] { background: #21262d; color: var(--text-bright); border: 1px solid var(--border); padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 0.9rem; transition: all 0.2s; }
    button:hover, input[type="submit"]:hover { background: #30363d; border-color: #8b949e; }
    button.active { background: #1f6feb; border-color: #58a6ff; color: #fff; }
    table { width: 100%; border-collapse: collapse; margin-top: 8px; font-size: 0.85rem; }
    th, td { padding: 8px 6px; text-align: left; border-bottom: 1px solid var(--border); }
    th { color: #8b949e; font-weight: 600; }
    .route { color: var(--accent-magenta); font-weight: bold; }
    .speed { color: var(--accent-green); }
    .alt { color: #e3b341; }
    .form-group { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 10px; }
    label { font-size: 0.8rem; color: #8b949e; display: block; margin-bottom: 4px; }
    input[type="text"], input[type="number"], input[type="password"], select { width: 100%; background: #0d1117; border: 1px solid var(--border); padding: 8px; border-radius: 6px; color: var(--text-bright); font-size: 0.9rem; }
    select { cursor: pointer; }
    .badge { padding: 3px 6px; border-radius: 4px; font-size: 0.7rem; font-weight: bold; background: rgba(56, 139, 253, 0.15); color: var(--accent); }
    .badge-green { background: rgba(63, 185, 80, 0.15); color: var(--accent-green); }
    #map { height: 280px; width: 100%; border-radius: 8px; border: 1px solid var(--border); margin-top: 6px; z-index: 1; }
    .leaflet-popup-content-wrapper, .leaflet-popup-tip { background: #161b22; color: #c9d1d9; border: 1px solid #30363d; }
    .plane-icon { display: flex; align-items: center; justify-content: center; text-shadow: 0 0 3px #000; font-size: 16px; }
    .rain-alert-banner {
      display: none;
      background: linear-gradient(135deg, rgba(239, 68, 68, 0.22), rgba(245, 158, 11, 0.22));
      border: 1px solid #ef4444;
      border-radius: 10px;
      padding: 12px 16px;
      margin-bottom: 16px;
      align-items: center;
      justify-content: space-between;
      box-shadow: 0 0 16px rgba(239, 68, 68, 0.35);
      animation: pulseAlert 2s infinite ease-in-out;
    }
    @keyframes pulseAlert {
      0%, 100% { box-shadow: 0 0 10px rgba(239, 68, 68, 0.3); border-color: #ef4444; }
      50% { box-shadow: 0 0 22px rgba(245, 158, 11, 0.6); border-color: #f59e0b; }
    }
  </style>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
</head>
<body>
  <div class="container">
    <header>
      <h1>🛰️ ESP GlobalRadar & Aircraft Tracker</h1>
      <p>RainViewer Global Weather • ADS-B Aircraft Tracker • GC9A01 LCD</p>
    </header>

    <!-- PRECIPITATION ALERT BANNER -->
    <div id="rain-alert-banner" class="rain-alert-banner">
      <div style="display:flex; align-items:center; gap:12px;">
        <span style="font-size:1.8rem;" id="rain-alert-icon">⛈️</span>
        <div>
          <div style="font-weight:bold; color:#fca5a5; font-size:1.05rem;" id="rain-alert-title">WARNING: Approaching Storm / Precipitation!</div>
          <div style="font-size:0.88rem; color:#e2e8f0; margin-top:2px;">
            Precipitation at distance <b id="rain-alert-dist" style="color:#fbbf24; font-size:1.05rem;">--</b> km towards <b id="rain-alert-dir" style="color:#60a5fa; font-size:1.05rem;">--</b> (<span id="rain-alert-deg">--</span>°) from your location.
          </div>
        </div>
      </div>
      <span class="badge" id="rain-alert-badge" style="background:#ef4444; color:#fff; font-weight:bold; padding:4px 10px; border-radius:20px; font-size:0.8rem;">&lt; 15 km</span>
    </div>

    <!-- CARD 1: RADAR STATUS -->
    <div class="card">
      <h2>📊 Radar Status <span class="badge badge-green" id="live-badge">LIVE DATA</span></h2>
      <div class="grid-stats">
        <div class="stat-box"><div class="label">Mode</div><div class="val" id="mode-val">--</div><div class="sub" id="car-sub">Carousel: ON</div></div>
        <div class="stat-box"><div class="label">Zoom Radius</div><div class="val" id="zoom-val">-- km</div><div class="sub" id="radar-time-sub">Radar: --:--</div></div>
        <div class="stat-box"><div class="label">Aircraft</div><div class="val" id="planes-count">0</div><div class="sub">In range</div></div>
        <div class="stat-box"><div class="label">WiFi Signal</div><div class="val" id="wifi-rssi">-- dBm</div><div class="sub" id="wifi-pct">Quality: -- %</div></div>
      </div>
    </div>

    <!-- CARD 2: SYSTEM & HARDWARE STATUS -->
    <div class="card">
      <h2>🖥️ System & Hardware Telemetry</h2>
      <div class="grid-stats">
        <div class="stat-box">
          <div class="label">CPU Clock</div>
          <div class="val" id="sys-cpu">160 MHz</div>
          <div class="sub" id="sys-temp">Temp: -- °C</div>
        </div>
        <div class="stat-box">
          <div class="label">Free RAM</div>
          <div class="val" id="sys-ram">-- KB</div>
          <div class="sub" id="sys-ram-sub">of 320 KB</div>
        </div>
        <div class="stat-box">
          <div class="label">Flash Memory</div>
          <div class="val" id="sys-flash">4 MB</div>
          <div class="sub" id="sys-heap-min">Min RAM: -- KB</div>
        </div>
        <div class="stat-box">
          <div class="label">Uptime</div>
          <div class="val" id="sys-uptime">00:00:00</div>
          <div class="sub" id="sys-ip">IP: --</div>
        </div>
      </div>
    </div>

    <!-- CARD 3: QUICK CONTROLS -->
    <div class="card">
      <h2>🎮 Quick Controls</h2>
      <label>Scale / Range (Zoom):</label>
      <div class="btn-group">
        <button onclick="setZoom(0)" id="zbtn-0">10 km</button>
        <button onclick="setZoom(1)" id="zbtn-1">25 km</button>
        <button onclick="setZoom(2)" id="zbtn-2">50 km</button>
        <button onclick="setZoom(3)" id="zbtn-3">100 km</button>
        <button onclick="setZoom(4)" id="zbtn-4">250 km</button>
      </div>
      <label style="margin-top: 14px;">View Mode:</label>
      <div class="btn-group">
        <button onclick="setMode('combined')" id="mbtn-comb">🛰️ Combined (Radar + Aircraft)</button>
        <button onclick="setMode('weather')" id="mbtn-weather">🌦️ Weather Radar Only</button>
        <button onclick="setMode('planes')" id="mbtn-planes">✈️ Aircraft Only</button>
        <button onclick="toggleCarousel()" id="mbtn-car">🔄 Carousel</button>
      </div>
    </div>

    <!-- CARD 4: AIRCRAFT TABLE -->
    <div class="card">
      <h2>✈️ Aircraft in Radar Range</h2>
      <div style="overflow-x: auto;">
        <table>
          <thead>
            <tr><th>Flight / Route</th><th>Type</th><th>Speed</th><th>Altitude</th><th>Position</th></tr>
          </thead>
          <tbody id="planes-tbody">
            <tr><td colspan="5" style="text-align:center; color:#8b949e;">Loading aircraft data...</td></tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- CARD: INTERACTIVE LIVE MAP (LEAFLET) -->
    <div class="card">
      <h2>🗺️ Interactive Radar Map</h2>
      <div id="map"></div>
      <div style="font-size: 0.8rem; color: #8b949e; margin-top: 8px; text-align: center;">
        💡 Click on the map or drag the marker to set your radar center location. Green circle shows <span id="map-radius-txt">50</span> km coverage.
      </div>
    </div>

    <!-- CARD 5: LOCATION & RADAR SETTINGS -->
    <div class="card">
      <h2>📍 Location & Radar Configuration</h2>
      
      <div style="margin-bottom: 12px;">
        <label>🔍 Search City, Town or Address:</label>
        <div style="display: flex; gap: 6px; margin-top: 4px;">
          <input type="text" id="inp-search-city" placeholder="e.g. London, Vienna, New York, Tokyo..." style="flex: 1;" onkeydown="if(event.key==='Enter'){event.preventDefault();searchCityLocation();}">
          <button type="button" onclick="searchCityLocation()" id="btn-search-city" style="background:#238636; border-color:#2ea043; white-space:nowrap; padding:6px 14px; font-weight:600;">🔍 Search</button>
        </div>
        <div id="city-search-results" style="display:none; margin-top:6px; background:#0d1117; border:1px solid #30363d; border-radius:6px; padding:6px;"></div>
      </div>

      <div style="margin-bottom: 12px;">
        <label>Quick City Presets:</label>
        <select onchange="onCityPreset(this)" style="margin-top: 4px;">
          <option value="">-- Choose city preset --</option>
          <option value="48.1486,17.1077">Bratislava (48.1486, 17.1077)</option>
          <option value="48.2082,16.3738">Vienna (48.2082, 16.3738)</option>
          <option value="50.0755,14.4378">Prague (50.0755, 14.4378)</option>
          <option value="47.4979,19.0402">Budapest (47.4979, 19.0402)</option>
          <option value="52.5200,13.4050">Berlin (52.5200, 13.4050)</option>
          <option value="51.5074,-0.1278">London (51.5074, -0.1278)</option>
          <option value="48.8566,2.3522">Paris (48.8566, 2.3522)</option>
          <option value="40.7128,-74.0060">New York (40.7128, -74.0060)</option>
          <option value="34.0522,-118.2437">Los Angeles (34.0522, -118.2437)</option>
          <option value="35.6762,139.6503">Tokyo (35.6762, 139.6503)</option>
          <option value="48.7363,19.1462">Banská Bystrica (48.7363, 19.1462)</option>
          <option value="48.7164,21.2611">Košice (48.7164, 21.2611)</option>
        </select>
      </div>

      <button type="button" onclick="useMyLocation()" id="btn-gps" style="background:#1f6feb; border-color:#58a6ff; width:100%; margin-bottom:12px; font-weight:600;">
        📍 Detect Location via GPS / Network
      </button>

      <form onsubmit="saveSettings(event)">
        <div class="form-group">
          <div><label>Latitude:</label><input type="text" name="lat" id="inp-lat" required oninput="userIsEditing=true"></div>
          <div><label>Longitude:</label><input type="text" name="lon" id="inp-lon" required oninput="userIsEditing=true"></div>
        </div>
        <div class="form-group">
          <div><label>Carousel Interval (s):</label><input type="number" name="car_int" id="inp-car" min="5" max="300" required oninput="userIsEditing=true"></div>
          <div><label>Timezone UTC Offset (hours):</label><input type="number" name="offset" id="inp-off" min="-12" max="14" required oninput="userIsEditing=true"></div>
        </div>
        <div class="form-group">
          <div>
            <label>Storm Alert Distance:</label>
            <select name="alert_km" id="inp-alert-km" onchange="userIsEditing=true">
              <option value="0">Disabled</option>
              <option value="5">5 km</option>
              <option value="10">10 km</option>
              <option value="15">15 km (Default)</option>
              <option value="20">20 km</option>
              <option value="25">25 km</option>
              <option value="30">30 km</option>
              <option value="50">50 km</option>
            </select>
          </div>
          <div>
            <label>Alert Sensitivity (Min Pixels):</label>
            <select name="alert_sens" id="inp-alert-sens" onchange="userIsEditing=true">
              <option value="10">High (10 px - Sensitive)</option>
              <option value="25">Medium (25 px - Recommended)</option>
              <option value="50">Low (50 px - Severe only)</option>
            </select>
          </div>
        </div>
        <div style="display: flex; gap: 8px; margin-top: 8px;">
          <input type="submit" id="btn-save-cfg" value="💾 Save Configuration" style="background:#238636; border-color:#2ea043; flex:1;">
          <button type="button" onclick="rebootEsp()" style="background:#da3633; border-color:#f85149;">🔄 Reboot</button>
        </div>
      </form>
    </div>

    <!-- CARD 6: CHANGE WI-FI -->
    <div class="card">
      <h2>📶 Wi-Fi Settings <button type="button" onclick="scanWifi()" id="btn-scan" style="font-size:0.8rem; padding:4px 10px;">🔍 Scan Networks</button></h2>
      <form onsubmit="changeWifi(event)">
        <div style="margin-bottom: 10px;">
          <label>Available Wi-Fi Networks:</label>
          <select id="wifi-select" onchange="onWifiSelect(this)" style="margin-bottom: 6px;">
            <option value="">-- Click Scan Networks or enter SSID manually below --</option>
          </select>
          <label>Network Name (SSID):</label>
          <input type="text" id="wifi-ssid" placeholder="e.g. MyHomeNetwork" required>
        </div>
        <div style="margin-bottom: 12px;">
          <label>Wi-Fi Password:</label>
          <input type="password" id="wifi-pass" placeholder="Password (leave empty for open networks)">
        </div>
        <input type="submit" id="btn-save-wifi" value="💾 Connect to Wi-Fi" style="background:#238636; border-color:#2ea043; width:100%;">
      </form>
    </div>

    <!-- CARD 7: OTA FIRMWARE UPDATE -->
    <div class="card">
      <h2>🚀 Firmware Update (OTA)</h2>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
        <div><b>Current Version:</b> <span id="ota-cur-ver" style="color:#58a6ff; font-weight:bold;">v2.0.0</span></div>
        <button type="button" onclick="checkOta()" id="btn-check-ota" style="font-size:0.8rem; padding:6px 12px; background:#1f6feb; border-color:#58a6ff;">🔍 Check GitHub</button>
      </div>

      <div id="ota-info-box" style="display:none; background:#0d1117; border:1px solid #30363d; border-radius:8px; padding:12px; margin-bottom:14px;">
        <div id="ota-status-text" style="font-weight:600; margin-bottom:6px;"></div>
        <div id="ota-release-notes" style="font-size:0.85rem; color:#8b949e; margin-bottom:10px; max-height:100px; overflow-y:auto; white-space:pre-wrap;"></div>
        <button type="button" onclick="startGithubOta()" id="btn-start-ota" style="background:#238636; border-color:#2ea043; width:100%; font-weight:bold; display:none;">
          ⬇️ Download & Update from GitHub
        </button>
      </div>

      <hr style="border:0; border-top:1px solid #30363d; margin:14px 0;">

      <label>📁 Manual OTA File Upload (.bin):</label>
      <div style="font-size:0.8rem; color:#f0883e; background:rgba(240,136,62,0.12); border:1px solid rgba(240,136,62,0.4); border-radius:6px; padding:10px; margin:6px 0 10px 0; line-height: 1.4;">
        ⚠️ <b>IMPORTANT NOTE FOR OTA FILES:</b><br>
        • Use only the standard <b><code>firmware.bin</code></b> application file for OTA uploads.<br>
        • <b>NEVER</b> upload <code>merged-firmware.bin</code> here! Merged binaries include the bootloader and partition table from 0x0 and are for USB flashing only.
      </div>
      <form id="upload-form" onsubmit="uploadLocalOta(event)">
        <input type="file" id="ota-file" accept=".bin" required style="margin-bottom:8px;">
        <input type="submit" id="btn-upload-ota" value="📁 Upload Firmware (.bin) to ESP" style="background:#30363d; border-color:#8b949e; width:100%;">
      </form>
    </div>
  </div>

  <script>
    let userIsEditing = false;

    function formatUptime(sec) {
      const d = Math.floor(sec / 86400);
      const h = Math.floor((sec % 86400) / 3600);
      const m = Math.floor((sec % 3600) / 60);
      const s = sec % 60;
      if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
      return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    }

    function rssiToPct(rssi) {
      if (rssi <= -100) return 0;
      if (rssi >= -50) return 100;
      return 2 * (rssi + 100);
    }

    async function loadData() {
      try {
        const res = await fetch('/api/status');
        const d = await res.json();

        // Precipitation Alert Banner
        if (d.rain_alert && d.precip_dist >= 0) {
          document.getElementById('rain-alert-banner').style.display = 'flex';
          document.getElementById('rain-alert-dist').innerText = d.precip_dist.toFixed(1);
          document.getElementById('rain-alert-dir').innerText = d.alert_dir || '--';
          document.getElementById('rain-alert-deg').innerText = (typeof d.alert_bearing === 'number' && d.alert_bearing >= 0) ? d.alert_bearing : '--';
          const alertTitle = document.getElementById('rain-alert-title');
          if (alertTitle) {
            alertTitle.innerText = d.alert_incoming ? '⛈️ WARNING: Approaching Storm (Wind direction)!' : '🌦️ NOTICE: Precipitation in monitored range';
          }
          if (document.getElementById('rain-alert-badge')) {
            document.getElementById('rain-alert-badge').innerText = (d.alert_incoming ? 'Incoming • ' : '') + '< ' + (d.alert_km || 15) + ' km';
          }
        } else {
          document.getElementById('rain-alert-banner').style.display = 'none';
        }

        // Radar Status
        document.getElementById('mode-val').innerText = d.mode === 0 ? '🛰️ Combined' : (d.mode === 1 ? '🌦️ Weather Only' : '✈️ Aircraft Only');
        document.getElementById('car-sub').innerText = d.car_en ? 'Carousel: ON (' + d.car_int + 's)' : 'Carousel: OFF';
        document.getElementById('zoom-val').innerText = d.radius + ' km';
        if (document.getElementById('radar-time-sub')) {
          document.getElementById('radar-time-sub').innerText = 'Radar: ' + (d.radar_time || '--:--');
        }
        document.getElementById('planes-count').innerText = d.planes ? d.planes.length : 0;
        document.getElementById('wifi-rssi').innerText = d.rssi + ' dBm';
        document.getElementById('wifi-pct').innerText = 'Quality: ' + rssiToPct(d.rssi) + ' % (' + (d.ssid || '') + ')';

        // Hardware Telemetry
        if (d.version) document.getElementById('ota-cur-ver').innerText = d.version;
        document.getElementById('sys-cpu').innerText = (d.cpu_mhz || 160) + ' MHz';
        document.getElementById('sys-temp').innerText = 'Temp: ' + (d.temp ? d.temp.toFixed(1) : '--') + ' °C';
        document.getElementById('sys-ram').innerText = d.heap_free + ' KB';
        const ramPct = Math.round((d.heap_free / (d.heap_total || 320)) * 100);
        document.getElementById('sys-ram-sub').innerText = 'Free ' + ramPct + ' % (of ' + (d.heap_total || 320) + ' KB)';
        document.getElementById('sys-flash').innerText = (d.flash_size || 4) + ' MB Flash';
        document.getElementById('sys-heap-min').innerText = 'Min RAM: ' + (d.heap_min || d.heap_free) + ' KB';
        document.getElementById('sys-uptime').innerText = formatUptime(d.uptime || 0);
        document.getElementById('sys-ip').innerText = 'IP: ' + (d.ip || '');

        // Zoom button active state
        for (let i = 0; i < 5; i++) {
          const btn = document.getElementById('zbtn-' + i);
          if (btn) btn.className = (d.zoom_idx === i) ? 'active' : '';
        }
        if (document.getElementById('mbtn-comb')) document.getElementById('mbtn-comb').className = (d.mode === 0) ? 'active' : '';
        document.getElementById('mbtn-weather').className = (d.mode === 1) ? 'active' : '';
        document.getElementById('mbtn-planes').className = (d.mode === 2) ? 'active' : '';
        document.getElementById('mbtn-car').innerText = d.car_en ? '🔄 Carousel: ON' : '⏸️ Carousel: OFF';

        // Form fields
        if (!userIsEditing && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'SELECT') {
          document.getElementById('inp-lat').value = (typeof d.lat === 'number') ? d.lat.toFixed(4) : d.lat;
          document.getElementById('inp-lon').value = (typeof d.lon === 'number') ? d.lon.toFixed(4) : d.lon;
          document.getElementById('inp-car').value = d.car_int;
          document.getElementById('inp-off').value = d.offset;
          if (document.getElementById('inp-alert-km') && d.alert_km !== undefined) {
            document.getElementById('inp-alert-km').value = d.alert_km;
          }
          if (document.getElementById('inp-alert-sens') && d.alert_sens !== undefined) {
            document.getElementById('inp-alert-sens').value = d.alert_sens;
          }
        }

        // Aircraft table
        const tbody = document.getElementById('planes-tbody');
        if (!d.planes || d.planes.length === 0) {
          tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:#8b949e;">No aircraft detected in ' + d.radius + ' km range</td></tr>';
        } else {
          let html = '';
          for (const p of d.planes) {
            const displayId = p.route ? '<span class="route">' + p.route + '</span> (' + p.cs + ')' : '<b>' + p.cs + '</b>';
            const kmh = Math.round(p.gs * 1.852);
            html += '<tr><td>' + displayId + '</td><td>' + (p.t || '-') + '</td><td class="speed">' + kmh + ' km/h</td><td class="alt">' + p.alt + '</td><td>' + p.lat.toFixed(3) + ', ' + p.lon.toFixed(3) + '</td></tr>';
          }
          tbody.innerHTML = html;
        }

        updateLeafletMap(d);
      } catch (e) {
        console.error(e);
      } finally {
        setTimeout(loadData, 3000);
      }
    }

    let mapInstance = null;
    let centerMarker = null;
    let radiusCircle = null;
    const planeMarkers = {};

    function updateLeafletMap(d) {
      if (typeof L === 'undefined') return;
      const lat = parseFloat(d.lat);
      const lon = parseFloat(d.lon);
      const radKm = parseFloat(d.radius);

      const radTxt = document.getElementById('map-radius-txt');
      if (radTxt) radTxt.innerText = radKm;

      if (!mapInstance) {
        mapInstance = L.map('map').setView([lat, lon], radKm <= 25 ? 10 : (radKm <= 50 ? 9 : (radKm <= 100 ? 8 : 7)));
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
          maxZoom: 18,
          attribution: '&copy; OpenStreetMap'
        }).addTo(mapInstance);

        centerMarker = L.marker([lat, lon], { draggable: true }).addTo(mapInstance);
        centerMarker.bindPopup('<b>Radar Center</b><br>Drag to change location');

        centerMarker.on('dragend', function(e) {
          const pos = e.target.getLatLng();
          document.getElementById('inp-lat').value = pos.lat.toFixed(4);
          document.getElementById('inp-lon').value = pos.lng.toFixed(4);
          userIsEditing = true;
          if (radiusCircle) radiusCircle.setLatLng(pos);
        });

        mapInstance.on('click', function(e) {
          centerMarker.setLatLng(e.latlng);
          document.getElementById('inp-lat').value = e.latlng.lat.toFixed(4);
          document.getElementById('inp-lon').value = e.latlng.lng.toFixed(4);
          userIsEditing = true;
          if (radiusCircle) radiusCircle.setLatLng(e.latlng);
        });

        radiusCircle = L.circle([lat, lon], {
          radius: radKm * 1000,
          color: '#3fb950',
          fillColor: '#3fb950',
          fillOpacity: 0.07,
          weight: 2
        }).addTo(mapInstance);
      } else {
        if (!userIsEditing) {
          centerMarker.setLatLng([lat, lon]);
          radiusCircle.setLatLng([lat, lon]);
        }
        radiusCircle.setRadius(radKm * 1000);
      }

      const currentKeys = {};
      if (d.planes && Array.isArray(d.planes)) {
        d.planes.forEach(p => {
          if (!p.lat || !p.lon) return;
          const key = p.cs || (p.lat + '_' + p.lon);
          currentKeys[key] = true;
          const isMil = p.mil;
          const isEmg = p.emg;
          const angle = p.trk || 0;

          const iconHtml = `<div style="transform: rotate(${angle}deg); font-size: 18px; line-height: 1;">✈️</div>`;
          const planeIcon = L.divIcon({ html: iconHtml, className: 'plane-icon', iconSize: [22, 22], iconAnchor: [11, 11] });

          const popupContent = `<b>${p.cs || 'NOCALL'}</b> ${p.t ? `(${p.t})` : ''}<br>` +
                               `${p.route ? `Route: <b>${p.route}</b><br>` : ''}` +
                               `Alt: ${p.alt || '--'} | Speed: ${p.gs ? Math.round(p.gs * 1.852) + ' km/h' : '--'}<br>` +
                               `${isEmg ? '<span style="color:#f85149; font-weight:bold;">🚨 EMERGENCY SQ ' + (p.sq || '') + '</span>' : ''}`;

          if (planeMarkers[key]) {
            planeMarkers[key].setLatLng([p.lat, p.lon]);
            planeMarkers[key].setIcon(planeIcon);
            planeMarkers[key].setPopupContent(popupContent);
          } else {
            planeMarkers[key] = L.marker([p.lat, p.lon], { icon: planeIcon }).addTo(mapInstance);
            planeMarkers[key].bindPopup(popupContent);
          }
        });
      }

      for (const k in planeMarkers) {
        if (!currentKeys[k]) {
          mapInstance.removeLayer(planeMarkers[k]);
          delete planeMarkers[k];
        }
      }
    }

    async function setZoom(idx) {
      await fetch('/api/set?zoom=' + idx);
      loadData();
    }
    async function setMode(m) {
      await fetch('/api/set?mode=' + m);
      loadData();
    }
    async function toggleCarousel() {
      await fetch('/api/set?toggle_car=1');
      loadData();
    }
    async function rebootEsp() {
      if (confirm('Reboot ESP-GlobalRadar?')) {
        await fetch('/api/reboot');
        alert('ESP is rebooting...');
      }
    }

    function applyCoords(lat, lon, src) {
      const fLat = parseFloat(lat).toFixed(4);
      const fLon = parseFloat(lon).toFixed(4);
      document.getElementById('inp-lat').value = fLat;
      document.getElementById('inp-lon').value = fLon;
      userIsEditing = true;
      if (centerMarker && radiusCircle) {
        const pos = [parseFloat(fLat), parseFloat(fLon)];
        centerMarker.setLatLng(pos);
        radiusCircle.setLatLng(pos);
        if (mapInstance) mapInstance.panTo(pos);
      }
      const btn = document.getElementById('btn-gps');
      if (btn) {
        const old = btn.innerText;
        btn.innerText = '✅ ' + (src || 'Selected') + ' (' + fLat + ', ' + fLon + ')';
        setTimeout(() => { btn.innerText = old; }, 3500);
      }
    }

    function onCityPreset(sel) {
      if (!sel.value) return;
      const parts = sel.value.split(',');
      applyCoords(parts[0], parts[1], sel.options[sel.selectedIndex].text.split('(')[0].trim());
    }

    async function saveSettings(e) {
      e.preventDefault();
      const btn = document.getElementById('btn-save-cfg');
      const old = btn.value;
      btn.value = '⏳ Saving...';
      btn.disabled = true;

      const lat = document.getElementById('inp-lat').value;
      const lon = document.getElementById('inp-lon').value;
      const car_int = document.getElementById('inp-car').value;
      const offset = document.getElementById('inp-off').value;
      const alert_km = document.getElementById('inp-alert-km').value;
      const alert_sens = document.getElementById('inp-alert-sens').value;

      try {
        await fetch('/api/save', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'lat=' + encodeURIComponent(lat) + 
                '&lon=' + encodeURIComponent(lon) + 
                '&car_int=' + encodeURIComponent(car_int) + 
                '&offset=' + encodeURIComponent(offset) +
                '&alert_km=' + encodeURIComponent(alert_km) +
                '&alert_sens=' + encodeURIComponent(alert_sens)
        });
        userIsEditing = false;
        btn.value = '✅ Saved!';
        setTimeout(() => { btn.value = old; btn.disabled = false; }, 2500);
        loadData();
      } catch (err) {
        alert('Save failed: ' + err);
        btn.value = old;
        btn.disabled = false;
      }
    }

    async function searchCityLocation() {
      const q = document.getElementById('inp-search-city').value.trim();
      if (!q) return;
      const btn = document.getElementById('btn-search-city');
      const resBox = document.getElementById('city-search-results');
      const old = btn.innerText;
      btn.innerText = '⏳ Searching...';
      btn.disabled = true;
      resBox.style.display = 'none';
      resBox.innerHTML = '';
      userIsEditing = true;

      try {
        const url = 'https://nominatim.openstreetmap.org/search?format=json&q=' + encodeURIComponent(q) + '&limit=5';
        const res = await fetch(url);
        const list = await res.json();

        if (list && list.length > 0) {
          if (list.length === 1) {
            applyCoords(list[0].lat, list[0].lon, list[0].display_name.split(',')[0]);
          } else {
            resBox.style.display = 'block';
            resBox.innerHTML = '<div style="font-size:0.8rem; color:#8b949e; margin-bottom:4px;">Select matching location:</div>';
            list.forEach(item => {
              const b = document.createElement('button');
              b.type = 'button';
              b.style.display = 'block';
              b.style.width = '100%';
              b.style.textAlign = 'left';
              b.style.marginBottom = '4px';
              b.style.fontSize = '0.8rem';
              b.style.padding = '4px 8px';
              b.innerText = '📍 ' + item.display_name.split(',').slice(0, 3).join(',');
              b.onclick = () => {
                applyCoords(item.lat, item.lon, item.display_name.split(',')[0]);
                resBox.style.display = 'none';
              };
              resBox.appendChild(b);
            });
          }
        } else {
          alert('Location "' + q + '" not found.');
        }
      } catch (err) {
        alert('Search error: ' + err);
      } finally {
        btn.innerText = old;
        btn.disabled = false;
      }
    }

    async function useMyLocation() {
      const btn = document.getElementById('btn-gps');
      const old = btn.innerText;
      btn.innerText = '⏳ Detecting location...';
      userIsEditing = true;

      if (navigator.geolocation) {
        try {
          navigator.geolocation.getCurrentPosition(
            (pos) => {
              applyCoords(pos.coords.latitude, pos.coords.longitude, 'GPS');
            },
            async (err) => {
              await fetchIpLocation();
            },
            { enableHighAccuracy: true, timeout: 6000 }
          );
          return;
        } catch (e) {}
      }
      await fetchIpLocation();

      async function fetchIpLocation() {
        btn.innerText = '⏳ Detecting location via IP...';
        try {
          const res = await fetch('https://ipwho.is/');
          const d = await res.json();
          if (d && d.success && d.latitude && d.longitude) {
            applyCoords(d.latitude, d.longitude, (d.city || 'IP Geo'));
            return;
          }
        } catch (e) {}

        try {
          const res2 = await fetch('https://freeipapi.com/api/json');
          const d2 = await res2.json();
          if (d2 && d2.latitude && d2.longitude) {
            applyCoords(d2.latitude, d2.longitude, (d2.cityName || 'IP Geo'));
            return;
          }
        } catch (e2) {}

        alert('Automatic location detection failed. Please search for your city above.');
        btn.innerText = old;
      }
    }

    async function scanWifi() {
      const btn = document.getElementById('btn-scan');
      const old = btn.innerText;
      btn.innerText = '⏳ Scanning...';
      try {
        const res = await fetch('/api/scan');
        const list = await res.json();
        const sel = document.getElementById('wifi-select');
        sel.innerHTML = '<option value="">-- Choose discovered network (' + list.length + ') --</option>';
        list.forEach(w => {
          const opt = document.createElement('option');
          opt.value = w.ssid;
          opt.innerText = w.ssid + ' (' + w.rssi + ' dBm' + (w.enc ? ' 🔒' : '') + ')';
          sel.appendChild(opt);
        });
        btn.innerText = '✅ Found: ' + list.length;
      } catch (e) {
        alert('WiFi scan error: ' + e);
        btn.innerText = old;
      }
      setTimeout(() => { btn.innerText = old; }, 3000);
    }

    function onWifiSelect(sel) {
      if (sel.value) {
        document.getElementById('wifi-ssid').value = sel.value;
        document.getElementById('wifi-pass').focus();
      }
    }

    async function changeWifi(e) {
      e.preventDefault();
      const ssid = document.getElementById('wifi-ssid').value;
      const pass = document.getElementById('wifi-pass').value;
      if (!confirm('Connect ESP to Wi-Fi network "' + ssid + '"?')) return;
      const btn = document.getElementById('btn-save-wifi');
      btn.disabled = true;
      btn.value = '⏳ Connecting to Wi-Fi...';
      try {
        await fetch('/api/wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass)
        });
        alert('Credentials sent. ESP is connecting to "' + ssid + '".\nCheck the display for the new IP address.');
      } catch (err) {
        alert('Failed to send Wi-Fi settings: ' + err);
        btn.disabled = false;
        btn.value = '💾 Connect to Wi-Fi';
      }
    }

    async function checkOta() {
      const btn = document.getElementById('btn-check-ota');
      const box = document.getElementById('ota-info-box');
      const statusTxt = document.getElementById('ota-status-text');
      const notes = document.getElementById('ota-release-notes');
      const startBtn = document.getElementById('btn-start-ota');
      
      btn.disabled = true;
      btn.innerText = '⏳ Checking...';
      box.style.display = 'block';
      statusTxt.innerText = 'Connecting to GitHub...';
      notes.innerText = '';
      startBtn.style.display = 'none';

      try {
        const res = await fetch('/api/ota/check');
        const d = await res.json();
        
        if (d.has_update) {
          statusTxt.innerHTML = '🎉 <span style="color:#2ea043;">New version available: ' + d.latest_version + '</span> (current: ' + d.current_version + ')';
          notes.innerText = d.notes || d.name || '';
          startBtn.style.display = 'block';
          startBtn.innerText = '🚀 Update to ' + d.latest_version;
        } else {
          statusTxt.innerHTML = '✅ <span style="color:#58a6ff;">Running latest version ' + d.current_version + '</span>';
          notes.innerText = d.name ? ('Latest release: ' + d.name) : '';
          startBtn.style.display = 'block';
          startBtn.innerText = '🔄 Reinstall ' + d.current_version + ' from GitHub';
        }
      } catch (err) {
        statusTxt.innerHTML = '❌ <span style="color:#f85149;">Check failed: ' + err + '</span>';
      } finally {
        btn.disabled = false;
        btn.innerText = '🔍 Check GitHub';
      }
    }

    async function startGithubOta() {
      if (!confirm('Start OTA update from GitHub?\nDo not power off the device during flashing!')) return;
      const startBtn = document.getElementById('btn-start-ota');
      startBtn.disabled = true;
      startBtn.innerText = '⏳ Downloading & installing... Watch ESP32 display';
      try {
        await fetch('/api/ota/github', { method: 'POST' });
        alert('OTA update started!\nESP32 will download firmware and restart.');
      } catch (e) {
        alert('Error starting OTA: ' + e);
        startBtn.disabled = false;
      }
    }

    async function uploadLocalOta(e) {
      e.preventDefault();
      const fileInp = document.getElementById('ota-file');
      if (!fileInp.files || fileInp.files.length === 0) return;
      if (!confirm('Upload firmware file "' + fileInp.files[0].name + '"?')) return;

      const btn = document.getElementById('btn-upload-ota');
      btn.disabled = true;
      btn.value = '⏳ Uploading firmware...';

      const formData = new FormData();
      formData.append('firmware', fileInp.files[0]);

      try {
        const res = await fetch('/api/ota/upload', {
          method: 'POST',
          body: formData
        });
        if (res.ok) {
          alert('Firmware uploaded successfully!\nESP32 is restarting...');
        } else {
          alert('Error uploading firmware file.');
          btn.disabled = false;
          btn.value = '📁 Upload Firmware (.bin) to ESP';
        }
      } catch (err) {
        alert('Upload failed: ' + err);
        btn.disabled = false;
        btn.value = '📁 Upload Firmware (.bin) to ESP';
      }
    }

    loadData();
  </script>
</body>
</html>
)rawliteral";

void handleWebRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleApiStatus() {
  JsonDocument doc;
  doc["version"] = CURRENT_VERSION;
  doc["mode"] = (int)currentMode;
  doc["radius"] = (int)currentRadiusKm;
  doc["zoom_idx"] = zoomIndex;
  doc["lat"] = centerLat;
  doc["lon"] = centerLon;
  doc["car_int"] = carouselIntervalSec;
  doc["car_en"] = carouselEnabled;
  doc["offset"] = timeOffsetHours;
  doc["radar_ts"] = radarTimestamp;
  doc["radar_time"] = getRadarTimeText(radarTimestamp);
  doc["rssi"] = WiFi.RSSI();
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["heap_free"] = (int)(ESP.getFreeHeap() / 1024);
  doc["heap_total"] = (int)(ESP.getHeapSize() / 1024);
  doc["heap_min"] = (int)(ESP.getMinFreeHeap() / 1024);
  doc["flash_size"] = (int)(ESP.getFlashChipSize() / (1024 * 1024));
  doc["cpu_mhz"] = getCpuFrequencyMhz();
  doc["temp"] = roundf(temperatureRead() * 10.0f) / 10.0f;
  doc["uptime"] = millis() / 1000;
  doc["rain_alert"] = rainAlertActive;
  doc["alert_incoming"] = isIncomingAlert;
  doc["precip_dist"] = (closestPrecipDistKm >= 0.0f) ? roundf(closestPrecipDistKm * 10.0f) / 10.0f : -1.0f;
  doc["alert_dir"] = alertBearingDir;
  doc["alert_bearing"] = (alertBearingDeg >= 0.0f) ? (int)roundf(alertBearingDeg) : -1;
  doc["alert_km"] = (int)rainAlertThresholdKm;
  doc["alert_sens"] = rainAlertMinPixels;
  doc["wind_dir_700"] = (windDir700hPa >= 0.0f) ? (int)roundf(windDir700hPa) : -1;
  doc["wind_spd_700"] = roundf(windSpeed700hPa * 10.0f) / 10.0f;
  doc["wind_dir_sfc"] = (windDirSurface >= 0.0f) ? (int)roundf(windDirSurface) : -1;
  doc["wind_spd_sfc"] = roundf(windSpeedSurface * 10.0f) / 10.0f;

  JsonArray pArr = doc["planes"].to<JsonArray>();
  for (size_t i = 0; i < aircraftCount; i++) {
    JsonObject obj = pArr.add<JsonObject>();
    obj["cs"] = aircraftList[i].callsign;
    if (aircraftList[i].route[0] != '\0') obj["route"] = aircraftList[i].route;
    obj["t"] = aircraftList[i].type;
    obj["gs"] = aircraftList[i].gs_knots;
    obj["alt"] = aircraftList[i].alt;
    obj["lat"] = aircraftList[i].lat;
    obj["lon"] = aircraftList[i].lon;
    obj["trk"] = (int)aircraftList[i].track;
    obj["mil"] = aircraftList[i].is_mil;
    obj["emg"] = aircraftList[i].is_emergency;
    obj["sq"] = aircraftList[i].squawk;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  server.send(200, "application/json", jsonStr);
}

void handleApiSet() {
  if (server.hasArg("zoom")) {
    setZoomIndex(server.arg("zoom").toInt());
  }
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "combined" || m == "0") setAppMode(MODE_COMBINED, true);
    else if (m == "weather" || m == "1") setAppMode(MODE_WEATHER, true);
    else if (m == "planes" || m == "2") setAppMode(MODE_PLANES, true);
  }
  if (server.hasArg("toggle_car")) {
    carouselEnabled = !carouselEnabled;
    lastCarouselSwitchMs = millis();
    prefs.begin("radar", false);
    prefs.putBool("car_en", carouselEnabled);
    prefs.end();
  }
  if (server.hasArg("car_en")) {
    String val = server.arg("car_en");
    carouselEnabled = (val == "1" || val == "true");
    lastCarouselSwitchMs = millis();
    prefs.begin("radar", false);
    prefs.putBool("car_en", carouselEnabled);
    prefs.end();
  }
  if (server.hasArg("alert_km")) {
    rainAlertThresholdKm = server.arg("alert_km").toFloat();
    prefs.begin("radar", false);
    prefs.putFloat("alert_km", rainAlertThresholdKm);
    prefs.end();
  }
  if (server.hasArg("alert_sens")) {
    rainAlertMinPixels = server.arg("alert_sens").toInt();
    prefs.begin("radar", false);
    prefs.putInt("alert_sens", rainAlertMinPixels);
    prefs.end();
  }
  server.send(200, "text/plain", "OK");
}

void handleApiSave() {
  if (server.hasArg("lat") && server.hasArg("lon")) {
    centerLat = server.arg("lat").toFloat();
    centerLon = server.arg("lon").toFloat();
    if (server.hasArg("car_int")) carouselIntervalSec = constrain(server.arg("car_int").toInt(), 5, 300);
    if (server.hasArg("offset")) timeOffsetHours = server.arg("offset").toInt();
    if (server.hasArg("alert_km")) rainAlertThresholdKm = server.arg("alert_km").toFloat();
    if (server.hasArg("alert_sens")) rainAlertMinPixels = server.arg("alert_sens").toInt();

    carouselIntervalMs = (uint32_t)carouselIntervalSec * 1000;

    prefs.begin("radar", false);
    prefs.putFloat("lat", centerLat);
    prefs.putFloat("lon", centerLon);
    prefs.putInt("car_int", carouselIntervalSec);
    prefs.putInt("offset", timeOffsetHours);
    prefs.putFloat("alert_km", rainAlertThresholdKm);
    prefs.putInt("alert_sens", rainAlertMinPixels);
    prefs.end();

    configTime(timeOffsetHours * 3600, 0, "pool.ntp.org", "time.nist.gov");
    fetchWindData();
    
    if (currentMode == MODE_WEATHER || currentMode == MODE_COMBINED) {
      downloadLatestRadar();
    }
    if (currentMode == MODE_PLANES || currentMode == MODE_COMBINED) {
      lastPlaneFetchMs = millis();
      fetchPlanesData();
    }
    renderScreen();
  }
  server.send(200, "text/plain", "OK");
}

void handleApiScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["enc"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  String jsonStr;
  serializeJson(doc, jsonStr);
  server.send(200, "application/json", jsonStr);
  WiFi.scanDelete();
}

void handleApiWifi() {
  if (server.hasArg("ssid")) {
    String newSsid = server.arg("ssid");
    String newPass = server.hasArg("pass") ? server.arg("pass") : "";
    server.send(200, "text/plain", "OK");
    delay(500);

    showStatus("Connecting to:\n" + newSsid);
    WiFi.disconnect(true);
    delay(300);
    WiFi.begin(newSsid.c_str(), newPass.c_str());

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
      delay(300);
    }
  }
}

void handleApiReboot() {
  server.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

void handleApiOtaCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"WiFi not connected\"}");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(4000);
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("ESP-GlobalRadar");

  JsonDocument doc;
  doc["current_version"] = CURRENT_VERSION;
  doc["has_update"] = false;
  doc["latest_version"] = CURRENT_VERSION;
  doc["name"] = "";
  doc["notes"] = "";

  if (http.begin(client, "https://api.github.com/repos/hackra76/ESP-GlobalRadar/releases/latest")) {
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      JsonDocument filter;
      filter["tag_name"] = true;
      filter["name"] = true;
      filter["body"] = true;

      JsonDocument ghDoc;
      DeserializationError err = deserializeJson(ghDoc, http.getStream(), DeserializationOption::Filter(filter));
      if (!err) {
        String tag = ghDoc["tag_name"] | "";
        String name = ghDoc["name"] | "";
        String body = ghDoc["body"] | "";

        doc["latest_version"] = tag;
        doc["name"] = name;
        doc["notes"] = body;
        doc["has_update"] = (tag.length() > 0 && tag != String(CURRENT_VERSION));
      }
    }
    http.end();
  }
  client.stop();

  String jsonStr;
  serializeJson(doc, jsonStr);
  server.send(200, "application/json", jsonStr);
}

void handleApiOtaGithub() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json", "{\"error\":\"WiFi not connected\"}");
    return;
  }

  server.send(200, "application/json", "{\"status\":\"starting\"}");
  delay(300);

  releaseCanvas();
  showStatus("OTA Update...\nConnecting GitHub...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(12000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(35000);
  http.setUserAgent("ESP-GlobalRadar");

  String url = "https://github.com/hackra76/ESP-GlobalRadar/releases/latest/download/firmware.bin";

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      int totalLen = http.getSize();
      WiFiClient* stream = http.getStreamPtr();

      if (Update.begin(totalLen > 0 ? totalLen : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        size_t written = 0;
        uint8_t buff[1024];
        int lastPct = -1;

        while (http.connected() && (written < (size_t)totalLen || totalLen <= 0)) {
          size_t avail = stream->available();
          if (avail) {
            size_t toRead = (avail < sizeof(buff)) ? avail : sizeof(buff);
            int n = stream->readBytes(buff, toRead);
            if (n > 0) {
              Update.write(buff, n);
              written += n;

              if (totalLen > 0) {
                int pct = (int)((written * 100) / totalLen);
                if (pct != lastPct && pct % 5 == 0) {
                  lastPct = pct;
                  showStatus("OTA Update...\nProgress: " + String(pct) + " %");
                }
              }
            }
          } else {
            delay(1);
          }
          if (totalLen > 0 && written >= (size_t)totalLen) break;
          if (stream->available() == 0 && !http.connected()) break;
        }

        if (Update.end(true)) {
          showStatus("OTA Complete!\nRebooting...");
          delay(1500);
          ESP.restart();
          return;
        } else {
          showStatus("OTA Write Error:\n" + String(Update.errorString()));
          delay(3000);
        }
      } else {
        showStatus("OTA Init Error");
        delay(3000);
      }
    } else {
      showStatus("Download Error\nHTTP: " + String(httpCode));
      delay(3000);
    }
    http.end();
  }
  client.stop();
}

void handleApiOtaUploadLoop() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    releaseCanvas();
    showStatus("Manual OTA...\nPreparing...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      showStatus("OTA Start Error!");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      showStatus("OTA Write Error!");
    } else {
      static int lastUploadPct = -1;
      int pct = (upload.totalSize > 0) ? (int)((upload.currentSize * 100) / upload.totalSize) : 0;
      if (pct != lastUploadPct && pct % 10 == 0) {
        lastUploadPct = pct;
        showStatus("Manual OTA...\n" + String(pct) + " %");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      showStatus("OTA Complete!\nRebooting...");
    } else {
      showStatus("OTA End Error!");
    }
  }
}

void handleApiOtaUploadDone() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(500, "text/plain", "OTA Update Failed");
  } else {
    server.send(200, "text/plain", "OK - Rebooting ESP...");
    delay(1000);
    ESP.restart();
  }
}

void setupWebServer() {
  if (MDNS.begin("espglobalradar")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] Responder started: http://espglobalradar.local");
  }
  server.on("/", HTTP_GET, handleWebRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/set", HTTP_GET, handleApiSet);
  server.on("/api/save", HTTP_POST, handleApiSave);
  server.on("/api/scan", HTTP_GET, handleApiScan);
  server.on("/api/wifi", HTTP_POST, handleApiWifi);
  server.on("/api/reboot", HTTP_GET, handleApiReboot);
  server.on("/api/ota/check", HTTP_GET, handleApiOtaCheck);
  server.on("/api/ota/github", HTTP_POST, handleApiOtaGithub);
  server.on("/api/ota/upload", HTTP_POST, handleApiOtaUploadDone, handleApiOtaUploadLoop);
  server.begin();
  Serial.println("[HTTP] Embedded web server started on port 80");
}


// =======================================================================================
// 5. RAINVIEWER GLOBAL WEATHER RADAR (FETCH & TILE DECODING)
// =======================================================================================

bool downloadLatestRadar() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[RADAR] WiFi not connected, skipping radar download.");
    return false;
  }

  releaseCanvas(); // Release 58 KB RAM for safe TLS handshake

  Serial.println("[RADAR] Checking latest image from RainViewer API (HTTPS)...");

  bool metaSuccess = false;
  String newHost = rainViewerHost;
  String newPath = "";
  uint32_t newTime = 0;

  for (int attempt = 1; attempt <= 2 && !metaSuccess; attempt++) {
    WiFiClientSecure client; 
    client.setInsecure();
    client.setHandshakeTimeout(8000);
    HTTPClient http; 
    http.setTimeout(12000);
    http.setUserAgent("ESP-GlobalRadar/2.0");

    if (http.begin(client, RAINVIEWER_API_URL)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["host"] = true;
        filter["radar"]["past"][0]["time"] = true;
        filter["radar"]["past"][0]["path"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (!err) {
          const char* h = doc["host"] | RAINVIEWER_DEFAULT_HOST;
          newHost = String(h);
          JsonArray past = doc["radar"]["past"].as<JsonArray>();
          if (past.size() > 0) {
            JsonObject latest = past[past.size() - 1];
            newTime = latest["time"] | 0;
            newPath = latest["path"] | "";
            metaSuccess = true;
            Serial.printf("[RADAR] RainViewer frame: ts=%u, path=%s\n", newTime, newPath.c_str());
          }
        } else {
          Serial.printf("[RADAR] RainViewer JSON parse error: %s\n", err.c_str());
        }
      } else {
        Serial.printf("[RADAR] HTTP GET failed, code: %d (attempt %d)\n", httpCode, attempt);
      }
      http.end();
      client.stop();
    }
    if (!metaSuccess && attempt < 2) delay(1000);
  }

  if (!metaSuccess) {
    ensureCanvas();
    return false;
  }

  rainViewerHost = newHost;
  radarPath = newPath;
  radarTimestamp = newTime;

  int z = ZOOM_LEVELS_Z[zoomIndex];
  if (z > RAINVIEWER_MAX_ZOOM) z = RAINVIEWER_MAX_ZOOM;

  double n = (double)(1 << z);
  double wx = (centerLon + 180.0) / 360.0 * n;
  double latRad = (double)centerLat * DEG_TO_RAD;
  double wy = (1.0 - asinh(tan(latRad)) / M_PI) / 2.0 * n;

  currentTileX = (int)floor(wx);
  currentTileY = (int)floor(wy);
  currentTileOffsetPx = (float)((wx - (double)currentTileX) * 256.0);
  currentTileOffsetPy = (float)((wy - (double)currentTileY) * 256.0);

  String tileUrl = rainViewerHost + radarPath + "/256/" + String(z) + "/" + String(currentTileX) + "/" + String(currentTileY) + "/" + String(RAINVIEWER_COLOR_SCHEME) + "/1_1.png";
  Serial.printf("[RADAR] Downloading tile (z=%d, x=%d, y=%d, px=%.1f, py=%.1f): %s\n", 
                z, currentTileX, currentTileY, currentTileOffsetPx, currentTileOffsetPy, tileUrl.c_str());

  bool dlSuccess = false;
  for (int attempt = 1; attempt <= 2 && !dlSuccess; attempt++) {
    WiFiClientSecure clientImg;
    clientImg.setInsecure();
    clientImg.setHandshakeTimeout(10000);
    HTTPClient httpImg;
    httpImg.setTimeout(18000);
    httpImg.setUserAgent("ESP-GlobalRadar/2.0");

    if (httpImg.begin(clientImg, tileUrl)) {
      int imgCode = httpImg.GET();
      if (imgCode == HTTP_CODE_OK) {
        if (SPIFFS.exists(RADAR_FILE)) SPIFFS.remove(RADAR_FILE);
        File f = SPIFFS.open(RADAR_FILE, "w");
        if (!f) {
          SPIFFS.format();
          SPIFFS.begin(true);
          f = SPIFFS.open(RADAR_FILE, "w");
        }
        if (f) {
          httpImg.writeToStream(&f);
          size_t sz = f.size();
          f.close();
          prefs.begin("radar", false);
          prefs.putUInt("last_ts", radarTimestamp);
          prefs.putString("last_path", radarPath);
          prefs.end();
          Serial.printf("[RADAR] Tile saved to SPIFFS (%u B)!\n", (unsigned int)sz);
          dlSuccess = true;
        }
      } else {
        Serial.printf("[RADAR] HTTP tile download error: %d\n", imgCode);
      }
      httpImg.end();
      clientImg.stop();
    }
    if (!dlSuccess && attempt < 2) delay(1000);
  }

  if (dlSuccess) {
    decodeRadarImage();
  }
  ensureCanvas();
  return dlSuccess;
}

/**
 * Fetch wind direction and speed from Open-Meteo API
 */
void fetchWindData() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (ESP.getFreeHeap() < 30000) return;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);

  char url[220];
  snprintf(url, sizeof(url), "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=wind_speed_10m,wind_direction_10m&hourly=wind_speed_700hPa,wind_direction_700hPa&forecast_hours=1", centerLat, centerLon);

  if (http.begin(client, url)) {
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      JsonDocument filter;
      filter["hourly"]["wind_direction_700hPa"][0] = true;
      filter["hourly"]["wind_speed_700hPa"][0] = true;
      filter["current"]["wind_direction_10m"] = true;
      filter["current"]["wind_speed_10m"] = true;

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
      if (!err) {
        if (doc["hourly"]["wind_direction_700hPa"].is<JsonArray>() && doc["hourly"]["wind_direction_700hPa"].size() > 0) {
          windDir700hPa = doc["hourly"]["wind_direction_700hPa"][0] | -1.0f;
          windSpeed700hPa = doc["hourly"]["wind_speed_700hPa"][0] | 0.0f;
        }
        if (doc["current"]["wind_direction_10m"].is<float>() || doc["current"]["wind_direction_10m"].is<int>()) {
          windDirSurface = doc["current"]["wind_direction_10m"] | -1.0f;
          windSpeedSurface = doc["current"]["wind_speed_10m"] | 0.0f;
        }
        Serial.printf("[WIND] Open-Meteo wind updated: 700hPa = %.0f deg (%.1f km/h), Surface = %.0f deg (%.1f km/h)\n",
                      windDir700hPa, windSpeed700hPa, windDirSurface, windSpeedSurface);
      } else {
        Serial.printf("[WIND] Open-Meteo JSON parse error: %s\n", err.c_str());
      }
    } else {
      Serial.printf("[WIND] Open-Meteo HTTP GET failed, code: %d\n", code);
    }
    http.end();
  }
  client.stop();
  lastWindFetchMs = millis();
}


// =======================================================================================
// 6. ADS-B AIRCRAFT TRACKER & ROUTE LOOKUP
// =======================================================================================

static constexpr int kAircraftNoseLenPx = 8;
static constexpr int kAircraftTailLenPx = 3;
static constexpr int kAircraftTailHalfPx = 4;
static constexpr float kVrateThresholdFpm = 128.0f;
static constexpr int kVrateArrowW = 8;
static constexpr int kVrateArrowH = 8;
static constexpr int kVrateArrowGapPx = 3;
static constexpr int kTypeSpeedGapPx = 4;
static constexpr int kAircraftLabelGapPx = 3;

// ---- Static VRS Route Lookup ----
static constexpr char kRouteBaseUrl[] = "https://vrs-standing-data.adsb.lol/routes/";
static constexpr size_t kRouteCacheSize = 48;

struct RouteCacheEntry {
  char callsign[9];
  char route[10];
  bool used;
};
static RouteCacheEntry s_route_cache[kRouteCacheSize] = {};
static size_t s_route_cache_next = 0;

static const char* routeCacheFind(const char* callsign) {
  for (size_t i = 0; i < kRouteCacheSize; ++i) {
    if (s_route_cache[i].used && strcmp(s_route_cache[i].callsign, callsign) == 0) {
      return s_route_cache[i].route;
    }
  }
  return nullptr;
}

static void routeCachePut(const char* callsign, const char* route) {
  RouteCacheEntry& e = s_route_cache[s_route_cache_next];
  s_route_cache_next = (s_route_cache_next + 1) % kRouteCacheSize;
  e.used = true;
  strlcpy(e.callsign, callsign, sizeof(e.callsign));
  strlcpy(e.route, route, sizeof(e.route));
}

static void buildRouteDisplay(const char* codes, char* out, size_t out_len) {
  out[0] = '\0';
  if (codes == nullptr || strcmp(codes, "unknown") == 0) return;
  const char* dash = strchr(codes, '-');
  if (dash == nullptr) return;
  const char* last = strrchr(codes, '-') + 1;
  size_t first_len = (size_t)(dash - codes);
  if (first_len > 4) first_len = 4;
  size_t last_len = strnlen(last, 4);
  if (first_len + 1 + last_len + 1 > out_len) return;
  memcpy(out, codes, first_len);
  out[first_len] = '>';
  memcpy(out + first_len + 1, last, last_len);
  out[first_len + 1 + last_len] = '\0';
}

static void fetchRouteForCallsign(const char* cs, char* out_route, size_t out_len) {
  out_route[0] = '\0';
  if (cs[0] == '\0' || strlen(cs) < 3) return;

  const char* cached = routeCacheFind(cs);
  if (cached != nullptr) {
    strlcpy(out_route, cached, out_len);
    return;
  }

  if (ESP.getFreeHeap() < 35000) return;

  char url[96];
  snprintf(url, sizeof(url), "%s%c%c/%s.json", kRouteBaseUrl, cs[0], cs[1], cs);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3000);
  HTTPClient http;
  http.setTimeout(3000);

  if (http.begin(client, url)) {
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      String resp = http.getString();
      JsonDocument filter;
      filter["_airport_codes_iata"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
        const char* codes = doc["_airport_codes_iata"] | "";
        if (strlen(codes) > 0) {
          buildRouteDisplay(codes, out_route, out_len);
          routeCachePut(cs, out_route);
        } else {
          routeCachePut(cs, "");
        }
      }
    } else if (code == HTTP_CODE_NOT_FOUND) {
      routeCachePut(cs, "");
    }
    http.end();
  }
  client.stop();
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) return 0;
  int len = (int)(gs_knots / 60.0f);
  return constrain(len, 3, 8);
}

void drawAircraftSymbol(LovyanGFX& target, int x, int y, float heading_deg, float track_deg, float gs_knots, bool is_mil, bool is_emergency) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad_h = heading_deg * kDegToRad;
  const float sin_h = sinf(rad_h);
  const float cos_h = cosf(rad_h);

  const int tip_x = x + (int)roundf(sin_h * (float)kAircraftNoseLenPx);
  const int tip_y = y - (int)roundf(cos_h * (float)kAircraftNoseLenPx);

  const int base_x = x - (int)roundf(sin_h * (float)kAircraftTailLenPx);
  const int base_y = y + (int)roundf(cos_h * (float)kAircraftTailLenPx);

  const int wing_x = (int)roundf(cos_h * (float)kAircraftTailHalfPx);
  const int wing_y = (int)roundf(sin_h * (float)kAircraftTailHalfPx);

  const int len = speedLineLengthPx(gs_knots);
  if (len > 0) {
    const float rad_t = track_deg * kDegToRad;
    const int ex = tip_x + (int)roundf(sinf(rad_t) * (float)len);
    const int ey = tip_y - (int)roundf(cosf(rad_t) * (float)len);
    target.drawLine(tip_x, tip_y, ex, ey, target.color565(180, 205, 230));
  }

  uint16_t symbolColor = target.color565(0, 130, 255);
  if (is_emergency) {
    symbolColor = (millis() % 600 < 300) ? target.color565(255, 30, 30) : target.color565(255, 255, 0);
  } else if (is_mil) {
    symbolColor = target.color565(255, 40, 40);
  }
  target.fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y, base_x - wing_x, base_y - wing_y, symbolColor);
}

const char* tagTopLine(const AircraftData& ac) {
  if (ac.route[0] != '\0') return ac.route;
  return ac.callsign;
}

void formatTypePart(const AircraftData& ac, char* out, size_t out_len) {
  const int kmh = (int)lroundf(ac.gs_knots * 1.852f);
  if (ac.type[0] != '\0' && kmh > 0) {
    snprintf(out, out_len, "%s,", ac.type);
  } else if (ac.type[0] != '\0') {
    snprintf(out, out_len, "%s", ac.type);
  } else {
    out[0] = '\0';
  }
}

void formatSpeedPart(const AircraftData& ac, char* out, size_t out_len) {
  const int kmh = (int)lroundf(ac.gs_knots * 1.852f);
  if (kmh > 0) {
    snprintf(out, out_len, "%d", kmh);
  } else {
    out[0] = '\0';
  }
}

int vrateDirection(const AircraftData& ac) {
  if (isnan(ac.vrate_fpm)) return 0;
  if (ac.vrate_fpm >= kVrateThresholdFpm) return 1;
  if (ac.vrate_fpm <= -kVrateThresholdFpm) return -1;
  return 0;
}

void drawVRateArrow(LovyanGFX& target, int x, int ly, int line_h, int dir) {
  const int ty = ly + (line_h - kVrateArrowH) / 2;
  if (dir > 0) {
    target.fillTriangle(x + kVrateArrowW / 2, ty, x, ty + kVrateArrowH, x + kVrateArrowW, ty + kVrateArrowH, target.color565(30, 220, 30));
  } else if (dir < 0) {
    target.fillTriangle(x + kVrateArrowW / 2, ty + kVrateArrowH, x, ty, x + kVrateArrowW, ty, target.color565(235, 40, 40));
  }
}

int measureTagBlockWidth(LovyanGFX& target, const AircraftData& ac) {
  target.setTextSize(0.80f);
  int max_w = 0;
  const char* top = tagTopLine(ac);
  if (top[0] != '\0') {
    int w = target.textWidth(top);
    if (w > max_w) max_w = w;
  }
  char type_part[12];
  char speed_part[10];
  formatTypePart(ac, type_part, sizeof(type_part));
  formatSpeedPart(ac, speed_part, sizeof(speed_part));
  int w2 = 0;
  if (type_part[0] != '\0') w2 += target.textWidth(type_part);
  if (speed_part[0] != '\0') {
    if (w2 > 0) w2 += kTypeSpeedGapPx;
    w2 += target.textWidth(speed_part);
  }
  if (w2 > max_w) max_w = w2;

  if (ac.alt[0] != '\0') {
    int w3 = target.textWidth(ac.alt);
    if (vrateDirection(ac) != 0) w3 += kVrateArrowGapPx + kVrateArrowW;
    if (w3 > max_w) max_w = w3;
  }
  return max_w;
}

void drawAircraftTag(LovyanGFX& target, int x, int y, const AircraftData& ac) {
  target.setTextSize(0.80f);
  const int line_h = target.fontHeight();
  const int block_w = measureTagBlockWidth(target, ac);
  const int block_h = line_h * 3;
  int ly = y - block_h / 2;

  const int symbol_half = kAircraftNoseLenPx + kAircraftTailHalfPx;
  const bool tag_on_right = x < (TFT_W / 2);
  int anchor_x = 0;

  if (tag_on_right) {
    anchor_x = x + symbol_half + kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, TFT_W - block_w - 2);
    target.setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 2);
    target.setTextDatum(textdatum_t::top_right);
  }
  ly = constrain(ly, 2, TFT_H - block_h - 2);

  int box_x = tag_on_right ? (anchor_x - 3) : (anchor_x - block_w - 3);
  int box_y = ly - 1;
  int box_w = block_w + 6;
  int box_h = block_h + 2;
  uint16_t borderColor = ac.is_emergency ? ((millis() % 600 < 300) ? target.color565(255, 30, 30) : target.color565(255, 255, 0)) : (ac.is_mil ? target.color565(180, 40, 40) : target.color565(35, 65, 100));
  target.fillRoundRect(box_x, box_y, box_w, box_h, 3, target.color565(12, 16, 22));
  target.drawRoundRect(box_x, box_y, box_w, box_h, 3, borderColor);

  // Line 1: Route or Callsign
  const char* top = tagTopLine(ac);
  if (top[0] != '\0') {
    uint16_t col = ac.is_mil ? target.color565(255, 60, 60) : (ac.route[0] != '\0' ? target.color565(255, 130, 255) : TFT_WHITE);
    if (ac.is_emergency) {
      col = (millis() % 600 < 300) ? target.color565(255, 30, 30) : target.color565(255, 255, 0);
    }
    target.setTextColor(col, target.color565(12, 16, 22));
    if (ac.is_emergency) {
      char emStr[24];
      snprintf(emStr, sizeof(emStr), "🚨%s %s", ac.squawk, top);
      target.drawString(emStr, anchor_x, ly);
    } else {
      target.drawString(top, anchor_x, ly);
    }
  }
  ly += line_h;

  // Line 2: Type & Speed
  char type_part[12];
  char speed_part[10];
  formatTypePart(ac, type_part, sizeof(type_part));
  formatSpeedPart(ac, speed_part, sizeof(speed_part));
  const int w_type = (type_part[0] != '\0') ? target.textWidth(type_part) : 0;
  const int w_speed = (speed_part[0] != '\0') ? target.textWidth(speed_part) : 0;

  if (tag_on_right) {
    if (type_part[0] != '\0') {
      target.setTextColor(target.color565(90, 200, 255), target.color565(12, 16, 22));
      target.drawString(type_part, anchor_x, ly);
    }
    if (speed_part[0] != '\0') {
      target.setTextColor(target.color565(150, 235, 150), target.color565(12, 16, 22));
      target.drawString(speed_part, anchor_x + (type_part[0] != '\0' ? w_type + kTypeSpeedGapPx : 0), ly);
    }
  } else {
    if (speed_part[0] != '\0') {
      target.setTextColor(target.color565(150, 235, 150), target.color565(12, 16, 22));
      target.drawString(speed_part, anchor_x, ly);
    }
    if (type_part[0] != '\0') {
      target.setTextColor(target.color565(90, 200, 255), target.color565(12, 16, 22));
      target.drawString(type_part, anchor_x - (speed_part[0] != '\0' ? w_speed + kTypeSpeedGapPx : 0), ly);
    }
  }
  ly += line_h;

  // Line 3: Altitude & Climb/Descent Arrow
  if (ac.alt[0] != '\0') {
    target.setTextColor(target.color565(255, 255, 0), target.color565(12, 16, 22));
    target.drawString(ac.alt, anchor_x, ly);
    const int dir = vrateDirection(ac);
    if (dir != 0) {
      const int w_alt = target.textWidth(ac.alt);
      int ax = tag_on_right ? (anchor_x + w_alt + kVrateArrowGapPx) : (anchor_x - w_alt - kVrateArrowGapPx - kVrateArrowW);
      drawVRateArrow(target, ax, ly, line_h, dir);
    }
  }
}

void drawEdgeIndicator(LovyanGFX& target, float lat, float lon, bool is_mil) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  float sx = gpsToScreenX(lat, lon);
  float sy = gpsToScreenY(lat, lon);
  float dx = sx - (float)cx;
  float dy = sy - (float)cy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 1.0f) return;

  float angle = atan2f(dy, dx);
  int edgeX = cx + (int)roundf(cosf(angle) * 114.0f);
  int edgeY = cy + (int)roundf(sinf(angle) * 114.0f);
  uint16_t dotColor = is_mil ? target.color565(255, 40, 40) : target.color565(255, 150, 0);

  target.fillCircle(edgeX, edgeY, 3, dotColor);
  target.drawCircle(edgeX, edgeY, 3, TFT_BLACK);
}

class BufferedStream : public Stream {
  Stream* _s;
  uint32_t _timeoutMs;
public:
  BufferedStream(Stream* s, uint32_t timeoutMs = 8000) : _s(s), _timeoutMs(timeoutMs) {}
  int available() override { return _s ? _s->available() : 0; }
  int read() override {
    if (!_s) return -1;
    uint32_t start = millis();
    while (!_s->available()) {
      if (millis() - start >= _timeoutMs) return -1;
      delay(1);
    }
    return _s->read();
  }
  int peek() override {
    if (!_s) return -1;
    uint32_t start = millis();
    while (!_s->available()) {
      if (millis() - start >= _timeoutMs) return -1;
      delay(1);
    }
    return _s->peek();
  }
  void flush() override { if (_s) _s->flush(); }
  size_t write(uint8_t c) override { return _s ? _s->write(c) : 0; }
};

void fetchPlanesData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ADS-B] WiFi not connected, skipping aircraft fetch.");
    return;
  }

  releaseCanvas();

  float fetchRadiusKm = currentRadiusKm;
  if (fetchRadiusKm >= 250.0f) fetchRadiusKm = 200.0f;
  float radiusNm = fetchRadiusKm / 1.852f;
  String url = "https://opendata.adsb.fi/api/v3/lat/" + String(centerLat, 4) + "/lon/" + String(centerLon, 4) + "/dist/" + String(radiusNm, 1);

  Serial.printf("[ADS-B] Fetching aircraft (HTTPS, center: %.4f, %.4f, radius: %.0f km)...\n", centerLat, centerLon, currentRadiusKm);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(12000);

  HTTPClient http;
  http.setTimeout(20000);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      JsonDocument filter;
      filter["ac"][0]["lat"] = true;
      filter["ac"][0]["lon"] = true;
      filter["ac"][0]["track"] = true;
      filter["ac"][0]["true_heading"] = true;
      filter["ac"][0]["gs"] = true;
      filter["ac"][0]["flight"] = true;
      filter["ac"][0]["t"] = true;
      filter["ac"][0]["alt_baro"] = true;
      filter["ac"][0]["baro_rate"] = true;
      filter["ac"][0]["dbFlags"] = true;
      filter["ac"][0]["squawk"] = true;
      filter["ac"][0]["emergency"] = true;

      JsonDocument doc;
      BufferedStream bStream(http.getStreamPtr(), 10000);
      DeserializationError err = deserializeJson(doc, bStream, DeserializationOption::Filter(filter));
      
      http.end();
      client.stop();

      if (err) {
        Serial.printf("[ADS-B] JSON parse error: %s\n", err.c_str());
      } else {
        JsonArray acList = doc["ac"].as<JsonArray>();

        struct Cand {
          float distKm;
          uint16_t idx;
          uint8_t prio;
        };

        Cand cands[64];
        size_t candCount = 0;
        float cosCenterLat = cosf(centerLat * DEG_TO_RAD);

        for (size_t i = 0; i < acList.size() && candCount < 64; i++) {
          JsonObject plane = acList[i];
          float lat = plane["lat"].as<float>();
          float lon = plane["lon"].as<float>();
          if (lat == 0.0f && lon == 0.0f) continue;

          bool isGround = (plane["alt_baro"].is<const char*>() && strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0);
          if (currentRadiusKm >= 100.0f && isGround) continue;

          int dbFlags = plane["dbFlags"] | 0;
          bool is_mil = (dbFlags & 1) != 0;
          const char* sq = plane["squawk"] | "";
          const char* emg = plane["emergency"] | "";
          bool is_emg = (strcmp(sq, "7700") == 0 || strcmp(sq, "7600") == 0 || strcmp(sq, "7500") == 0 ||
                         (emg[0] != '\0' && strcmp(emg, "none") != 0));

          float dLatKm = (lat - centerLat) * 111.32f;
          float dLonKm = (lon - centerLon) * (111.32f * cosCenterLat);
          float distKm = sqrtf(dLatKm * dLatKm + dLonKm * dLonKm);

          uint8_t prio = is_emg ? 0 : (is_mil ? 1 : 2);
          cands[candCount++] = {distKm, (uint16_t)i, prio};
        }

        std::sort(cands, cands + candCount, [](const Cand& a, const Cand& b) {
          if (a.prio != b.prio) return a.prio < b.prio;
          return a.distKm < b.distKm;
        });

        size_t count = 0;
        for (size_t k = 0; k < candCount && count < MAX_AIRCRAFT; k++) {
          JsonObject plane = acList[cands[k].idx];
          AircraftData& ac = aircraftList[count++];

          ac.lat = plane["lat"].as<float>();
          ac.lon = plane["lon"].as<float>();
          ac.track = plane["track"] | 0.0f;
          ac.nose_deg = plane["true_heading"] | ac.track;
          ac.gs_knots = plane["gs"] | 0.0f;
          ac.vrate_fpm = plane["baro_rate"] | 0.0f;

          int dbFlags = plane["dbFlags"] | 0;
          ac.is_mil = (dbFlags & 1) != 0;

          const char* sq = plane["squawk"] | "";
          strlcpy(ac.squawk, sq, sizeof(ac.squawk));
          const char* emg = plane["emergency"] | "";
          ac.is_emergency = (strcmp(ac.squawk, "7700") == 0 || strcmp(ac.squawk, "7600") == 0 || strcmp(ac.squawk, "7500") == 0 ||
                             (emg[0] != '\0' && strcmp(emg, "none") != 0));

          const char* fl = plane["flight"] | "";
          strlcpy(ac.callsign, fl, sizeof(ac.callsign));
          size_t csLen = strlen(ac.callsign);
          while (csLen > 0 && ac.callsign[csLen - 1] == ' ') {
            ac.callsign[--csLen] = '\0';
          }
          if (ac.callsign[0] == '\0') strlcpy(ac.callsign, "NOCALL", sizeof(ac.callsign));

          const char* typeStr = plane["t"] | "";
          strlcpy(ac.type, typeStr, sizeof(ac.type));

          if (plane["alt_baro"].is<const char*>() && strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0) {
            strlcpy(ac.alt, "GND", sizeof(ac.alt));
          } else {
            float altFeet = plane["alt_baro"] | 0.0f;
            int altMeters = (int)(altFeet * 0.3048f);
            snprintf(ac.alt, sizeof(ac.alt), "%dm", altMeters);
          }

          ac.route[0] = '\0';
          if (strcmp(ac.callsign, "NOCALL") != 0 && !ac.is_mil) {
            const char* cached = routeCacheFind(ac.callsign);
            if (cached != nullptr) {
              strlcpy(ac.route, cached, sizeof(ac.route));
            }
          }
        }

        aircraftCount = count;
        lastPlaneFetchFixMs = millis();

        int routesFetched = 0;
        for (size_t i = 0; i < aircraftCount && routesFetched < 2; i++) {
          if (aircraftList[i].route[0] == '\0' && strcmp(aircraftList[i].callsign, "NOCALL") != 0 && !aircraftList[i].is_mil) {
            fetchRouteForCallsign(aircraftList[i].callsign, aircraftList[i].route, sizeof(aircraftList[i].route));
            routesFetched++;
          }
        }

        Serial.printf("[ADS-B] Parsed %u aircraft from API (selected: %u priority/closest, Free RAM: %u B)\n", 
                      (unsigned int)acList.size(), (unsigned int)count, (unsigned int)ESP.getFreeHeap());
      }
    } else {
      Serial.printf("[ADS-B] HTTP error: %d\n", httpCode);
      http.end();
      client.stop();
    }
  }

  ensureCanvas();
  renderScreen();
}

void drawWorldBorders(LovyanGFX& target) {
  float latRadiusDeg = currentRadiusKm / 111.32f;
  float cosLat = cosf(centerLat * DEG_TO_RAD);
  float lonRadiusDeg = (fabsf(cosLat) > 0.01f) ? (currentRadiusKm / (111.32f * cosLat)) : 0.0f;

  float marginLat = latRadiusDeg * 1.25f;
  float marginLon = lonRadiusDeg * 1.25f;

  int16_t boxMinLat = (int16_t)floorf((centerLat - marginLat) * 100.0f);
  int16_t boxMaxLat = (int16_t)ceilf((centerLat + marginLat) * 100.0f);
  int16_t boxMinLon = (int16_t)floorf((centerLon - marginLon) * 100.0f);
  int16_t boxMaxLon = (int16_t)ceilf((centerLon + marginLon) * 100.0f);

  uint16_t borderColor = target.color565(40, 160, 180); // Tactical teal-cyan border

  for (size_t s = 0; s < WORLD_BORDER_SEGMENT_COUNT; s++) {
    int16_t sMinLat = pgm_read_word(&WORLD_BORDER_SEGMENTS[s].minLat);
    int16_t sMaxLat = pgm_read_word(&WORLD_BORDER_SEGMENTS[s].maxLat);
    int16_t sMinLon = pgm_read_word(&WORLD_BORDER_SEGMENTS[s].minLon);
    int16_t sMaxLon = pgm_read_word(&WORLD_BORDER_SEGMENTS[s].maxLon);

    if (sMaxLat < boxMinLat || sMinLat > boxMaxLat ||
        sMaxLon < boxMinLon || sMinLon > boxMaxLon) {
      continue;
    }

    uint16_t ptCount = pgm_read_word(&WORLD_BORDER_SEGMENTS[s].pointCount);
    const int16_t (*pts)[2] = (const int16_t (*)[2])pgm_read_ptr(&WORLD_BORDER_SEGMENTS[s].points);

    int prevSx = -999, prevSy = -999;
    for (uint16_t i = 0; i < ptCount; i++) {
      int16_t rawLat = pgm_read_word(&pts[i][0]);
      int16_t rawLon = pgm_read_word(&pts[i][1]);

      float ptLat = (float)rawLat / 100.0f;
      float ptLon = (float)rawLon / 100.0f;

      int sx = (int)roundf(gpsToScreenX(ptLat, ptLon));
      int sy = (int)roundf(gpsToScreenY(ptLat, ptLon));

      if (i > 0) {
        if ((prevSx >= -40 && prevSx <= TFT_W + 40 && prevSy >= -40 && prevSy <= TFT_H + 40) ||
            (sx >= -40 && sx <= TFT_W + 40 && sy >= -40 && sy <= TFT_H + 40)) {
          target.drawLine(prevSx, prevSy, sx, sy, borderColor);
        }
      }
      prevSx = sx;
      prevSy = sy;
    }
  }
}

void drawPlaneRadarGrid(LovyanGFX& target) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  // 1. Global World Borders
  drawWorldBorders(target);

  // 2. Landmark Cities
  target.setTextSize(0.80f);
  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_ORANGE, TFT_BLACK);
  for (size_t i = 0; i < CITY_COUNT; i++) {
    if (currentRadiusKm >= 100.0f && !CITIES[i].isMajor) continue;

    int sx = (int)roundf(gpsToScreenX(CITIES[i].lat, CITIES[i].lon));
    int sy = (int)roundf(gpsToScreenY(CITIES[i].lat, CITIES[i].lon));
    if (sx >= 10 && sx <= TFT_W - 10 && sy >= 10 && sy <= TFT_H - 10) {
      target.drawLine(sx - 2, sy, sx + 2, sy, TFT_RED);
      target.drawLine(sx, sy - 2, sx, sy + 2, TFT_RED);
      target.drawString(CITIES[i].name, sx, sy - 3);
    }
  }

  // 3. Concentric Range Rings & Crosshairs
  uint16_t gridColor = target.color565(0, 200, 0);     
  uint16_t dimGridColor = target.color565(0, 80, 0);   

  target.drawLine(cx - 110, cy, cx + 110, cy, dimGridColor);
  target.drawLine(cx, cy - 110, cx, cy + 110, dimGridColor);

  target.drawCircle(cx, cy, 35, gridColor);
  target.drawCircle(cx, cy, 70, gridColor);
  target.drawCircle(cx, cy, 105, gridColor);

  // 4. Cardinal Directions (N, S, W, E)
  target.setTextSize(0.75f);
  target.setTextDatum(textdatum_t::middle_center);
  target.setTextColor(target.color565(180, 220, 180), TFT_BLACK);
  target.drawString("N", cx, cy - 105 + 10);
  target.drawString("S", cx, cy + 105 - 10);
  target.drawString("W", cx - 105 + 10, cy);
  target.drawString("E", cx + 105 - 10, cy);

  // 5. Scale and Time
  target.setTextSize(0.75f);
  target.setTextDatum(textdatum_t::top_center);
  target.setTextColor(target.color565(0, 255, 0), TFT_BLACK);
  target.drawString(String((int)currentRadiusKm) + " km", cx, 3);

  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_WHITE, TFT_BLACK);
  target.drawString(getCurrentSystemTimeText(), cx, TFT_H - 3);
}

void drawPlanesOverlay(LovyanGFX& target) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  float dt_s = (lastPlaneFetchFixMs > 0) ? (float)(millis() - lastPlaneFetchFixMs) / 1000.0f : 0.0f;
  if (dt_s > 30.0f) dt_s = 30.0f;

  struct RenderedPlane {
    int sx, sy;
    float dist;
    bool is_on_screen;
    bool show_tag;
    AircraftData ac;
  };

  RenderedPlane planes[MAX_AIRCRAFT];
  size_t onScreenCount = 0;

  for (size_t i = 0; i < aircraftCount; i++) {
    AircraftData ac = aircraftList[i];

    if (dt_s > 0.0f && ac.gs_knots > 0.0f && !isnan(ac.track)) {
      float dist_km = ac.gs_knots * 1.852f * dt_s / 3600.0f;
      float dLat = dist_km * cosf(ac.track * DEG_TO_RAD) / 111.32f;
      float cosLat = cosf(ac.lat * DEG_TO_RAD);
      float dLon = (fabsf(cosLat) > 0.01f) ? (dist_km * sinf(ac.track * DEG_TO_RAD) / (111.32f * cosLat)) : 0.0f;
      ac.lat += dLat;
      ac.lon += dLon;
    }

    int sx = (int)roundf(gpsToScreenX(ac.lat, ac.lon));
    int sy = (int)roundf(gpsToScreenY(ac.lat, ac.lon));

    float distFromCenter = sqrtf((float)((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy)));

    planes[i].sx = sx;
    planes[i].sy = sy;
    planes[i].dist = distFromCenter;
    planes[i].is_on_screen = (distFromCenter <= 106.0f);
    planes[i].show_tag = false;
    planes[i].ac = ac;

    if (planes[i].is_on_screen) {
      onScreenCount++;
    }
  }

  if (currentRadiusKm <= 50.0f) {
    for (size_t i = 0; i < aircraftCount; i++) {
      if (planes[i].is_on_screen) planes[i].show_tag = true;
    }
  } else {
    for (size_t i = 0; i < aircraftCount; i++) {
      if (planes[i].is_on_screen && (planes[i].ac.is_emergency || planes[i].ac.is_mil)) {
        planes[i].show_tag = true;
      }
    }
    int currentTags = 0;
    for (size_t i = 0; i < aircraftCount; i++) {
      if (planes[i].show_tag) currentTags++;
    }
    const int maxTags = (currentRadiusKm <= 100.0f) ? 3 : 2;
    while (currentTags < maxTags) {
      float minDist = 999999.0f;
      int bestIdx = -1;
      for (size_t i = 0; i < aircraftCount; i++) {
        if (planes[i].is_on_screen && !planes[i].show_tag) {
          if (planes[i].dist < minDist) {
            minDist = planes[i].dist;
            bestIdx = (int)i;
          }
        }
      }
      if (bestIdx >= 0) {
        planes[bestIdx].show_tag = true;
        currentTags++;
      } else {
        break;
      }
    }
  }

  // Tag Collision Avoidance
  for (size_t i = 0; i < aircraftCount; i++) {
    if (!planes[i].show_tag || planes[i].ac.is_emergency) continue;
    for (size_t j = i + 1; j < aircraftCount; j++) {
      if (!planes[j].show_tag || planes[j].ac.is_emergency) continue;
      int ddx = planes[i].sx - planes[j].sx;
      int ddy = planes[i].sy - planes[j].sy;
      if (ddx * ddx + ddy * ddy < 34 * 34) {
        if (planes[i].dist > planes[j].dist && !planes[i].ac.is_mil) {
          planes[i].show_tag = false;
        } else if (!planes[j].ac.is_mil) {
          planes[j].show_tag = false;
        }
      }
    }
  }

  // Detekcia, či sa v okruhu nachádza núdzový let
  bool hasEmergency = false;
  String emgInfo;
  for (size_t i = 0; i < aircraftCount; i++) {
    if (planes[i].ac.is_emergency) {
      hasEmergency = true;
      emgInfo = String(planes[i].ac.callsign) + " (SQ" + String(planes[i].ac.squawk) + ")";
      break;
    }
  }

  // Samotné vykreslenie symbolov, štítkov a okrajových indikátorov
  for (size_t i = 0; i < aircraftCount; i++) {
    // V prípade núdze skryjeme všetky ostatné bežné lety pre maximálny taktický prehľad
    if (hasEmergency && !planes[i].ac.is_emergency) {
      continue;
    }

    if (planes[i].is_on_screen) {
      drawAircraftSymbol(target, planes[i].sx, planes[i].sy, planes[i].ac.nose_deg, planes[i].ac.track, planes[i].ac.gs_knots, planes[i].ac.is_mil, planes[i].ac.is_emergency);
      if (planes[i].show_tag || planes[i].ac.is_emergency) {
        drawAircraftTag(target, planes[i].sx, planes[i].sy, planes[i].ac);
      }
    } else {
      drawEdgeIndicator(target, planes[i].ac.lat, planes[i].ac.lon, planes[i].ac.is_mil);
    }
  }

  // Zobrazenie núdzového výstražného bannera pri Squawk 7700/7600/7500
  if (hasEmergency) {
    target.setTextSize(0.80f);
    target.setTextDatum(textdatum_t::top_center);
    uint16_t emgCol = (millis() % 600 < 300) ? target.color565(255, 30, 30) : target.color565(255, 255, 0);
    target.setTextColor(emgCol, TFT_BLACK);
    target.drawString("⚠️ NUDZA: " + emgInfo, cx, 38);
  }
}

void drawWeatherOverlay(LovyanGFX& target, bool showTime) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  // Global World Borders
  drawWorldBorders(target);

  // Landmark Cities
  target.setTextSize(0.80f);
  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_ORANGE, TFT_BLACK);
  for (size_t i = 0; i < CITY_COUNT; i++) {
    if (currentRadiusKm >= 100.0f && !CITIES[i].isMajor) continue;

    int sx = (int)roundf(gpsToScreenX(CITIES[i].lat, CITIES[i].lon));
    int sy = (int)roundf(gpsToScreenY(CITIES[i].lat, CITIES[i].lon));
    if (sx >= 10 && sx <= TFT_W - 10 && sy >= 10 && sy <= TFT_H - 10) {
      target.drawLine(sx - 2, sy, sx + 2, sy, TFT_RED);
      target.drawLine(sx, sy - 2, sx, sy + 2, TFT_RED);
      target.drawString(CITIES[i].name, sx, sy - 3);
    }
  }

  // Radar Circles
  target.drawCircle(cx, cy, TFT_W / 2 - 2, TFT_DARKGREY);
  target.drawCircle(cx, cy, TFT_W / 4, TFT_DARKGREY);
  target.drawLine(cx - 6, cy, cx + 6, cy, TFT_WHITE);
  target.drawLine(cx, cy - 6, cx, cy + 6, TFT_WHITE);

  target.setTextSize(0.75f);
  target.setTextDatum(textdatum_t::top_center);
  target.setTextColor(TFT_WHITE, TFT_BLACK);
  target.drawString(String((int)currentRadiusKm) + " km", cx, 3);

  if (showTime) {
    target.setTextDatum(textdatum_t::bottom_center);
    target.drawString(getRadarTimeText(radarTimestamp), cx, TFT_H - 3);
  }
}


// =======================================================================================
// 7. PNG DECODER & TILE RESAMPLING TO RAW CACHE
// =======================================================================================

void* pngOpen(const char* filename, int32_t* size) {
  pngFile = SPIFFS.open(filename, "r");
  if (!pngFile) return nullptr;
  *size = pngFile.size();
  return &pngFile;
}

void pngClose(void* handle) {
  if (pngFile) pngFile.close();
}

int32_t pngRead(PNGFILE* handle, uint8_t* buffer, int32_t length) {
  return pngFile.read(buffer, length);
}

int32_t pngSeek(PNGFILE* handle, int32_t position) {
  return pngFile.seek(position) ? position : -1;
}

int drawPngLine(PNGDRAW* pDraw) {
  int ty = pDraw->y;
  if (ty < 0 || ty >= RADAR_TILE_SIZE) return 1;
  int chunkIdx = ty >> 6;
  int rowInChunk = ty & 63;
  if (!tileChunks[chunkIdx]) return 1;

  png.getLineAsRGB565(pDraw, currLine565, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
  uint8_t* rowDst = &tileChunks[chunkIdx][rowInChunk * RADAR_TILE_SIZE];

  for (int tx = 0; tx < RADAR_TILE_SIZE; tx++) {
    uint16_t c565 = currLine565[tx];
    if (c565 == 0x0000) {
      rowDst[tx] = 0x00;
    } else {
      uint8_t r = (c565 >> 11) & 0x1F;
      uint8_t g = (c565 >> 5) & 0x3F;
      uint8_t b = c565 & 0x1F;
      uint8_t col8 = (uint8_t)((((r >> 2) & 0x07) << 5) | (((g >> 3) & 0x07) << 2) | ((b >> 3) & 0x03));
      if (col8 == 0x00) col8 = 0x04;
      rowDst[tx] = col8;
    }
  }
  return 1;
}

void decodeRadarImage() {
  if (!SPIFFS.exists(RADAR_FILE)) {
    if (SPIFFS.exists(RADAR_RAW_CACHE_FILE)) SPIFFS.remove(RADAR_RAW_CACHE_FILE);
    radarRawCacheValid = false;
    rainAlertActive = false;
    isIncomingAlert = false;
    alertBearingDeg = -1.0f;
    alertTargetDistPx = 0.0f;
    alertBearingDir[0] = '\0';
    closestPrecipDistKm = -1.0f;
    return;
  }

  bool allocOk = true;
  for (int i = 0; i < 4; i++) {
    if (tileChunks[i]) free(tileChunks[i]);
    tileChunks[i] = (uint8_t*)malloc(64 * RADAR_TILE_SIZE);
    if (!tileChunks[i]) allocOk = false;
    else memset(tileChunks[i], 0, 64 * RADAR_TILE_SIZE);
  }

  if (!allocOk) {
    Serial.println("[RADAR] Error: Failed to allocate tile buffer chunks!");
    for (int i = 0; i < 4; i++) {
      if (tileChunks[i]) { free(tileChunks[i]); tileChunks[i] = nullptr; }
    }
    radarRawCacheValid = false;
    return;
  }

  if (png.open(RADAR_FILE, pngOpen, pngClose, pngRead, pngSeek, drawPngLine) == PNG_SUCCESS) {
    png.decode(nullptr, 0);
    png.close();
  }

  if (SPIFFS.exists(RADAR_RAW_CACHE_FILE)) SPIFFS.remove(RADAR_RAW_CACHE_FILE);
  fRawOut = SPIFFS.open(RADAR_RAW_CACHE_FILE, "w");
  if (!fRawOut) {
    Serial.println("[RADAR] Error creating raw cache file for write!");
    for (int i = 0; i < 4; i++) {
      if (tileChunks[i]) { free(tileChunks[i]); tileChunks[i] = nullptr; }
    }
    radarRawCacheValid = false;
    return;
  }

  minPrecipDistSqPx = 999999.0f;
  alertIncomingPixelCount = 0;
  alertIncomingMinSqPx = 999999.0f;
  alertIncomingTargetDx = 0.0f;
  alertIncomingTargetDy = 0.0f;

  alertAnyPixelCount = 0;
  alertAnyMinSqPx = 999999.0f;
  alertAnyTargetDx = 0.0f;
  alertAnyTargetDy = 0.0f;

  int z = ZOOM_LEVELS_Z[zoomIndex];
  if (z > RAINVIEWER_MAX_ZOOM) z = RAINVIEWER_MAX_ZOOM;

  float m_per_tile_px = (40075017.0f * cosf(centerLat * DEG_TO_RAD)) / (256.0f * (float)(1 << z));
  float m_per_screen_px = (currentRadiusKm * 1000.0f) / 120.0f;
  float tile_scale = m_per_tile_px / m_per_screen_px;

  for (int sy = 0; sy < TFT_H; sy++) {
    uint8_t row8[TFT_W];
    float dY_px = (float)sy - 120.0f;

    for (int sx = 0; sx < TFT_W; sx++) {
      float dX_px = (float)sx - 120.0f;
      float dSq = dX_px * dX_px + dY_px * dY_px;
      if (dSq > 14400.0f) {
        row8[sx] = 0x00;
        continue;
      }

      int tx = (int)roundf(currentTileOffsetPx + dX_px / tile_scale);
      int ty = (int)roundf(currentTileOffsetPy + dY_px / tile_scale);

      uint8_t col8 = 0x00;
      if (tx >= 0 && tx < RADAR_TILE_SIZE && ty >= 0 && ty < RADAR_TILE_SIZE) {
        int chunkIdx = ty >> 6;
        int rowInChunk = ty & 63;
        if (tileChunks[chunkIdx]) {
          col8 = tileChunks[chunkIdx][rowInChunk * RADAR_TILE_SIZE + tx];
        }
      }
      row8[sx] = col8;

      if (col8 != 0x00) {
        if (dSq < minPrecipDistSqPx) minPrecipDistSqPx = dSq;
        if (rainAlertThresholdKm > 0.0f) {
          float distKm = (sqrtf(dSq) / 120.0f) * currentRadiusKm;
          if (distKm <= rainAlertThresholdKm) {
            alertAnyPixelCount++;
            if (dSq < alertAnyMinSqPx) {
              alertAnyMinSqPx = dSq;
              alertAnyTargetDx = dX_px;
              alertAnyTargetDy = dY_px;
            }
            float effectiveWindDir = (windDir700hPa >= 0.0f) ? windDir700hPa : windDirSurface;
            if (effectiveWindDir >= 0.0f) {
              float pixelBearing = atan2f(dX_px, -dY_px) * 57.2957795f;
              if (pixelBearing < 0.0f) pixelBearing += 360.0f;
              float diff = fabsf(pixelBearing - effectiveWindDir);
              if (diff > 180.0f) diff = 360.0f - diff;
              if (diff <= 60.0f) {
                alertIncomingPixelCount++;
                if (dSq < alertIncomingMinSqPx) {
                  alertIncomingMinSqPx = dSq;
                  alertIncomingTargetDx = dX_px;
                  alertIncomingTargetDy = dY_px;
                }
              }
            }
          }
        }
      }
    }
    fRawOut.write(row8, TFT_W);
  }

  fRawOut.flush();
  fRawOut.close();

  for (int i = 0; i < 4; i++) {
    if (tileChunks[i]) {
      free(tileChunks[i]);
      tileChunks[i] = nullptr;
    }
  }
  radarRawCacheValid = true;

  int totalFoundPixels = alertAnyPixelCount + alertIncomingPixelCount;
  Serial.printf("[RADAR] Decoded precipitation pixels in range: %d (Any: %d, Incoming: %d, scale: %.2f, radius: %.0f km)\n", 
                totalFoundPixels, alertAnyPixelCount, alertIncomingPixelCount, tile_scale, currentRadiusKm);

  if (rainAlertThresholdKm > 0.0f && alertIncomingPixelCount >= rainAlertMinPixels && alertIncomingMinSqPx <= 14400.0f) {
    rainAlertActive = true;
    isIncomingAlert = true;
    closestPrecipDistKm = (sqrtf(alertIncomingMinSqPx) / 120.0f) * currentRadiusKm;
    alertTargetDistPx = sqrtf(alertIncomingMinSqPx);

    float rad = atan2f(alertIncomingTargetDx, -alertIncomingTargetDy);
    float deg = rad * 57.2957795f;
    if (deg < 0.0f) deg += 360.0f;
    alertBearingDeg = deg;
    strlcpy(alertBearingDir, getCompassDirText(deg), sizeof(alertBearingDir));
  } else if (rainAlertThresholdKm > 0.0f && alertAnyPixelCount >= rainAlertMinPixels && alertAnyMinSqPx <= 14400.0f) {
    rainAlertActive = true;
    isIncomingAlert = false;
    closestPrecipDistKm = (sqrtf(alertAnyMinSqPx) / 120.0f) * currentRadiusKm;
    alertTargetDistPx = sqrtf(alertAnyMinSqPx);

    float rad = atan2f(alertAnyTargetDx, -alertAnyTargetDy);
    float deg = rad * 57.2957795f;
    if (deg < 0.0f) deg += 360.0f;
    alertBearingDeg = deg;
    strlcpy(alertBearingDir, getCompassDirText(deg), sizeof(alertBearingDir));
  } else {
    closestPrecipDistKm = (minPrecipDistSqPx <= 14400.0f) ? (sqrtf(minPrecipDistSqPx) / 120.0f) * currentRadiusKm : -1.0f;
    rainAlertActive = false;
    isIncomingAlert = false;
    alertBearingDeg = -1.0f;
    alertTargetDistPx = 0.0f;
    alertBearingDir[0] = '\0';
  }
}

/** 
 * Main Unified Rendering:
 * Layer 1: RainViewer Weather Radar (Blazing fast raw cache stream)
 * Layer 2: Grid, Borders, Cities, Range Rings
 * Layer 3: ADS-B Aircraft Overlay with HUD Labels
 * Layer 4: Tactical Threat Sector Beam & Warning Banner
 */
void renderScreen() {
  ensureCanvas();
  if (!canvasReady) return;
  
  canvas.fillScreen(TFT_BLACK);

  // 1. Layer: Weather Radar
  if (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER) {
    if (radarRawCacheValid && SPIFFS.exists(RADAR_RAW_CACHE_FILE)) {
      File fIn = SPIFFS.open(RADAR_RAW_CACHE_FILE, "r");
      if (fIn) {
        uint8_t* canvasBuf = (uint8_t*)canvas.getBuffer();
        uint8_t row8[TFT_W];
        for (int y = 0; y < TFT_H; y++) {
          if (fIn.read(row8, TFT_W) == TFT_W) {
            if (canvasBuf) {
              uint8_t* linePtr = canvasBuf + y * TFT_W;
              for (int x = 0; x < TFT_W; x++) {
                if (row8[x] != 0x00) {
                  linePtr[x] = row8[x];
                }
              }
            }
          }
        }
        fIn.close();
      }
    }
  }

  // 2. Layer: Grid & Map
  if (currentMode == MODE_COMBINED || currentMode == MODE_PLANES) {
    drawPlaneRadarGrid(canvas);
  } else {
    drawWeatherOverlay(canvas, true);
  }

  // 3. Layer: Aircraft
  if (currentMode == MODE_COMBINED || currentMode == MODE_PLANES) {
    drawPlanesOverlay(canvas);
  }

  // 4. Layer: Tactical Threat Sector Beam & Alert
  if (rainAlertActive && (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER)) {
    uint32_t phase = millis() % 1000;
    uint16_t beaconColor;
    uint16_t beamColor;
    uint16_t accentColor;

    if (isIncomingAlert) {
      uint8_t g = (phase < 500) ? (uint8_t)map(phase, 0, 500, 20, 170) : (uint8_t)map(phase, 500, 1000, 170, 20);
      uint8_t r = (phase < 500) ? (uint8_t)map(phase, 0, 500, 210, 255) : (uint8_t)map(phase, 500, 1000, 255, 210);
      beamColor = canvas.color565(r, g, 0);
      beaconColor = canvas.color565(255, (uint8_t)min(255, g + 50), 0);
      accentColor = TFT_WHITE;
    } else {
      uint8_t g = (phase < 500) ? (uint8_t)map(phase, 0, 500, 170, 235) : (uint8_t)map(phase, 500, 1000, 235, 170);
      uint8_t r = (phase < 500) ? (uint8_t)map(phase, 0, 500, 220, 255) : (uint8_t)map(phase, 500, 1000, 255, 220);
      beamColor = canvas.color565(r, g, 0);
      beaconColor = canvas.color565(255, 240, 40);
      accentColor = TFT_WHITE;
    }

    if (alertBearingDeg >= 0.0f) {
      constexpr float kDegToRad = 0.01745329252f;
      float centerDeg = alertBearingDeg;
      float halfSpanDeg = 18.0f;
      float leftDeg = centerDeg - halfSpanDeg;
      float rightDeg = centerDeg + halfSpanDeg;

      float rStart = 10.0f;
      float rEnd = 114.0f;
      float rTarget = constrain(alertTargetDistPx, 14.0f, 114.0f);

      float radL = leftDeg * kDegToRad;
      float radR = rightDeg * kDegToRad;
      float radC = centerDeg * kDegToRad;

      // Sector boundary rays
      int xL_start = (int)roundf(120.0f + rStart * sinf(radL));
      int yL_start = (int)roundf(120.0f - rStart * cosf(radL));
      int xL_end   = (int)roundf(120.0f + rEnd * sinf(radL));
      int yL_end   = (int)roundf(120.0f - rEnd * cosf(radL));

      int xR_start = (int)roundf(120.0f + rStart * sinf(radR));
      int yR_start = (int)roundf(120.0f - rStart * cosf(radR));
      int xR_end   = (int)roundf(120.0f + rEnd * sinf(radR));
      int yR_end   = (int)roundf(120.0f - rEnd * cosf(radR));

      canvas.drawLine(xL_start, yL_start, xL_end, yL_end, beamColor);
      canvas.drawLine(xR_start, yR_start, xR_end, yR_end, beamColor);

      // Concentric Range Arcs
      const float gridRadii[] = { 40.0f, 75.0f, 110.0f };
      for (float gR : gridRadii) {
        if (gR > rStart && gR < rEnd && fabsf(gR - rTarget) > 8.0f) {
          for (float a = leftDeg; a < rightDeg; a += 3.0f) {
            float a1 = a * kDegToRad;
            float a2 = (a + 2.0f) * kDegToRad;
            int ax1 = (int)roundf(120.0f + gR * sinf(a1));
            int ay1 = (int)roundf(120.0f - gR * cosf(a1));
            int ax2 = (int)roundf(120.0f + gR * sinf(a2));
            int ay2 = (int)roundf(120.0f - gR * cosf(a2));
            canvas.drawLine(ax1, ay1, ax2, ay2, beamColor);
          }
        }
      }

      // Center Laser Tracer
      for (float rStep = rStart; rStep < rEnd; rStep += 8.0f) {
        float rStep2 = min(rStep + 4.0f, rEnd);
        int cx1 = (int)roundf(120.0f + rStep * sinf(radC));
        int cy1 = (int)roundf(120.0f - rStep * cosf(radC));
        int cx2 = (int)roundf(120.0f + rStep2 * sinf(radC));
        int cy2 = (int)roundf(120.0f - rStep2 * cosf(radC));
        canvas.drawLine(cx1, cy1, cx2, cy2, beamColor);
      }

      // Target Range Arc
      for (float a = leftDeg; a <= rightDeg; a += 1.5f) {
        float aRad = a * kDegToRad;
        float sinA = sinf(aRad);
        float cosA = cosf(aRad);
        int tx1 = (int)roundf(120.0f + rTarget * sinA);
        int ty1 = (int)roundf(120.0f - rTarget * cosA);
        int tx2 = (int)roundf(120.0f + (rTarget - 1.0f) * sinA);
        int ty2 = (int)roundf(120.0f - (rTarget - 1.0f) * cosA);
        canvas.drawPixel(tx1, ty1, accentColor);
        canvas.drawPixel(tx2, ty2, beamColor);
      }

      // Target Reticle
      int tgtX = (int)roundf(120.0f + rTarget * sinf(radC));
      int tgtY = (int)roundf(120.0f - rTarget * cosf(radC));
      canvas.drawCircle(tgtX, tgtY, 4, accentColor);
      canvas.drawCircle(tgtX, tgtY, 5, beamColor);
      canvas.drawPixel(tgtX, tgtY, TFT_WHITE);

      // Perimeter Direction Chevron ▼
      float chevDirX = sinf(radC);
      float chevDirY = -cosf(radC);
      float chevPerpX = cosf(radC);
      float chevPerpY = sinf(radC);

      int tipX = (int)roundf(120.0f + 104.0f * chevDirX);
      int tipY = (int)roundf(120.0f + 104.0f * chevDirY);
      int wingLx = (int)roundf(120.0f + 114.0f * chevDirX - 6.5f * chevPerpX);
      int wingLy = (int)roundf(120.0f + 114.0f * chevDirY - 6.5f * chevPerpY);
      int wingRx = (int)roundf(120.0f + 114.0f * chevDirX + 6.5f * chevPerpX);
      int wingRy = (int)roundf(120.0f + 114.0f * chevDirY + 6.5f * chevPerpY);

      canvas.fillTriangle(tipX, tipY, wingLx, wingLy, wingRx, wingRy, beamColor);
      canvas.drawTriangle(tipX, tipY, wingLx, wingLy, wingRx, wingRy, accentColor);

      // Perimeter Sector Arc
      for (float a = leftDeg; a <= rightDeg; a += 1.0f) {
        float aRad = a * kDegToRad;
        float sinA = sinf(aRad);
        float cosA = cosf(aRad);
        int ax1 = (int)roundf(120.0f + 117.0f * sinA);
        int ay1 = (int)roundf(120.0f - 117.0f * cosA);
        int ax2 = (int)roundf(120.0f + 118.0f * sinA);
        int ay2 = (int)roundf(120.0f - 118.0f * cosA);
        canvas.drawPixel(ax1, ay1, beamColor);
        canvas.drawPixel(ax2, ay2, beaconColor);
      }
    }

    // Header Alert Text
    canvas.setTextSize(0.75f);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(beaconColor, TFT_BLACK);
    char alertBuf[36];
    if (alertBearingDir[0] != '\0') {
      snprintf(alertBuf, sizeof(alertBuf), "! %s: %.1f km %s !", isIncomingAlert ? "INCOMING" : "STORM", closestPrecipDistKm, alertBearingDir);
    } else {
      snprintf(alertBuf, sizeof(alertBuf), "! %s: %.1f km !", isIncomingAlert ? "INCOMING" : "STORM", closestPrecipDistKm);
    }
    canvas.drawString(alertBuf, 120, 18);
  }

  canvas.pushSprite(0, 0);
}


// =======================================================================================
// 8. SYSTEM INITIALIZATION & MAIN LOOP
// =======================================================================================

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n\n========================================");
  Serial.printf("  ESP-GlobalRadar %s (ESP32-C3)\n", CURRENT_VERSION);
  Serial.printf("  Free RAM: %u B, Flash: %u MB\n", (unsigned int)ESP.getFreeHeap(), (unsigned int)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.println("========================================\n");
  
  tft.init(); 
  tft.setRotation(0); 
  tft.setBrightness(180); 
  tft.loadFont(ui_font_vlw, lgfx::IFont::font_type_t::ft_vlw);
  tft.setTextSize(0.80f);

  pinMode(ZOOM_BUTTON_PIN, INPUT_PULLUP);
  
  prefs.begin("radar", true);
  centerLat = prefs.getFloat("lat", atof(DEFAULT_CENTER_LAT));
  centerLon = prefs.getFloat("lon", atof(DEFAULT_CENTER_LON));
  currentRadiusKm = prefs.getFloat("radius", atof(DEFAULT_RADIUS_KM_TEXT));
  timeOffsetHours = prefs.getInt("offset", DEFAULT_TIME_OFFSET_HOURS);
  carouselIntervalSec = prefs.getInt("car_int", 30);
  carouselEnabled = prefs.getBool("car_en", true);
  int savedMode = prefs.getInt("mode", (int)MODE_COMBINED);
  if (savedMode >= 0 && savedMode <= 2) {
    currentMode = (AppMode)savedMode;
  }
  int savedZoom = prefs.getInt("zoom_idx", -1);
  if (savedZoom >= 0 && savedZoom < ZOOM_LEVEL_COUNT) {
    zoomIndex = savedZoom;
    currentRadiusKm = ZOOM_LEVELS_KM[zoomIndex];
  } else {
    for (int i = 0; i < ZOOM_LEVEL_COUNT; i++) {
      if (fabs(ZOOM_LEVELS_KM[i] - currentRadiusKm) < 0.5f) {
        zoomIndex = i;
        break;
      }
    }
  }
  rainAlertThresholdKm = prefs.getFloat("alert_km", 15.0f);
  rainAlertMinPixels = prefs.getInt("alert_sens", 25);
  radarTimestamp = prefs.getUInt("last_ts", 0);
  radarPath = prefs.getString("last_path", "");
  prefs.end();

  if (carouselIntervalSec < 5) carouselIntervalSec = 5;
  carouselIntervalMs = (uint32_t)carouselIntervalSec * 1000;

  checkResetButtonAtBoot();
  if (!SPIFFS.begin(false)) {
    Serial.println("[SPIFFS] Mounting failed, formatting SPIFFS partition...");
    SPIFFS.format();
    if (SPIFFS.begin(true)) {
      Serial.println("[SPIFFS] SPIFFS formatted and mounted successfully!");
    } else {
      Serial.println("[SPIFFS] SPIFFS mount error!");
    }
  } else {
    Serial.println("[SPIFFS] SPIFFS mounted successfully.");
  }
  connectWiFi();
  setupWebServer();

  tft.fillScreen(TFT_BLACK);
  drawPlaneRadarGrid(tft);
  tft.setTextSize(0.75f);
  tft.setTextDatum(textdatum_t::bottom_center);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Loading data...", TFT_W / 2, TFT_H - 12);

  fetchWindData();
  downloadLatestRadar();
  if (currentMode != MODE_WEATHER) {
    fetchPlanesData();
  }
  renderScreen();
  
  lastWeatherUpdateMs = millis();
  lastCarouselSwitchMs = millis();
  lastPlaneFetchMs = millis();
  lastPlaneRedrawMs = millis();
}

void loop() {
  uint32_t now = millis();

  handleButton();
  server.handleClient();
  updateNightMode();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
  }

  // Periodic wind update from Open-Meteo (every 15 minutes)
  if (now - lastWindFetchMs >= WIND_FETCH_INTERVAL_MS) {
    fetchWindData();
  }

  // Carousel mode rotation
  if (carouselEnabled && (now - lastCarouselSwitchMs >= carouselIntervalMs)) {
    lastCarouselSwitchMs = now;
    setAppMode((AppMode)((currentMode + 1) % 3), false);
  }

  // Threat Sector Beam pulsating animation (20 FPS / 50 ms)
  static uint32_t lastAlertAnimMs = 0;
  if (rainAlertActive && (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER)) {
    if (now - lastAlertAnimMs >= 50) {
      lastAlertAnimMs = now;
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  }

  // Periodic data updates
  if (currentMode == MODE_COMBINED) {
    uint32_t weatherInt = (radarTimestamp == 0 || !SPIFFS.exists(RADAR_FILE)) ? 15000 : UPDATE_INTERVAL_MS;
    if (now - lastWeatherUpdateMs >= weatherInt) {
      lastWeatherUpdateMs = now;
      if (downloadLatestRadar()) renderScreen();
    }
    if (now - lastPlaneFetchMs >= PLANE_FETCH_INTERVAL_MS) {
      lastPlaneFetchMs = now;
      fetchPlanesData();
    }
    if (now - lastPlaneRedrawMs >= PLANE_REDRAW_INTERVAL_MS) {
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  } else if (currentMode == MODE_WEATHER) {
    uint32_t interval = (radarTimestamp == 0 || !SPIFFS.exists(RADAR_FILE)) ? 15000 : UPDATE_INTERVAL_MS;
    if (now - lastWeatherUpdateMs >= interval) {
      lastWeatherUpdateMs = now;
      if (downloadLatestRadar()) renderScreen();
    }
  } else if (currentMode == MODE_PLANES) {
    if (now - lastPlaneFetchMs >= PLANE_FETCH_INTERVAL_MS) {
      lastPlaneFetchMs = now;
      fetchPlanesData();
    }
    if (now - lastPlaneRedrawMs >= PLANE_REDRAW_INTERVAL_MS) {
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  }
  
  delay(10);
}
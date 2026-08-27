#pragma once

// =======================================================================================
// ESP-GlobalRadar - Global Weather Radar & ADS-B Aircraft Tracker
// Target Hardware: ESP32-C3 SuperMini + GC9A01 240x240 Round Display
// =======================================================================================

// Default center coordinates (Configurable globally via Web Dashboard or WiFiManager)
// Default location: Central Europe (48.6690° N, 19.6990° E)
#define DEFAULT_CENTER_LAT "48.6690"
#define DEFAULT_CENTER_LON "19.6990"

// Default display radius after boot (km). Button / Web cycles through presets: 10, 25, 50, 100, 250 km
#define DEFAULT_RADIUS_KM_TEXT "50"

// Timezone UTC offset in hours for local time conversion on the round display
#define DEFAULT_TIME_OFFSET_HOURS 2

// Multi-function button connected between GPIO9 and GND (uses internal pull-up)
// Single click: cycle zoom | Double click: switch view modes | 3s long press: reset settings
#define ZOOM_BUTTON_PIN 9
static constexpr uint32_t RESET_HOLD_MS = 3000;

// Weather radar refresh interval (every 5 minutes)
static constexpr uint32_t UPDATE_INTERVAL_MS = 5UL * 60UL * 1000UL;

// ===== RainViewer Global Weather Radar API =====
static constexpr const char* RAINVIEWER_API_URL = "https://api.rainviewer.com/public/weather-maps.json";
static constexpr const char* RAINVIEWER_DEFAULT_HOST = "https://tilecache.rainviewer.com";
static constexpr int RAINVIEWER_COLOR_SCHEME = 2; // 2 = Universal Rainbow (Blue/Green/Yellow/Red/Purple)
static constexpr int RAINVIEWER_SMOOTH = 1;       // 1 = Smooth raster enabled
static constexpr int RAINVIEWER_SNOW = 1;         // 1 = Snow classification enabled
static constexpr int RADAR_TILE_SIZE = 256;       // 256x256 px tile size
static constexpr int RAINVIEWER_MAX_ZOOM = 7;     // Max supported zoom level by RainViewer tile cache

// ===== GC9A01 Round Display (240x240 px) =====
static constexpr int TFT_W = 240;
static constexpr int TFT_H = 240;

// SPI Pin definitions for ESP32-C3 SuperMini
#define TFT_MOSI 3
#define TFT_SCLK 4
#define TFT_CS   1
#define TFT_DC   10
#define TFT_RST  0
#define TFT_BL   -1 // -1 = unused if display backlight is wired to 3.3V
/**
 * =======================================================================================
 * @file main.cpp
 * @brief ESP32-C3 MeteoRadar + ADS-B Plane Radar (Slovakia)
 * @author hackra76 / Antigravity AI
 * @version 1.9.0
 * 
 * @details 
 * Multifunkčný radar pre okrúhly GC9A01 240x240 displej a ESP32-C3 SuperMini.
 * 
 * Nové a pokročilé funkcie vo v1.9.0:
 * 1. 💾 TRVALÉ UKLADANIE STAVOV (NVS Perzistencia):
 *    - Režim radaru (Tactical / Počasie / Lietadlá), úroveň priblíženia (Zoom) aj stav
 *      karuselu sa okamžite ukladajú a automaticky obnovujú po reštarte dosky.
 * 
 * 2. ⛈️ VÝSTRAHA PRED BLÍZKYMI ZRÁŽKAMI (Approaching Rain Alert):
 *    - Automatický výpočet vzdialenosti k najbližším zrážkam počas dekódovania radaru.
 *    - Pri zrážkach < 15 km sa aktivuje pulzujúci jantárovo-červený maják na okrúhlom
 *      displeji a výstražný banner vo webovom dashboarde.
 * 
 * 3. 🛡️ STABILITA PAMÄTE & 100% FLICKER-FREE RENDERING:
 *    - Bezpečný HTTPS prenos s izoláciou pamäte pre TLS handshake.
 *    - Žiadne preblikávanie displeja pri periodickom prekresľovaní lietadiel.
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

static const char* CURRENT_VERSION = "v1.11.0";

// =======================================================================================
// 1. GLOBÁLNE INŠTANCIE & DÁTOVÉ ŠTRUKTÚRY
// =======================================================================================

LGFX tft;                 ///< Ovládač displeja LovyanGFX
LGFX_Sprite canvas(&tft); ///< Dynamický offscreen buffer (Double Buffering)
bool canvasReady = false;
PNG png;                  ///< PNG dekodér pre SHMÚ radarové snímky
File pngFile;             ///< Súborový deskriptor pre SPIFFS
Preferences prefs;        ///< Trvalé úložisko NVS pre konfiguráciu
WebServer server(80);     ///< Lokálny webový server na porte 80

static const char* RADAR_FILE = "/radar.png";
static const char* RADAR_RAW_CACHE_FILE = "/radar_cache.raw";
bool radarRawCacheValid = false;
File fRawOut;

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

// Detekcia blízkych zrážok (Dážď / Búrka / Krupobitie / Sneh) & Smerový indikátor s Open-Meteo vetrom
float minPrecipDistSqPx = 999999.0f;
float closestPrecipDistKm = -1.0f;
bool rainAlertActive = false;
bool isIncomingAlert = false;
float rainAlertThresholdKm = 15.0f; ///< Vzdialenosť výstrahy v km (0 = vypnuté)
int rainAlertMinPixels = 25;        ///< Minimálny plošný rozsah (filter šumu: 10=vysoká, 25=stredná, 50=nízka)

// Prichádzajúce zrážky (v nátokovom kuželi vetra)
int alertIncomingPixelCount = 0;
float alertIncomingMinSqPx = 999999.0f;
float alertIncomingTargetDx = 0.0f;
float alertIncomingTargetDy = 0.0f;

// Všeobecné najbližšie zrážky (v okruhu výstrahy)
int alertAnyPixelCount = 0;
float alertAnyMinSqPx = 999999.0f;
float alertAnyTargetDx = 0.0f;
float alertAnyTargetDy = 0.0f;

float alertBearingDeg = -1.0f;
float alertTargetDistPx = 0.0f;
char alertBearingDir[8] = "";

// Vietor z Open-Meteo API pre detekciu smeru postupu zrážok
float windDir700hPa = -1.0f;     ///< Smer vetra v hladine 700 hPa (0-359°, odkiaľ fúka vietor)
float windSpeed700hPa = 0.0f;    ///< Rýchlosť vetra v 700 hPa (km/h)
float windDirSurface = -1.0f;    ///< Smer vetra pri zemi (0-359°)
float windSpeedSurface = 0.0f;   ///< Rýchlosť vetra pri zemi (km/h)
uint32_t lastWindFetchMs = 0;
static constexpr uint32_t WIND_FETCH_INTERVAL_MS = 15 * 60 * 1000; // 15 minút

inline const char* getCompassDirText(float deg) {
  if (deg < 0.0f) return "--";
  if (deg >= 337.5f || deg < 22.5f) return "S";
  if (deg >= 22.5f && deg < 67.5f) return "SV";
  if (deg >= 67.5f && deg < 112.5f) return "V";
  if (deg >= 112.5f && deg < 157.5f) return "JV";
  if (deg >= 157.5f && deg < 202.5f) return "J";
  if (deg >= 202.5f && deg < 247.5f) return "JZ";
  if (deg >= 247.5f && deg < 292.5f) return "Z";
  if (deg >= 292.5f && deg < 337.5f) return "SZ";
  return "--";
}

// Polygón štátnej hranice Slovenskej republiky (zahustené GPS body)
static const float SK_BORDER[][2] = {
  {16.96, 48.48}, {16.90, 48.38}, {16.85, 48.28}, {16.95, 48.21}, {17.06, 48.14},
  {17.11, 48.08}, {17.16, 48.02}, {17.40, 47.90}, {17.65, 47.78}, {17.87, 47.77},
  {18.10, 47.76}, {18.20, 47.77}, {18.30, 47.78}, {18.52, 47.78}, {18.75, 47.79},
  {18.78, 47.93}, {18.82, 48.08}, {18.91, 48.12}, {19.00, 48.16}, {19.41, 48.12},
  {19.82, 48.08}, {19.94, 48.17}, {20.07, 48.27}, {20.26, 48.38}, {20.45, 48.50},
  {20.53, 48.52}, {20.61, 48.55}, {20.83, 48.53}, {21.05, 48.52}, {21.36, 48.44},
  {21.68, 48.37}, {21.91, 48.40}, {22.15, 48.44}, {22.14, 48.30}, {22.14, 48.16},
  {22.35, 48.62}, {22.56, 49.08}, {22.47, 49.10}, {22.38, 49.12}, {21.84, 49.27},
  {21.31, 49.42}, {21.08, 49.41}, {20.85, 49.40}, {20.75, 49.40}, {20.65, 49.41},
  {20.37, 49.37}, {20.10, 49.33}, {19.84, 49.38}, {19.58, 49.44}, {19.40, 49.48},
  {19.22, 49.52}, {19.03, 49.51}, {18.84, 49.51}, {18.64, 49.48}, {18.45, 49.45},
  {18.25, 49.31}, {18.06, 49.18}, {17.95, 49.06}, {17.85, 48.95}, {17.73, 48.91},
  {17.62, 48.87}, {17.47, 48.86}, {17.32, 48.85}, {17.19, 48.81}, {17.07, 48.77},
  {17.01, 48.62}, {16.96, 48.48}
};
static constexpr size_t SK_BORDER_COUNT = sizeof(SK_BORDER) / sizeof(SK_BORDER[0]);

// Zoznam miest pre orientáciu na mape (isMajor = zobrazené aj pri veľkom odzoomovaní)
struct City { const char* name; float lat; float lon; bool isMajor; };
static const City CITIES[] = {
  {"BA", 48.1486, 17.1077, true},  {"TT", 48.3775, 17.5883, false},
  {"NR", 48.3061, 18.0864, true},  {"TN", 48.8945, 18.0444, false},
  {"ZA", 49.2231, 18.7397, true},  {"BB", 48.7363, 19.1462, true},
  {"PO", 48.9984, 21.2393, true},  {"KE", 48.7164, 21.2611, true},
  {"BJ", 49.2918, 21.2727, false}, {"PP", 49.0595, 20.2978, false},
  {"MI", 48.7547, 21.9195, false}, {"LC", 48.3294, 19.6648, false}
};
static constexpr size_t CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

// Výrez (Crop) radarového obrazu
struct CropBox { 
  int x1, y1, x2, y2; 
  int w() const { return x2 - x1 + 1; } 
  int h() const { return y2 - y1 + 1; } 
};
CropBox crop;

// Vyrovnávacie pamäte pre dekódovanie PNG a plynulú streamovanú bilineárnu interpoláciu
uint16_t prevLine565[RADAR_IMG_W];
uint16_t currLine565[RADAR_IMG_W];
uint16_t outLine[TFT_W];
int currentScreenDy = 0;
bool prevLineValid = false;

// Globálne premenné pre konfiguráciu a stav
String lastPngName;
float centerLat = atof(DEFAULT_CENTER_LAT);
float centerLon = atof(DEFAULT_CENTER_LON);
int timeOffsetHours = DEFAULT_TIME_OFFSET_HOURS;
int carouselIntervalSec = 30;
uint32_t carouselIntervalMs = 30000;
bool carouselEnabled = true;

// Nočný režim
bool nightModeEnabled = true;
int nightStartHour = 22; // 22:00
int nightEndHour = 6;    // 06:00
bool isNightActive = false;

// Úrovne priblíženia (km)
static const float ZOOM_LEVELS_KM[] = {10.0f, 25.0f, 50.0f, 100.0f, 250.0f};
static constexpr int ZOOM_LEVEL_COUNT = sizeof(ZOOM_LEVELS_KM) / sizeof(ZOOM_LEVELS_KM[0]);
int zoomIndex = 2; // Predvolene 50 km
float currentRadiusKm = atof(DEFAULT_RADIUS_KM_TEXT);

// Stavový automat aplikácie
enum AppMode { MODE_COMBINED = 0, MODE_WEATHER = 1, MODE_PLANES = 2 };
AppMode currentMode = MODE_COMBINED;

uint32_t lastWeatherUpdateMs = 0;
uint32_t lastCarouselSwitchMs = 0;
uint32_t lastPlaneFetchMs = 0;
uint32_t lastPlaneRedrawMs = 0;
uint32_t lastPlaneFetchFixMs = 0;
static constexpr uint32_t PLANE_FETCH_INTERVAL_MS = 10000;
static constexpr uint32_t PLANE_REDRAW_INTERVAL_MS = 1000; // 1 Hz prekresľovanie pre hodiny a plynulú extrapoláciu

// Dátový model lietadla pre vykreslenie štítku
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
  char route[10];    ///< Formát letísk napr. "VIE>AMS"
  char callsign[9];  ///< Volací znak napr. "KLM1902"
  char type[5];      ///< Typ ICAO napr. "A21N"
  char alt[12];      ///< Výška v metroch napr. "10250m"
};

static constexpr size_t MAX_AIRCRAFT = 32;
AircraftData aircraftList[MAX_AIRCRAFT];
size_t aircraftCount = 0;

// Dopredné deklarácie funkcií
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
// 2. GEOGRAFICKÉ & PROJEKČNÉ FUNKCIE (MAPPING)
// =======================================================================================

inline int lonToX(float lon) { 
  return roundf((lon - LON_LEFT) * (RADAR_IMG_W - 1) / (LON_RIGHT - LON_LEFT)); 
}

inline int latToY(float lat) { 
  return roundf((LAT_TOP - lat) * (RADAR_IMG_H - 1) / (LAT_TOP - LAT_BOTTOM)); 
}

inline float mapXToScreenX(float mapX) { 
  return (mapX - crop.x1) * (float)TFT_W / (float)crop.w(); 
}

inline float mapYToScreenY(float mapY) { 
  return (mapY - crop.y1) * (float)TFT_H / (float)crop.h(); 
}

CropBox makeCrop(float lat, float lon, float radiusKm) {
  float degLat = radiusKm / 111.32f;
  float degLon = radiusKm / (111.32f * cosf(lat * DEG_TO_RAD));
  
  int cx = lonToX(lon);
  int cy = latToY(lat);
  int spanX = (int)roundf(degLon * (float)(RADAR_IMG_W - 1) / (LON_RIGHT - LON_LEFT));
  int spanY = (int)roundf(degLat * (float)(RADAR_IMG_H - 1) / (LAT_TOP - LAT_BOTTOM));
  int span = std::max(spanX, spanY);
  if (span < 10) span = 10;
  
  return {cx - span, cy - span, cx + span, cy + span};
}


// =======================================================================================
// 3. POUŽÍVATEĽSKÉ ROZHRANIE, NASTAVENIA & NOČNÝ REŽIM
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

String getRadarTimeText(const String& filename) {
  const String prefix = "cmax.kruh.";
  int start = filename.indexOf(prefix);
  if (start < 0) return "--:--";
  int dateStart = start + prefix.length();
  if (filename.length() < dateStart + 13) return "--:--";
  String hhmm = filename.substring(dateStart + 9, dateStart + 13);
  int hour = hhmm.substring(0, 2).toInt();
  int minute = hhmm.substring(2, 4).toInt();
  hour = (hour + timeOffsetHours) % 24;
  if (hour < 0) hour += 24;
  char out[6];
  snprintf(out, sizeof(out), "%02d:%02d", hour, minute);
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
  showStatus("Reset nastavenia...");
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
  showStatus("Drz pre reset");
  uint32_t start = millis();
  while (digitalRead(ZOOM_BUTTON_PIN) == LOW) {
    if (millis() - start >= RESET_HOLD_MS) resetSettingsAndRestart();
    delay(20);
  }
}

// Astronomický výpočet východu a západu slnka (v minútach od polnoci lokálneho času)
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

/** Kontrola a automatická úprava jasu podľa nočného režimu (Astronomický západ/východ slnka) */
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
  if (t->tm_year < 100) return; // NTP ešte nie je zosynchronizované

  int currentMin = t->tm_hour * 60 + t->tm_min;
  int sunriseMin = 6 * 60;  // fallback 06:00
  int sunsetMin = 21 * 60;  // fallback 21:00

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
    Serial.printf("Nočný režim (Slnko): %s (jas %d, Východ: %02d:%02d, Západ: %02d:%02d)\n", 
                  isNightActive ? "AKTÍVNY" : "VYPNUTÝ", isNightActive ? 30 : 180,
                  sunriseMin / 60, sunriseMin % 60, sunsetMin / 60, sunsetMin % 60);
  }
}

/** Zmena zoomu */
void setZoomIndex(int newIndex) {
  zoomIndex = (newIndex >= 0 && newIndex < ZOOM_LEVEL_COUNT) ? newIndex : 2;
  currentRadiusKm = ZOOM_LEVELS_KM[zoomIndex];
  crop = makeCrop(centerLat, centerLon, currentRadiusKm);
  
  prefs.begin("radar", false);
  prefs.putInt("zoom_idx", zoomIndex);
  prefs.putFloat("radius", currentRadiusKm);
  prefs.end();

  decodeRadarImage();

  if (currentMode == MODE_WEATHER) {
    renderScreen();
  } else {
    lastPlaneFetchMs = millis();
    fetchPlanesData();
    renderScreen();
  }
}

/** Prepnutie režimu aplikácie */
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

/** 
 * Inteligentná obsluha tlačidla:
 * - 1 klik = Zmena zoomu (10, 25, 50, 100, 250 km)
 * - 2 kliky = Prepnutie režimu (Spojený -> Iba počasie -> Iba lietadlá)
 * - Dlhé podržanie = Továrenský reset
 */
void handleButton() {
  static bool lastBtnState = HIGH;
  static uint32_t pressStartMs = 0;
  static uint32_t lastReleaseMs = 0;
  static int pendingClicks = 0;
  static constexpr uint32_t DOUBLE_CLICK_GAP_MS = 350;

  bool btnState = digitalRead(ZOOM_BUTTON_PIN);
  uint32_t now = millis();

  // Stlačenie tlačidla
  if (lastBtnState == HIGH && btnState == LOW) {
    pressStartMs = now;
  }
  // Držanie tlačidla
  else if (btnState == LOW) {
    if (now - pressStartMs >= RESET_HOLD_MS) {
      resetSettingsAndRestart();
      return;
    }
  }
  // Uvoľnenie tlačidla
  else if (lastBtnState == LOW && btnState == HIGH) {
    uint32_t duration = now - pressStartMs;
    if (duration >= 30 && duration < RESET_HOLD_MS) {
      pendingClicks++;
      lastReleaseMs = now;
    }
  }
  lastBtnState = btnState;

  // Vyhodnotenie kliknutí po uplynutí okna pre dvojklik
  if (pendingClicks > 0 && (now - lastReleaseMs >= DOUBLE_CLICK_GAP_MS)) {
    if (pendingClicks == 1) {
      // 1x Klik = Zmena zoomu
      setZoomIndex((zoomIndex + 1) % ZOOM_LEVEL_COUNT);
    } else if (pendingClicks >= 2) {
      // 2x Klik = Cyklické prepnutie režimu (0: Spojený, 1: Počasie, 2: Lietadlá)
      setAppMode((AppMode)((currentMode + 1) % 3), true);
    }
    pendingClicks = 0;
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA); 
  delay(100);
  showStatus("ESP MeteoRadar " + String(CURRENT_VERSION) + "\nPripajam WiFi...");

  prefs.begin("radar", true);
  String curLat = String(prefs.getFloat("lat", atof(DEFAULT_CENTER_LAT)), 4);
  String curLon = String(prefs.getFloat("lon", atof(DEFAULT_CENTER_LON)), 4);
  String curRad = String((int)prefs.getFloat("radius", atof(DEFAULT_RADIUS_KM_TEXT)));
  String curOff = String(prefs.getInt("offset", DEFAULT_TIME_OFFSET_HOURS));
  String curCar = String(prefs.getInt("car_int", 30));
  prefs.end();

  WiFiManagerParameter custom_lat("lat", "Zemepisna sirka (Lat)", curLat.c_str(), 10);
  WiFiManagerParameter custom_lon("lon", "Zemepisna dlzka (Lon)", curLon.c_str(), 10);
  WiFiManagerParameter custom_rad("radius", "Predvoleny rozsah (km)", curRad.c_str(), 5);
  WiFiManagerParameter custom_off("offset", "Casovy offset (hodiny)", curOff.c_str(), 3);
  WiFiManagerParameter custom_car("car_int", "Interval karuselu (sekundy)", curCar.c_str(), 4);

  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(15);
  wm.setBreakAfterConfig(true);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  wm.addParameter(&custom_lat);
  wm.addParameter(&custom_lon);
  wm.addParameter(&custom_rad);
  wm.addParameter(&custom_off);
  wm.addParameter(&custom_car);

  if (!wm.autoConnect("ESPMeteoRadar")) {
    showStatus("WiFi chyba\nPodrz tlacidlo 3s\npre reset");
    return;
  }

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

  carouselIntervalMs = (uint32_t)carouselIntervalSec * 1000;
  configTime(timeOffsetHours * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  // Krátke čakanie na synchronizáciu reálneho NTP času pre bezchybný TLS handshake
  uint32_t ntpStart = millis();
  while (time(nullptr) < 100000 && millis() - ntpStart < 3500) {
    delay(100);
  }

  String ipStr = WiFi.localIP().toString();
  Serial.println("\n=======================================================");
  Serial.printf("WiFi Pripojené! IP adresa: %s\n", ipStr.c_str());
  if (MDNS.begin("espmeteoradar")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("Web Dashboard: http://espmeteoradar.local alebo http://" + ipStr);
  }
  Serial.println("=======================================================\n");

  showStatus("WiFi Pripojene!\n\nIP: " + ipStr + "\nespmeteoradar.local");
  delay(2500);
}


// =======================================================================================
// 4. LOKÁLNY WEB DASHBOARD (EMBEDDED WEB SERVER)
// =======================================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="sk">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP MeteoRadar & Plane Dashboard</title>
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
      <h1>🛰️ ESP MeteoRadar & Plane Dashboard</h1>
      <p>ESP32-C3 SuperMini • GC9A01 240x240 LCD</p>
    </header>

    <!-- VÝSTRAHA PRED BLÍZKYMI ZRÁŽKAMI -->
    <div id="rain-alert-banner" class="rain-alert-banner">
      <div style="display:flex; align-items:center; gap:12px;">
        <span style="font-size:1.8rem;" id="rain-alert-icon">⛈️</span>
        <div>
          <div style="font-weight:bold; color:#fca5a5; font-size:1.05rem;">VÝSTRAHA: Blížiace sa zrážky / búrka!</div>
          <div style="font-size:0.88rem; color:#e2e8f0; margin-top:2px;">
            Zrážky vo vzdialenosti <b id="rain-alert-dist" style="color:#fbbf24; font-size:1.05rem;">--</b> km smerom na <b id="rain-alert-dir" style="color:#60a5fa; font-size:1.05rem;">--</b> (<span id="rain-alert-deg">--</span>°) od vašej polohy.
          </div>
        </div>
      </div>
      <span class="badge" id="rain-alert-badge" style="background:#ef4444; color:#fff; font-weight:bold; padding:4px 10px; border-radius:20px; font-size:0.8rem;">&lt; 15 km</span>
    </div>

    <!-- KARTA 1: AKTUÁLNY STAV RADARU -->
    <div class="card">
      <h2>📊 Aktuálny stav radaru <span class="badge badge-green" id="live-badge">ŽIVÉ DÁTA</span></h2>
      <div class="grid-stats">
        <div class="stat-box"><div class="label">Režim</div><div class="val" id="mode-val">--</div><div class="sub" id="car-sub">Karusel: ZAP</div></div>
        <div class="stat-box"><div class="label">Zoom</div><div class="val" id="zoom-val">-- km</div><div class="sub">Rozsah radaru</div></div>
        <div class="stat-box"><div class="label">Lietadlá</div><div class="val" id="planes-count">0</div><div class="sub">V okruhu</div></div>
        <div class="stat-box"><div class="label">WiFi Signál</div><div class="val" id="wifi-rssi">-- dBm</div><div class="sub" id="wifi-pct">Kvalita: -- %</div></div>
      </div>
    </div>

    <!-- KARTA 2: STAV SYSTÉMU & HARDVÉRU -->
    <div class="card">
      <h2>🖥️ Systém & Stav hardvéru</h2>
      <div class="grid-stats">
        <div class="stat-box">
          <div class="label">Procesor (CPU)</div>
          <div class="val" id="sys-cpu">160 MHz</div>
          <div class="sub" id="sys-temp">Teplota: -- °C</div>
        </div>
        <div class="stat-box">
          <div class="label">Voľná RAM</div>
          <div class="val" id="sys-ram">-- KB</div>
          <div class="sub" id="sys-ram-sub">z 320 KB</div>
        </div>
        <div class="stat-box">
          <div class="label">Flash Pamäť</div>
          <div class="val" id="sys-flash">4 MB</div>
          <div class="sub" id="sys-heap-min">Min RAM: -- KB</div>
        </div>
        <div class="stat-box">
          <div class="label">Doba behu</div>
          <div class="val" id="sys-uptime">00:00:00</div>
          <div class="sub" id="sys-ip">IP: --</div>
        </div>
      </div>
    </div>

    <!-- KARTA 3: RÝCHLE OVLÁDANIE -->
    <div class="card">
      <h2>🎮 Rýchle ovládanie radaru</h2>
      <label>Zmena mierky (Zoom):</label>
      <div class="btn-group">
        <button onclick="setZoom(0)" id="zbtn-0">10 km</button>
        <button onclick="setZoom(1)" id="zbtn-1">25 km</button>
        <button onclick="setZoom(2)" id="zbtn-2">50 km</button>
        <button onclick="setZoom(3)" id="zbtn-3">100 km</button>
        <button onclick="setZoom(4)" id="zbtn-4">250 km</button>
      </div>
      <label style="margin-top: 14px;">Manuálne prepnutie režimu:</label>
      <div class="btn-group">
        <button onclick="setMode('combined')" id="mbtn-comb">🛰️ Spojený (Radar + Lietadlá)</button>
        <button onclick="setMode('weather')" id="mbtn-weather">🌦️ Iba počasie</button>
        <button onclick="setMode('planes')" id="mbtn-planes">✈️ Iba lietadlá</button>
        <button onclick="toggleCarousel()" id="mbtn-car">🔄 Karusel</button>
      </div>
    </div>

    <!-- KARTA 4: TABUĽKA LIETADIEL -->
    <div class="card">
      <h2>✈️ Zoznam lietadiel v dosahu radaru</h2>
      <div style="overflow-x: auto;">
        <table>
          <thead>
            <tr><th>Let / Trasa</th><th>Typ</th><th>Rýchlosť</th><th>Výška</th><th>Pozícia</th></tr>
          </thead>
          <tbody id="planes-tbody">
            <tr><td colspan="5" style="text-align:center; color:#8b949e;">Načítavam zoznam lietadiel...</td></tr>
          </tbody>
        </table>
      </div>
    </div>

    <!-- KARTA: INTERAKTÍVNA ŽIVÁ MAPA (LEAFLET) -->
    <div class="card">
      <h2>🗺️ Živá radarová mapa (Leaflet)</h2>
      <div id="map"></div>
      <div style="font-size: 0.8rem; color: #8b949e; margin-top: 8px; text-align: center;">
        💡 Kliknutím na mapu alebo potiahnutím značky zmeníte stred radaru. Zelený kruh zobrazuje okruh <span id="map-radius-txt">50</span> km.
      </div>
    </div>

    <!-- KARTA 5: NASTAVENIA POLOHY & RADARU -->
    <div class="card">
      <h2>📍 Nastavenia polohy a radaru</h2>
      
      <div style="margin-bottom: 12px;">
        <label>🔍 Vyhľadať obec, mesto alebo adresu:</label>
        <div style="display: flex; gap: 6px; margin-top: 4px;">
          <input type="text" id="inp-search-city" placeholder="Napr. Senec, Terchová, Zvolen..." style="flex: 1;" onkeydown="if(event.key==='Enter'){event.preventDefault();searchCityLocation();}">
          <button type="button" onclick="searchCityLocation()" id="btn-search-city" style="background:#238636; border-color:#2ea043; white-space:nowrap; padding:6px 14px; font-weight:600;">🔍 Hľadať</button>
        </div>
        <div id="city-search-results" style="display:none; margin-top:6px; background:#0d1117; border:1px solid #30363d; border-radius:6px; padding:6px;"></div>
      </div>

      <div style="margin-bottom: 12px;">
        <label>Alebo rýchly výber mesta:</label>
        <select onchange="onCityPreset(this)" style="margin-top: 4px;">
          <option value="">-- Zvoľte mesto pre automatické vyplnenie --</option>
          <option value="48.1486,17.1077">Bratislava (48.1486, 17.1077)</option>
          <option value="48.7164,21.2611">Košice (48.7164, 21.2611)</option>
          <option value="48.9984,21.2393">Prešov (48.9984, 21.2393)</option>
          <option value="49.2231,18.7397">Žilina (49.2231, 18.7397)</option>
          <option value="48.7363,19.1462">Banská Bystrica (48.7363, 19.1462)</option>
          <option value="48.3061,18.0864">Nitra (48.3061, 18.0864)</option>
          <option value="48.3775,17.5883">Trnava (48.3775, 17.5883)</option>
          <option value="48.8945,18.0444">Trenčín (48.8945, 18.0444)</option>
          <option value="49.0595,20.2978">Poprad (49.0595, 20.2978)</option>
          <option value="49.2918,21.2727">Bardejov (49.2918, 21.2727)</option>
          <option value="48.7547,21.9195">Michalovce (48.7547, 21.9195)</option>
          <option value="48.6690,19.1230">Zvolen (48.6690, 19.1230)</option>
          <option value="48.3294,19.6648">Lučenec (48.3294, 19.6648)</option>
          <option value="49.0806,19.3004">Ružomberok (49.0806, 19.3004)</option>
          <option value="49.0645,18.9228">Martin (49.0645, 18.9228)</option>
          <option value="48.7712,18.6253">Prievidza (48.7712, 18.6253)</option>
        </select>
      </div>

      <button type="button" onclick="useMyLocation()" id="btn-gps" style="background:#1f6feb; border-color:#58a6ff; width:100%; margin-bottom:12px; font-weight:600;">
        📍 Zistiť polohu zo siete / GPS
      </button>

      <form onsubmit="saveSettings(event)">
        <div class="form-group">
          <div><label>Zemepisná šírka (Lat):</label><input type="text" name="lat" id="inp-lat" required oninput="userIsEditing=true"></div>
          <div><label>Zemepisná dĺžka (Lon):</label><input type="text" name="lon" id="inp-lon" required oninput="userIsEditing=true"></div>
        </div>
        <div class="form-group">
          <div><label>Interval karuselu (s):</label><input type="number" name="car_int" id="inp-car" min="5" max="300" required oninput="userIsEditing=true"></div>
          <div><label>Časový offset (h):</label><input type="number" name="offset" id="inp-off" min="-12" max="12" required oninput="userIsEditing=true"></div>
        </div>
        <div class="form-group">
          <div>
            <label>Vzdialenosť výstrahy:</label>
            <select name="alert_km" id="inp-alert-km" onchange="userIsEditing=true">
              <option value="0">Vypnuté</option>
              <option value="5">5 km</option>
              <option value="10">10 km</option>
              <option value="15">15 km (predvolené)</option>
              <option value="20">20 km</option>
              <option value="25">25 km</option>
              <option value="30">30 km</option>
              <option value="50">50 km</option>
            </select>
          </div>
          <div>
            <label>Citlivosť výstrahy (Min. rozsah):</label>
            <select name="alert_sens" id="inp-alert-sens" onchange="userIsEditing=true">
              <option value="10">Vysoká (10 px - citlivá)</option>
              <option value="25">Stredná (25 px - odporúčaná)</option>
              <option value="50">Nízka (50 px - iba veľké mraky)</option>
            </select>
          </div>
        </div>
        <div style="display: flex; gap: 8px; margin-top: 8px;">
          <input type="submit" id="btn-save-cfg" value="💾 Uložiť nastavenia" style="background:#238636; border-color:#2ea043; flex:1;">
          <button type="button" onclick="rebootEsp()" style="background:#da3633; border-color:#f85149;">🔄 Reštart</button>
        </div>
      </form>
    </div>

    <!-- KARTA 6: ZMENA WI-FI -->
    <div class="card">
      <h2>📶 Zmena Wi-Fi siete <button type="button" onclick="scanWifi()" id="btn-scan" style="font-size:0.8rem; padding:4px 10px;">🔍 Vyhľadať siete</button></h2>
      <form onsubmit="changeWifi(event)">
        <div style="margin-bottom: 10px;">
          <label>Dostupné Wi-Fi siete v okolí:</label>
          <select id="wifi-select" onchange="onWifiSelect(this)" style="margin-bottom: 6px;">
            <option value="">-- Kliknite na Vyhľadať siete alebo zadajte ručne nižšie --</option>
          </select>
          <label>Názov siete (SSID):</label>
          <input type="text" id="wifi-ssid" placeholder="Napr. MojaDomacaWiFi" required>
        </div>
        <div style="margin-bottom: 12px;">
          <label>Heslo k Wi-Fi sieti:</label>
          <input type="password" id="wifi-pass" placeholder="Heslo (ponechajte prázdne pre otvorenú sieť)">
        </div>
        <input type="submit" id="btn-save-wifi" value="💾 Pripojiť k novej Wi-Fi" style="background:#238636; border-color:#2ea043; width:100%;">
      </form>
    </div>

    <!-- KARTA 7: AKTUALIZÁCIA FIRMVÉRU (OTA) -->
    <div class="card">
      <h2>🚀 Aktualizácia firmvéru (OTA)</h2>
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">
        <div><b>Aktuálna verzia:</b> <span id="ota-cur-ver" style="color:#58a6ff; font-weight:bold;">v1.9.0</span></div>
        <button type="button" onclick="checkOta()" id="btn-check-ota" style="font-size:0.8rem; padding:6px 12px; background:#1f6feb; border-color:#58a6ff;">🔍 Skontrolovať GitHub</button>
      </div>

      <div id="ota-info-box" style="display:none; background:#0d1117; border:1px solid #30363d; border-radius:8px; padding:12px; margin-bottom:14px;">
        <div id="ota-status-text" style="font-weight:600; margin-bottom:6px;"></div>
        <div id="ota-release-notes" style="font-size:0.85rem; color:#8b949e; margin-bottom:10px; max-height:100px; overflow-y:auto; white-space:pre-wrap;"></div>
        <button type="button" onclick="startGithubOta()" id="btn-start-ota" style="background:#238636; border-color:#2ea043; width:100%; font-weight:bold; display:none;">
          ⬇️ Stiahnuť a aktualizovať z GitHubu
        </button>
      </div>

      <hr style="border:0; border-top:1px solid #30363d; margin:14px 0;">

      <label>📁 Manuálny OTA upload z počítača / mobilu:</label>
      <div style="font-size:0.8rem; color:#f0883e; background:rgba(240,136,62,0.12); border:1px solid rgba(240,136,62,0.4); border-radius:6px; padding:10px; margin:6px 0 10px 0; line-height: 1.4;">
        ⚠️ <b>UPOZORNENIE K SÚBOROM FIRMVÉRU:</b><br>
        • Pre tento webový OTA formulár použite <b>výhradne súbor <code>firmware.bin</code></b> (samotná aplikácia z GitHub Release alebo <code>.pio/build/...</code>).<br>
        • <b>NIKDY</b> sem nenahrávajte <code>merged-firmware.bin</code>! Súbor <code>merged-firmware.bin</code> obsahuje bootloader a partície od adresy 0x0 a je určený <b>iba pre flashovanie cez USB kábel</b>.
      </div>
      <form id="upload-form" onsubmit="uploadLocalOta(event)">
        <input type="file" id="ota-file" accept=".bin" required style="margin-bottom:8px;">
        <input type="submit" id="btn-upload-ota" value="📁 Nahrať firmvér (.bin) do ESP" style="background:#30363d; border-color:#8b949e; width:100%;">
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

        // Výstraha pred zrážkami
        if (d.rain_alert && d.precip_dist >= 0) {
          document.getElementById('rain-alert-banner').style.display = 'flex';
          document.getElementById('rain-alert-dist').innerText = d.precip_dist.toFixed(1);
          document.getElementById('rain-alert-dir').innerText = d.alert_dir || '--';
          document.getElementById('rain-alert-deg').innerText = (typeof d.alert_bearing === 'number' && d.alert_bearing >= 0) ? d.alert_bearing : '--';
          const alertTitle = document.querySelector('#rain-alert-banner div div:first-child');
          if (alertTitle) {
            alertTitle.innerText = d.alert_incoming ? '⛈️ VÝSTRAHA: Blížiace sa zrážky (v smere vetra)!' : '🌦️ UPOZORNENIE: Zrážky v monitorovanom okruhu';
          }
          if (document.getElementById('rain-alert-badge')) {
            document.getElementById('rain-alert-badge').innerText = (d.alert_incoming ? 'Smerujú k nám • ' : '') + '< ' + (d.alert_km || 15) + ' km';
          }
        } else {
          document.getElementById('rain-alert-banner').style.display = 'none';
        }

        // Stav radaru
        document.getElementById('mode-val').innerText = d.mode === 0 ? '🛰️ Spojený (Tactical)' : (d.mode === 1 ? '🌦️ Iba počasie' : '✈️ Iba lietadlá');
        document.getElementById('car-sub').innerText = d.car_en ? 'Karusel: ZAP (' + d.car_int + 's)' : 'Karusel: VYP';
        document.getElementById('zoom-val').innerText = d.radius + ' km';
        document.getElementById('planes-count').innerText = d.planes ? d.planes.length : 0;
        document.getElementById('wifi-rssi').innerText = d.rssi + ' dBm';
        document.getElementById('wifi-pct').innerText = 'Kvalita: ' + rssiToPct(d.rssi) + ' % (' + (d.ssid || '') + ')';

        // Hardvérová telemetria & Stav
        if (d.version) document.getElementById('ota-cur-ver').innerText = d.version;
        document.getElementById('sys-cpu').innerText = (d.cpu_mhz || 160) + ' MHz';
        document.getElementById('sys-temp').innerText = 'Teplota: ' + (d.temp ? d.temp.toFixed(1) : '--') + ' °C';
        document.getElementById('sys-ram').innerText = d.heap_free + ' KB';
        const ramPct = Math.round((d.heap_free / (d.heap_total || 320)) * 100);
        document.getElementById('sys-ram-sub').innerText = 'Voľných ' + ramPct + ' % (z ' + (d.heap_total || 320) + ' KB)';
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
        document.getElementById('mbtn-car').innerText = d.car_en ? '🔄 Karusel: ZAP' : '⏸️ Karusel: VYP';

        // Form fields (iba ak používateľ práve neupravuje formulár)
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

        // Planes table
        const tbody = document.getElementById('planes-tbody');
        if (!d.planes || d.planes.length === 0) {
          tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; color:#8b949e;">V okruhu ' + d.radius + ' km nie sú žiadne lietadlá</td></tr>';
        } else {
          let html = '';
          for (const p of d.planes) {
            const displayId = p.route ? '<span class="route">' + p.route + '</span> (' + p.cs + ')' : '<b>' + p.cs + '</b>';
            const kmh = Math.round(p.gs * 1.852);
            html += '<tr><td>' + displayId + '</td><td>' + (p.t || '-') + '</td><td class="speed">' + kmh + ' km/h</td><td class="alt">' + p.alt + '</td><td>' + p.lat.toFixed(3) + ', ' + p.lon.toFixed(3) + '</td></tr>';
          }
          tbody.innerHTML = html;
        }

        // Aktualizácia interaktívnej Leaflet mapy
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
        centerMarker.bindPopup('<b>Stred radaru</b><br>Potiahnutím zmeníte polohu');

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

      // Aktualizácia lietadiel na mape
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
                               `${p.route ? `Trasa: <b>${p.route}</b><br>` : ''}` +
                               `Výška: ${p.alt || '--'} | Rýchlosť: ${p.gs ? Math.round(p.gs * 1.852) + ' km/h' : '--'}<br>` +
                               `${isEmg ? '<span style="color:#f85149; font-weight:bold;">🚨 NÚDZA SQ ' + (p.sq || '') + '</span>' : ''}`;

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
      if (confirm('Naozaj reštartovať ESP MeteoRadar?')) {
        await fetch('/api/reboot');
        alert('ESP sa reštartuje...');
      }
    }

    function onCityPreset(sel) {
      if (!sel.value) return;
      userIsEditing = true;
      const parts = sel.value.split(',');
      document.getElementById('inp-lat').value = parseFloat(parts[0]).toFixed(4);
      document.getElementById('inp-lon').value = parseFloat(parts[1]).toFixed(4);
    }

    async function saveSettings(e) {
      e.preventDefault();
      const btn = document.getElementById('btn-save-cfg');
      const old = btn.value;
      btn.value = '⏳ Ukladám...';
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
        btn.value = '✅ Uložené!';
        setTimeout(() => { btn.value = old; btn.disabled = false; }, 2500);
        loadData();
      } catch (err) {
        alert('Chyba pri ukladaní: ' + err);
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
      btn.innerText = '⏳ Hľadám...';
      btn.disabled = true;
      resBox.style.display = 'none';
      resBox.innerHTML = '';
      userIsEditing = true;

      try {
        const url = 'https://nominatim.openstreetmap.org/search?format=json&q=' + encodeURIComponent(q) + '&countrycodes=sk,cz&limit=5';
        const res = await fetch(url);
        const list = await res.json();

        if (list && list.length > 0) {
          if (list.length === 1) {
            applyCoords(list[0].lat, list[0].lon, list[0].display_name.split(',')[0]);
          } else {
            resBox.style.display = 'block';
            resBox.innerHTML = '<div style="font-size:0.8rem; color:#8b949e; margin-bottom:4px;">Zvoľte správne miesto:</div>';
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
          alert('Miesto "' + q + '" sa nenašlo. Skúste zadať iný alebo presnejší názov.');
        }
      } catch (err) {
        alert('Chyba pri vyhľadávaní: ' + err);
      } finally {
        btn.innerText = old;
        btn.disabled = false;
      }
    }

    async function useMyLocation() {
      const btn = document.getElementById('btn-gps');
      const old = btn.innerText;
      btn.innerText = '⏳ Zisťujem polohu...';
      userIsEditing = true;

      if (navigator.geolocation) {
        try {
          navigator.geolocation.getCurrentPosition(
            (pos) => {
              applyCoords(pos.coords.latitude, pos.coords.longitude, 'GPS');
            },
            async (err) => {
              console.warn('GPS nedostupné na HTTP, prepínam na sieťovú IP...', err);
              await fetchIpLocation();
            },
            { enableHighAccuracy: true, timeout: 6000 }
          );
          return;
        } catch (e) {
          // Prehliadač odmietol volanie
        }
      }
      await fetchIpLocation();

      async function fetchIpLocation() {
        btn.innerText = '⏳ Zisťujem polohu cez sieť...';
        try {
          const res = await fetch('https://ipwho.is/');
          const d = await res.json();
          if (d && d.success && d.latitude && d.longitude) {
            applyCoords(d.latitude, d.longitude, (d.city || 'IP Sieť'));
            return;
          }
        } catch (e) {}

        try {
          const res2 = await fetch('https://freeipapi.com/api/json');
          const d2 = await res2.json();
          if (d2 && d2.latitude && d2.longitude) {
            applyCoords(d2.latitude, d2.longitude, (d2.cityName || 'IP Sieť'));
            return;
          }
        } catch (e2) {}

        alert('Automatické zistenie polohy cez sieť zlyhalo. Použite prosím vyhľadanie mesta vyššie.');
        btn.innerText = old;
      }

      function applyCoords(lat, lon, src) {
        const fLat = parseFloat(lat).toFixed(4);
        const fLon = parseFloat(lon).toFixed(4);
        document.getElementById('inp-lat').value = fLat;
        document.getElementById('inp-lon').value = fLon;
        btn.innerText = '✅ Nastavené: ' + src + ' (' + fLat + ', ' + fLon + ')';
        setTimeout(() => { btn.innerText = old; }, 4000);
      }
    }

    async function scanWifi() {
      const btn = document.getElementById('btn-scan');
      const old = btn.innerText;
      btn.innerText = '⏳ Skenujem...';
      try {
        const res = await fetch('/api/scan');
        const list = await res.json();
        const sel = document.getElementById('wifi-select');
        sel.innerHTML = '<option value="">-- Vyberte nájdenú sieť (' + list.length + ') --</option>';
        list.forEach(w => {
          const opt = document.createElement('option');
          opt.value = w.ssid;
          opt.innerText = w.ssid + ' (' + w.rssi + ' dBm' + (w.enc ? ' 🔒' : '') + ')';
          sel.appendChild(opt);
        });
        btn.innerText = '✅ Nájdených: ' + list.length;
      } catch (e) {
        alert('Chyba pri skenovaní sietí: ' + e);
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
      if (!confirm('Naozaj chcete prepnúť zariadenie na Wi-Fi sieť "' + ssid + '"?')) return;
      const btn = document.getElementById('btn-save-wifi');
      btn.disabled = true;
      btn.value = '⏳ Pripájam k novej sieti...';
      try {
        await fetch('/api/wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass)
        });
        alert('Prihlasovacie údaje boli odoslané. ESP sa pripája k sieti "' + ssid + '".\nSkontrolujte novú IP adresu na displeji zariadenia.');
      } catch (err) {
        alert('Odoslanie zlyhalo: ' + err);
        btn.disabled = false;
        btn.value = '💾 Pripojiť k novej Wi-Fi';
      }
    }

    async function checkOta() {
      const btn = document.getElementById('btn-check-ota');
      const box = document.getElementById('ota-info-box');
      const statusTxt = document.getElementById('ota-status-text');
      const notes = document.getElementById('ota-release-notes');
      const startBtn = document.getElementById('btn-start-ota');
      
      btn.disabled = true;
      btn.innerText = '⏳ Kontrolujem...';
      box.style.display = 'block';
      statusTxt.innerText = 'Pripájam k serveru GitHub...';
      notes.innerText = '';
      startBtn.style.display = 'none';

      try {
        const res = await fetch('/api/ota/check');
        const d = await res.json();
        
        if (d.has_update) {
          statusTxt.innerHTML = '🎉 <span style="color:#2ea043;">Dostupná nová verzia: ' + d.latest_version + '</span> (vaša: ' + d.current_version + ')';
          notes.innerText = d.notes || d.name || '';
          startBtn.style.display = 'block';
          startBtn.innerText = '🚀 Aktualizovať na ' + d.latest_version;
        } else {
          statusTxt.innerHTML = '✅ <span style="color:#58a6ff;">Používate najnovšiu verziu ' + d.current_version + '</span>';
          notes.innerText = d.name ? ('Posledný release: ' + d.name) : '';
          startBtn.style.display = 'block';
          startBtn.innerText = '🔄 Preinštalovať ' + d.current_version + ' z GitHubu';
        }
      } catch (err) {
        statusTxt.innerHTML = '❌ <span style="color:#f85149;">Chyba pri kontrole: ' + err + '</span>';
      } finally {
        btn.disabled = false;
        btn.innerText = '🔍 Skontrolovať GitHub';
      }
    }

    async function startGithubOta() {
      if (!confirm('Naozaj spustiť OTA aktualizáciu z GitHubu?\nPočas aktualizácie nevypínajte napájanie zariadenia!')) return;
      const startBtn = document.getElementById('btn-start-ota');
      startBtn.disabled = true;
      startBtn.innerText = '⏳ Sťahujem a inštalujem... Sledujte displej ESP32';
      try {
        await fetch('/api/ota/github', { method: 'POST' });
        alert('OTA aktualizácia bola spustená!\nESP32 sťahuje firmvér a po dokončení sa automaticky reštartuje.');
      } catch (e) {
        alert('Chyba pri spustení: ' + e);
        startBtn.disabled = false;
      }
    }

    async function uploadLocalOta(e) {
      e.preventDefault();
      const fileInp = document.getElementById('ota-file');
      if (!fileInp.files || fileInp.files.length === 0) return;
      if (!confirm('Naozaj nahrať vybraný firmvér "' + fileInp.files[0].name + '"?')) return;

      const btn = document.getElementById('btn-upload-ota');
      btn.disabled = true;
      btn.value = '⏳ Nahrávam firmvér do ESP...';

      const formData = new FormData();
      formData.append('firmware', fileInp.files[0]);

      try {
        const res = await fetch('/api/ota/upload', {
          method: 'POST',
          body: formData
        });
        if (res.ok) {
          alert('Firmvér úspešne nahraný!\nESP32 sa reštartuje...');
        } else {
          alert('Chyba pri nahrávaní súboru.');
          btn.disabled = false;
          btn.value = '📁 Nahrať firmvér z PC/mobilu';
        }
      } catch (err) {
        alert('Zlyhalo: ' + err);
        btn.disabled = false;
        btn.value = '📁 Nahrať firmvér z PC/mobilu';
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
    crop = makeCrop(centerLat, centerLon, currentRadiusKm);

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
    decodeRadarImage();
    
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

    showStatus("Pripajam k novej\nWiFi:\n" + newSsid);
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
    server.send(503, "application/json", "{\"error\":\"WiFi nie je pripojené\"}");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(4000);
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("ESP32-MeteoRadar");

  JsonDocument doc;
  doc["current_version"] = CURRENT_VERSION;
  doc["has_update"] = false;
  doc["latest_version"] = CURRENT_VERSION;
  doc["name"] = "";
  doc["notes"] = "";

  if (http.begin(client, "https://api.github.com/repos/hackra76/ESP-MeteoRadar/releases/latest")) {
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
    server.send(503, "application/json", "{\"error\":\"WiFi nie je pripojené\"}");
    return;
  }

  server.send(200, "application/json", "{\"status\":\"starting\"}");
  delay(300);

  releaseCanvas();
  showStatus("OTA Aktualizacia...\nPripajam GitHub...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(12000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(35000);
  http.setUserAgent("ESP32-MeteoRadar");

  String url = "https://github.com/hackra76/ESP-MeteoRadar/releases/latest/download/firmware.bin";

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
                  showStatus("OTA Aktualizacia...\nStahujem: " + String(pct) + " %");
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
          showStatus("OTA Dokoncena!\nRestartujem...");
          delay(1500);
          ESP.restart();
          return;
        } else {
          showStatus("Chyba zapisu OTA:\n" + String(Update.errorString()));
          delay(3000);
        }
      } else {
        showStatus("Chyba inicializacie\nOTA Update");
        delay(3000);
      }
    } else {
      showStatus("Chyba stahovania\nHTTP: " + String(httpCode));
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
    showStatus("Manualna OTA...\nPripravujem...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      showStatus("Chyba OTA start!");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      showStatus("Chyba zapisu OTA!");
    } else {
      static int lastUploadPct = -1;
      int pct = (upload.totalSize > 0) ? (int)((upload.currentSize * 100) / upload.totalSize) : 0;
      if (pct != lastUploadPct && pct % 10 == 0) {
        lastUploadPct = pct;
        showStatus("Manualna OTA...\n" + String(pct) + " %");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      showStatus("OTA Dokoncena!\nRestartujem...");
    } else {
      showStatus("Chyba OTA END!");
    }
  }
}

void handleApiOtaUploadDone() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(500, "text/plain", "Chyba OTA aktualizacie");
  } else {
    server.send(200, "text/plain", "OK - Restartujem ESP...");
    delay(1000);
    ESP.restart();
  }
}

void setupWebServer() {
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
  Serial.println("Web server spustený na porte 80!");
}


// =======================================================================================
// 5. SHMÚ METEORADAR (SŤAHOVANIE & SPRACOVANIE)
// =======================================================================================

String findLatestPngNameInText(const String& text, String& newestTs) {
  const String prefix = "cmax.kruh.";
  String latest;
  int pos = 0;
  while (true) {
    int idx = text.indexOf(prefix, pos);
    if (idx < 0) break;
    int end = text.indexOf(".png", idx);
    if (end < 0) break;
    String name = text.substring(idx, end + 4);
    String ts = name.substring(name.indexOf(prefix) + prefix.length(), name.indexOf(prefix) + prefix.length() + 13);
    if (ts > newestTs) { 
      newestTs = ts; 
      latest = name; 
    }
    pos = end + 4;
  }
  return latest;
}

bool downloadLatestRadar() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SHMU] WiFi nie je pripojené, preskakujem sťahovanie radaru.");
    return false;
  }

  releaseCanvas(); // Uvoľníme 58 KB RAM pre bezpečný TLS handshake

  Serial.println("[SHMU] Sťahujem zoznam snímok zo SHMÚ API (HTTPS)...");

  bool success = false;
  for (int attempt = 1; attempt <= 2 && !success; attempt++) {
    WiFiClientSecure client; 
    client.setInsecure();
    client.setHandshakeTimeout(10000);
    HTTPClient http; 
    http.setTimeout(15000);

    if (http.begin(client, SHMU_API_URL)) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String window, latest, newestTs;
        WiFiClient* stream = http.getStreamPtr();
        uint8_t buf[512];
        uint32_t startReadMs = millis();

        while (http.connected() || stream->available()) {
          size_t avail = stream->available();
          if (avail) {
            size_t toRead = (avail < sizeof(buf)) ? avail : sizeof(buf);
            int n = stream->readBytes(buf, toRead);
            if (n > 0) {
              window += String((const char*)buf, n);
              String candidate = findLatestPngNameInText(window, newestTs);
              if (!candidate.isEmpty()) latest = candidate;
              if (window.length() > 300) window = window.substring(window.length() - 200);
            }
            startReadMs = millis();
          } else {
            if (millis() - startReadMs > 10000) break;
            delay(2);
          }
          if (stream->available() == 0 && !http.connected()) break;
        }
        http.end(); 
        client.stop();

        if (!latest.isEmpty()) {
          Serial.printf("[SHMU] Najnovšia snímka: %s\n", latest.c_str());
          if (latest == lastPngName && SPIFFS.exists(RADAR_FILE)) {
            Serial.println("[SHMU] Snímka je už stiahnutá v SPIFFS cache.");
            success = true;
          } else {
            String url = String(SHMU_BASE_URL) + latest;
            Serial.printf("[SHMU] Sťahujem PNG: %s\n", url.c_str());
            WiFiClientSecure clientImg;
            clientImg.setInsecure();
            clientImg.setHandshakeTimeout(15000);
            HTTPClient httpImg; 
            httpImg.setTimeout(25000);
            int imgCode = httpImg.begin(clientImg, url) ? httpImg.GET() : -1;
            if (imgCode == HTTP_CODE_OK) {
              if (SPIFFS.exists(RADAR_FILE)) SPIFFS.remove(RADAR_FILE);
              File f = SPIFFS.open(RADAR_FILE, "w");
              if (!f) {
                Serial.println("[SHMU] Zlyhal zápis do SPIFFS, formátujem a skúšam znova...");
                SPIFFS.format();
                SPIFFS.begin(true);
                f = SPIFFS.open(RADAR_FILE, "w");
              }
              if (f) {
                httpImg.writeToStream(&f);
                size_t sz = f.size();
                f.close();
                lastPngName = latest;
                prefs.begin("radar", false);
                prefs.putString("last_png", lastPngName);
                prefs.end();
                httpImg.end();
                clientImg.stop();
                Serial.printf("[SHMU] PNG úspešne uložené do SPIFFS (%u B)!\n", (unsigned int)sz);
                success = true;
              } else {
                Serial.println("[SHMU] Chyba otvorenia SPIFFS pre zápis!");
              }
            } else {
              Serial.printf("[SHMU] Chyba sťahovania PNG, HTTP: %d\n", imgCode);
            }
            httpImg.end();
            clientImg.stop();
          }
        } else {
          Serial.println("[SHMU] V odpovedi sa nenašiel žiadny cmax.kruh PNG súbor.");
        }
      } else {
        Serial.printf("[SHMU] Chyba API GET, HTTP: %d (pokus %d)\n", httpCode, attempt);
        http.end();
        client.stop();
      }
    } else {
      Serial.printf("[SHMU] Zlyhal http.begin() pre API (pokus %d)\n", attempt);
      client.stop();
    }
    if (!success && attempt < 2) delay(2000);
  }

  ensureCanvas(); // Obnovíme canvas okamžite po ukončení HTTPS spojenia
  if (success) {
    decodeRadarImage();
  }
  return success;
}

/**
 * Sťahovanie smeru a rýchlosti vetra z Open-Meteo API
 * Hladina 700 hPa (~3000 m n. m.) je riadiaca hladina pre pohyb zrážkových systémov
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
        Serial.printf("[WIND] Open-Meteo vietor úspešne načítaný: 700hPa = %.0f° (%.1f km/h), Povrch = %.0f° (%.1f km/h)\n",
                      windDir700hPa, windSpeed700hPa, windDirSurface, windSpeedSurface);
      } else {
        Serial.printf("[WIND] Chyba parsovania JSON z Open-Meteo: %s\n", err.c_str());
      }
    } else {
      Serial.printf("[WIND] Open-Meteo HTTP GET zlyhal, kód: %d\n", code);
    }
    http.end();
  }
  client.stop();
  lastWindFetchMs = millis();
}


// =======================================================================================
// 6. ADS-B LETECKÝ RADAR & VRS LETOVÉ TRASY
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

inline void applyTagStyle()      { tft.setTextSize(0.80f); }
inline void applyCardinalStyle() { tft.setTextSize(0.75f); }
inline void applyScaleStyle()    { tft.setTextSize(0.75f); }
inline void applyCityStyle()     { tft.setTextSize(0.80f); }

// ---- Statické vyhľadávanie letových trás (VRS standing-data) ----
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
  // Jemný, prehľadný vektor kurzu: 4 až 9 pixelov podľa rýchlosti
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

  // 1px jemný a decentný vektor kurzu
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

  // HUD kontrastný box pre perfektnú čitateľnosť textu aj nad zrážkovými mrakmi
  int box_x = tag_on_right ? (anchor_x - 3) : (anchor_x - block_w - 3);
  int box_y = ly - 1;
  int box_w = block_w + 6;
  int box_h = block_h + 2;
  uint16_t borderColor = ac.is_emergency ? ((millis() % 600 < 300) ? target.color565(255, 30, 30) : target.color565(255, 255, 0)) : (ac.is_mil ? target.color565(180, 40, 40) : target.color565(35, 65, 100));
  target.fillRoundRect(box_x, box_y, box_w, box_h, 3, target.color565(12, 16, 22));
  target.drawRoundRect(box_x, box_y, box_w, box_h, 3, borderColor);

  // Riadok 1: Trasa letísk (fialová) alebo Callsign (biela/červená/núdza)
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

  // Riadok 2: Typ lietadla (modrá) + Rýchlosť v km/h (svetlozelená)
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

  // Riadok 3: Výška v metroch (žltá) + Šípka stúpania/klesania
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

void drawEdgeIndicator(LovyanGFX& target, int mapX, int mapY, bool is_mil) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  float dx = (float)(mapX - crop.x1) * TFT_W / crop.w() - cx;
  float dy = (float)(mapY - crop.y1) * TFT_H / crop.h() - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 1.0f) return;

  float angle = atan2f(dy, dx);
  // Umiestnenie do vonkajšieho prstenca (114 px) medzi zeleným kruhom (105 px) a okrajom displeja (120 px)
  int edgeX = cx + (int)roundf(cosf(angle) * 114.0f);
  int edgeY = cy + (int)roundf(sinf(angle) * 114.0f);
  uint16_t dotColor = is_mil ? target.color565(255, 40, 40) : target.color565(255, 150, 0);

  target.fillCircle(edgeX, edgeY, 3, dotColor);
  target.drawCircle(edgeX, edgeY, 3, TFT_BLACK);
}

// Stream wrapper, ktorý trpezlivo čaká na príchod TLS paketov bez výpadku pamäte
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

/** Stiahnutie zoznamu lietadiel z ADS-B API */
void fetchPlanesData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ADS-B] WiFi nie je pripojené, preskakujem sťahovanie lietadiel.");
    return;
  }

  releaseCanvas(); // Uvoľníme 58 KB RAM pre bezpečný a stabilný TLS handshake aj pri 100/250 km!

  // Optimalizovaný rádius dopytu: pri 250 km postačuje 200 km (ušetrí ~40% dát)
  float fetchRadiusKm = currentRadiusKm;
  if (fetchRadiusKm >= 250.0f) fetchRadiusKm = 200.0f;
  float radiusNm = fetchRadiusKm / 1.852f;
  String url = "https://opendata.adsb.fi/api/v3/lat/" + String(centerLat, 4) + "/lon/" + String(centerLon, 4) + "/dist/" + String(radiusNm, 1);

  Serial.printf("[ADS-B] Sťahujem lietadlá (HTTPS, stred: %.4f, %.4f, okruh: %.0f km)...\n", centerLat, centerLon, currentRadiusKm);

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

      JsonDocument doc;
      BufferedStream bStream(http.getStreamPtr(), 10000);
      DeserializationError err = deserializeJson(doc, bStream, DeserializationOption::Filter(filter));
      
      http.end();
      client.stop();

      if (err) {
        Serial.printf("[ADS-B] Chyba parsovania JSON: %s\n", err.c_str());
      } else {
        JsonArray acList = doc["ac"].as<JsonArray>();

        struct Cand {
          float distKm;
          uint16_t idx;
          uint8_t prio; // 0 = Emergency, 1 = Military, 2 = Civil
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
          bool is_emg = (strcmp(sq, "7700") == 0 || strcmp(sq, "7600") == 0 || strcmp(sq, "7500") == 0);

          float dLatKm = (lat - centerLat) * 111.32f;
          float dLonKm = (lon - centerLon) * (111.32f * cosCenterLat);
          float distKm = sqrtf(dLatKm * dLatKm + dLonKm * dLonKm);

          uint8_t prio = is_emg ? 0 : (is_mil ? 1 : 2);
          cands[candCount++] = {distKm, (uint16_t)i, prio};
        }

        // Zoradenie: Prioritné lety prvé, potom podľa vzdialenosti
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
          ac.is_emergency = (strcmp(ac.squawk, "7700") == 0 || strcmp(ac.squawk, "7600") == 0 || strcmp(ac.squawk, "7500") == 0);

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

        // Samostatné sekvenčné dohľadanie trás maximálne pre 2 lietadlá (po úplnom uzavretí ADS-B spojenia!)
        int routesFetched = 0;
        for (size_t i = 0; i < aircraftCount && routesFetched < 2; i++) {
          if (aircraftList[i].route[0] == '\0' && strcmp(aircraftList[i].callsign, "NOCALL") != 0 && !aircraftList[i].is_mil) {
            fetchRouteForCallsign(aircraftList[i].callsign, aircraftList[i].route, sizeof(aircraftList[i].route));
            routesFetched++;
          }
        }

        Serial.printf("[ADS-B] Načítaných %u lietadiel z API (vybraných: %u najbližších/prioritných, Free RAM: %u B)\n", 
                      (unsigned int)acList.size(), (unsigned int)count, (unsigned int)ESP.getFreeHeap());
      }
    } else {
      Serial.printf("[ADS-B] Chyba HTTP: %d\n", httpCode);
      http.end();
      client.stop();
    }
  }

  ensureCanvas(); // Okamžite obnovíme canvas
  renderScreen(); // A hneď prekreslíme nový stav
}

/** Vykreslenie radarovej mriežky s mestami a kružnicami */
void drawPlaneRadarGrid(LovyanGFX& target) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  // 1. Hranica SR
  for (size_t i = 0; i < SK_BORDER_COUNT - 1; i++) {
    int sx1 = (int)mapXToScreenX(lonToX(SK_BORDER[i][0]));
    int sy1 = (int)mapYToScreenY(latToY(SK_BORDER[i][1]));
    int sx2 = (int)mapXToScreenX(lonToX(SK_BORDER[i+1][0]));
    int sy2 = (int)mapYToScreenY(latToY(SK_BORDER[i+1][1]));
    target.drawLine(sx1, sy1, sx2, sy2, TFT_CYAN);
  }

  // 2. Filtrovanie a zobrazenie miest
  target.setTextSize(0.80f);
  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_ORANGE, TFT_BLACK);
  for (size_t i = 0; i < CITY_COUNT; i++) {
    if (currentRadiusKm >= 100.0f && !CITIES[i].isMajor) continue;

    int sx = (int)mapXToScreenX(lonToX(CITIES[i].lon));
    int sy = (int)mapYToScreenY(latToY(CITIES[i].lat));
    if (sx >= 10 && sx <= TFT_W - 10 && sy >= 10 && sy <= TFT_H - 10) {
      target.drawLine(sx - 2, sy, sx + 2, sy, TFT_RED);
      target.drawLine(sx, sy - 2, sx, sy + 2, TFT_RED);
      target.drawString(CITIES[i].name, sx, sy - 3);
    }
  }

  // 3. Koncentrické zelené kruhy a kríž
  uint16_t gridColor = target.color565(0, 200, 0);     
  uint16_t dimGridColor = target.color565(0, 80, 0);   

  target.drawLine(cx - 110, cy, cx + 110, cy, dimGridColor);
  target.drawLine(cx, cy - 110, cx, cy + 110, dimGridColor);

  target.drawCircle(cx, cy, 35, gridColor);
  target.drawCircle(cx, cy, 70, gridColor);
  target.drawCircle(cx, cy, 105, gridColor);

  // 4. Svetové strany (N, S, W, E) - umiestnené na vnútornom obvode hlavného kruhu r=105
  target.setTextSize(0.75f);
  target.setTextDatum(textdatum_t::middle_center);
  target.setTextColor(target.color565(180, 220, 180), TFT_BLACK);
  target.drawString("N", cx, cy - 105 + 10);
  target.drawString("S", cx, cy + 105 - 10);
  target.drawString("W", cx - 105 + 10, cy);
  target.drawString("E", cx + 105 - 10, cy);

  // 5. Mierka a čas - umiestnené na hornom a dolnom okraji displeja bez kolízie
  target.setTextSize(0.75f);
  target.setTextDatum(textdatum_t::top_center);
  target.setTextColor(target.color565(0, 255, 0), TFT_BLACK);
  target.drawString(String((int)currentRadiusKm) + " km", cx, 3);

  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_WHITE, TFT_BLACK);
  target.drawString(getCurrentSystemTimeText(), cx, TFT_H - 3);
}

/** Vykreslenie vrstvy lietadiel s plynulou extrapoláciou pohybu a HUD štítkami */
void drawPlanesOverlay(LovyanGFX& target) {
  int cx = TFT_W / 2;
  int cy = TFT_H / 2;

  // Výpočet uplynutého času od posledného sieťového fixu
  float dt_s = (lastPlaneFetchFixMs > 0) ? (float)(millis() - lastPlaneFetchFixMs) / 1000.0f : 0.0f;
  if (dt_s > 30.0f) dt_s = 30.0f;

  struct RenderedPlane {
    int sx, sy;
    int mapX, mapY;
    float dist;
    bool is_on_screen;
    bool show_tag;
    AircraftData ac;
  };

  RenderedPlane planes[MAX_AIRCRAFT];
  size_t onScreenCount = 0;

  // 1. Krok: Extrapolácia pozícií a výpočet súradníc pre všetky lietadlá
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

    int mapX = lonToX(ac.lon);
    int mapY = latToY(ac.lat);
    int sx = (int)mapXToScreenX(mapX);
    int sy = (int)mapYToScreenY(mapY);

    float distFromCenter = sqrtf((float)((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy)));

    planes[i].sx = sx;
    planes[i].sy = sy;
    planes[i].mapX = mapX;
    planes[i].mapY = mapY;
    planes[i].dist = distFromCenter;
    planes[i].is_on_screen = (distFromCenter <= 106.0f);
    planes[i].show_tag = false;
    planes[i].ac = ac;

    if (planes[i].is_on_screen) {
      onScreenCount++;
    }
  }

  // 2. Krok: Určenie, ktoré lietadlá dostanú popis (Smart Tag Visibility + Ochrana pred prekrytím)
  if (currentRadiusKm <= 50.0f) {
    for (size_t i = 0; i < aircraftCount; i++) {
      if (planes[i].is_on_screen) planes[i].show_tag = true;
    }
  } else {
    // Pri 100 km a 250 km: zobrazíme najviac 2-3 štítky, aby sa neprekrývali
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

  // Ochrana pred vzájomnou kolíziou štítkov (Collision Avoidance)
  // Ak sú dve lietadlá so štítkom k sebe bližšie ako 34 px, vypneme štítok tomu vzdialenejšiemu
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

  // 3. Krok: Samotné vykreslenie symbolov, štítkov a okrajových indikátorov
  bool hasEmergency = false;
  String emgInfo;

  for (size_t i = 0; i < aircraftCount; i++) {
    if (planes[i].is_on_screen) {
      drawAircraftSymbol(target, planes[i].sx, planes[i].sy, planes[i].ac.nose_deg, planes[i].ac.track, planes[i].ac.gs_knots, planes[i].ac.is_mil, planes[i].ac.is_emergency);
      if (planes[i].show_tag) {
        drawAircraftTag(target, planes[i].sx, planes[i].sy, planes[i].ac);
      }
      if (planes[i].ac.is_emergency) {
        hasEmergency = true;
        emgInfo = String(planes[i].ac.callsign) + " (SQ" + String(planes[i].ac.squawk) + ")";
      }
    } else {
      drawEdgeIndicator(target, planes[i].mapX, planes[i].mapY, planes[i].ac.is_mil);
      if (planes[i].ac.is_emergency) {
        hasEmergency = true;
        emgInfo = String(planes[i].ac.callsign) + " (SQ" + String(planes[i].ac.squawk) + ")";
      }
    }
  }

  // 4. Krok: Zobrazenie núdzového výstražného bannera pri Squawk 7700/7600/7500
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

  // Hranice SR
  for (size_t i = 0; i < SK_BORDER_COUNT - 1; i++) {
    int sx1 = (int)mapXToScreenX(lonToX(SK_BORDER[i][0]));
    int sy1 = (int)mapYToScreenY(latToY(SK_BORDER[i][1]));
    int sx2 = (int)mapXToScreenX(lonToX(SK_BORDER[i+1][0]));
    int sy2 = (int)mapYToScreenY(latToY(SK_BORDER[i+1][1]));
    target.drawLine(sx1, sy1, sx2, sy2, TFT_CYAN);
  }

  // Zobrazenie miest
  target.setTextSize(0.80f);
  target.setTextDatum(textdatum_t::bottom_center);
  target.setTextColor(TFT_ORANGE, TFT_BLACK);
  for (size_t i = 0; i < CITY_COUNT; i++) {
    if (currentRadiusKm >= 100.0f && !CITIES[i].isMajor) continue;

    int sx = (int)mapXToScreenX(lonToX(CITIES[i].lon));
    int sy = (int)mapYToScreenY(latToY(CITIES[i].lat));
    if (sx >= 10 && sx <= TFT_W - 10 && sy >= 10 && sy <= TFT_H - 10) {
      target.drawLine(sx - 2, sy, sx + 2, sy, TFT_RED);
      target.drawLine(sx, sy - 2, sx, sy + 2, TFT_RED);
      target.drawString(CITIES[i].name, sx, sy - 3);
    }
  }

  // Kruhy a zameriavač pre meteoradar
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
    target.drawString(getRadarTimeText(lastPngName), cx, TFT_H - 3);
  }
}


// =======================================================================================
// 8. PNG DEKÓDER & SPIFFS CALLBACK FUNKCIE
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

inline bool isNoDataPixel(uint16_t c) {
  if (c == 0x0000) return true;
  // Sivá maska SHMÚ (No Data Mask: indexy 112 a 113 s RGB ~216/230)
  if (c == 0xE73C || c == 0x3CE7 || c == 0xE71C || c == 0x1CE7 ||
      c == 0xD6BA || c == 0xBAD6 || c == 0xCE79 || c == 0x79CE ||
      c == 0xDEFB || c == 0xFBDE || c == 0xEF7D || c == 0x7DEF) {
    return true;
  }
  // Všeobecný filter pre svetlosivé masky (R >= 200, G >= 200, B >= 200)
  uint16_t r = (c >> 11) & 0x1F;
  uint16_t g = (c >> 5) & 0x3F;
  uint16_t b = c & 0x1F;
  if (r >= 26 && g >= 52 && b >= 26 && abs((int)r - (int)b) <= 2) {
    return true;
  }
  return false;
}

void processInterpolatedScreenRow(int dy, float fy) {
  if (dy < 0 || dy >= TFT_H) return;

  uint8_t row8[TFT_W];
  float dY_px = (float)dy - 120.0f;
  const float xRatio = (float)crop.w() / (float)TFT_W;

  int ify = (int)(fy * 256.0f);
  if (ify < 0) ify = 0; else if (ify > 256) ify = 256;
  int nify = 256 - ify;

  for (int dx = 0; dx < TFT_W; dx++) {
    float dX_px = (float)dx - 120.0f;
    float dSq = dX_px * dX_px + dY_px * dY_px;

    // Mimo okrúhleho displeja (r > 120 px)
    if (dSq > 14400.0f) {
      row8[dx] = 0x00;
      continue;
    }

    float srcX_f = (float)crop.x1 + (float)dx * xRatio;
    int x0 = (int)floorf(srcX_f);
    int x1 = x0 + 1;
    float fx = srcX_f - (float)x0;
    x0 = constrain(x0, 0, RADAR_IMG_W - 1);
    x1 = constrain(x1, 0, RADAR_IMG_W - 1);

    int ifx = (int)(fx * 256.0f);
    if (ifx < 0) ifx = 0; else if (ifx > 256) ifx = 256;
    int nifx = 256 - ifx;

    uint16_t raw00 = prevLine565[x0];
    uint16_t raw10 = prevLine565[x1];
    uint16_t raw01 = currLine565[x0];
    uint16_t raw11 = currLine565[x1];

    uint16_t c00 = isNoDataPixel(raw00) ? 0x0000 : raw00;
    uint16_t c10 = isNoDataPixel(raw10) ? 0x0000 : raw10;
    uint16_t c01 = isNoDataPixel(raw01) ? 0x0000 : raw01;
    uint16_t c11 = isNoDataPixel(raw11) ? 0x0000 : raw11;

    if (c00 == 0 && c10 == 0 && c01 == 0 && c11 == 0) {
      row8[dx] = 0x00;
      continue;
    }

    // Váhy pre 4 rohy (súčet váh = 256)
    int w00 = (nifx * nify) >> 8;
    int w10 = (ifx * nify) >> 8;
    int w01 = (nifx * ify) >> 8;
    int w11 = (ifx * ify) >> 8;

    // Rozklad na RGB565 zložky (5-6-5)
    uint16_t r00 = (c00 >> 11) & 0x1F, g00 = (c00 >> 5) & 0x3F, b00 = c00 & 0x1F;
    uint16_t r10 = (c10 >> 11) & 0x1F, g10 = (c10 >> 5) & 0x3F, b10 = c10 & 0x1F;
    uint16_t r01 = (c01 >> 11) & 0x1F, g01 = (c01 >> 5) & 0x3F, b01 = c01 & 0x1F;
    uint16_t r11 = (c11 >> 11) & 0x1F, g11 = (c11 >> 5) & 0x3F, b11 = c11 & 0x1F;

    int r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 8;
    int g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 8;
    int b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 8;

    if (r <= 0 && g <= 0 && b <= 0) {
      row8[dx] = 0x00;
    } else {
      // 8-bit RGB332 konverzia pre LovyanGFX 8-bit sprite
      uint8_t col8 = (uint8_t)((((r >> 2) & 0x07) << 5) | (((g >> 3) & 0x07) << 2) | ((b >> 3) & 0x03));
      if (col8 == 0x00) col8 = 0x04; // Minimálna hodnota pre slabé zrážky
      row8[dx] = col8;

      // Spracovanie výstrah pred zrážkami
      if (dSq < minPrecipDistSqPx) {
        minPrecipDistSqPx = dSq;
      }
      if (rainAlertThresholdKm > 0.0f) {
        float distKm = (sqrtf(dSq) / 120.0f) * currentRadiusKm;
        if (distKm <= rainAlertThresholdKm) {
          alertAnyPixelCount++;
          if (dSq < alertAnyMinSqPx) {
            alertAnyMinSqPx = dSq;
            alertAnyTargetDx = dX_px;
            alertAnyTargetDy = dY_px;
          }

          // Detekcia, či sa zrážky pohybujú smerom k našej polohe (podľa smeru vetra v 700 hPa / pri zemi)
          float effectiveWindDir = (windDir700hPa >= 0.0f) ? windDir700hPa : windDirSurface;
          if (effectiveWindDir >= 0.0f) {
            float pixelBearing = atan2f(dX_px, -dY_px) * 57.2957795f;
            if (pixelBearing < 0.0f) pixelBearing += 360.0f;

            float diff = fabsf(pixelBearing - effectiveWindDir);
            if (diff > 180.0f) diff = 360.0f - diff;

            // Nátokový kužeľ vetra (±60° proti smeru vetra): zrážky sú unášané vetrom priamo k našej polohe
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

  if (fRawOut) {
    fRawOut.write(row8, TFT_W);
  }
}

int drawPngLine(PNGDRAW* pDraw) {
  int srcY = pDraw->y;
  if (srcY < crop.y1 - 1 || srcY > crop.y2 + 1) return 1;

  png.getLineAsRGB565(pDraw, currLine565, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

  if (!prevLineValid || srcY <= crop.y1) {
    memcpy(prevLine565, currLine565, sizeof(prevLine565));
    prevLineValid = true;
  }

  const float yRatio = (float)crop.h() / (float)TFT_H;
  while (currentScreenDy < TFT_H) {
    float targetSrcY_f = (float)crop.y1 + (float)currentScreenDy * yRatio;
    if (targetSrcY_f > (float)srcY) {
      break;
    }
    float fy = targetSrcY_f - (float)(srcY - 1);
    if (fy < 0.0f) fy = 0.0f;
    else if (fy > 1.0f) fy = 1.0f;

    processInterpolatedScreenRow(currentScreenDy, fy);
    currentScreenDy++;
  }

  memcpy(prevLine565, currLine565, sizeof(prevLine565));
  return 1;
}

/**
 * Dekódovanie SHMÚ radarového PNG zo SPIFFS do dekomprimovanej raw cache.
 * Vykonáva sa iba 1x pri stiahnutí novej snímky alebo zmene zoomu/stredu.
 */
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

  if (SPIFFS.exists(RADAR_RAW_CACHE_FILE)) SPIFFS.remove(RADAR_RAW_CACHE_FILE);
  fRawOut = SPIFFS.open(RADAR_RAW_CACHE_FILE, "w");
  if (!fRawOut) {
    Serial.println("[RADAR] Chyba vytvorenia cache suboru pre zapis!");
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

  currentScreenDy = 0;
  prevLineValid = false;

  if (png.open(RADAR_FILE, pngOpen, pngClose, pngRead, pngSeek, drawPngLine) == PNG_SUCCESS) {
    png.decode(nullptr, 0);
    png.close();
  }

  while (currentScreenDy < TFT_H) {
    processInterpolatedScreenRow(currentScreenDy, 1.0f);
    currentScreenDy++;
  }

  fRawOut.flush();
  fRawOut.close();
  radarRawCacheValid = true;

  // Vyhodnotenie výstrahy pred zrážkami
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
 * Hlavný unifikovaný rendering:
 * Vrstva 1: SHMÚ zrážky (bleskové načítanie z raw cache v < 1.5 ms)
 * Vrstva 2: Hranice SR, mestá a radarové kruhy
 * Vrstva 3: Lietadlá s HUD štítkami
 * Vrstva 4: Taktický výstražný radarový lúč (Threat Sector Beam)
 */
void renderScreen() {
  ensureCanvas();
  if (!canvasReady) return;
  
  canvas.fillScreen(TFT_BLACK);

  // 1. Vrstva: Zrážky (bleskové načítanie z dekomprimovanej 8-bit raw cache súboru)
  if (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER) {
    if (radarRawCacheValid && SPIFFS.exists(RADAR_RAW_CACHE_FILE)) {
      File fIn = SPIFFS.open(RADAR_RAW_CACHE_FILE, "r");
      if (fIn) {
        uint8_t row8[TFT_W];
        for (int y = 0; y < TFT_H; y++) {
          if (fIn.read(row8, TFT_W) == TFT_W) {
            for (int x = 0; x < TFT_W; x++) {
              if (row8[x] != 0x00) {
                canvas.drawPixel(x, y, row8[x]);
              }
            }
          }
        }
        fIn.close();
      }
    }
  }

  // 2. Vrstva: Mriežka a mapa
  if (currentMode == MODE_COMBINED || currentMode == MODE_PLANES) {
    drawPlaneRadarGrid(canvas);
  } else {
    drawWeatherOverlay(canvas, true);
  }

  // 3. Vrstva: Lietadlá
  if (currentMode == MODE_COMBINED || currentMode == MODE_PLANES) {
    drawPlanesOverlay(canvas);
  }

  // 4. Vrstva: Taktický radarový lúč a výstraha (Červený lúč = prichádzajúce, Žltý lúč = okolité)
  if (rainAlertActive && (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER)) {
    uint32_t phase = millis() % 1000;
    uint16_t beaconColor;
    uint16_t beamColor;
    uint16_t accentColor;

    if (isIncomingAlert) {
      // Prichádzajúce zrážky v smere vetra: Pulzujúci jas od sýtej červenej po jantárovo-červenú
      uint8_t g = (phase < 500) ? (uint8_t)map(phase, 0, 500, 20, 170) : (uint8_t)map(phase, 500, 1000, 170, 20);
      uint8_t r = (phase < 500) ? (uint8_t)map(phase, 0, 500, 210, 255) : (uint8_t)map(phase, 500, 1000, 255, 210);
      beamColor = canvas.color565(r, g, 0);
      beaconColor = canvas.color565(255, (uint8_t)min(255, g + 50), 0);
      accentColor = TFT_WHITE;
    } else {
      // Iba okolité zrážky v monitorovanom okruhu: Pulzujúci žltý lúč
      uint8_t g = (phase < 500) ? (uint8_t)map(phase, 0, 500, 170, 235) : (uint8_t)map(phase, 500, 1000, 235, 170);
      uint8_t r = (phase < 500) ? (uint8_t)map(phase, 0, 500, 220, 255) : (uint8_t)map(phase, 500, 1000, 255, 220);
      beamColor = canvas.color565(r, g, 0);
      beaconColor = canvas.color565(255, 240, 40);
      accentColor = TFT_WHITE;
    }

    // Taktický radarový lúč (Threat Sector Beam)
    if (alertBearingDeg >= 0.0f) {
      constexpr float kDegToRad = 0.01745329252f;
      float centerDeg = alertBearingDeg;
      float halfSpanDeg = 18.0f; // Šírka sektora lúča ±18° (celkový uhol 36°)
      float leftDeg = centerDeg - halfSpanDeg;
      float rightDeg = centerDeg + halfSpanDeg;

      float rStart = 10.0f;
      float rEnd = 114.0f;
      float rTarget = constrain(alertTargetDistPx, 14.0f, 114.0f);

      float radL = leftDeg * kDegToRad;
      float radR = rightDeg * kDegToRad;
      float radC = centerDeg * kDegToRad;

      // 1. Bočné lúče sektora ohrozenia (Boundary Rays)
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

      // 2. Taktická radarová mriežka vnútri sektora (Concentric Range Arcs)
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

      // 3. Stredová čerchovaná navigačná os (Center Laser Tracer)
      for (float rStep = rStart; rStep < rEnd; rStep += 8.0f) {
        float rStep2 = min(rStep + 4.0f, rEnd);
        int cx1 = (int)roundf(120.0f + rStep * sinf(radC));
        int cy1 = (int)roundf(120.0f - rStep * cosf(radC));
        int cx2 = (int)roundf(120.0f + rStep2 * sinf(radC));
        int cy2 = (int)roundf(120.0f - rStep2 * cosf(radC));
        canvas.drawLine(cx1, cy1, cx2, cy2, beamColor);
      }

      // 4. Zvýraznený cieľový oblúk (Target Range Arc) priamo na vzdialenosti zrážok
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

      // 5. Zameriavací terčík na zrážkovom objekte (Target Reticle)
      int tgtX = (int)roundf(120.0f + rTarget * sinf(radC));
      int tgtY = (int)roundf(120.0f - rTarget * cosf(radC));
      canvas.drawCircle(tgtX, tgtY, 4, accentColor);
      canvas.drawCircle(tgtX, tgtY, 5, beamColor);
      canvas.drawPixel(tgtX, tgtY, TFT_WHITE);

      // 6. Obvodový smerový ševrón (Perimeter Direction Chevron ▼)
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

      // 7. Pulzujúci obvodový oblúk výseku sektora (Perimeter Sector Arc)
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

    // Výstražný text so vzdialenosťou a svetovou stranou (napr. "! ZRAZKY: 8.5 km JZ !" alebo "! BLIZI SA: 8.5 km JZ !")
    canvas.setTextSize(0.75f);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.setTextColor(beaconColor, TFT_BLACK);
    char alertBuf[36];
    if (alertBearingDir[0] != '\0') {
      snprintf(alertBuf, sizeof(alertBuf), "! %s: %.1f km %s !", isIncomingAlert ? "BLIZI SA" : "ZRAZKY", closestPrecipDistKm, alertBearingDir);
    } else {
      snprintf(alertBuf, sizeof(alertBuf), "! %s: %.1f km !", isIncomingAlert ? "BLIZI SA" : "ZRAZKY", closestPrecipDistKm);
    }
    canvas.drawString(alertBuf, 120, 18);
  }

  canvas.pushSprite(0, 0);
}


// =======================================================================================
// 9. ŠTART A HLAVNÝ CYKLUS (SETUP & LOOP)
// =======================================================================================

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n\n========================================");
  Serial.printf("  ESP-MeteoRadar %s (ESP32-C3)\n", CURRENT_VERSION);
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
  lastPngName = prefs.getString("last_png", "");
  prefs.end();

  if (carouselIntervalSec < 5) carouselIntervalSec = 5;
  carouselIntervalMs = (uint32_t)carouselIntervalSec * 1000;

  checkResetButtonAtBoot();
  if (!SPIFFS.begin(false)) {
    Serial.println("[SPIFFS] SPIFFS nebolo možné pripojiť, formátujem oddiel...");
    SPIFFS.format();
    if (SPIFFS.begin(true)) {
      Serial.println("[SPIFFS] Formátovanie úspešné a SPIFFS pripojené!");
    } else {
      Serial.println("[SPIFFS] Chyba pripojenia SPIFFS!");
    }
  } else {
    Serial.println("[SPIFFS] SPIFFS pripojené v poriadku.");
  }
  connectWiFi();
  setupWebServer();
  
  crop = makeCrop(centerLat, centerLon, currentRadiusKm);

  tft.fillScreen(TFT_BLACK);
  drawPlaneRadarGrid(tft);
  tft.setTextSize(0.75f);
  tft.setTextDatum(textdatum_t::bottom_center);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Nacitavam data...", TFT_W / 2, TFT_H - 12);

  // Úvodné stiahnutie počasia, smeru vetra a lietadiel
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

  // Spracovanie tlačidla (1x klik: zoom, 2x klik: prepnutie 3 režimov, 3s: reset)
  handleButton();

  // Spracovanie požiadaviek lokálneho webového servera
  server.handleClient();

  // Kontrola nočného režimu (Auto-Dimming)
  updateNightMode();

  // Automatické opätovné pripojenie k WiFi v prípade výpadku
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
  }

  // Periodická aktualizácia smeru a rýchlosti vetra z Open-Meteo (každých 15 minút)
  if (now - lastWindFetchMs >= WIND_FETCH_INTERVAL_MS) {
    fetchWindData();
  }

  // Prepínanie režimov v Karuseli (iba ak je používateľom explicitne zapnutý)
  if (carouselEnabled && (now - lastCarouselSwitchMs >= carouselIntervalMs)) {
    lastCarouselSwitchMs = now;
    setAppMode((AppMode)((currentMode + 1) % 3), false);
  }

  // Plynulá animácia pulzovania výstražného taktického lúča (20 FPS / 50 ms)
  static uint32_t lastAlertAnimMs = 0;
  if (rainAlertActive && (currentMode == MODE_COMBINED || currentMode == MODE_WEATHER)) {
    if (now - lastAlertAnimMs >= 50) {
      lastAlertAnimMs = now;
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  }

  // Periodické aktualizácie podľa aktívneho režimu
  if (currentMode == MODE_COMBINED) {
    // 1. Sťahovanie počasia (každých 5 minút)
    uint32_t weatherInt = (lastPngName.isEmpty() || !SPIFFS.exists(RADAR_FILE)) ? 15000 : UPDATE_INTERVAL_MS;
    if (now - lastWeatherUpdateMs >= weatherInt) {
      lastWeatherUpdateMs = now;
      if (downloadLatestRadar()) renderScreen();
    }
    // 2. Sťahovanie lietadiel (každých 10 sekúnd)
    if (now - lastPlaneFetchMs >= PLANE_FETCH_INTERVAL_MS) {
      lastPlaneFetchMs = now;
      fetchPlanesData();
    }
    // 3. Plynulé extrapolované prekresľovanie lietadiel nad mapou (každú sekundu)
    if (now - lastPlaneRedrawMs >= PLANE_REDRAW_INTERVAL_MS) {
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  } else if (currentMode == MODE_WEATHER) {
    uint32_t interval = (lastPngName.isEmpty() || !SPIFFS.exists(RADAR_FILE)) ? 15000 : UPDATE_INTERVAL_MS;
    if (now - lastWeatherUpdateMs >= interval) {
      lastWeatherUpdateMs = now;
      if (downloadLatestRadar()) renderScreen();
    }
  } else if (currentMode == MODE_PLANES) {
    // Sťahovanie čerstvých ADS-B dát každých 10s
    if (now - lastPlaneFetchMs >= PLANE_FETCH_INTERVAL_MS) {
      lastPlaneFetchMs = now;
      fetchPlanesData();
    }
    // Plynulé extrapolované prekresľovanie lietadiel každú sekundu
    if (now - lastPlaneRedrawMs >= PLANE_REDRAW_INTERVAL_MS) {
      lastPlaneRedrawMs = now;
      renderScreen();
    }
  }
  
  delay(10);
}
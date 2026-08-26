#pragma once

/*
merged bin cfg
python3 -m esptool --chip esp32c3 merge_bin -o esp-meteoradar.bin --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0000 .pio/build/esp32-c3-devkitm-1/bootloader.bin 0x8000 .pio/build/esp32-c3-devkitm-1/partitions.bin 0x10000 .pio/build/esp32-c3-devkitm-1/firmware.bin
*/

// ===== Predvolená poloha stredu zobrazenia =====
// Tieto hodnoty sa použijú pri prvom štarte (Banská Bystrica - stred SR). Potom sa dajú zmeniť vo WiFiManager portáli.
#define DEFAULT_CENTER_LAT "48.6690"
#define DEFAULT_CENTER_LON "19.6990"

// Predvolený okruh zobrazenia po štarte. Tlačidlom sa prepína 10 / 25 / 50 / 100 / 250 km.
#define DEFAULT_RADIUS_KM_TEXT "50"

// Offset času radarového snímku iba pre zobrazenie na displeji.
// SHMÚ súbor je v UTC: leto +2, zima +1.
#define DEFAULT_TIME_OFFSET_HOURS 2

// Tlačidlo medzi GPIO9 a GND, používa sa interný pull-up.
// Krátke stlačenie prepína zoom, dlhé podržanie resetuje WiFi/polohu a spustí portál.
#define ZOOM_BUTTON_PIN 9
static constexpr uint32_t RESET_HOLD_MS = 3000;

// Aktualizácia radaru (zmenené na 5 minút)
static constexpr uint32_t UPDATE_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 min

// ===== SHMÚ OpenData =====
static constexpr const char* SHMU_API_URL  = "https://www.shmu.sk/api/v1/meteo/getradardata";
static constexpr const char* SHMU_BASE_URL = "https://www.shmu.sk/data/dataradary/data.cmax/";

// PNG radar kompozit SHMÚ (cmax.kruh) má rozmer 800 x 550 px.
static constexpr int RADAR_IMG_W = 800;
static constexpr int RADAR_IMG_H = 550;

// Kalibrácia súradníc PNG pre Slovensko (SHMÚ Leaflet overlay bounds).
static constexpr float LAT_TOP    = 50.7000f;
static constexpr float LAT_BOTTOM = 46.0500f;
static constexpr float LON_LEFT   = 13.6000f;
static constexpr float LON_RIGHT  = 23.7900f;

// ===== Displej =====
static constexpr int TFT_W = 240;
static constexpr int TFT_H = 240;

// Piny pre bežný okrúhly GC9A01 240x240 cez SPI.
// Uprav podľa svojej dosky/displeja.
#define TFT_MOSI 3
#define TFT_SCLK 4
#define TFT_CS   1
#define TFT_DC   10
#define TFT_RST  0
#define TFT_BL   -1 // -1 = nepoužíva sa, ak je pin pod napätím, displej svieti
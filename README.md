# 🛰️ ESP-GlobalRadar (ESP32-C3 & GC9A01)

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32--C3%20SuperMini-blue?style=for-the-badge&logo=espressif" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/Display-GC9A01%20240x240%20IPS-orange?style=for-the-badge" alt="GC9A01">
  <img src="https://img.shields.io/badge/Coverage-Worldwide%20%28RainViewer%29-red?style=for-the-badge" alt="Coverage Worldwide">
  <img src="https://img.shields.io/badge/Release-v2.0.0-success?style=for-the-badge" alt="Release v2.0.0">
  <img src="https://img.shields.io/badge/Framework-PlatformIO%20%2F%20Arduino-brightgreen?style=for-the-badge&logo=platformio" alt="PlatformIO">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="MIT License">
</p>

<p align="center">
  <b>Univerzálny celosvetový stolný radar pre okrúhly 1.28″ LCD displej spájajúci globálny zrážkový meteoradar (RainViewer API) a živé sledovanie lietadiel (ADS-B) s taktickým sektorovým lúčom, detekciou vetra, webovým panelom a plynulou animáciou bez blikania.</b>
</p>

---

## 🌟 Prehľad projektu

Tento open-source projekt transformuje miniatúrnu vývojovú dosku **ESP32-C3 SuperMini** a okrúhly **1.28″ TFT displej GC9A01 (240×240 px)** na celosvetovo použiteľný taktický radar. Funguje na **ľubovoľných GPS súradniciach na Zemi** (Európa, Severná a Južná Amerika, Ázia, Afrika, Austrália).

Zariadenie v nastaviteľnom časovom intervale (alebo po kliknutí tlačidla) prepína:
1. **🌍 Globálny zrážkový meteoradar (RainViewer):** Sťahuje a dekóduje celosvetové dlaždice zrážok (Web Mercator Slippy Map $256 \times 256\text{ px}$) a vykresľuje ich na vektorovej mape sveta.
2. **✈️ Globálny ADS-B letecký radar:** Monitoruje lietadlá v okolí v reálnom čase s extrapoláciou polohy (`Dead Reckoning`), trasami letísk (**ODKIAĽ > KAM**) a prioritnou núdzovou izoláciou (**Squawk 7700 / 7600 / 7500**).
3. **🛰️ Tactical ATC Kombinovaný režim:** Zobrazuje zrážkovú oblačnosť a lietadlá súčasne na jednej taktickej obrazovke.

---

## 🚀 Kľúčové funkcie

* 🌍 **Celosvetové pokrytie (RainViewer Global API):**
  - Web Mercator Slippy Map dlaždicová projekcia pre akékoľvek GPS súradnice na planéte.
  - Rýchly streamovaný dekompresor a pamäťovo optimalizovaný 4-chunkový buffer.
  - Vektorové hranice štátov sveta ([include/world_borders.h](include/world_borders.h)) vykresľované v reálnom čase.
* 🧭 **Taktický radarový lúč (Threat Sector Beam) & Detekcia vetra (Open-Meteo 700 hPa):**
  - **Červený pulzujúci lúč (INCOMING):** Zameriava búrky v nátokovom kuželi výškového vetra ($\pm 60^\circ$ proti vetru) s výstrahou napr. `! INCOMING: 8.5 km JZ !`.
  - **Žltý taktický lúč (STORM):** Signalizuje zrážky v bezprostrednom okruhu s textom `! STORM: 8.5 km JZ !`.
  - **Prvky sektora:** $36^\circ$ taktický lúč, zameriavací cieľový oblúk na presnej vzdialenosti mraku, terčík a obvodový smerový ševrón ($\blacktriangledown$).
  - **Blesková SPIFFS Raw Cache:** Dekompresia prebieha iba 1× pri stiahnutí novej snímky (20 FPS plynulé pulzovanie).
* 🚨 **Exkluzívna izolácia núdzových letov (Emergency Focus):**
  - Pri zachytení núdzového letu (**Squawk 7700 / 7600 / 7500** alebo ADS-B discrete flag) sa **všetky bežné lietadlá okamžite skryjú**.
  - Na displeji zostáva iba núdzový let s blikajúcim výstražným orámovaním, trvalým štítkom a horným bannerom `⚠️ NUDZA: CALLSIGN (SQ7700)`.
* 🛡️ **Zero-Flicker Double Buffering & TLS izolácia:**
  - 100 % plynulá animácia letu lietadiel (1 Hz `Dead Reckoning`) bez akéhokoľvek blikania displeja.
* 🏷️ **HUD Contrast Boxy & Vyhľadávanie letových trás:**
  - Polopriehľadné zaoblené boxy s vysokým kontrastom nad zrážkovými jadrami.
  - Preklad volacieho znaku na letiskové trasy (napr. `VIE>AMS`, `JFK>LHR`, `HND>ITM`) s lokálnou cache pamäťou.
* 🌅 **Astronomický Nočný režim (Auto-Dimming):**
  - Automatický výpočet východu a západu slnka podľa GPS súradníc a dňa v roku s jemným stlmením jasu.
* 🔘 **Multi-Click Tlačidlo (GPIO9):**
  - **1x Klik:** Zmena zoomu ($10 \rightarrow 25 \rightarrow 50 \rightarrow 100 \rightarrow 250\text{ km}$).
  - **2x Klik:** Okamžité prepnutie režimu (Kombinovaný $\rightarrow$ Počasie $\rightarrow$ Lietadlá).
  - **Dlhé podržanie (3s):** Továrenský reset WiFi a NVS pamäte.
* 🌐 **Lokálny Web Dashboard (Port 80):**
  - Živá mapa Leaflet.js, telemetria čipu, GPS nastavenie polohy prehliadačom a 1-kliknutím OTA aktualizácia priamo z GitHubu.

---

## 🛠️ Hardvér a schéma zapojenia

| Komponent | Popis |
| :--- | :--- |
| **ESP32-C3 SuperMini** | Riadiaci mikrokontrolér (RISC-V 160MHz, Wi-Fi 2.4GHz, USB-C) |
| **GC9A01 1.28″ Round LCD** | Okrúhly 240×240 px IPS farebný displej (SPI) |
| **Tlačidlo (BOOT / GPIO9)** | Ovládanie zoomu, režimov a resetu (interný pull-up) |

```
   ESP32-C3 SuperMini                  GC9A01 LCD (240x240)
 +--------------------+               +--------------------+
 |               3.3V |-------------->| VCC & BLK (Podsv.) |
 |                GND |-------------->| GND                |
 |              GPIO4 |-------------->| SCL / SCLK (Clock) |
 |              GPIO3 |-------------->| SDA / MOSI (Data)  |
 |              GPIO0 |-------------->| RES / RST (Reset)  |
 |             GPIO10 |-------------->| DC (Data/Command)  |
 |              GPIO1 |-------------->| CS (Chip Select)   |
 +--------------------+               +--------------------+
```

---

## 🚀 Rýchly štart v 3 krokoch

### 1. Nahratie firmvéru (Web Flash cez prehliadač)
1. Otvorte **[web.esphome.io](https://web.esphome.io/)** v prehliadači Chrome / Edge.
2. Pripojte ESP32-C3 cez USB kábel k počítaču a kliknite na **CONNECT**.
3. Zvoľte **Install** a vyberte stiahnutý súbor [`merged-firmware.bin`](merged-firmware.bin).
4. Po dokončení stlačte tlačidlo **RST** na doske.

### 2. Pripojenie k Wi-Fi a nastavenie polohy (WiFiManager)
1. Po prvom štarte sa pripojte k Wi-Fi sieti **`ESPGlobalRadar`** (alebo `ESPMeteoRadar`).
2. V otvorenom prehliadači (`http://192.168.4.1`) zadajte:
   - Názov a heslo domácej Wi-Fi siete.
   - **GPS súradnice** vašej lokality (Latitude & Longitude).
   - Predvolený rozsah (napr. `50` km) a časový posun (UTC offset).
3. Kliknite na **Save**. Doska sa pripojí na internet a okamžite spustí radar.

### 3. Používanie a Web Dashboard
- Otvorte v sieti **`http://espglobalradar.local`** (alebo IP adresu dosky) pre prístup k interaktívnemu panelu, telemetrii a OTA aktualizáciám.

---

## 📄 Licencia & Poďakovanie

Tento projekt je zverejnený pod slobodnou licenciou **[MIT License](LICENSE)**.

Stavia na otvorených dátach a projektoch:
- 🌧️ **[RainViewer API](https://www.rainviewer.com/api.html):** Globálne meteorologické radarové dlaždice.
- 📡 **[ADSB.fi](https://opendata.adsb.fi/):** Komunitný otvorený feed ADS-B leteckých dát.
- 🌤️ **[Open-Meteo API](https://open-meteo.com/):** Globálne výškové a prízemné veterné dáta.
- 🗺️ **[vrs-standing-data (adsb.lol)](https://vrs-standing-data.adsb.lol/):** Databáza letových trás pre lietadlá.
- 🖨️ **[3D Case Model (MakerWorld)](https://makerworld.com/cs/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display):** 3D model krabičky pre okrúhly displej.

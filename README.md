# 🛰️ ESP32-C3 MeteoRadar & ADS-B Plane Radar (Slovensko)

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32--C3%20SuperMini-blue?style=for-the-badge&logo=espressif" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/Display-GC9A01%20240x240%20SPI-orange?style=for-the-badge" alt="GC9A01">
  <img src="https://img.shields.io/badge/Web%20Dashboard-Port%2080-purple?style=for-the-badge&logo=html5" alt="Web Dashboard">
  <img src="https://img.shields.io/badge/Release-v1.11.0-success?style=for-the-badge" alt="Release v1.11.0">
  <img src="https://img.shields.io/badge/Framework-PlatformIO%20%2F%20Arduino-brightgreen?style=for-the-badge&logo=platformio" alt="PlatformIO">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="MIT License">
  <img src="https://img.shields.io/badge/Region-Slovakia%20%28SHM%C3%9A%29-red?style=for-the-badge" alt="Region Slovakia">
</p>

<p align="center">
  <b>Kompaktný stolný radar pre okrúhly 1.28″ LCD displej spájajúci zrážkový meteoradar SHMÚ a živé sledovanie lietadiel (ADS-B) s webovým panelom, hardvérovou telemetriou a plynulou animáciou bez blikania.</b>
</p>

<div align="center">
  <table>
    <tr>
      <td align="center" width="50%">
        <b>🛰️ Tactical ATC Kombinovaný Radar</b><br>
        <i>(Zrážky SHMÚ + ADS-B lietadlá + Taktický lúč)</i><br><br>
        <img src="data/combined_radar_live.jpg" width="280" alt="Combined Radar Live">
      </td>
      <td align="center" width="50%">
        <b>✈️ ADS-B Letecký Radar & Trasy</b><br>
        <i>(Plynulý pohyb lietadiel + Trasy ODKIAĽ>KAM)</i><br><br>
        <img src="data/plane_radar_live.jpg" width="280" alt="Plane Radar Live">
      </td>
    </tr>
    <tr>
      <td align="center" width="50%">
        <b>🌧️ SHMÚ Meteoradar & Taktický Sektor</b><br>
        <i>(Bilineárne filtrovanie + Cieľový oblúk)</i><br><br>
        <img src="data/weather_radar_live.jpg" width="280" alt="Weather Radar Live">
      </td>
      <td align="center" width="50%">
        <b>🌐 Vstavaný Web Dashboard (Port 80)</b><br>
        <i>(Živá telemetria, radarové výstrahy a ovládanie)</i><br><br>
        <img src="data/web_dashboard.png" width="320" alt="Web Dashboard">
      </td>
    </tr>
  </table>
</div>

---

## 📖 Obsah
- [Prehľad projektu](#-prehľad-projektu)
- [Ukážka funkcií a režimov displeja](#-ukážka-funkcií-a-režimov-displeja)
- [Hlavné funkcie (v1.11.0)](#-hlavné-funkcie-v1110)
- [Použitý hardvér](#-použitý-hardvér)
- [Schéma zapojenia](#-schéma-zapojenia)
- [Ovládanie a funkcie tlačidla](#-ovládanie-a-funkcie-tlačidla)
- [Lokálny Web Dashboard (Port 80)](#-lokálny-web-dashboard-port-80)
- [Prvé spustenie a konfigurácia (WiFiManager)](#-prvé-spustenie-a-konfigurácia-wifimanager)
- [Ako nahrať a aktualizovať firmvér](#-ako-nahrať-a-aktualizovať-firmvér)
  - [Metóda 1: Rýchly flash cez webový prehliadač (USB kábel)](#metóda-1-rýchly-flash-cez-webový-prehliadač-usb-kábel)
  - [Metóda 2: 1-Kliknutím OTA aktualizácia priamo z GitHubu](#metóda-2-1-kliknutím-ota-aktualizácia-priamo-z-githubu)
  - [Metóda 3: Manuálny OTA Upload cez Web Dashboard](#metóda-3-manuálny-ota-upload-cez-web-dashboard)
  - [Metóda 4: Kompilácia a nahratie cez PlatformIO](#metóda-4-kompilácia-a-nahratie-cez-platformio)
- [3D Tlač krabičky (Enclosure)](#-3d-tlač-krabičky-enclosure)
- [Časté otázky a riešenie problémov (Troubleshooting)](#-časté-otázky-a-riešenie-problémov)
- [Pôvodné projekty, inšpirácia a poďakovanie](#-pôvodné-projekty-inšpirácia-a-poďakovanie)
- [Licencia](#-licencia)

---

## 🌟 Prehľad projektu

Tento DIY projekt transformuje miniatúrnu vývojovú dosku **ESP32-C3 SuperMini** a okrúhly **1.28-palcový TFT displej GC9A01 (240×240 px)** na plnohodnotný stolný radarový prístroj. 

Zariadenie v nastaviteľnom časovom intervale (Karusel) automaticky strieda:
1. **Zrážkový meteoradar:** Sťahuje a resampluje oficiálne radarové kompozity zrážok zo **Slovenského hydrometeorologického ústavu (SHMÚ)** na vektorovej mape SR.
2. **ADS-B Letecký radar:** V reálnom čase monitoruje leteckú prevádzku v okolí vašej polohy cez **ADS-B feed** s plynulou animáciou letu a vyhľadávaním letových trás (**ODKIAĽ > KAM** napr. `VIE>AMS`, `BGY>WAW`).

---

## 🚀 Hlavné funkcie (v1.11.0)

* 🌫️ **Plynulá bilineárna interpolácia (Streaming Bilinear Filtering):** Radarové zrážkové vrstvy sú vyhladzované v reálnom čase pomocou 2-riadkovej bilineárnej interpolácie. Úplne odstraňuje kockovitosť/pixelizáciu na detailných zoomoch (10 km a 25 km) a zabezpečuje hladký anti-aliasing na všetkých úrovniach (10 až 250 km) s minimálnou pamäťovou réžiou (iba 3.2 kB RAM).
* 🧭 **Taktický radarový lúč (Threat Sector Beam) & Detekcia vetra (Open-Meteo 700 hPa):** 
  - Inteligentné smerovanie na zrážkové bunky s využitím výškového vetra v riadiacej hladine 700 hPa (~3000 m n. m.) z Open-Meteo API.
  - **Červený pulzujúci lúč:** Detekuje prichádzajúce zrážky v nátokovom kuželi vetra ($\pm 60^\circ$ proti vetru) s výstrahou `! BLIZI SA: 8.5 km JZ !`.
  - **Žltý taktický lúč:** Zobrazuje okolité zrážky v monitorovanom okruhu s textom `! ZRAZKY: 8.5 km JZ !`.
  - **Vizuálne prvky sektora:** $36^\circ$ taktický lúč, zameriavací cieľový oblúk na presnej vzdialenosti mraku, stredový terčík, obvodový smerový ševrón ($\blacktriangledown$) a pulzujúci výsek obvodu displeja.
  - **Plošný filter šumu a citlivosti:** Tri voliteľné úrovne (Vysoká 10 px / Stredná 25 px / Nízka 50 px) na spoľahlivú elimináciu izolovaných radarových artefaktov.
  - **Blesková SPIFFS Raw Cache:** Dekompresia prebieha iba 1× pri stiahnutí novej snímky, vďaka čomu animácia pulzovania beží pri 20 FPS úplne plynulo bez záťaže RAM.
* 🛡️ **Zero-Flicker Double Buffering & Izolácia TLS:** 100 % flicker-free plynulá animácia lietadiel bez čiernych prebliknutí displeja pri zachovaní plného HTTPS zabezpečenia.
* 🛰️ **Tactical ATC Combined Radar:** Zjednotené zobrazenie zrážkovej oblačnosti (SHMÚ) a ADS-B lietadiel na jednej obrazovke v reálnom čase s plynulou 1 Hz extrapoláciou (`Dead Reckoning`).
* 🏷️ **HUD Contrast Boxy:** Každý štítok lietadla má tmavý polopriehľadný zaoblený box s orámovaním, vďaka čomu je text 100 % čitateľný aj nad pestrofarebnými búrkovými jadrami.
* 🎯 **Anti-Collision Tag Filtering & Jemná Grafika:** Inteligentná ochrana pred vzájomným prekrytím textových štítkov, decentné 1-pixelové vektory rýchlosti a ostré symboly lietadiel.
* 📍 **Zvýraznené Okrajové Indikátory:** Výrazné body na vonkajšom obvode ($r = 114\text{ px}$) pre lietadlá letiace tesne za hranicou aktuálneho priblíženia.
* 🔄 **Automatický Karusel:** Plynulé striedanie režimov počasia a lietadiel v nastaviteľnom intervale (5 až 300 sekúnd).
* 🔘 **Multi-Click Tlačidlo:**
  * **1x Klik:** Zmena zoomu (10 $\rightarrow$ 25 $\rightarrow$ 50 $\rightarrow$ 100 $\rightarrow$ 250 km).
  * **2x Klik (Dvojklik):** Okamžité manuálne prepnutie režimu.
  * **Dlhé podržanie (3s):** Továrenský reset WiFi a NVS pamäte.
* ✈️ **Pokročilý Letecký radar (ADS-B) s plynulou extrapoláciou (Dead Reckoning):**
  * **🚨 ADS-B Núdzové výstrahy (Squawk 7700 / 7600 / 7500):** Okamžitá vizuálna indikácia núdze – blikanie lietadla a výstražný banner v hornej časti displeja.
  * **🧠 Adaptívne štítky (Smart Tag Visibility):** Pri rozsahu 10–50 km zobrazuje štítky všetkým lietadlám; pri 100–250 km prioritizuje vojenské lety a 3 najbližšie lietadlá pre čistý a nezaplnený displej.
  * **Dynamický Double Buffering (0 % blikanie):** 240×240 pixelový off-screen canvas s dynamickou izoláciou pamäte pre bezpečný TLS handshake.
  * **Automatické zisťovanie letových trás:** Prepojenie volacieho znaku s databázou letových plánov (VRS standing-data) zobrazuje trasu letu (`VIE>AMS`).
  * **Kruhová vyrovnávacia pamäť (Route Cache):** Ukladá 48 letových trás do RAM pre okamžité vykreslenie a minimálny dátový prenos.
  * **Čitateľné 3-riadkové štítky:**
    * *Riadok 1:* Trasa letísk (fialová) alebo Callsign (biela / červená pre vojenské / blikajúca núdza).
    * *Riadok 2:* Typ lietadla ICAO (svetlomodrá, napr. `A21N`) a rýchlosť v km/h (svetlozelená).
    * *Riadok 3:* Nadmorská výška v metroch (žltá) a farebná šípka stúpania / klesania.
  * **Edge Dots:** Obvodové body na okraji displeja indikujúce lietadlá nachádzajúce sa tesne za hranicou aktuálneho priblíženia.
  * **Vojenské lety:** Automatické zvýraznenie vojenských transpondérov červenou farbou.
* 🌧️ **SHMÚ Meteoradar (Slovensko):**
  * Sťahovanie najnovšieho zrážkového PNG kompozitu (`cmax.kruh`) zo serverov SHMÚ cez zabezpečené HTTPS pomocou nízko-pamäťového streamovaného chunked parsera (< 1 KB RAM).
  * Trvalé NVS ukladanie posledného timestampu radaru.
  * Zameriavací kríž a diaľkové kružnice.
* 🌅 **Astronomický Nočný režim (Auto-Dimming):** Matematický prepočet presného času východu a západu slnka podľa zadaných GPS súradníc a dňa v roku – displej sa automaticky stlmí po západe slnka.
* 🌐 **Lokálny Web Dashboard s interaktívnou mapou (Leaflet) a telemetriou:**
  * **🗺️ Živá mapa Leaflet.js:** Zobrazenie stredu radaru, akčného okruhu a **živých lietadiel v reálnom čase s kurzom letu**. Kliknutím alebo potiahnutím značky na mape sa okamžite prepočítajú súradnice.
  * **🔍 Vyhľadávanie obcí a miest (OpenStreetMap Nominatim):** Rýchle fulltextové vyhľadanie akejkoľvek slovenskej obce alebo adresy bez nutnosti manuálneho hľadania GPS súradníc.
  * **🚀 1-Kliknutím OTA aktualizácia priamo z GitHubu:** Automatické overenie najnovšieho vydania cez GitHub API a inštalácia nového firmvéru bez kábla.
  * **📁 Manuálny OTA Upload:** Možnosť nahrať vygenerovaný súbor `firmware.bin` priamo z webového prehliadača.
  * **Hardvérová telemetria:** Frekvencia CPU (MHz), interná teplota čipu (°C), voľná a celková RAM (KB / %), veľkosť Flash pamäte a čas behu (Uptime).
  * **Živá tabuľka lietadiel:** Zoznam všetkých zachytených lietadiel v reálnom čase s trasou, rýchlosťou, výškou, squawkom a koordinátmi.
  * **Skenovanie Wi-Fi sietí:** Vyhľadanie sietí v okolí s indikátorom sily signálu (RSSI) a pripojenie k novej Wi-Fi.
  * **Asynchrónne AJAX ukladanie:** Okamžité uloženie bez nechceného znovunačítania stránky s okamžitou aktualizáciou radaru.
* 🗺️ **Vektorová mapa SR a mestá:** Detailný polygón hranice Slovenskej republiky a mestá (BA, TT, NR, TN, ZA, BB, PO, KE, BJ, PP, MI, LC) s dynamickým filtrovaním podľa zoomu.

---

## 🛠️ Použitý hardvér

| Komponent | Popis | Odporúčanie |
| :--- | :--- | :--- |
| **ESP32-C3 SuperMini** | Riadiaci mikrokontrolér (RISC-V 160MHz, WiFi, BLE, USB-C) | Kompaktný rozmer, nízka spotreba |
| **GC9A01 1.28″ Round LCD** | Okrúhly IPS displej 240×240 px, SPI zbernica | Krásne pozorovacie uhly a živé farby |
| **Tlačidlo (BOOT / Externé)** | Tlačidlo pripojené medzi GPIO9 a GND | Prepínanie zoomu, režimov a reset |
| **3D Krabička** | Krabička prispôsobená pre okrúhly radar | [Dostupná na MakerWorld](https://makerworld.com/cs/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display) |

---

## 🔌 Schéma zapojenia

Pripojenie displeja **GC9A01** k doske **ESP32-C3 SuperMini**:

```
           ESP32-C3 SuperMini                GC9A01 LCD (240x240)
         +--------------------+             +--------------------+
         |               3.3V |------------>| VCC                |
         |                GND |------------>| GND                |
         |              GPIO4 |------------>| SCL / SCLK (Clock) |
         |              GPIO3 |------------>| SDA / MOSI (Data)  |
         |              GPIO0 |------------>| RES / RST (Reset)  |
         |             GPIO10 |------------>| DC (Data/Command)  |
         |              GPIO1 |------------>| CS (Chip Select)   |
         |               3.3V |------------>| BLK / BL (Podsv.)  |
         +--------------------+             +--------------------+
```

### Zapojenie tlačidla:
* **Tlačidlo BOOT na doske:** Už je od výroby zapojené medzi **GPIO9** a **GND**.
* **Externé tlačidlo (voliteľné):** Zapojte medzi pin **GPIO9** a **GND**. V softvéri je zapnutý interný `INPUT_PULLUP`, externý rezistor nie je potrebný.

---

## 🕹️ Ovládanie a funkcie tlačidla

Zariadenie využíva inteligentnú detekciu stlačení tlačidla na **GPIO9**:

* 🔘 **1x Krátke stlačenie:** Cyklické prepínanie mierky zobrazenia:
  $$\text{10 km} \longrightarrow \text{25 km} \longrightarrow \text{50 km} \longrightarrow \text{100 km} \longrightarrow \text{250 km}$$
* ✌️ **2x Krátke stlačenie (Dvojklik):** Okamžité manuálne cyklické prepnutie medzi 3 režimami (**Spojený Tactical**, **Iba počasie**, **Iba lietadlá**) bez čakania na karusel.
* 🔴 **Dlhé podržanie (≥ 3 sekundy):** Vykoná kompletný reset nastavení WiFi, vymaže NVS pamäť a reštartuje dosku do režimu konfiguračného portálu.

---

## 🌐 Lokálny Web Dashboard (Port 80 / mDNS)

Po pripojení dosky k domácej Wi-Fi sieti je v prehliadači na adrese **`http://espmeteoradar.local`** (alebo priamo na pridelenej IP adrese, napr. `http://192.168.1.150`) dostupný moderný vstavaný **Web Dashboard**:

* 📊 **Živý stav radaru:** Zobrazenie aktuálneho režimu, zvoleného zoomu, sily signálu WiFi (RSSI) a počtu lietadiel v dosahu.
* 🎮 **Diaľkové ovládanie:** Prepínanie zoomu (10, 25, 50, 100, 250 km), prepínanie medzi 3 režimami (Spojený, Iba počasie, Iba lietadlá) a pozastavenie/zapnutie karuselu.
* ✈️ **Živá tabuľka lietadiel:** Zoznam všetkých zachytených lietadiel v reálnom čase s informáciami o trase letísk (`VIE>AMS`), type lietadla, rýchlosti v km/h, výške v metroch a presných GPS súradniciach.
* 📍 **Automatické nastavenie GPS polohy:** Jedným kliknutím na tlačidlo *„📍 Nastaviť polohu podľa GPS prehliadača“* zariadenie načíta vašu presnú polohu z GPS vášho smartfónu alebo notebooku.
* 📶 **Zmena Wi-Fi siete priamo z webu:** Tlačidlo *„🔍 Vyhľadať siete“* prehľadá okolie, zobrazí silu signálu (RSSI) a umožní jednoducho prepojiť dosku na inú domácu Wi-Fi sieť.
* ⚙️ **Rýchla zmena nastavení:** Úprava intervalu karuselu a časového offsetu bez nutnosti hardvérového resetu.

---

## ⚙️ Prvé spustenie a konfigurácia (WiFiManager)

1. Po prvom zapnutí zariadenie vytvorí vlastný Wi-Fi prístupový bod s názvom **`ESPMeteoRadar`** (bez hesla).
2. Pripojte sa k sieti z mobilu alebo počítača. Automaticky sa otvorí konfiguračná stránka (ak nie, otvorte prehliadač a zadajte adresu `http://192.168.4.1`).
3. Zvoľte **Configure WiFi** a vyplňte:
   * **Názov a heslo domácej Wi-Fi siete**
   * **Zemepisná šírka (Lat):** Súradnice vášho domova/miesta (napr. `48.1486` pre Bratislavu, `48.7363` pre Banskú Bystricu, `48.7164` pre Košice).
   * **Zemepisná dĺžka (Lon):** (napr. `17.1077` pre Bratislavu, `19.1462` pre Banskú Bystricu, `21.2611` pre Košice).
   * **Predvolený rozsah (km):** Štartovacia mierka radaru (napr. `50`).
   * **Časový offset (hodiny):** `2` pre letný čas (CEST), `1` pre zimný čas (CET).
   * **Interval karuselu (sekundy):** Ako často sa má striedať počasie a lietadlá (napr. `30`).
4. Kliknite na **Save**. Doska sa pripojí k vašej Wi-Fi a okamžite načíta živé dáta.

---

## 📦 Ako nahrať a aktualizovať firmvér

> [!IMPORTANT]
> **Rozdiel medzi binárnymi súbormi (Veľmi dôležité!):**
> * **`merged-firmware.bin`** (cca 1.6 MB) – Kompletný obraz celej Flash pamäte vrátane bootloadera a tabuľky partícií od offsetu `0x0`. Používa sa **výhradne pre USB kábel** (Web Flasher, esptool).
> * **`firmware.bin`** (cca 1.5 MB) – Samotná aplikačná partícia od offsetu `0x10000`. Používa sa **výhradne pre bezdrôtový OTA Web Upload** v prehliadači! *NIKDY nenahrávajte `merged-firmware.bin` do webového OTA formulára!*

---

### Metóda 1: Rýchly flash cez webový prehliadač (USB kábel)
Nemusíte inštalovať žiadne programovacie prostredie, ovládače ani knižnice.

1. Otvorte stránku **[web.esphome.io](https://web.esphome.io/)** v prehliadači Google Chrome, MS Edge alebo Opera.
2. Pripojte dosku ESP32-C3 cez USB kábel k počítaču.
3. Kliknite na **CONNECT** a v zozname zvoľte sériový port vášho ESP32 (napr. `COMx` na Windows, `/dev/ttyUSBx` alebo `/dev/ttyACMx` na Mac/Linux).
   > *Tip: Ak prehliadač dosku nevidí, podržte na doske tlačidlo **BOOT**, stlačte tlačidlo **RST** a pustite BOOT.*
4. Zvoľte **Install** &rarr; vyberte súbor [`merged-firmware.bin`](merged-firmware.bin) z tohto repozitára.
5. Počkajte na dokončenie nahrávania na 100 % a stlačte tlačidlo **RST** na doske.

---

### Metóda 2: 1-Kliknutím OTA aktualizácia priamo z GitHubu (Bez kábla)
Ak už máte radar pripojený k domácej Wi-Fi sieti:
1. Otvorte webové rozhranie na IP adrese vášho radaru (alebo `http://esp-meteoradar.local`).
2. V sekcii **🚀 Aktualizácia firmvéru (OTA)** kliknite na **🔍 Skontrolovať GitHub**.
3. ESP32 sa spojí s GitHub API a porovná vašu verziu s najnovším oficiálnym vydaním.
4. Kliknite na **🚀 Aktualizovať** – doska stiahne nový firmvér, overí integritu, zapíše do Flash a sama sa reštartuje.

---

### Metóda 3: Manuálny OTA Upload cez Web Dashboard
1. Stiahnite si súbor **`firmware.bin`** z najnovšieho [GitHub Release](../../releases) (alebo z priečinka `.pio/build/nologo_esp32c3_super_mini/firmware.bin` po kompilácii).
2. Otvorte webový dashboard v prehliadači a prejdite na kartu OTA.
3. V časti *Manuálny OTA upload* vyberte súbor **`firmware.bin`** a kliknite na **📁 Nahrať firmvér**.
   > [!WARNING]
   > Nenahrávajte `merged-firmware.bin`! Pre webový upload slúži výhradne `firmware.bin`.

---

### Metóda 4: Kompilácia a nahratie cez PlatformIO

1. Nainštalujte **[Visual Studio Code](https://code.visualstudio.com/)** a rozšírenie **PlatformIO IDE**.
2. Naklonujte tento repozitár:
   ```bash
   git clone https://github.com/hackra76/ESP-MeteoRadar.git
   ```
3. Otvorte priečinok v prostredí VS Code.
4. PlatformIO automaticky stiahne potrebné knižnice (`LovyanGFX`, `WiFiManager`, `ArduinoJson`, `PNGdec`).
5. Pripojte ESP32-C3 a kliknite na tlačidlo **Upload** (šípka &rarr;) v spodnej lište.

Skript [`scripts/merge_bin.py`](scripts/merge_bin.py) po každej kompilácii automaticky vygeneruje aj pripravený spojený súbor `merged-firmware.bin` pre USB inštaláciu.

---

## 🖨️ 3D Tlač krabičky (Enclosure)

Pre tento projekt odporúčame použiť pôvodný overený 3D model krabičky z projektu **[ESP32-Plane-Radar (MatixYo)](https://github.com/MatixYo/ESP32-Plane-Radar)** alebo z **[MakerWorld: ESP32 Plane Radar](https://makerworld.com/en/search/models?keyword=ESP32+Plane+Radar)**.

---

## ❓ Časté otázky a riešenie problémov

> [!TIP]
> **Doska sa po uploade nespustí automaticky:**
> ESP32-C3 SuperMini využíva natívny interný USB radič bez prídavných DTR/RTS tranzistorov. Po úspešnom nahraní firmvéru stačí **krátko stlačiť hardvérové tlačidlo RST** na doske (alebo odpojiť a znova pripojiť USB kábel).

> [!NOTE]
> **Ako znova vyvolať konfiguračnú stránku WiFi?**
> Podržte tlačidlo na GPIO9 aspoň na 3 sekundy. Na displeji sa zobrazí hlásenie *Reset nastavenia...* a zariadenie opäť vytvorí prístupový bod `ESPMeteoRadar`.

> [!IMPORTANT]
> **Prečo sa pri niektorých lietadlách zobrazuje kód (napr. WZZ488R) a pri iných trasa (napr. VIE>AMS)?**
> Komerčné lety s letovým plánom v databáze sa automaticky preložia na kód letísk. Súkromné alebo vojenské lety letový plán nemajú, preto sa pri nich zobrazuje ich oficiálny volací znak (Callsign).

---

## 🤝 Pôvodné projekty, inšpirácia a poďakovanie

Tento projekt stavia na myšlienkach a práci open-source komunity:

* 🌦️ **[ESP-MeteoRadar (Petanovo.cz)](https://www.petanovo.cz/esp-meteoradar-od-letadel-k-meteo-radarovym-datum/):** Pôvodný koncept meteo radaru pre ČR a inšpirácia pre hardvérové prevedenie.
* ✈️ **[ESP32-Plane-Radar (MatixYo)](https://github.com/MatixYo/ESP32-Plane-Radar):** Základný projekt leteckého radaru pre okrúhly displej GC9A01.
* 📏 **[ESP32-Plane-Radar-metrers (synex-c21)](https://github.com/synex-c21/ESP32-Plane-Radar-metrers):** Implementácia metrických jednotiek a integrácia vyhľadávania letových trás cez VRS standing-data.
* 🛰️ **[OpenData SHMÚ](https://opendata.shmu.sk/):** Poskytovateľ otvorených meteorologických radarových snímok pre územie Slovenska.
* 📡 **[ADSB.fi](https://opendata.adsb.fi/):** Komunitný otvorený feed ADS-B leteckých dát.
* 🗺️ **[vrs-standing-data (adsb.lol)](https://vrs-standing-data.adsb.lol/):** Statická databáza letových trás pre Virtual Radar Server.
* 🖨️ **[3D Case Model (MakerWorld)](https://makerworld.com/cs/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display):** Skvelý 3D model krabičky.

---

## 📄 Licencia

Tento projekt je zverejnený pod slobodnou licenciou **[MIT License](LICENSE)**. Kód môžete voľne používať, upravovať a šíriť.

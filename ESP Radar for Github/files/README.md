# ESP-Radar

ESP32-basiertes Flugzeug-Radar Display — zeigt Flugzeuge in der Nähe auf einem ST7789 240x240 IPS Display in Echtzeit an.

## Hardware

| Komponente | Details |
|---|---|
| Mikrocontroller | ESP32-WROOM-DA (WEMOS D1 Mini Format) |
| Display | GMT130-V1.0, IPS 240x240, ST7789 |

## Verkabelung

| Display Pin | ESP32 GPIO |
|---|---|
| GND | GND |
| VCC | 3.3V |
| BLK | 3.3V |
| SCK | GPIO18 |
| SDA | GPIO23 |
| RES | GPIO5 |
| DC | GPIO2 |
| CS | nicht angeschlossen |

## Bibliotheken (Arduino IDE)

- TFT_eSPI (2.5.43+)
- ArduinoJson
- ESP32 Board-Paket 2.0.17+

## TFT_eSPI Konfiguration (User_Setup.h)

```cpp
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS    -1
#define TFT_DC     2
#define TFT_RST    5
#define CGRAM_OFFSET
#define SPI_FREQUENCY  10000000
```

## Konfiguration

Vor dem Hochladen im Code anpassen:

```cpp
// FlightAware API Key (https://www.flightaware.com/aeroapi/)
const char* FA_API_KEY = "DEIN_FLIGHTAWARE_API_KEY";

// WLAN (können auch über Web-Interface eingestellt werden)
const char* WLAN_LIST[][2] = {
  {"WLAN_NAME_1", "WLAN_PASSWORT_1"},
  ...
};

// OpenSky Network Login (https://opensky-network.org/)
http.setAuthorization("DEIN_OPENSKY_USERNAME", "DEIN_OPENSKY_PASSWORT");
```

## Features

- **Liste** — Callsign, Distanz, Höhe, Route, Geschwindigkeit, Airline-Name
- **Radar** — Übersicht mit Distanzringen und km-Beschriftung
- **Spotlight** — Startende und landende Flugzeuge gross angezeigt
- **Web-Interface** — Einstellungen über Browser erreichbar
- **Multi-WLAN** — verbindet sich mit dem stärksten bekannten Netz
- **Route-Cache** — bis zu 1000 Routen persistent im Flash gespeichert
- **CSV Import/Export** — Cache herunterladen oder hochladen

## Datenquellen

| Quelle | Zweck | Kosten |
|---|---|---|
| OpenSky Network | Flugzeugpositionen | Kostenlos |
| OpenSky Network | Routenabfrage | Kostenlos (Account nötig) |
| FlightAware AeroAPI | Routenabfrage (Fallback) | 100 Credits/Monat kostenlos |

## Farben auf dem Display

| Farbe | Bedeutung |
|---|---|
| Grün | Route aus Cache |
| Cyan | Route von OpenSky |
| Gelb | Route von FlightAware |
| Grau | Route unbekannt |

## Projektstruktur

```
ESP-Radar/
├── ESP_Radar.ino
├── ESP_Radar_types.h
└── README.md
```

## Lizenz

MIT License

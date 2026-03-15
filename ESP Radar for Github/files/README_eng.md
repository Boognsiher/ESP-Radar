# ESP-Radar

ESP32-based flight radar display — shows nearby aircraft in real-time on a ST7789 240x240 IPS display.

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32-WROOM-DA (WEMOS D1 Mini format) |
| Display | GMT130-V1.0, IPS 240x240, ST7789 |

## Wiring

| Display Pin | ESP32 GPIO |
|---|---|
| GND | GND |
| VCC | 3.3V |
| BLK | 3.3V |
| SCK | GPIO18 |
| SDA | GPIO23 |
| RES | GPIO5 |
| DC | GPIO2 |
| CS | not connected |

## Required Libraries (Arduino IDE)

- TFT_eSPI (2.5.43+)
- ArduinoJson
- ESP32 Board Package 2.0.17+

## TFT_eSPI Configuration (User_Setup.h)

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

## Configuration

Before uploading, edit the following in the code:

```cpp
// FlightAware API Key (https://www.flightaware.com/aeroapi/)
const char* FA_API_KEY = "YOUR_FLIGHTAWARE_API_KEY";

// WiFi credentials (can also be set via web interface)
const char* WLAN_LIST[][2] = {
  {"WIFI_NAME_1", "WIFI_PASSWORD_1"},
  {"WIFI_NAME_2", "WIFI_PASSWORD_2"},
  ...
};

// OpenSky Network login (https://opensky-network.org/)
http.setAuthorization("YOUR_OPENSKY_USERNAME", "YOUR_OPENSKY_PASSWORD");
```

## Features

- **List view** — Callsign, distance, altitude, route, speed, airline name
- **Radar view** — Overview with distance rings and compass directions
- **Spotlight view** — Departing and arriving aircraft displayed prominently
- **Web interface** — Configure settings via browser
- **Multi-WiFi** — Automatically connects to the strongest known network
- **Route cache** — Up to 1000 routes stored persistently in flash memory
- **CSV import/export** — Download or upload route cache as CSV file
- **IP geolocation** — Location determined automatically on first boot

## Web Interface Tabs

| Tab | Content |
|---|---|
| Settings | Location, radius, display duration, FlightAware credits |
| WiFi | Configure up to 5 WiFi networks |
| Status | Live data, cache export/import, reboot |
| Log | System log with color-coded entries |

## Data Sources

| Source | Purpose | Cost |
|---|---|---|
| OpenSky Network | Aircraft positions | Free |
| OpenSky Network | Route lookup | Free (account required) |
| FlightAware AeroAPI | Route lookup (fallback) | 100 credits/month free |

## Display Colors

| Color | Meaning |
|---|---|
| Green | Route from cache |
| Cyan | Route from OpenSky |
| Yellow | Route from FlightAware |
| Grey | Route unknown |

## Project Structure

```
ESP-Radar/
├── ESP_Radar.ino
├── ESP_Radar_types.h
└── README.md
```

## License

MIT License — free to use and modify.

# ESP-Radar ✈️

A real-time flight radar for ESP32 with a TFT display. Tracks aircraft around any airport using live ADS-B data from OpenSky Network, with optional route data from FlightAware.

---

## Features

- **Live flight tracking** via OpenSky Network (ADS-B data)
- **5 display screens** navigated by buttons:
  - **List** — paginated list of nearby aircraft with callsign, route, altitude, speed, climb rate
  - **Radar MAX** — full-radius radar (up to 300 km) with aircraft positions and heading vectors
  - **Spotlight** — highlights the currently departing and landing aircraft at your home airport
  - **Radar APR** — 20 km approach radar focused on departures/landings
  - **Track** — follow a specific flight by callsign
- **Geolocated runway lines** on radar screens, drawn at the actual airport position (not the center) for 25 supported airports
- **Route display** (origin → destination) via cache, OpenSky OAuth2 API, or FlightAware
- **Web UI** for full configuration — no reflashing needed
- **Auto standby** after 5 data loads without button press (press any button to wake)
- **Logo splash screen** and aviation facts on data load screen
- **Navigation bar** showing current screen and adjacent screens (`‹ PREV · CURRENT · NEXT ›`)
- **Countdown timer** in header showing time until next data refresh

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 (ESP32-WROOM-32 recommended) |
| Display | 240×320 px TFT (ST7789 or ILI9341 driver, configured via TFT_eSPI) |
| Button 1 (Forward) | Tactile switch on GPIO 21 → GND |
| Button 2 (Backward) | Tactile switch on GPIO 22 → GND |
| Power | USB (5V via ESP32 dev board) |

> Internal pull-ups are used — no resistors needed for the buttons.

---

## Required Libraries

Install via Arduino Library Manager:

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — configure `User_Setup.h` for your display
- [ArduinoJson](https://arduinojson.org/) — version 6.x

Built-in ESP32 libraries (no install needed):
- `WiFi`, `HTTPClient`, `Preferences`, `WebServer`

---

## Project Files

| File | Description |
|------|-------------|
| `ESP_Radar_v20.ino` | Main sketch |
| `ESP_Radar_types.h` | Struct definitions (`Flugzeug`, `RouteCache`, `AirportInfo`, etc.) |
| `ESP_Radar_logo.h` | 100×100px 1-bit bitmap logo (auto-generated from PNG) |

All three files must be in the same Arduino project folder.

---

## Installation

1. Clone or download this repository
2. Open `ESP_Radar_v20.ino` in Arduino IDE
3. Configure `TFT_eSPI` for your display in its `User_Setup.h`
4. Edit the fallback WLAN credentials at the top of the `.ino`:
   ```cpp
   const char* WLAN_LIST[][2] = {
     {"YourSSID", "YourPassword"},
     ...
   };
   ```
5. Flash to your ESP32
6. Open the Web UI in a browser at the IP shown on the display

---

## Web UI Configuration

After flashing, open the IP address shown on the display in any browser. All settings are configurable without reflashing:

| Setting | Description |
|---------|-------------|
| Location (lat/lon) | Your position or auto-detected via IP geolocation |
| Radius | Radar range in km (5–300) |
| Home airport | IATA code (e.g. `ZRH`) — used for runway lines and Spotlight screen |
| Data interval | How often new flight data is fetched (seconds) |
| Track callsign | Follow a specific flight (e.g. `SWR123`) |
| OpenSky Client ID / Secret | OAuth2 credentials for route data |
| FlightAware API Key | Optional fallback for route data |
| FlightAware credits limit | Safety cap on API usage |
| WLAN networks | Up to 5 networks, ESP connects to strongest |

---

## OpenSky OAuth2 Setup

Route data (origin/destination) requires an OpenSky account with OAuth2:

1. Create an account at [opensky-network.org](https://opensky-network.org)
2. Go to **Account → API Client → Create new client**
3. Copy the **Client ID** and **Client Secret**
4. Enter both in the Web UI under **OpenSky**
5. Save — the ESP will automatically fetch and refresh tokens

> Anonymous access still works for live positions (`/api/states/all`). OAuth2 is only needed for flight routes.

---

## Supported Airports (Runway Lines)

Runway directions are drawn at the correct geographic position on the radar:

ZRH, GVA, BSL, BRN, SIR, LHR, CDG, AMS, FRA, MUC, VIE, FCO, MAD, BCN, BRU, CPH, OSL, ARN, HEL, WAW, PRG, BUD, ATH, LIS, DUB

Each runway line is 10 km total (5 km each direction from center).

---

## Screen Navigation

| Button | Action |
|--------|--------|
| GPIO 21 (Forward) | Next screen / next list page |
| GPIO 22 (Backward) | Previous screen / previous list page |

Screen order: `List → Radar MAX → Spotlight → Radar APR → Track → List`

Screens without content (e.g. Track with no callsign set, Radar APR with no departures) are skipped automatically.

**Auto standby:** After 5 automatic data refreshes without a button press, automatic loading stops. The header shows `ZZZ`. Press any button to reset the counter and resume.

---

## Route Data Sources

| Source | Color | Description |
|--------|-------|-------------|
| Cache | Green | Previously fetched and stored in flash |
| OpenSky | Cyan | Live route via OAuth2 API |
| FlightAware | Yellow | Fallback via AeroAPI |
| Fake route | Gray | Estimated from altitude/climb rate near airport |

---

## License

MIT License — free to use, modify and distribute.

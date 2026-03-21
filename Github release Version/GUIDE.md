# ESP-Radar — User Guide

## First-Time Setup

### 1. Flash the firmware

1. Download all three project files and place them in the same folder:
   - `ESP_Radar_v20.ino`
   - `ESP_Radar_types.h`
   - `ESP_Radar_logo.h`
2. Open `ESP_Radar_v20.ino` in Arduino IDE
3. Configure `TFT_eSPI` for your display (edit the library's `User_Setup.h`)
4. Select your ESP32 board under **Tools → Board → ESP32 Arduino**
5. Click **Upload**

### 2. Add your Wi-Fi credentials

At the top of `ESP_Radar_v20.ino`, add your Wi-Fi network(s) as fallback values:

```cpp
const char* WLAN_LIST[][2] = {
  {"YourSSID",       "YourPassword"},
  {"AnotherNetwork", "AnotherPassword"},
  {"",               ""},
  {"",               ""},
  {"",               ""},
};
```

The ESP connects to the strongest known network automatically. Up to 5 networks are supported. These are fallback values only — you can manage networks through the Web UI after first boot.

### 3. First boot

On first boot, the display shows:
1. **Splash screen** — ESP-Radar logo
2. **Wi-Fi connection** — scanning and connecting
3. **IP address screen** — shows the local IP address for 3 seconds
4. **Data loading screen** — fetches live flight data
5. **Flight list** — the radar is now running

If no known Wi-Fi is found, the ESP starts a hotspot:
- **SSID:** `ESP-Radar`
- **Password:** `12345678`
- **IP:** `192.168.4.1`

Open that IP in a browser to configure Wi-Fi, then restart.

---

## Web UI Walkthrough

Open the IP address shown on the display in any browser on the same network. The Web UI has four tabs.

---

### Tab 1 — Settings

#### Location
| Field | Description |
|-------|-------------|
| Latitude / Longitude | Your position. Used as radar center. |
| Radius (km) | How far the radar reaches (5–300 km). |
| Auto-detect location | Button — detects your position via IP geolocation (accuracy ~1–5 km). |

#### Airport selector
Choose your home airport from the dropdown. This sets the coordinates and runway directions shown on the radar, and determines which flights are considered departures/landings in Spotlight mode.

#### Data interval
How often (in seconds) the ESP fetches new flight data automatically. Recommended: 30–60 seconds. OpenSky allows roughly one request per 10 seconds for anonymous access.

#### Track flight
Enter a callsign (e.g. `SWR123`) to follow a specific flight in the **Track** screen. Leave empty to disable.

#### FlightAware
| Field | Description |
|-------|-------------|
| API Key | Your AeroAPI key from flightaware.com |
| Credits limit | Maximum API credits to use (safety cap) |
| FlightAware enabled | Toggle on/off (default: off) |

FlightAware is used as a fallback for route data when OpenSky returns nothing.

#### OpenSky
| Field | Description |
|-------|-------------|
| Client ID | Your OpenSky OAuth2 Client ID |
| Client Secret | Your OpenSky OAuth2 Client Secret |
| Token status | Shows how long the current token is valid |

#### Home airport (IATA)
Three-letter airport code used for Spotlight detection and runway lines. Automatically set when you choose an airport from the dropdown.

#### Save & restart
All settings are saved to flash memory and the ESP restarts automatically.

---

### Tab 2 — Wi-Fi

Up to 5 Wi-Fi networks can be stored. The ESP scans for all known networks on boot and connects to the strongest one.

Enter SSID and password for each network, then click **Save & restart**.

---

### Tab 3 — Status

Live system information:

| Field | Description |
|-------|-------------|
| Wi-Fi | Connected network name |
| IP | Current IP address |
| Location | Configured coordinates |
| Uptime | Time since last boot |
| Aircraft | Number of aircraft in last fetch |
| FA requests | FlightAware API calls today |
| OSK requests | OpenSky route requests today |
| Route cache | Cached routes stored in flash |

Buttons:
- **Restart** — reboots the ESP
- **Factory Reset** — clears all settings and cache (Wi-Fi is kept)
- **Export cache (CSV)** — downloads all cached routes as a CSV file
- **Import cache** — paste a CSV to load previously exported routes

---

### Tab 4 — Log

Shows the last 20 system events. Useful for troubleshooting. Click **Refresh** to update.

Color coding:
- 🟢 Green — display events (`[DSP]`, `[ZRH]`)
- 🟡 Yellow — FlightAware events (`[FA]`)
- 🔵 Blue — network events (`[SKY]`, `[GEO]`, `[WLAN]`)
- 🩵 Cyan — web server events (`[WEB]`)

---

## Button Navigation

| Button | GPIO | Action |
|--------|------|--------|
| Forward | 21 | Next screen / next list page |
| Backward | 22 | Previous screen / previous list page |

**Screen order:**
```
List → Radar MAX → Spotlight → Radar APR → Track → List
```

On the **List** screen, the forward button pages through the list before advancing to the next screen. The backward button pages back before going to the previous screen.

Screens with no content are skipped automatically:
- **Radar APR** is skipped if no departures/landings are detected
- **Track** is skipped if no callsign is configured

**Auto standby:** After 5 automatic data refreshes without a button press, automatic loading pauses and the header shows `ZZZ`. Press any button to resume — the counter resets to 5.

---

## Getting API Credentials

### OpenSky Network (free, required for routes)

1. Go to [opensky-network.org](https://opensky-network.org) and create a free account
2. Log in and click your username → **Account**
3. Scroll to **API Client** → click **Create new client**
4. Give it a name (e.g. `esp-radar`) and confirm
5. Copy the **Client ID** and **Client Secret** shown — the secret is only shown once
6. Enter both in the Web UI under **Settings → OpenSky**

Free tier includes 4,000 API credits per day. The ESP uses one credit per route lookup.

> Note: OpenSky no longer supports basic username/password authentication. OAuth2 Client credentials are required.

### FlightAware AeroAPI (optional, paid)

FlightAware is used as a fallback when OpenSky returns no route. It is disabled by default.

1. Go to [flightaware.com/aeroapi](https://www.flightaware.com/commercial/aeroapi/)
2. Sign up and create an API key
3. Enter the key in the Web UI under **Settings → FlightAware**
4. Set a credits limit to control costs
5. Enable FlightAware with the toggle

AeroAPI charges per request. The credits limit in the Web UI acts as a hard cap to prevent unexpected costs.

---

## Troubleshooting

| Problem | Likely cause | Fix |
|---------|-------------|-----|
| No aircraft shown | OpenSky rate limit (HTTP 429) | Wait 10–15 minutes, avoid rapid restarts |
| Routes missing | OpenSky OAuth2 not configured | Add Client ID and Secret in Web UI |
| Display blank | TFT_eSPI not configured | Check `User_Setup.h` for your display |
| Can't connect to Web UI | Wrong IP or different network | Check IP on display, ensure same Wi-Fi |
| `ZZZ` in header | Auto standby active | Press any button to wake |
| Factory reset needed | Corrupted settings | Status tab → Factory Reset |

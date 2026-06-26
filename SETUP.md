# Setup Guide

This guide walks you through setting up the ATEM ESP-NOW Tally system from scratch — base station and tally units.

---

## What you need

### Hardware

| Item | Notes |
|---|---|
| ESP32-C3 with external antenna | One per tally unit (bridge + receivers — all identical hardware) |
| WS2812 LED strip, 6 LEDs per unit | Cut from a longer strip |
| USB-A to USB-C cables | One per unit (power only) |
| USB power supplies | One per unit, or a powered USB hub |
| Mac, PC, or Raspberry Pi | For the base station |

### Software prerequisites

| Tool | Purpose | Install |
|---|---|---|
| Node.js 18+ | Runs the base station | [nodejs.org](https://nodejs.org) |
| PlatformIO CLI | Builds and flashes firmware | See [section 4](#4-firmware-prerequisites) |
| Python 3 | Required by PlatformIO | [python.org](https://www.python.org) |
| Git | Cloning the repo | [git-scm.com](https://git-scm.com) |

> **Docker (optional, Linux only):** Docker can be used on Linux and Raspberry Pi. It is
> not recommended on macOS or Windows because Docker Desktop blocks the mDNS multicast
> that ESP32 devices use to find the server. Run natively on those platforms.

---

## 1. Clone the repo

```bash
git clone https://github.com/bakedpanda/ATEM-ESP-Now-Tally.git
cd ATEM-ESP-Now-Tally
```

---

## 2. Base station

The base station is a Node.js server that connects to your ATEM over the network and serves the web UI. ESP32 devices find it automatically via mDNS (`atem-tally.local`) — no IP configuration needed.

### macOS / Windows — run natively

```bash
cd server
npm install
npm start
```

> **Why not Docker?** Docker Desktop on macOS and Windows runs containers inside a Linux VM. mDNS multicast UDP never crosses that VM boundary, so ESP32 devices can't discover the server. Run Node.js natively instead.

### Linux / Raspberry Pi — Docker (optional)

```bash
cd server
docker compose up --build -d
```

Config is stored in a Docker volume so it survives restarts. The `docker-compose.yml` uses `network_mode: host` so mDNS packets reach the container.

To run natively on Linux instead (same as macOS/Windows):

```bash
cd server
npm install
npm start
```

### Verify it's running

Open **http://localhost:8259** in a browser. You should see the dashboard.

> **On Raspberry Pi or a remote machine:** replace `localhost` with the machine's IP address. Make sure port 8259 is reachable from your network.

### Stop / restart (Docker)

```bash
docker compose down      # stop
docker compose up -d     # start again (no rebuild needed unless you changed server code)
```

---

## 3. Connect to your ATEM

1. Open **http://[server-ip]:8259/settings**
2. Enter your ATEM's IP address in the **ATEM IP Address** field
3. Click **Save Settings**

The dashboard should update to show **ATEM: connected** within a few seconds. If it shows "disconnected" or "retrying", check that the ATEM IP is correct and that the base station can reach it on the network (same subnet, port 9910 UDP not blocked).

---

## 4. Firmware prerequisites

Install PlatformIO using the official installer for your OS. **Do not use `pip install platformio`** — the standalone installer sets up an isolated Python environment and is the recommended approach.

### macOS

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/develop/get-platformio.py -o get-platformio.py
python3 get-platformio.py
```

Add PlatformIO to your PATH (add this to `~/.zshrc` to make it permanent):

```bash
export PATH=$PATH:~/.platformio/penv/bin
```

### Windows

Download and run the installer script in PowerShell:

```powershell
(Invoke-WebRequest -Uri "https://raw.githubusercontent.com/platformio/platformio-core-installer/develop/get-platformio.py" -OutFile "get-platformio.py").Content | python -
```

Or install via [PlatformIO IDE for VS Code](https://platformio.org/install/ide?install=vscode) — the extension handles everything automatically.

### Linux

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/develop/get-platformio.py -o get-platformio.py
python3 get-platformio.py
```

Add to your PATH (add to `~/.bashrc` or `~/.zshrc`):

```bash
export PATH=$PATH:~/.platformio/penv/bin
```

> **Linux only:** you'll also need udev rules so PlatformIO can access the serial port without `sudo`:
> ```bash
> curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
> sudo udevadm control --reload-rules && sudo udevadm trigger
> ```

### Verify

```bash
pio --version
```

The first firmware build will download the ESP32 toolchain automatically (~500 MB, one-time).

---

## 5. Flash all units

All units — bridge and receivers — run **identical firmware**. Role (bridge or receiver) and unit ID are assigned from the web UI after first boot, not at flash time.

### Configure

Edit **`firmware/include/config.h`** once for your site. Every unit gets this same file:

```cpp
// WS2812 LED strip
#define LED_PIN   4    // GPIO pin for data line
#define LED_COUNT 6    // Number of LEDs per unit

// WiFi (2.4 GHz only — ESP32-C3 does not support 5 GHz)
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// Server is discovered automatically via mDNS (atem-tally.local)
```

> **Tip:** Use a 2.4 GHz network. ESP32-C3 does not support 5 GHz.

### Build and flash

Connect an ESP32-C3 via USB, then:

```bash
cd firmware
pio run -e tally --target upload
```

PlatformIO will detect the serial port automatically. If it can't find the device, specify it manually:

```bash
pio run -e tally --target upload --upload-port COM3          # Windows
pio run -e tally --target upload --upload-port /dev/ttyUSB0  # Linux/Mac
```

**Repeat for every unit** — same command, same firmware, no changes needed between units.

### Verify

Open the serial monitor to confirm each unit connects:

```bash
pio device monitor
```

You should see:
```
Connecting to WiFi......... connected
WiFi connected. IP: 192.168.x.x  Channel: 6
WS connected — MAC AA:BB:CC:DD:EE:FF
Unprovisioned — awaiting assignment in /assign
```

The LEDs will show **blue breathing** — this means the unit is connected to the server and waiting to be assigned a role.

---

## 6. Assign roles and unit IDs

Once units are powered on and connected to WiFi, they appear on the assign page immediately.

1. Open **http://[server-ip]:8259/assign**
2. Each unit appears as a row showing its MAC address (last 8 chars) and online status 🟢
3. For each unit, set:
   - **Unit ID** — a number 1–20 (must be unique per receiver; bridge can share an ID or have its own)
   - **Role** — `Bridge` for the one unit connected over WiFi; `Receiver` for all others
   - **ATEM Input** — which ATEM source this unit follows (leave 0 for the bridge if it has no camera)
4. Click **Save Assignments**

Units receive their role assignment in real time if they're still connected. After saving:
- The **bridge** stays connected to the server over WiFi and begins broadcasting tally via ESP-NOW. Its LEDs reflect its own ATEM input.
- **Receivers** disconnect from WiFi and switch to ESP-NOW-only mode. Their LEDs will show amber breathing until the bridge comes online.

> **One bridge only.** The UI will warn you if more than one unit is set to Bridge.

> **Identify button:** Click **Identify** next to any unit to trigger a 5-second white flash on that unit's LEDs — useful for matching physical units to rows in the table.

---

## 7. LED wiring

Each unit has a WS2812 strip with 6 LEDs. Connect it to the ESP32-C3:

| Strip wire | ESP32-C3 pin |
|---|---|
| Data | GPIO 4 (configurable via `LED_PIN` in `config.h`) |
| 5V / VCC | 5V (VBUS pin, or external 5V) |
| GND | GND |

> **Current draw:** 6 WS2812 LEDs at full white = ~360 mA. At typical tally brightness (20–80%), draw is much lower. USB power is sufficient for all states.

---

## 8. LED states

| Colour | Pattern | Meaning |
|---|---|---|
| Amber | Breathing | No WiFi / no server connection |
| Blue | Breathing | Connected to server, awaiting role assignment |
| White | Breathing | Assigned, but ATEM not connected or not configured |
| Dim white | Solid | Standby — connected, not on any ATEM bus |
| Green | Solid | On preview |
| Red | Solid | On program (on air) |
| White | Rapid flash (5 s) | Identify triggered from web UI |

---

## 9. Settings

All settings are at **http://[server-ip]:8259/settings** and persist across restarts.

| Setting | Default | Notes |
|---|---|---|
| ATEM IP | — | IP address of your ATEM switcher |
| Global Brightness | 80% | Overall LED brightness cap |
| Program Brightness | 80% | Solid red when on air |
| Preview Brightness | 80% | Solid green when on preview |
| Standby Brightness | 20% | Dim white when connected but not active |
| Animation Speed | Slow | Speed of the breathing animation (slow / medium / fast) |

---

## 10. Replacing a unit

If a unit fails, flash a replacement with the same firmware (`pio run -e tally --target upload`) and power it on. It will appear on the `/assign` page as a new MAC address showing blue breathing.

Assign it the same Unit ID and Role as the failed unit, click **Save**. The replacement slots straight in with no other changes needed.

---

## Troubleshooting

### Dashboard shows "Bridge: disconnected"
- Check the bridge serial monitor — did it connect to WiFi and the server?
- Confirm the server is running and reachable on your network (try `http://[server-ip]:8259`)
- On macOS/Windows, make sure you're running the server natively (`npm start`), not via Docker Desktop (Docker Desktop blocks mDNS)
- Make sure the Docker container is running (Linux/Pi): `docker compose ps`
- Confirm the unit was assigned `Role: Bridge` on the `/assign` page

### Unit shows blue breathing and doesn't progress
- The unit is connected to the server but hasn't been assigned a role yet
- Go to `/assign` and assign it a Role and Unit ID, then click **Save**

### Receiver units show amber breathing after bridge is on
- The receiver learns the bridge MAC from the first ESP-NOW broadcast — wait a few seconds after the bridge starts
- Confirm the bridge is connected to the server (dashboard shows "Bridge: connected")
- Confirm the bridge was assigned `Role: Bridge` on `/assign`

### Units don't appear in /assign
- Units appear as soon as they connect and send their `hello` message (within a few seconds of booting)
- Check the serial monitor — if it's stuck on `Connecting to WiFi`, check `WIFI_SSID` / `WIFI_PASSWORD` in `config.h`
- If the serial monitor shows `mDNS init failed`, try power-cycling the unit after the server is fully started
- Make sure the server is reachable as `atem-tally.local` from your network (mDNS must not be blocked by the router)

### ATEM won't connect
- Verify the IP in Settings is correct
- Confirm the base station and ATEM are on the same network
- ATEM uses UDP port 9910 — check no firewall is blocking it

### Serial port not found during flash
- On Windows, check Device Manager for the COM port number
- On Linux, you may need to add your user to the `dialout` group: `sudo usermod -aG dialout $USER`
- On Mac, try `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`

### PlatformIO can't find the native toolchain (for running tests)
```bash
pio pkg install --platform native
```

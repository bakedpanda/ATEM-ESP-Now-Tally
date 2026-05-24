# Setup Guide

This guide walks you through setting up the ATEM ESP-NOW Tally system from scratch — base station, bridge unit, and camera units.

---

## What you need

### Hardware

| Item | Notes |
|---|---|
| ESP32-C3 with external antenna | 1× bridge unit + 1× per camera |
| WS2812 LED strip, 6 LEDs per unit | Cut from a longer strip |
| USB-A to USB-C cables | One per unit (power only) |
| USB power supplies | One per unit, or a powered USB hub |
| Mac, PC, or Raspberry Pi | For the base station (anything that runs Docker) |

### Software prerequisites

| Tool | Purpose | Install |
|---|---|---|
| Docker Desktop (Mac/Win) or Docker Engine (Linux/Pi) | Runs the base station | [docker.com](https://www.docker.com/get-started/) |
| PlatformIO CLI | Builds and flashes firmware | See [section 4](#4-firmware-prerequisites) |
| Python 3 | Required by PlatformIO | [python.org](https://www.python.org) |
| Git | Cloning the repo | [git-scm.com](https://git-scm.com) |

---

## 1. Clone the repo

```bash
git clone https://github.com/bakedpanda/ATEM-ESP-Now-Tally.git
cd ATEM-ESP-Now-Tally
```

---

## 2. Base station (Docker)

The base station is a Node.js server that connects to your ATEM over the network and serves the web UI.

### Start the server

```bash
cd server
docker compose up --build -d
```

This builds the container and starts it in the background. Config is stored in a Docker volume so it survives restarts.

### Verify it's running

Open **http://localhost:8259** in a browser. You should see the dashboard.

> **On Raspberry Pi or a remote machine:** replace `localhost` with the machine's IP address. Make sure port 8259 is reachable from your network.

### Stop / restart

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

## 5. Flash the bridge unit

The bridge connects to your WiFi network and relays tally data to all camera units over ESP-NOW.

### Configure

Edit **`firmware/include/config.h`**:

```cpp
#define UNIT_ID       20              // Bridge is always unit 20

#define LED_PIN       8               // GPIO pin for WS2812 strip
#define LED_COUNT     6               // Number of LEDs on this unit

#define WIFI_SSID     "your-network"  // Your WiFi network name
#define WIFI_PASSWORD "your-password" // Your WiFi password
#define SERVER_HOST   "192.168.1.100" // IP of the machine running the Docker server
#define SERVER_PORT   8259
```

> **Tip:** Use a 2.4 GHz network. ESP32-C3 does not support 5 GHz.

### Build and flash

Connect the bridge ESP32-C3 via USB, then:

```bash
cd firmware
pio run -e bridge --target upload
```

PlatformIO will detect the serial port automatically. If it can't find the device, specify it manually:

```bash
pio run -e bridge --target upload --upload-port COM3   # Windows
pio run -e bridge --target upload --upload-port /dev/ttyUSB0  # Linux/Mac
```

### Verify

Open the serial monitor to confirm it connected:

```bash
pio device monitor
```

You should see:
```
Connecting to WiFi......... connected
WiFi connected. IP: 192.168.x.x  Channel: 6
WS connected to server
Bridge unit 20 ready
```

The bridge LEDs will show:
- **Amber breathing** → connecting to WiFi / server
- **White breathing** → connected to server, ATEM not yet configured or disconnected
- **Dim white** → connected and standby (if bridge is mapped to an unused ATEM input)

---

## 6. Flash camera units

Each camera unit only needs a unit ID set — no WiFi credentials required.

### Configure

Edit **`firmware/include/config.h`**:

```cpp
#define UNIT_ID   1    // ← Change this for each unit (1–19)

#define LED_PIN   8    // GPIO pin for WS2812 strip
#define LED_COUNT 6    // Number of LEDs
```

Leave the WiFi defines as-is — camera units don't use them.

### Build and flash

```bash
cd firmware
pio run -e camera --target upload
```

### Verify

Open the serial monitor:

```bash
pio device monitor
```

Expected:
```
Camera unit 1 ready
```

The LEDs will show **amber breathing** until the bridge is powered on and broadcasting.

### Repeat for all camera units

For each unit:
1. Change `UNIT_ID` in `config.h` to the next number (1, 2, 3 …)
2. Flash: `pio run -e camera --target upload`
3. Label the physical unit with its ID

---

## 7. Assign units to cameras

Once units are powered on and the bridge is running, they appear in the web UI via heartbeat.

1. Open **http://[server-ip]:8259/assign**
2. Each unit ID that has been seen will appear in the table with a "Last Seen" timestamp
3. Use the dropdown to assign each unit to an ATEM input (input names are pulled live from your switcher)
4. Click **Save Assignments**

The dashboard at **http://[server-ip]:8259** shows live tally state for all units.

---

## 8. LED wiring

Each unit has a WS2812 strip with 6 LEDs. Connect it to the ESP32-C3:

| Strip wire | ESP32-C3 pin |
|---|---|
| Data | GPIO 8 (configurable via `LED_PIN` in `config.h`) |
| 5V / VCC | 5V (VBUS pin, or external 5V) |
| GND | GND |

> **Current draw:** 6 WS2812 LEDs at full white = ~360 mA. At typical tally brightness (20–80%), draw is much lower. USB power is sufficient for all states.

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

If a unit fails, you have two options:

**Option A — Same unit ID:** Flash the replacement with the same `UNIT_ID`. No config changes needed; it slots straight in.

**Option B — New unit ID:** Flash with a new ID, then go to `/assign` and remap it to the correct ATEM input.

---

## Troubleshooting

### Dashboard shows "Bridge: disconnected"
- Check the bridge serial monitor — did it connect to WiFi and the server?
- Confirm `SERVER_HOST` in `config.h` points to the correct IP
- Make sure the Docker container is running: `docker compose ps`

### Camera units show amber breathing after bridge is on
- The camera learns the bridge MAC from the first broadcast. Wait a few seconds after the bridge starts.
- If it persists, confirm the bridge is broadcasting (serial monitor should show "WS connected to server")

### Units don't appear in /assign
- Units appear only after sending a heartbeat (every 5 seconds). Wait up to 10 seconds after powering on.
- Check the bridge is connected to the server (dashboard shows "Bridge: connected")

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

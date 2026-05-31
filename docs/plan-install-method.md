# Plan: Native Installation (replacing Docker)

Replace the Docker deployment with a native install experience:

- **Windows / Mac / Linux desktop** — Electron app with a system tray icon
- **Headless Linux / Raspberry Pi** — bash install script + systemd service

The server code (`server/`) is unchanged. Both targets run the same Node.js server;
the difference is only in how it is packaged and managed.

---

## Target 1: Electron tray app (Windows, Mac, Linux desktop)

### What it does

- Runs the Express/Socket.io server in the Electron main process
- Adds a system tray icon with a right-click menu:
  - **Open Dashboard** — opens `http://localhost:8259` in the default browser
  - **Start at login** — toggles `app.setLoginItemSettings()`
  - **Quit**
- No Electron window is ever shown — it is tray-only
- On first launch, opens the dashboard automatically

### New directory: `electron/`

```
electron/
├── main.js          # Electron entry point — starts server, creates tray
├── package.json     # Electron deps + electron-builder config
└── assets/
    ├── tray-icon.png          # 16×16 / 32×32 tray icon (template image on Mac)
    └── tray-icon-active.png   # Optional: different icon when ATEM is connected
```

**`electron/main.js`** outline:

```js
import { app, Tray, Menu, shell } from 'electron'
import { createServer } from '../server/src/index.js'  // or spawn as child process

app.whenReady().then(() => {
  // Start the server
  startServer()

  // Build tray
  const tray = new Tray('assets/tray-icon.png')
  tray.setContextMenu(Menu.buildFromTemplate([
    { label: 'Open Dashboard', click: () => shell.openExternal('http://localhost:8259') },
    { type: 'separator' },
    { label: 'Start at Login', type: 'checkbox', checked: getLoginItem(),
      click: (item) => app.setLoginItemSettings({ openAtLogin: item.checked }) },
    { type: 'separator' },
    { label: 'Quit', click: () => app.quit() },
  ]))
})
```

The server can either be imported directly (same process) or spawned as a child
process. Direct import is simpler; child process gives a clean restart mechanism
if the server crashes.

### Packaging with electron-builder

Add `electron/package.json` with electron-builder config:

```json
{
  "build": {
    "appId": "com.yourname.atem-tally",
    "productName": "ATEM Tally",
    "mac":     { "target": "dmg", "category": "public.app-category.utilities" },
    "win":     { "target": "nsis" },
    "linux":   { "target": "AppImage" }
  }
}
```

This produces:
- **Mac**: `.dmg` installer (user drags to Applications)
- **Windows**: NSIS `.exe` installer (standard next-next-finish)
- **Linux**: `.AppImage` (runs without installing)

### Mac code signing

Without code signing, macOS Gatekeeper will block the app on first launch with
"app can't be opened because Apple cannot check it for malicious software".
Users can override this, but it is a bad experience.

Proper signing requires:
- Apple Developer Program membership (~$99/year)
- Developer ID Application certificate
- Notarisation (submit the built `.dmg` to Apple's notary service after signing)

electron-builder handles signing and notarisation automatically if the certificate
is available in the build environment. This is configured via environment variables
(`APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`) in CI.

If signing is out of scope for now, include clear instructions in the README for
the right-click → Open workaround.

---

## Target 2: Headless Linux / Raspberry Pi

No Electron. The server binary runs as a systemd service managed by an install
script.

### Install script: `install.sh`

Single script, run with `curl … | bash` or cloned and run locally:

```bash
#!/usr/bin/env bash
# Installs ATEM Tally server as a systemd service

set -e

# 1. Check / install Node.js 20+
if ! command -v node &>/dev/null || [[ $(node -e 'process.exit(+process.version.slice(1)<20)') ]]; then
  curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
  sudo apt-get install -y nodejs
fi

# 2. Clone or update repo
INSTALL_DIR="/opt/atem-tally"
if [ -d "$INSTALL_DIR/.git" ]; then
  git -C "$INSTALL_DIR" pull
else
  sudo git clone https://github.com/yourname/atem-tally "$INSTALL_DIR"
fi

# 3. Install server dependencies
sudo npm ci --prefix "$INSTALL_DIR/server" --omit=dev

# 4. Create config directory
sudo mkdir -p /etc/atem-tally

# 5. Write systemd unit
sudo tee /etc/systemd/system/atem-tally.service > /dev/null <<EOF
[Unit]
Description=ATEM Tally Server
After=network.target

[Service]
Type=simple
User=atem-tally
WorkingDirectory=$INSTALL_DIR/server
ExecStart=/usr/bin/node src/index.js
Restart=on-failure
RestartSec=5
Environment=CONFIG_PATH=/etc/atem-tally/config.json

[Install]
WantedBy=multi-user.target
EOF

# 6. Create dedicated user (no login shell, no home)
id -u atem-tally &>/dev/null || sudo useradd -r -s /usr/sbin/nologin atem-tally

# 7. Enable and start
sudo systemctl daemon-reload
sudo systemctl enable --now atem-tally

echo ""
echo "ATEM Tally installed. Dashboard at http://$(hostname -I | awk '{print $1}'):8259"
```

### Managing the service on Pi

```bash
sudo systemctl status atem-tally    # check running
sudo systemctl restart atem-tally   # restart after config change
journalctl -u atem-tally -f         # follow logs
sudo systemctl disable atem-tally   # remove from startup
```

### Updating

Re-run `install.sh` — it pulls the latest code and restarts the service.
Or manually:

```bash
cd /opt/atem-tally && sudo git pull && sudo npm ci --prefix server --omit=dev
sudo systemctl restart atem-tally
```

---

## Release pipeline

Use **GitHub Actions** to build all three Electron targets and the Pi tarball on
every tagged release.

`.github/workflows/release.yml` outline:

```yaml
on:
  push:
    tags: ['v*']

jobs:
  build-electron:
    strategy:
      matrix:
        os: [macos-latest, windows-latest, ubuntu-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
      - run: npm ci --prefix electron
      - run: npm run dist --prefix electron
        env:
          APPLE_ID: ${{ secrets.APPLE_ID }}                           # Mac only
          APPLE_APP_SPECIFIC_PASSWORD: ${{ secrets.APPLE_ASP }}       # Mac only
          APPLE_TEAM_ID: ${{ secrets.APPLE_TEAM_ID }}                 # Mac only
      - uses: actions/upload-artifact@v4
        with:
          path: electron/dist/*
```

Release artefacts published to GitHub Releases:
- `ATEM-Tally-mac.dmg`
- `ATEM-Tally-win.exe`
- `ATEM-Tally-linux.AppImage`

---

## What happens to Docker

`docker-compose.yml` can stay as an optional deployment method for users who
prefer it (e.g. running alongside other Docker services on a NAS or server).
It does not need to be removed — the new install methods are additions, not
replacements.

The `SERVER_HOST` / `SERVER_PORT` references in the firmware flashing plan
(`plan-firmware-flashing.md`) are unaffected — the server port is the same
regardless of how it is deployed.

---

## Summary

| | Windows / Mac / Linux desktop | Headless Pi / Linux |
|---|---|---|
| **Method** | Electron DMG / EXE / AppImage | `install.sh` + systemd |
| **Server code changes** | None | None |
| **Node.js required** | No (bundled by Electron) | Yes (installed by script) |
| **Auto-start** | `app.setLoginItemSettings()` | `systemctl enable` |
| **Update** | Re-download and reinstall | Re-run `install.sh` |
| **New code** | `electron/main.js` + build config | `install.sh` |

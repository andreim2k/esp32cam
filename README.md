# ESP32-CAM Capture Server

Firmware for an AI Thinker ESP32-CAM module with a browser-based capture UI,
JSON camera controls, persistent WiFi configuration, PWM flash control, and
Arduino OTA updates.

The firmware serves the UI directly from the ESP32. There is no separate web
asset build step.

## Current Capabilities

- Browser UI at `http://<device-ip>/`
- Still image capture through `POST /snapshot`
- Full camera control payload:
  - resolution
  - JPEG quality
  - flash
  - brightness
  - contrast
  - saturation
  - exposure
  - gain
  - special effect
  - white balance mode
  - horizontal mirror
  - vertical flip
- System status through `GET /status`
- WiFi credential and bandwidth updates through `POST /wifi`
- Persistent configuration using EEPROM emulation
- Static IP defaults for the local network
- OTA firmware uploads with PlatformIO `espota`
- Watchdog, reconnect, and memory recovery logic for long-running operation

## Hardware Target

- Board: ESP32-CAM AI Thinker or compatible
- Camera: OV2640
- Flash LED: GPIO4, PWM controlled
- PSRAM: required for high-resolution captures
- WiFi: 2.4 GHz only

## Repository Layout

```text
.
|-- platformio.ini
|-- partitions_ota.csv
|-- README.md
|-- read_serial.py
`-- src
    |-- main.cpp
    `-- modules
        |-- camera.cpp / camera.h
        |-- config.cpp / config.h
        |-- credentials.h.example
        |-- flash.cpp / flash.h
        `-- webserver.cpp / webserver.h
```

## Firmware Architecture

### `src/main.cpp`

Owns the runtime lifecycle:

1. Initializes watchdog and stack protection.
2. Loads persistent configuration.
3. Initializes flash PWM.
4. Initializes the camera.
5. Connects WiFi.
6. Starts the HTTP server.
7. Starts Arduino OTA when WiFi is connected.
8. Runs the main loop:
   - watchdog reset
   - memory checks
   - WiFi reconnection
   - deferred WiFi reconnects requested by the web UI
   - `ArduinoOTA.handle()`
   - HTTP client handling

### `src/modules/config.*`

Stores device configuration and migrates older EEPROM layouts.

Configuration includes:

- WiFi SSID and password
- static IP, gateway, subnet, DNS
- API key field
- device hostname
- JPEG quality
- default camera resolution
- flash light threshold
- WiFi bandwidth mode

Credentials are loaded from `src/modules/credentials.h`, which is intentionally
ignored by git. Use `src/modules/credentials.h.example` as the template.

### `src/modules/camera.*`

Owns ESP camera initialization, frame capture, resolution switching, image
settings, statistics, and frame buffer cleanup.

Supported settings in the HTTP payload are represented by `CameraSettings`:

```cpp
struct CameraSettings {
  framesize_t resolution;
  uint8_t jpeg_quality;
  int8_t brightness;
  int8_t contrast;
  int8_t saturation;
  uint16_t exposure;
  uint8_t gain;
  uint8_t special_effect;
  uint8_t wb_mode;
  bool hmirror;
  bool vflip;
};
```

### `src/modules/flash.*`

Controls the built-in flash LED on GPIO4 through LEDC PWM.

It supports:

- off, low, medium, and high duty presets
- direct duty control
- light-level analysis from camera frames
- flash status reporting

### `src/modules/webserver.*`

Implements a small HTTP server using `WiFiServer`. It parses HTTP requests into
fixed-size buffers, routes requests, serves the embedded HTML UI, streams JPEG
responses, and returns JSON for status/configuration requests.

Current routes:

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/` | Browser UI |
| `GET` | `/status` | Device, WiFi, flash, and camera status |
| `POST` | `/snapshot` | Capture a JPEG using JSON camera settings |
| `POST` | `/wifi` | Save WiFi settings and request reconnect |

## Critical OTA Partition Requirement

OTA updates require **two application partitions**.

This project uses `partitions_ota.csv` and `platformio.ini` explicitly sets:

```ini
board_build.partitions = partitions_ota.csv
```

The current partition table is:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
spiffs,   data, spiffs,  0x290000,0x160000,
coredump, data, coredump,0x3F0000,0x10000,
```

The important parts are:

- `app0` is one firmware slot.
- `app1` is the second firmware slot.
- `otadata` records which slot should boot.
- Each app slot is `0x140000` bytes, or 1,310,720 bytes.

During OTA, the currently running firmware stays active in one app partition
while the new firmware is written to the other app partition. After the upload
is validated, the boot metadata is updated and the ESP32 boots the new slot.

Without two app partitions, OTA cannot safely replace the running firmware.

Do not switch this project to `huge_app.csv` or any single-app partition table
if OTA is required. A single large app slot may allow a bigger firmware image,
but it removes the inactive slot that OTA needs.

### First Flash vs OTA Flash

The first firmware installation should be done over serial:

```bash
pio run -e esp32cam --target upload
```

This writes the firmware and the OTA-capable partition table.

After the board is running this partition layout and is reachable on WiFi, use
OTA for normal updates:

```bash
pio run -e esp32cam-ota --target upload
```

If you change the partition table later, flash over serial again. Do not rely
on OTA to safely rewrite the flash layout that OTA itself depends on.

### OTA Size Limit

PlatformIO checks the maximum program size against the selected app partition.
For this project, each OTA slot is 1,310,720 bytes. Keep the firmware below
that limit or increase both `app0` and `app1` equally in the partition table.

Both OTA slots must remain large enough for the firmware.

## PlatformIO Environments

### Serial environment: `esp32cam`

Defined in `platformio.ini`:

```ini
[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino
monitor_speed = 115200
upload_speed = 460800
board_build.partitions = partitions_ota.csv
upload_port = /dev/cu.usbserial-110
monitor_port = /dev/cu.usbserial-110
```

Use this for initial flashing, partition-table changes, and recovery.

### OTA environment: `esp32cam-ota`

```ini
[env:esp32cam-ota]
extends = env:esp32cam
upload_protocol = espota
upload_port = 192.168.50.3
monitor_port = /dev/cu.usbserial-110
lib_deps =
    ArduinoJson @ 7.4.3
```

Use this after the device is already running and reachable at the configured
IP address.

## Initial Setup

1. Install PlatformIO.

   ```bash
   brew install platformio
   ```

2. Create local credentials.

   ```bash
   cp src/modules/credentials.h.example src/modules/credentials.h
   ```

3. Edit `src/modules/credentials.h`.

   ```cpp
   #define DEFAULT_SSID "your_wifi_ssid"
   #define DEFAULT_PASSWORD "your_wifi_password"
   ```

4. Build the firmware.

   ```bash
   pio run
   ```

5. Put the ESP32-CAM into serial boot mode.

   - Connect IO0 to GND.
   - Reset or power-cycle the board.

6. Flash over serial.

   ```bash
   pio run -e esp32cam --target upload
   ```

7. Remove IO0 from GND and reset the board.

8. Open the UI.

   ```text
   http://192.168.50.3/
   ```

The default persistent network profile currently forces:

- IP: `192.168.50.3`
- Gateway: `192.168.50.1`
- Subnet: `255.255.255.0`
- Primary DNS: `192.168.50.1`
- Secondary DNS: `8.8.8.8`

## OTA Update Workflow

Use OTA only after the board has already been flashed with
`partitions_ota.csv` and is running on WiFi.

```bash
pio run -e esp32cam-ota --target upload
```

During OTA:

- the HTTP server is stopped to free the TCP socket
- the camera is deinitialized to free PSRAM frame buffers
- watchdog resets continue during upload progress
- the server is restarted only if OTA errors before completion

Successful OTA ends with output similar to:

```text
Result: OK
Success
```

## Web UI

The UI is embedded in `WebServerManager::handleRoot()`.

The main page contains:

- screen capture card
- camera settings card
- network status card
- WiFi settings card
- camera status card

Camera settings are sent only when capturing a snapshot. The UI builds the
JSON payload and posts it to `/snapshot`.

## HTTP API

### `GET /`

Returns the embedded HTML interface.

### `GET /status`

Returns JSON status for flash, WiFi, and camera.

Example:

```bash
curl http://192.168.50.3/status
```

Status fields include:

- flash state and PWM duty
- local IP, gateway, subnet, DNS, MAC, SSID
- RSSI and signal percentage
- WiFi protocol, estimated speed, and bandwidth mode
- camera readiness, resolution, PSRAM status, and capture counters

### `POST /snapshot`

Captures and returns a JPEG.

Example:

```bash
curl -X POST "http://192.168.50.3/snapshot" \
  -H "Content-Type: application/json" \
  -d '{
    "resolution": "UXGA",
    "quality": 10,
    "flash": false,
    "brightness": 0,
    "contrast": 0,
    "saturation": 0,
    "exposure": 300,
    "gain": 0,
    "special_effect": 0,
    "wb_mode": 0,
    "hmirror": false,
    "vflip": false
  }' \
  -o photo.jpg
```

Supported resolution values:

- `UXGA`
- `SXGA`
- `XGA`
- `SVGA`
- `VGA`
- `CIF`
- `QVGA`
- `HQVGA`

Camera setting ranges:

| Field | Range | Notes |
| --- | --- | --- |
| `quality` | `10` to `63` | Lower value means better JPEG quality |
| `brightness` | `-2` to `2` | Sensor brightness |
| `contrast` | `-2` to `2` | Sensor contrast |
| `saturation` | `-2` to `2` | Sensor saturation |
| `exposure` | `0` to `1200` | Manual exposure value for lower resolutions |
| `gain` | `0` to `30` | `0` enables auto gain |
| `special_effect` | `0` to `6` | Sensor effect mode |
| `wb_mode` | `0` to `4` | `0` is auto white balance |
| `flash` | boolean | Enables flash for the capture |
| `hmirror` | boolean | Horizontal mirror |
| `vflip` | boolean | Vertical flip |

The firmware restores the previous camera resolution after a snapshot if the
request temporarily changed it.

### `POST /wifi`

Saves WiFi settings to EEPROM and requests a reconnect.

Example:

```bash
curl -X POST "http://192.168.50.3/wifi" \
  -H "Content-Type: application/json" \
  -d '{
    "ssid": "MyNetwork",
    "password": "MyPassword",
    "bandwidth": 0
  }'
```

Bandwidth values:

| Value | Mode | Purpose |
| --- | --- | --- |
| `0` | 802.11b | Maximum range |
| `1` | HT20 | Balanced |
| `2` | HT40 | Maximum speed |

The web UI can submit bandwidth changes without changing SSID or password.

## Security Notes

- `src/modules/credentials.h` is gitignored and must stay out of version
  control.
- The HTTP server does not currently enforce authentication.
- Keep the device on a trusted LAN or isolated IoT network.
- OTA is also intended for trusted-network use.

## Troubleshooting

### Serial upload fails

- Confirm the USB serial port in `platformio.ini`.
- Connect IO0 to GND before reset/power-on.
- Use a stable 5 V supply.
- Disconnect IO0 from GND after flashing.

### OTA upload fails

- Confirm the device is already running firmware built with
  `partitions_ota.csv`.
- Confirm `upload_port` in `[env:esp32cam-ota]` matches the device IP.
- Confirm the computer and ESP32-CAM are on the same network.
- Confirm the firmware still fits inside one OTA slot.
- If the partition table changed, flash over serial.

### Camera fails to initialize

- Confirm PSRAM-capable ESP32-CAM hardware.
- Confirm the camera ribbon cable is seated.
- Use a stable power supply.
- Check serial logs at 115200 baud.

### WiFi connects poorly

- ESP32-CAM supports 2.4 GHz only.
- Try bandwidth mode `0` for long-range 802.11b behavior.
- Keep the board away from noisy USB power and weak antennas.

## Useful Commands

Build all environments:

```bash
pio run
```

Serial upload:

```bash
pio run -e esp32cam --target upload
```

OTA upload:

```bash
pio run -e esp32cam-ota --target upload
```

Serial monitor:

```bash
pio device monitor -e esp32cam
```

Clean build outputs:

```bash
pio run --target clean
```

## Maintenance Checklist

Before releasing or OTA flashing a major change:

1. Run `pio run`.
2. Confirm firmware size is below the OTA slot limit.
3. Confirm `board_build.partitions = partitions_ota.csv`.
4. Confirm the board is reachable at the OTA upload IP.
5. Use serial flashing for partition-table changes.
6. Use OTA only for normal firmware updates after the OTA layout is installed.

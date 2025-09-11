# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Development Commands

### Build and Upload
```bash
# Build the project
pio run

# Upload firmware (replace port as needed)
pio run --target upload --upload-port /dev/cu.usbserial-110

# Monitor serial output
pio device monitor --port /dev/cu.usbserial-110 --baud 115200

# Clean build artifacts
pio run --target clean
```

### Hardware Setup
```bash
# List available serial ports
ls /dev/cu.*

# Check if device is detected
pio device list
```

## Architecture Overview

### Core Components
- **Camera Module**: ESP32-CAM with OV3660 sensor, supports multiple resolutions (UXGA down to HQVGA)
- **Flash Control**: PWM-based LED brightness control on GPIO4 with intelligent light detection
- **HTTP Server**: Custom lightweight web server for API-only operation (no web interface)
- **WiFi Management**: Configurable static IP or DHCP with comprehensive network status reporting

### Key Design Patterns
- **Synchronized Flash Capture**: Multi-frame technique with pre-warming to ensure stable flash exposure
- **Light Detection**: Cached ambient light analysis using camera frame buffer sampling
- **Resolution Management**: Dynamic resolution switching with automatic restoration
- **API-First Design**: RESTful endpoints with JSON responses, no HTML interface

### HTTP Request Flow
1. **Request Parsing**: Parse URL and query parameters from raw HTTP requests
2. **Parameter Validation**: Extract and validate resolution, flash mode, and other parameters  
3. **Camera Configuration**: Temporarily set resolution and capture settings
4. **Image Capture**: Execute synchronized capture with optional flash control
5. **Response Generation**: Stream JPEG data or JSON status responses
6. **Cleanup**: Restore original camera settings and free frame buffers

### Configuration Areas
- **WiFi Settings**: SSID, password, static IP vs DHCP configuration in `main.cpp`
- **Camera Settings**: Resolution defaults, JPEG quality, sensor optimizations
- **Flash Settings**: PWM frequency, light threshold, auto-flash sensitivity
- **Hardware Pins**: GPIO mappings for ESP32-CAM AI Thinker board

## Key Code Locations

- **Main Application**: `src/main.cpp` - Single file containing all functionality
- **Camera Initialization**: Lines 229-324 - Camera config and sensor optimization
- **HTTP Request Handler**: Lines 446-718 - Core API endpoint processing
- **Flash Control**: Lines 94-115 - PWM flash LED management
- **Light Detection**: Lines 122-164 - Ambient light analysis for auto-flash
- **WiFi Configuration**: Lines 329-388 - Network setup with static/DHCP support

## API Endpoints

### Image Capture
- `GET /capture?size=RESOLUTION&flash=auto|1` - Configurable resolution capture
- `GET /snap` - Quick UXGA capture (no flash)
- `GET /snap/flash` - Quick UXGA capture with flash
- `GET /snap/auto` - Quick UXGA capture with auto flash
- `GET /snap/RESOLUTION/flash` - Quick capture with specified resolution and flash
- `GET /snap/RESOLUTION/auto` - Quick capture with specified resolution and auto flash
- `GET /snap/RESOLUTION` - Quick capture with specified resolution (no flash)

### Flash Control  
- `GET /flash?on=1&duty=128` - Manual flash control with brightness
- `GET /flash/off|low|medium|high` - Flash presets

### System Status
- `GET /status` - Complete device status (flash, WiFi, camera)
- `GET /` - API information and available endpoints

## Development Notes

### Camera Resolutions
- UXGA (1600×1200) - Default for high-quality stills
- SXGA (1280×1024), XGA (1024×768), SVGA (800×600)
- VGA (640×480), CIF (400×296), QVGA (320×240), HQVGA (240×176)

### Flash Modes
- **Force Flash**: Always on for capture (`flash=1`)
- **Auto Flash**: Light-level dependent (`flash=auto`)  
- **No Flash**: Default behavior
- **Manual Control**: PWM duty cycle 0-255

### Network Configuration
- Edit `src/main.cpp` WiFi credentials and IP settings
- Toggle `USE_STATIC_IP` between static and DHCP modes
- Default static IP: 192.168.50.3

### Hardware Requirements
- ESP32-CAM module (AI Thinker or compatible)
- FTDI/USB-Serial adapter for programming
- Connect IO0 to GND only during programming
- 5V power supply recommended for stable operation

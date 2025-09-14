# ESP32-CAM Photo Capture Server

A practical ESP32-CAM firmware with modern web interface for camera control and photo capture. Features toggle controls, responsive design, and JSON-based API.

## ✨ Features

### 📷 **Camera Control**
- **Resolution Selection**: Multiple camera resolutions from UXGA (1600x1200) to QVGA (320x240)
- **Image Quality Controls**: Adjust brightness, contrast, and exposure settings
- **Image Orientation**: Horizontal mirror and vertical flip options with toggle controls
- **Flash Control**: Toggle-based flash control for better photos in low light
- **Capture Feedback**: Visual spinner overlay during photo capture process

### 🎛️ **Modern User Interface**
- **Toggle Controls**: Intuitive switch-style controls for flash and image orientation
- **Responsive Layout**: Adapts to various screen sizes from desktop to mobile
- **Real-time Settings**: Instantly see current camera settings in the UI
- **Capture Button Lock**: Prevents multiple simultaneous captures
- **Reset to Defaults**: One-click button to restore default settings

### 🔧 **Technical Features**
- **Modular Architecture**: Clean separation between camera, flash, and web server modules
- **Persistent Settings**: Camera settings apply immediately to live preview
- **JSON API**: Photo capture with comprehensive JSON request/response handling
- **Optimized Performance**: Fast photo capture with proper resource handling

## 🚀 Quick Start

### Hardware Requirements
- ESP32-CAM module (AI Thinker or compatible)
- FTDI/USB-Serial adapter for programming

### Wiring for Programming
```
ESP32-CAM    FTDI Adapter
------------------------
VCC       -> 5V (or 3.3V)
GND       -> GND
U0R       -> TX
U0T       -> RX
IO0       -> GND (for programming mode)
```

**Important**: Connect IO0 to GND only during programming. Remove this connection before normal operation.

### Software Setup

1. **Install PlatformIO**: 
   ```bash
   # macOS with Homebrew
   brew install platformio
   
   # Or install PlatformIO IDE extension in VS Code
   ```

2. **Clone and Build**:
   ```bash
   git clone https://github.com/andreim2k/esp32cam.git
   cd esp32
   pio run
   ```

3. **Upload Firmware**:
   ```bash
   # Find your serial port
   ls /dev/cu.*
   
   # Upload (replace with your port)
   pio run --target upload --upload-port /dev/cu.usbserial-110
   ```

4. **Configuration Setup**:
   
   Edit `src/modules/config.cpp` to change default WiFi settings:
   ```cpp
   void ConfigManager::resetToDefaults() {
     strncpy(config.wifi_ssid, "YOUR_WIFI_SSID", SSID_MAX_LEN - 1);
     strncpy(config.wifi_password, "YOUR_WIFI_PASSWORD", PASSWORD_MAX_LEN - 1);
     
     config.use_static_ip = true;
     config.static_ip = IPAddress(192, 168, 1, 100);  // Your desired IP
     config.gateway = IPAddress(192, 168, 1, 1);      // Your router IP
     config.subnet = IPAddress(255, 255, 255, 0);     // Network mask
   }
   ```

## 🏗️ Project Structure

The firmware is organized into separate modules:

### 📁 Files and Directories
```
esp32/
├── src/
│   ├── main.cpp              # Main application entry point
│   └── modules/
│       ├── config.h/cpp      # WiFi and network configuration
│       ├── camera.h/cpp      # Camera initialization and capture
│       ├── flash.h/cpp       # Flash LED control
│       └── webserver.h/cpp   # HTTP server and web interface
├── web/
│   └── index.html           # Standalone web interface template
├── platformio.ini            # Build configuration
└── README.md                 # This documentation
```

### 🔧 Module Overview

#### ConfigManager (`modules/config.*`)
- Manages WiFi credentials and network settings
- Handles static IP configuration
- Stores settings in EEPROM for persistence

#### CameraManager (`modules/camera.*`)
- Initializes camera hardware
- Handles resolution switching
- Manages camera settings (brightness, contrast, etc.)
- Tracks capture statistics

#### FlashManager (`modules/flash.*`)
- Controls the onboard LED flash
- Supports on/off toggle functionality
- Manages flash brightness

#### WebServerManager (`modules/webserver.*`)
- Implements a simple HTTP server
- Serves the web interface
- Handles API endpoints for camera control
- Processes JSON requests
- Returns image data and status information

## 📡 API Documentation

### Base URL
Once connected to WiFi, access your ESP32-CAM at: `http://[ESP32_IP_ADDRESS]`

### 📸 Photo Capture Endpoint

```http
POST /snapshot
Content-Type: application/json
```

This endpoint captures a photo with the specified settings.

#### Request Structure
```json
{
  "resolution": "UXGA",
  "flash": true,
  "brightness": 0,
  "contrast": 0,
  "exposure": 300,
  "hmirror": false,
  "vflip": false
}
```

#### Resolution Options
**Supported resolutions:**
- `UXGA` - 1600×1200 (2MP, highest quality)
- `SXGA` - 1280×1024 (1.3MP)
- `XGA` - 1024×768 (0.8MP)
- `SVGA` - 800×600 (0.5MP)
- `VGA` - 640×480 (0.3MP, balanced)
- `CIF` - 400×296 (0.1MP)
- `QVGA` - 320×240 (0.08MP, fastest)

#### Camera Settings Parameters
- **brightness**: -2 to +2 (exposure compensation)
- **contrast**: -2 to +2 (image contrast)
- **exposure**: 0-1200 (manual exposure value)
- **hmirror**: Horizontal mirror (boolean)
- **vflip**: Vertical flip (boolean)
- **flash**: Flash control (boolean)

### 📊 Status Endpoint

```http
GET /status
```

Returns JSON system status with real-time information:

```json
{
  "flash": {
    "on": false,
    "duty": 0,
    "brightness_percent": 0
  },
  "wifi": {
    "ip": "192.168.50.3",
    "gateway": "192.168.50.1",
    "subnet": "255.255.255.0",
    "dns": "192.168.50.1",
    "mac": "78:1C:3C:F6:8B:B0",
    "ssid": "YourNetwork",
    "mode": "Static",
    "rssi": -62,
    "connected": true
  },
  "camera": {
    "resolution": "UXGA (1600x1200)",
    "ready": true,
    "total_captures": 158,
    "failed_captures": 3
  }
}
```

### 🌐 Device Information Endpoint

```http
GET /
```

Returns the web interface for camera control. This endpoint serves the HTML user interface.

## 🌐 Web Interface

The firmware provides a modern, user-friendly web interface for controlling the ESP32-CAM.

### 📱 Interface Layout
Access the web interface at `http://[ESP32_IP_ADDRESS]` to see:

**Left Side (Photo Display):**
- 📷 **Photo Capture Area**: Shows captured photos with clear status indicator
- 🔄 **Capture Status**: Visual feedback during the capture process
- ⏳ **Loading Spinner**: Centered spinner appears during photo capture

**Right Side (Controls):**
- ⚙️ **Camera Controls Panel**:
  - Resolution selector (UXGA to QVGA)
  - Brightness slider (-2 to +2)
  - Contrast slider (-2 to +2)
  - Exposure slider (0-1200)
  - Toggle switches for Horizontal Mirror and Vertical Flip
  - Flash toggle switch
  - "Reset to Defaults" button to restore settings
  - "SNAPSHOT" button to capture photos

- 📊 **API Payload Panel**:
  - Shows the current camera settings in JSON format
  - Updates in real-time as you adjust settings
  - Displays timestamp of last update

### 📡 API Endpoints
The firmware supports these key endpoints:
- `POST /snapshot` - Capture photos with JSON settings payload
- `GET /status` - Get current camera and system status
- `GET /` - Access the web interface

## 📷 Usage Examples

### cURL Examples

```bash
# Take a photo with default settings
curl -X POST "http://192.168.1.100/snapshot" \
  -H "Content-Type: application/json" \
  -d '{
    "resolution": "UXGA",
    "flash": false,
    "brightness": 0,
    "contrast": 0,
    "exposure": 300,
    "hmirror": false,
    "vflip": false
  }' \
  -o photo.jpg

# High-resolution photo with flash
curl -X POST "http://192.168.1.100/snapshot" \
  -H "Content-Type: application/json" \
  -d '{
    "resolution": "UXGA",
    "flash": true,
    "brightness": 1,
    "contrast": 1,
    "exposure": 600
  }' \
  -o photo_flash.jpg

# Capture with horizontal mirror and vertical flip
curl -X POST "http://192.168.1.100/snapshot" \
  -H "Content-Type: application/json" \
  -d '{
    "resolution": "VGA",
    "flash": false,
    "brightness": 0,
    "contrast": 0,
    "exposure": 300,
    "hmirror": true,
    "vflip": true
  }' \
  -o flipped.jpg

# Check device status
curl "http://192.168.1.100/status" | jq .
```

### Python Example

```python
import requests
import json

# ESP32-CAM Configuration
ESP32_IP = "192.168.1.100"
BASE_URL = f"http://{ESP32_IP}"

def capture_photo(filename="photo.jpg", settings=None):
    """Capture a photo with custom settings"""
    url = f"{BASE_URL}/snapshot"
    
    default_settings = {
        "resolution": "UXGA",
        "flash": False,
        "brightness": 0,
        "contrast": 0,
        "exposure": 300,
        "hmirror": False,
        "vflip": False
    }
    
    if settings:
        default_settings.update(settings)
    
    response = requests.post(url, 
                          headers={'Content-Type': 'application/json'},
                          data=json.dumps(default_settings))
    
    if response.status_code == 200:
        with open(filename, 'wb') as f:
            f.write(response.content)
        print(f"Photo saved as {filename} ({len(response.content)} bytes)")
        return True
    else:
        print(f"Failed to capture photo: {response.status_code}")
        return False

def get_device_status():
    """Get device status"""
    response = requests.get(f"{BASE_URL}/status")
    
    if response.status_code == 200:
        status = response.json()
        
        # Print formatted status
        print("\n=== ESP32-CAM Status ===")
        print(f"IP: {status['wifi']['ip']}")
        print(f"Camera: {status['camera']['resolution']}")
        print(f"Flash: {'ON' if status['flash']['on'] else 'OFF'}")
        
        return status
    else:
        print(f"Status request failed: {response.status_code}")
        return None

# Example usage
if __name__ == "__main__":
    # Get device status
    status = get_device_status()
    
    if status:
        # Basic capture
        capture_photo("default.jpg")
        
        # With flash
        capture_photo("with_flash.jpg", {"flash": True})
        
        # Custom resolution
        capture_photo("vga.jpg", {
            "resolution": "VGA",
            "brightness": 1
        })
        
        # Flipped image
        capture_photo("flipped.jpg", {
            "hmirror": True,
            "vflip": True
        })
    else:
        print("Device not accessible")
```

## ⚙️ Configuration

### Network Configuration

The ESP32-CAM uses EEPROM to store WiFi and network settings. Default settings are defined in `src/modules/config.cpp` and include:

- WiFi SSID and password
- Static IP address (if enabled)
- Gateway and subnet settings

### Camera Configuration

Camera settings can be configured through the web interface and include:

- Resolution (UXGA, SXGA, XGA, SVGA, VGA, CIF, QVGA)
- Brightness (-2 to +2)
- Contrast (-2 to +2)
- Exposure (0-1200)
- Horizontal mirror (on/off)
- Vertical flip (on/off)
- Flash control (on/off)

These settings are applied immediately when changed in the UI and are sent in the JSON payload when capturing photos through the API.

## 🔧 Troubleshooting

### Common Issues

**Camera fails to initialize:**
- Check all wiring connections
- Ensure adequate power supply (5V recommended)
- Remove IO0-GND connection after programming

**WiFi connection issues:**
- Verify SSID and password in the config.cpp file
- Use 2.4GHz network (ESP32 doesn't support 5GHz)

**Flash not working:**
- The flash LED is on GPIO4 (built-in on most ESP32-CAM boards)
- Check if flash LED is physically damaged

**Poor image quality:**
- Ensure adequate lighting or enable flash
- Clean the camera lens
- Try different resolutions

### Serial Monitoring

Monitor serial output for debugging:
```bash
pio device monitor --port /dev/cu.usbserial-110 --baud 115200
```

Look for status messages like:
- Camera initialization
- WiFi connection
- HTTP server startup

### Build and Upload

Clean and rebuild if you encounter build problems:
```bash
pio run --target clean
pio run --target upload --upload-port /dev/cu.usbserial-110
```

## 🔄 Recent Updates

### Latest Features
- ✅ **Modern Toggle Switches**: Replaced checkboxes/buttons with toggles for flash, horizontal mirror, and vertical flip
- ✅ **Capture Feedback**: Added spinner overlay during photo capture
- ✅ **Capture Button Disable**: Prevents multiple simultaneous captures
- ✅ **Reset to Defaults Button**: One-click restore of camera settings
- ✅ **Renamed "Take Photo" to "SNAPSHOT"**: More concise button labeling
- ✅ **Performance Optimization**: Removed cyclic status polling
- ✅ **Eliminated Auto-Download**: Removed automatic photo downloads after capture
- ✅ **UI Improvements**: Clean, responsive design with proper toggle switch styling

## 📦 API Endpoint Reference

| Endpoint | Method | Purpose | Response Type | Description |
|----------|--------|---------|---------------|-------------|
| `/` | GET | Web Interface | HTML | Serves the main web interface |
| `/snapshot` | POST | Photo Capture | Binary JPEG | Captures a photo with JSON settings |
| `/status` | GET | System Status | JSON | Returns camera, flash, and WiFi status |

## 🛠️ Hardware Information

### ESP32-CAM Specifications
- **Board**: ESP32-CAM (AI Thinker) with PSRAM support
- **Camera**: OV2640 with resolution up to UXGA (1600×1200)
- **Flash**: Built-in LED on GPIO4 (PWM controlled)
- **Memory**: PSRAM enabled for image processing
- **WiFi**: 2.4GHz 802.11 b/g/n

### Build Configuration
```ini
[env:esp32cam]
platform = espressif32
board = esp32cam
framework = arduino

# Performance optimizations
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
board_build.partitions = huge_app.csv

# Hardware features
build_flags = 
    -DBOARD_HAS_PSRAM
    -DCAMERA_MODEL_AI_THINKER
    -DCONFIG_ESP32_SPIRAM_SUPPORT=1

# Libraries
lib_deps = 
    bblanchon/ArduinoJson@^7.0.4
```

## 🎃 Use Cases

### Home Projects
- **Security Camera**: Take photos when motion is detected
- **Garden Monitoring**: Document plant growth over time
- **Pet Monitoring**: Check on pets remotely
- **Smart Home Integration**: Integrate with home automation systems

### Educational & Development
- **Learning ESP32**: Great project for learning embedded systems
- **Computer Vision**: Capture images for AI/ML processing
- **IoT Prototyping**: Add camera capabilities to IoT projects

## 🤝 Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test your changes thoroughly
4. Submit a pull request with clear description

## 📄 License

This project is open source. See LICENSE file for details.

---

**ESP32-CAM Photo Capture Server** - Modern web interface for ESP32-CAM with toggle controls and responsive design

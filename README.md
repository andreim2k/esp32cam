# ESP32-CAM Advanced Capture Server v2.0

A lightweight ESP32-CAM firmware optimized for API-only operation with comprehensive REST API, smart flash control, and high-resolution photo capture capabilities.

## ✨ Features

- **🚀 API-Only Design**: Lightweight firmware optimized for direct API access
- **🔥 Smart Flash Control**: PWM-based brightness control with automatic light detection
- **📸 High-Resolution Capture**: Support for multiple resolutions (UXGA, SXGA, XGA, SVGA, VGA, etc.)
- **🌐 REST API**: Complete HTTP API for remote control and integration
- **⚡ Quick Capture Modes**: Fast snapshot endpoints for instant photos
- **💡 Auto Flash**: Intelligent flash activation based on ambient light levels
- **📊 Status Monitoring**: Real-time device and flash status reporting
- **🔧 Configurable Network**: Static IP or DHCP with detailed network information

## 🚀 Quick Start

### Hardware Requirements
- ESP32-CAM module (AI Thinker or compatible)
- FTDI/USB-Serial adapter for programming
- MicroSD card (optional)

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
   git clone <your-repo>
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

4. **Configure WiFi**: Edit `src/main.cpp` and update:
   ```cpp
   // WiFi Credentials
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   
   // Network Mode (choose one)
   #define USE_STATIC_IP     true    // true = static IP, false = DHCP
   
   // Static IP Settings (only used if USE_STATIC_IP is true)
   IPAddress staticIP(192, 168, 50, 3);      // Your desired IP
   IPAddress gateway(192, 168, 50, 1);       // Your router IP
   IPAddress subnet(255, 255, 255, 0);       // Network mask
   IPAddress primaryDNS(192, 168, 50, 1);    // DNS server
   ```

## 📡 API Documentation

### Base URL
Once connected to WiFi, access your ESP32-CAM at: `http://[ESP32_IP_ADDRESS]`

### 📸 Capture Endpoints

#### Basic Capture
```http
GET /capture
```
Captures a photo with default settings (UXGA resolution, no flash).

#### Flash Capture
```http
GET /capture?flash=1
```
Captures a photo with flash forced ON.

#### Auto Flash Capture
```http
GET /capture?flash=auto
```
Captures a photo with intelligent flash (activates only in low light).

#### Resolution Control
```http
GET /capture?size=RESOLUTION
```

**Supported resolutions:**
- `UXGA` - 1600×1200 (default for high quality)
- `SXGA` - 1280×1024
- `XGA` - 1024×768
- `SVGA` - 800×600
- `VGA` - 640×480
- `CIF` - 400×296
- `QVGA` - 320×240
- `HQVGA` - 240×176

#### Combined Parameters
```http
GET /capture?size=UXGA&flash=auto
```

### ⚡ Flash Control Endpoints

#### Turn Flash On/Off
```http
GET /flash?on=1        # Turn ON (full brightness)
GET /flash?on=0        # Turn OFF
```

#### Brightness Control
```http
GET /flash?duty=VALUE  # Set brightness (0-255)
```
- `0` = OFF
- `64` = 25% brightness  
- `128` = 50% brightness
- `255` = 100% brightness (maximum)

#### Combined Flash Control
```http
GET /flash?on=1&duty=128   # Turn ON with 50% brightness
```

#### Flash Preset Shortcuts
```http
GET /flash/off        # Turn OFF (0)
GET /flash/low        # Low brightness (64)
GET /flash/medium     # Medium brightness (128) 
GET /flash/high       # High brightness (255)
```

### 🚀 Quick Capture (Snap) Endpoints

Fast capture endpoints with pre-configured UXGA resolution:

```http
GET /snap             # Quick normal capture
GET /snap/flash       # Quick capture with flash
GET /snap/auto        # Quick capture with auto flash
```

### 📊 Status Endpoint

```http
GET /status
```

Returns JSON with current system status:
```json
{
  "flash": {
    "on": true,
    "duty": 128,
    "brightness_percent": 50
  },
  "wifi": {
    "ip": "192.168.50.3",
    "gateway": "192.168.50.1",
    "subnet": "255.255.255.0",
    "dns": "192.168.50.1",
    "mac": "AA:BB:CC:DD:EE:FF",
    "ssid": "MNZ",
    "mode": "Static",
    "rssi": -45,
    "connected": true
  },
  "camera": {
    "resolution": "UXGA (1600x1200)"
  }
}
```

## 🌐 API-Only Interface

This firmware is designed for direct API access without a web interface. Access the device info at `http://[ESP32_IP_ADDRESS]` which returns:

- **Device information** in JSON format
- **Available endpoints** list
- **Network configuration** details
- **Current operational mode** status

Example response:
```json
{
  "device": "ESP32-CAM Advanced Capture Server v2.0",
  "mode": "API-Only",
  "endpoints": {
    "capture": "/capture",
    "flash": "/flash",
    "snap": "/snap",
    "status": "/status"
  },
  "network": {
    "ip": "192.168.50.3",
    "mode": "Static"
  }
}
```

## 💡 Usage Examples

### cURL Examples

```bash
# Basic photo capture
curl -o photo.jpg "http://192.168.50.3/capture"

# High-res photo with auto flash
curl -o photo_hd.jpg "http://192.168.50.3/capture?size=UXGA&flash=auto"

# Set flash to medium brightness
curl "http://192.168.50.3/flash/medium"

# Quick snapshot with flash
curl -o snapshot.jpg "http://192.168.50.3/snap/flash"

# Check device status
curl "http://192.168.50.3/status"
```

### Browser Testing

Navigate to any endpoint in your browser:
```
http://192.168.50.3/capture?flash=auto
http://192.168.50.3/flash?duty=100
http://192.168.50.3/status
```

### Integration Examples

#### Python
```python
import requests

# Capture with auto flash
response = requests.get('http://192.168.50.3/capture?flash=auto')
with open('photo.jpg', 'wb') as f:
    f.write(response.content)

# Set flash brightness
requests.get('http://192.168.50.3/flash?duty=128')

# Get status
status = requests.get('http://192.168.50.3/status').json()
print(f"Flash is {'ON' if status['flash']['on'] else 'OFF'}")
print(f"Network mode: {status['wifi']['mode']} - IP: {status['wifi']['ip']}")
```

#### JavaScript/Node.js
```javascript
// Capture photo
const response = await fetch('http://192.168.50.3/capture?size=UXGA');
const buffer = await response.arrayBuffer();
fs.writeFileSync('photo.jpg', Buffer.from(buffer));

// Control flash
await fetch('http://192.168.50.3/flash/high');

// Get status
const status = await fetch('http://192.168.50.3/status').then(r => r.json());
console.log('Current brightness:', status.flash.brightness_percent + '%');
console.log('Network mode:', status.wifi.mode, '- IP:', status.wifi.ip);
```

## ⚙️ Configuration

### Camera Settings
The firmware is optimized for high-quality still photography:
- **Default Resolution**: UXGA (1600×1200)
- **JPEG Quality**: 10 (high quality)
- **Frame Buffer**: Double buffering with PSRAM
- **Auto Exposure**: Enabled with optimized settings

### Flash Settings  
Configurable in `src/main.cpp`:
```cpp
#define LIGHT_THRESHOLD   100  // Auto flash sensitivity (0-255)
#define LIGHT_CHECK_INTERVAL 1000  // Light check frequency (ms)
```

### WiFi Settings
Update your network configuration in `src/main.cpp`:

#### DHCP Mode (Automatic IP)
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
#define USE_STATIC_IP     false    // Use DHCP
```

#### Static IP Mode
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
#define USE_STATIC_IP     true     // Use static IP

// Configure your network settings
IPAddress staticIP(192, 168, 50, 3);      // ESP32-CAM IP address
IPAddress gateway(192, 168, 50, 1);       // Router/gateway IP
IPAddress subnet(255, 255, 255, 0);       // Subnet mask
IPAddress primaryDNS(192, 168, 50, 1);    // DNS server (usually router)
IPAddress secondaryDNS(8, 8, 8, 8);       // Backup DNS (Google)
```

**Default Configuration:**
- SSID: `MNZ`
- Static IP: `192.168.50.3`
- Gateway: `192.168.50.1`
- Subnet: `255.255.255.0`

## 🔧 Troubleshooting

### Common Issues

**Camera fails to initialize:**
- Check all wiring connections
- Ensure adequate power supply (5V recommended)
- Remove IO0-GND connection after programming

**WiFi connection issues:**
- Verify SSID and password in code
- Check WiFi signal strength
- Use 2.4GHz network (ESP32 doesn't support 5GHz)

**Flash not working:**
- Flash LED is on GPIO4 (built-in on most ESP32-CAM boards)
- Check if flash LED is physically damaged
- Verify PWM initialization in serial output

**Poor image quality:**
- Ensure adequate lighting or use flash
- Check camera lens for dust/fingerprints
- Try different resolutions

### Serial Debugging

Monitor serial output for debugging:
```bash
pio device monitor --port /dev/cu.usbserial-110 --baud 115200
```

Look for these status messages:
- `Flash LED initialized on GPIO4 with PWM control`
- `Camera initialization complete` 
- `WiFi connected successfully`
- `HTTP server started on port 80`

### Build Issues

Clean and rebuild if you encounter build problems:
```bash
pio run --target clean
pio run
```

## 📁 Project Structure

```
esp32/
├── src/
│   └── main.cpp          # Main firmware code
├── platformio.ini        # PlatformIO configuration
└── README.md            # This documentation
```

## 🔄 Version History

### v2.0 (Current) - API-Only Release
- ✅ **API-Only Design**: Removed HTML interface for optimal performance
- ✅ **Network Configuration**: Static IP and DHCP support with detailed status
- ✅ Complete code reorganization and cleanup
- ✅ Comprehensive REST API with all endpoints
- ✅ Smart flash control with PWM brightness
- ✅ Quick capture (snap) endpoints
- ✅ Status monitoring endpoint with network details
- ✅ Flash preset shortcuts
- ✅ Better error handling and serial debugging
- ✅ Reduced flash usage and faster response times

### v1.0 
- Basic capture functionality
- Simple flash control
- Web interface for manual operation

## 📝 API Response Formats

### Image Endpoints
- **Content-Type**: `image/jpeg`
- **Response**: Binary JPEG data
- **Status Codes**: 200 (success), 500 (camera error)

### Control Endpoints  
- **Content-Type**: `application/json`
- **CORS**: Enabled (`Access-Control-Allow-Origin: *`)
- **Response Format**: `{"status": "ok", ...additional fields}`

### Status Endpoint
- **Content-Type**: `application/json`
- **Response**: Complete device status object

## 🎯 Use Cases

- **Security Cameras**: Remote photo capture with motion detection integration
- **IoT Projects**: Automated photography based on sensor triggers  
- **Remote Monitoring**: Wildlife cameras, construction site monitoring
- **Home Automation**: Integration with smart home systems
- **Development**: API testing and camera functionality validation

## 🤝 Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test your changes thoroughly
4. Submit a pull request with clear description

## 📄 License

This project is open source. See LICENSE file for details.

---

**ESP32-CAM Advanced Capture Server v2.0** - Professional camera control with comprehensive API

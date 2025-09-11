/*
 * ESP32-CAM Advanced Capture Server
 * 
 * Features:
 * - High-resolution photo capture with configurable resolution
 * - Smart flash control with PWM brightness adjustment
 * - Auto flash based on ambient light detection
 * - Comprehensive REST API for remote control
 * - Web interface for manual control
 * 
 * Author: ESP32-CAM Project
 * Version: 2.0
 */

#include "esp_camera.h"
#include <WiFi.h>

// ===================
// CONFIGURATION
// ===================

// WiFi Settings
const char* ssid = "MNZ";
const char* password = "debianhusk1";

// Network Configuration - Set USE_STATIC_IP to true for static IP
#define USE_STATIC_IP     true    // true = static IP, false = DHCP

// Static IP Configuration (only used if USE_STATIC_IP is true)
IPAddress staticIP(192, 168, 50, 3);      // Static IP address
IPAddress gateway(192, 168, 50, 1);       // Gateway address
IPAddress subnet(255, 255, 255, 0);       // Subnet mask
IPAddress primaryDNS(192, 168, 50, 1);    // Primary DNS (usually gateway)
IPAddress secondaryDNS(8, 8, 8, 8);       // Secondary DNS (Google DNS)

// Camera Pin Configuration (AI Thinker ESP32-CAM)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN     4   // Built-in flash LED

// Flash Control Configuration
#define FLASH_CH          2   // LEDC channel for PWM flash control
#define FLASH_FREQ        5000 // PWM frequency in Hz
#define FLASH_RESOLUTION  8   // PWM resolution (8-bit = 0-255)
#define LIGHT_THRESHOLD   100 // Brightness threshold for auto flash
#define LIGHT_CHECK_INTERVAL 1000 // Light check interval in ms

// ===================
// GLOBAL VARIABLES
// ===================
static uint8_t currentFlashDuty = 0; // Track current flash brightness
WiFiServer server(80);

// ===================
// HELPER FUNCTIONS
// ===================

/**
 * Convert resolution string parameter to framesize_t enum
 */
framesize_t getFrameSize(String sizeParam) {
  if (sizeParam == "UXGA") return FRAMESIZE_UXGA;   // 1600x1200
  if (sizeParam == "SXGA") return FRAMESIZE_SXGA;   // 1280x1024
  if (sizeParam == "XGA") return FRAMESIZE_XGA;     // 1024x768
  if (sizeParam == "SVGA") return FRAMESIZE_SVGA;   // 800x600
  if (sizeParam == "VGA") return FRAMESIZE_VGA;     // 640x480
  if (sizeParam == "CIF") return FRAMESIZE_CIF;     // 400x296
  if (sizeParam == "QVGA") return FRAMESIZE_QVGA;   // 320x240
  if (sizeParam == "HQVGA") return FRAMESIZE_HQVGA; // 240x176
  return FRAMESIZE_VGA; // Default
}

// ===================
// FLASH CONTROL
// ===================

/**
 * Initialize PWM flash LED control
 */
void flashInit() {
  ledcSetup(FLASH_CH, FLASH_FREQ, FLASH_RESOLUTION);
  ledcAttachPin(FLASH_LED_PIN, FLASH_CH);
  ledcWrite(FLASH_CH, 0); // Start with flash OFF
  currentFlashDuty = 0;
  Serial.println("Flash LED initialized on GPIO4 with PWM control");
}

/**
 * Set flash brightness (0-255)
 */
void setFlashDuty(uint8_t duty) {
  currentFlashDuty = duty;
  ledcWrite(FLASH_CH, duty);
}

/**
 * Turn flash on/off with full brightness
 */
void setFlash(bool enable) {
  setFlashDuty(enable ? 255 : 0);
}

// ===================
// LIGHT DETECTION
// ===================

/**
 * Analyze brightness from existing camera frame buffer
 * Uses optimized sampling to avoid performance impact
 */
bool isLightLow(camera_fb_t* fb) {
  if (!fb || fb->len < 1000) return false;
  
  // Sample pixels from center area for brightness analysis
  uint32_t brightness = 0;
  int sampleSize = min(500, (int)(fb->len / 8));
  int start = fb->len / 4; // Start from center area
  
  // Sample every 4th pixel for speed optimization
  for (int i = start; i < start + sampleSize && i < (int)fb->len; i += 4) {
    brightness += fb->buf[i];
  }
  
  uint8_t avgBrightness = brightness / (sampleSize / 4);
  Serial.printf("Light level: %d (threshold: %d)\n", avgBrightness, LIGHT_THRESHOLD);
  return avgBrightness < LIGHT_THRESHOLD;
}

/**
 * Cached light detection for performance
 * Checks light level periodically to avoid continuous frame captures
 */
bool isLightLowSimple() {
  static unsigned long lastCheck = 0;
  static bool lastResult = true; // Default to flash ON for safety
  
  if (millis() - lastCheck > LIGHT_CHECK_INTERVAL) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      lastResult = isLightLow(fb);
      esp_camera_fb_return(fb);
      Serial.printf("Light check updated: %s\n", lastResult ? "LOW" : "BRIGHT");
    } else {
      lastResult = true; // Fail-safe: assume low light if can't get frame
      Serial.println("Light check failed, defaulting to LOW");
    }
    lastCheck = millis();
  }
  return lastResult;
}

// ===================
// CAMERA CAPTURE
// ===================

/**
 * Synchronized camera capture with optional flash
 * Uses multi-frame technique for stable flash exposure
 */
camera_fb_t* captureWithFlash(bool useFlash) {
  if (useFlash) {
    Serial.println("FLASH CAPTURE: Starting synchronized flash capture");
    
    // Turn on flash and allow stabilization time
    setFlash(true);
    Serial.println("FLASH CAPTURE: Flash ON - stabilizing");
    delay(500);
    
    // Discard warm-up frames for consistent exposure
    Serial.println("FLASH CAPTURE: Warming up camera...");
    for (int i = 0; i < 3; i++) {
      camera_fb_t * warmup = esp_camera_fb_get();
      if (warmup) {
        esp_camera_fb_return(warmup);
      }
      delay(100);
    }
    
    // Capture the final frame with stable flash
    Serial.println("FLASH CAPTURE: Capturing final frame");
    camera_fb_t * fb = esp_camera_fb_get();
    
    // Ensure full exposure completion
    delay(200);
    
    // Turn off flash
    setFlash(false);
    Serial.println("FLASH CAPTURE: Complete - flash OFF");
    
    return fb;
  } else {
    // Normal capture without flash
    return esp_camera_fb_get();
  }
}


// HTML interface removed - API-only mode

// ===================
// FUNCTION PROTOTYPES
// ===================
void startCameraServer();
void handleWebClient(WiFiClient client);
void initCamera();
void initWiFi();

// ===================
// INITIALIZATION
// ===================

/**
 * Initialize camera with optimized settings
 */
void initCamera() {
  Serial.println("Initializing camera...");
  
  // Camera config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10; // High quality for stills
  config.fb_count = 2; // Double buffering

  // If PSRAM IC present, init with UXGA resolution and higher JPEG quality
  // for larger pre-allocated frame buffer.
  if(config.pixel_format == PIXFORMAT_JPEG){
    if(psramFound()){
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // Initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // Set frame size for streaming
  if(config.pixel_format == PIXFORMAT_JPEG){
    s->set_framesize(s, FRAMESIZE_UXGA);  // High resolution
  }

  // Additional optimizations for real-time streaming
  s->set_brightness(s, 0);     // 0 = default
  s->set_contrast(s, 0);       // 0 = default
  s->set_saturation(s, 0);     // 0 = default
  s->set_special_effect(s, 0); // 0 = no effect
  s->set_whitebal(s, 1);       // 1 = enable auto white balance
  s->set_awb_gain(s, 1);       // 1 = enable auto white balance gain
  s->set_wb_mode(s, 0);        // 0 = auto white balance mode
  s->set_exposure_ctrl(s, 1);  // 1 = enable auto exposure
  s->set_aec2(s, 0);           // 0 = disable AEC2
  s->set_ae_level(s, 0);       // 0 = auto exposure level
  s->set_aec_value(s, 300);    // Auto exposure value
  s->set_gain_ctrl(s, 1);      // 1 = enable auto gain
  s->set_agc_gain(s, 0);       // 0 = auto gain value
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 = 2x gain ceiling
  s->set_bpc(s, 0);            // 0 = disable bad pixel correction
  s->set_wpc(s, 1);            // 1 = enable white pixel correction
  s->set_raw_gma(s, 1);        // 1 = enable gamma correction
  s->set_lenc(s, 1);           // 1 = enable lens correction
  s->set_hmirror(s, 0);        // 0 = no horizontal mirror
  s->set_vflip(s, 0);          // 0 = no vertical flip
  s->set_dcw(s, 1);            // 1 = enable downscale
  s->set_colorbar(s, 0);       // 0 = disable color bar test pattern
  
  Serial.println("Camera initialization complete");
}

/**
 * Initialize WiFi connection with DHCP or Static IP
 */
void initWiFi() {
  Serial.println("========== WiFi Configuration ==========");
  Serial.printf("SSID: %s\n", ssid);
  
  // Configure IP settings
  if (USE_STATIC_IP) {
    Serial.println("Configuring Static IP...");
    Serial.printf("Static IP: %s\n", staticIP.toString().c_str());
    Serial.printf("Gateway:   %s\n", gateway.toString().c_str());
    Serial.printf("Subnet:    %s\n", subnet.toString().c_str());
    Serial.printf("DNS:       %s, %s\n", primaryDNS.toString().c_str(), secondaryDNS.toString().c_str());
    
    // Configure static IP before connecting
    if (!WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("ERROR: Failed to configure static IP!");
      return;
    }
  } else {
    Serial.println("Using DHCP (automatic IP assignment)");
  }
  
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  // Connection timeout and status monitoring
  int attempts = 0;
  const int maxAttempts = 30; // 15 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Print status every 10 attempts
    if (attempts % 10 == 0) {
      Serial.println();
      Serial.printf("Connection attempt %d/%d...\n", attempts, maxAttempts);
      Serial.printf("WiFi status: %d\n", WiFi.status());
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("======== WiFi Connected Successfully ========");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway:    %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet:     %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("DNS:        %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("RSSI:       %d dBm\n", WiFi.RSSI());
    Serial.printf("MAC:        %s\n", WiFi.macAddress().c_str());
    Serial.println("===========================================");
  } else {
    Serial.println();
    Serial.println("ERROR: WiFi connection failed!");
    Serial.println("Check your WiFi credentials and network settings.");
    Serial.printf("Final WiFi status: %d\n", WiFi.status());
    Serial.println("Device will continue but network features won't work.");
  }
}

/**
 * Main setup function
 */
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("ESP32-CAM Advanced Capture Server v2.0");
  Serial.println("======================================");
  
  // Initialize hardware components
  flashInit();
  initCamera();
  initWiFi();
  
  // Start web server
  startCameraServer();

  // Print available endpoints and network information
  Serial.println();
  Serial.println("============ API SERVER READY ============");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("API Base URL:  http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println();
    Serial.println("📡 Available API Endpoints:");
    Serial.printf("  📸 Capture:    http://%s/capture\n", WiFi.localIP().toString().c_str());
    Serial.printf("  ⚡ Flash:      http://%s/flash\n", WiFi.localIP().toString().c_str());
    Serial.printf("  🚀 Quick Snap: http://%s/snap\n", WiFi.localIP().toString().c_str());
    Serial.printf("  📊 Status:     http://%s/status\n", WiFi.localIP().toString().c_str());
    Serial.printf("  ℹ️  Info:       http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println();
    Serial.printf("Mode: API-Only (No Web Interface)\n");
    Serial.printf("Network: %s\n", USE_STATIC_IP ? "Static IP" : "DHCP");
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("❌ WiFi not connected - API unavailable");
  }
  Serial.println("==========================================");
}

// ===================
// MAIN LOOP
// ===================

void loop() {
  WiFiClient client = server.available();
  if (client) {
    handleWebClient(client);
  }
  delay(1); // Small delay to prevent watchdog issues
}

// ===================
// HTTP REQUEST HANDLER
// ===================

void handleWebClient(WiFiClient client) {
  String currentLine = "";
  String request = "";

  while (client.connected()) {
    if (client.available()) {
      char c = client.read();

      if (c == '\n') {
        if (currentLine.length() == 0) {
          // HTTP request ended, process it
          if (request.indexOf("GET / ") >= 0) {
            // Serve API info (no HTML interface)
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            
            String apiInfo = "{";
            apiInfo += "\"device\":\"ESP32-CAM Advanced Capture Server v2.0\",";
            apiInfo += "\"mode\":\"API-Only\",";
            apiInfo += "\"endpoints\":{";
            apiInfo += "\"capture\":\"/capture\",";
            apiInfo += "\"flash\":\"/flash\",";
            apiInfo += "\"snap\":\"/snap\",";
            apiInfo += "\"status\":\"/status\"";
            apiInfo += "},";
            apiInfo += "\"network\":{";
            apiInfo += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            apiInfo += "\"mode\":\"" + String(USE_STATIC_IP ? "Static" : "DHCP") + "\"";
            apiInfo += "}";
            apiInfo += "}";
            
            client.println(apiInfo);
          }
          else if (request.indexOf("GET /capture") >= 0) {
            // Parse resolution parameter
            String sizeParam = "UXGA"; // Default high res for stills
            int sizeIndex = request.indexOf("size=");
            if (sizeIndex >= 0) {
              sizeParam = request.substring(sizeIndex + 5, request.indexOf(" ", sizeIndex));
              if (sizeParam.indexOf("&") >= 0) {
                sizeParam = sizeParam.substring(0, sizeParam.indexOf("&"));
              }
            }
            
            // Set resolution temporarily
            sensor_t * s = esp_camera_sensor_get();
            framesize_t originalSize = s->status.framesize;
            s->set_framesize(s, getFrameSize(sizeParam));
            
            // Check flash modes
            bool forceFlash = request.indexOf("flash=1") >= 0;
            bool autoFlash = request.indexOf("flash=auto") >= 0;
            bool useFlash = forceFlash;
            
            if (autoFlash && !forceFlash) {
              useFlash = isLightLowSimple(); // Only flash if actually needed
              Serial.printf("AUTO FLASH: Light check result = %s\n", useFlash ? "FLASH ON" : "FLASH OFF");
            }
            
            if (forceFlash) {
              Serial.println("FORCE FLASH: Always ON");
            }
            
            // Capture image with synchronized flash
            camera_fb_t * fb = captureWithFlash(useFlash);
            
            // Restore original resolution
            s->set_framesize(s, originalSize);
            
            if (fb) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: image/jpeg");
              client.printf("Content-Length: %u\r\n", fb->len);
              client.println("Connection: close");
              client.println();
              client.write(fb->buf, fb->len);
              esp_camera_fb_return(fb);
            } else {
              client.println("HTTP/1.1 500 Internal Server Error");
              client.println();
            }
          }
          else if (request.indexOf("GET /flash") >= 0) {
            // Flash control endpoint
            int onIndex = request.indexOf("on=");
            int dutyIndex = request.indexOf("duty=");
            
            String responseMsg = "";
            
            if (dutyIndex >= 0) {
              String dutyParam = request.substring(dutyIndex + 5, request.indexOf(" ", dutyIndex));
              if (dutyParam.indexOf("&") >= 0) {
                dutyParam = dutyParam.substring(0, dutyParam.indexOf("&"));
              }
              uint8_t duty = constrain(dutyParam.toInt(), 0, 255);
              setFlashDuty(duty);
              responseMsg = "\"duty\":" + String(duty);
              Serial.printf("Flash duty set to: %d\n", duty);
            }
            
            if (onIndex >= 0) {
              String onParam = request.substring(onIndex + 3, request.indexOf(" ", onIndex));
              if (onParam.indexOf("&") >= 0) {
                onParam = onParam.substring(0, onParam.indexOf("&"));
              }
              bool flashOn = onParam == "1";
              
              if (dutyIndex < 0) { // Only set on/off if duty wasn't specified
                setFlash(flashOn);
              }
              
              if (responseMsg.length() > 0) responseMsg += ",";
              responseMsg += "\"on\":" + String(flashOn ? "true" : "false");
              Serial.printf("Flash turned: %s\n", flashOn ? "ON" : "OFF");
            }
            
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            // Clean up trailing comma if present
            if (responseMsg.endsWith(",")) {
              responseMsg = responseMsg.substring(0, responseMsg.length() - 1);
            }
            client.println("{\"status\":\"ok\"," + responseMsg + "}");
          }
          else if (request.indexOf("GET /snap") >= 0) {
            // Quick capture endpoints
            bool useFlash = false;
            bool autoFlash = false;
            String resolutionStr = "UXGA"; // Default resolution
            
            // Parse URL for pattern like /snap/RESOLUTION/flash, /snap/RESOLUTION/auto, or /snap/RESOLUTION
            if (request.indexOf("/snap/") >= 0) {
              // Extract resolution - check both with and without trailing slash
              if (request.indexOf("/snap/UXGA") >= 0) resolutionStr = "UXGA";
              else if (request.indexOf("/snap/SXGA") >= 0) resolutionStr = "SXGA";
              else if (request.indexOf("/snap/XGA") >= 0) resolutionStr = "XGA";
              else if (request.indexOf("/snap/SVGA") >= 0) resolutionStr = "SVGA";
              else if (request.indexOf("/snap/VGA") >= 0) resolutionStr = "VGA";
              else if (request.indexOf("/snap/CIF") >= 0) resolutionStr = "CIF";
              else if (request.indexOf("/snap/QVGA") >= 0) resolutionStr = "QVGA";
              else if (request.indexOf("/snap/HQVGA") >= 0) resolutionStr = "HQVGA";
              
              // Check for standard flash modes after resolution or directly
              if (request.indexOf("/flash") >= 0) {
                useFlash = true;
                Serial.printf("QUICK SNAP: Force flash with %s resolution\n", resolutionStr.c_str());
              } else if (request.indexOf("/auto") >= 0) {
                autoFlash = true;
                useFlash = isLightLowSimple();
                Serial.printf("QUICK SNAP: Auto flash = %s with %s resolution\n", 
                             useFlash ? "ON" : "OFF", resolutionStr.c_str());
              } else {
                Serial.printf("QUICK SNAP: Normal (no flash) with %s resolution\n", resolutionStr.c_str());
              }
            } else {
              // Original behavior for simple /snap endpoint (no resolution or flash specified)
              Serial.println("QUICK SNAP: Normal (no flash) with default UXGA resolution");
            }
            
            // Set the requested resolution for this capture
            sensor_t * s = esp_camera_sensor_get();
            framesize_t originalSize = s->status.framesize;
            s->set_framesize(s, getFrameSize(resolutionStr));
            
            camera_fb_t * fb = captureWithFlash(useFlash);
            
            // Restore original resolution
            s->set_framesize(s, originalSize);
            
            if (fb) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: image/jpeg");
              client.printf("Content-Length: %u\r\n", fb->len);
              client.println("Connection: close");
              client.println();
              client.write(fb->buf, fb->len);
              esp_camera_fb_return(fb);
            } else {
              client.println("HTTP/1.1 500 Internal Server Error");
              client.println();
            }
          }
          else if (request.indexOf("GET /status") >= 0) {
            // Status endpoint - returns current flash state and device info
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            
            String status = "{";
            status += "\"flash\":{";
            status += "\"on\":" + String(currentFlashDuty > 0 ? "true" : "false") + ",";
            status += "\"duty\":" + String(currentFlashDuty) + ",";
            status += "\"brightness_percent\":" + String((currentFlashDuty * 100) / 255);
            status += "},";
            status += "\"wifi\":{";
            status += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
            status += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
            status += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
            status += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";
            status += "\"mac\":\"" + WiFi.macAddress() + "\",";
            status += "\"ssid\":\"" + String(ssid) + "\",";
            status += "\"mode\":\"" + String(USE_STATIC_IP ? "Static" : "DHCP") + "\",";
            status += "\"rssi\":" + String(WiFi.RSSI()) + ",";
            status += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
            status += "},";
            status += "\"camera\":{";
            
            sensor_t * s = esp_camera_sensor_get();
            if (s) {
              status += "\"resolution\":\"";
              switch(s->status.framesize) {
                case FRAMESIZE_UXGA: status += "UXGA (1600x1200)"; break;
                case FRAMESIZE_SXGA: status += "SXGA (1280x1024)"; break;
                case FRAMESIZE_XGA: status += "XGA (1024x768)"; break;
                case FRAMESIZE_SVGA: status += "SVGA (800x600)"; break;
                case FRAMESIZE_VGA: status += "VGA (640x480)"; break;
                default: status += "Other"; break;
              }
              status += "\"";
            }
            status += "}";
            status += "}";
            
            client.println(status);
          }
          else if (request.indexOf("GET /flash/") >= 0) {
            // Flash preset endpoints
            uint8_t duty = 0;
            String preset = "";
            
            if (request.indexOf("/flash/off") >= 0) {
              duty = 0;
              preset = "off";
            } else if (request.indexOf("/flash/low") >= 0) {
              duty = 64;
              preset = "low";
            } else if (request.indexOf("/flash/medium") >= 0) {
              duty = 128;
              preset = "medium";
            } else if (request.indexOf("/flash/high") >= 0) {
              duty = 255;
              preset = "high";
            }
            
            if (preset != "") {
              setFlashDuty(duty);
              Serial.printf("Flash preset '%s' applied: duty=%d\n", preset.c_str(), duty);
              
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: application/json");
              client.println("Access-Control-Allow-Origin: *");
              client.println();
              
              String response = "{";
              response += "\"status\":\"ok\",";
              response += "\"preset\":\"" + preset + "\",";
              response += "\"duty\":" + String(duty) + ",";
              response += "\"brightness_percent\":" + String((duty * 100) / 255);
              response += "}";
              
              client.println(response);
            } else {
              client.println("HTTP/1.1 404 Not Found");
              client.println();
            }
          }
          else {
            // 404 Not Found
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-type:text/html");
            client.println();
            client.print("404 Not Found");
          }
          break;
        } else {
          currentLine = "";
        }
      } else if (c != '\r') {
        currentLine += c;
        if (currentLine.startsWith("GET ")) {
          request = currentLine;
        }
      }
    }
  }
  client.stop();
}

// ===================
// SERVER MANAGEMENT
// ===================

/**
 * Start the HTTP server on port 80
 */
void startCameraServer() {
  server.begin();
  Serial.println("HTTP server started on port 80");
}

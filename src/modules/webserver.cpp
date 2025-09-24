#include "webserver.h"
#include "esp_wifi.h"

// Global instance
WebServerManager webServerManager;

WebServerManager::WebServerManager() :
  server(nullptr),
  server_running(false),
  server_port(80),
  total_requests(0),
  error_requests(0),
  last_request_time(0) {
}

bool WebServerManager::begin(uint16_t port) {
  if (server) {
    delete server;
  }
  
  server_port = port;
  server = new WiFiServer(port);
  
  if (!server) {
    Serial.println("Failed to create WiFi server");
    return false;
  }
  
  server->begin();
  server_running = true;
  
  Serial.printf("HTTP server started on port %d\n", port);
  return true;
}

void WebServerManager::stop() {
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  server_running = false;
  Serial.println("HTTP server stopped");
}

void WebServerManager::handleClients() {
  if (!server_running || !server) {
    return;
  }
  
  WiFiClient client = server->available();
  if (client) {
    handleClient(client);
  }
}

void WebServerManager::handleClient(WiFiClient& client) {
  if (!client.connected()) {
    return;
  }
  
  total_requests++;
  last_request_time = millis();
  
  // Parse HTTP request
  HttpRequest request;
  if (!parseHttpRequest(client, request)) {
    Serial.println("Failed to parse HTTP request");
    error_requests++;
    client.stop();
    return;
  }
  
  logRequest(request);
  
  // Process request and generate response
  ApiResponse response = processRequest(request);
  
  logResponse(response);
  
  // Send response
  sendResponse(client, response);
  
  // Clean up binary data if allocated
  if (response.is_binary && response.binary_data) {
    free(response.binary_data);
  }
  
  client.stop();
}

bool WebServerManager::parseHttpRequest(WiFiClient& client, HttpRequest& request) {
  String current_line = "";
  bool headers_complete = false;
  int content_length = 0;
  
  request.type = REQ_UNKNOWN;
  request.has_content_length = false;
  request.content_length = 0;
  
  // Parse headers
  while (client.connected() && !headers_complete) {
    if (client.available()) {
      char c = client.read();
      
      if (c == '\n') {
        if (current_line.length() == 0) {
          headers_complete = true;
        } else {
          // Process header line
          if (current_line.startsWith("GET ")) {
            request.type = REQ_GET;
            int space_index = current_line.indexOf(' ', 4);
            if (space_index > 0) {
              String full_path = current_line.substring(4, space_index);
              int question_mark = full_path.indexOf('?');
              if (question_mark >= 0) {
                request.path = full_path.substring(0, question_mark);
                request.query_params = full_path.substring(question_mark + 1);
              } else {
                request.path = full_path;
                request.query_params = "";
              }
            }
          } else if (current_line.startsWith("POST ")) {
            request.type = REQ_POST;
            int space_index = current_line.indexOf(' ', 5);
            if (space_index > 0) {
              request.path = current_line.substring(5, space_index);
            }
          } else if (current_line.startsWith("Content-Length: ")) {
            content_length = current_line.substring(16).toInt();
            request.has_content_length = true;
            request.content_length = content_length;
          }
          
          request.headers += current_line + "\n";
          current_line = "";
        }
      } else if (c != '\r') {
        current_line += c;
      }
    }
  }
  
  // Read POST body if present
  if (request.type == REQ_POST && request.has_content_length && content_length > 0) {
    int bytes_read = 0;
    unsigned long timeout = millis() + 5000; // 5 second timeout
    
    while (bytes_read < content_length && millis() < timeout && client.connected()) {
      if (client.available()) {
        request.body += (char)client.read();
        bytes_read++;
      }
    }
  }
  
  return request.type != REQ_UNKNOWN;
}

ApiResponse WebServerManager::processRequest(const HttpRequest& request) {
  // Route to appropriate handler - Only essential endpoints
  if (request.path == "/") {
    return handleRoot();
  } else if (request.path == "/status") {
    return handleStatus();
  } else if (request.path == "/snapshot") {
    return handleSnapshot(request);
  } else {
    return handle404();
  }
}

void WebServerManager::sendResponse(WiFiClient& client, const ApiResponse& response) {
  // Send status line
  client.printf("HTTP/1.1 %d %s\r\n", response.status_code, 
                response.status_code == 200 ? "OK" : 
                response.status_code == 404 ? "Not Found" : "Error");
  
  // Send headers
  client.printf("Content-Type: %s\r\n", response.content_type.c_str());
  
  if (response.is_binary && response.binary_data) {
    client.printf("Content-Length: %u\r\n", response.content_length);
  } else {
    client.printf("Content-Length: %u\r\n", response.body.length());
  }
  
  // CORS headers
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  
  // Send body
  if (response.is_binary && response.binary_data) {
    client.write(response.binary_data, response.content_length);
  } else {
    client.print(response.body);
  }
}

// API Endpoints
ApiResponse WebServerManager::handleRoot() {
  ApiResponse response;
  response.status_code = 200;
  response.content_type = "text/html";
  response.is_binary = false;
  
  response.body = generateWebPage();
  return response;
}

ApiResponse WebServerManager::handleStatus() {
  ApiResponse response;
  response.status_code = 200;
  response.content_type = "application/json";
  response.is_binary = false;
  
  JsonDocument doc;
  generateStatusJson(doc);
  
  response.body = "";
  serializeJson(doc, response.body);
  
  return response;
}

ApiResponse WebServerManager::handleSnapshot(const HttpRequest& request) {
  // This handles the legacy POST /snapshot endpoint with JSON body
  ApiResponse response;
  response.is_binary = true;
  response.content_type = "image/jpeg";
  
  if (request.type != REQ_POST) {
    response.status_code = 405; // Method not allowed
    response.is_binary = false;
    response.content_type = "application/json";
    response.body = createErrorResponse("Method not allowed");
    return response;
  }
  
  // Parse JSON body
  JsonDocument json;
  if (!parseJsonBody(request.body, json)) {
    response.status_code = 400;
    response.is_binary = false;
    response.content_type = "application/json";
    response.body = createErrorResponse("Invalid JSON");
    return response;
  }
  
  // Parse camera settings and flash mode
  CameraSettings settings;
  bool use_flash;
  if (!parseRequestSettings(json, settings, use_flash)) {
    response.status_code = 400;
    response.is_binary = false;
    response.content_type = "application/json";
    response.body = createErrorResponse("Invalid camera settings");
    return response;
  }
  
  // Apply settings
  framesize_t original_resolution = cameraManager.getCurrentResolution();
  if (!cameraManager.applySettings(settings)) {
    response.status_code = 500;
    response.is_binary = false;
    response.content_type = "application/json";
    response.body = createErrorResponse("Failed to apply camera settings");
    return response;
  }
  
  // Handle flash
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_MEDIUM);
    delay(200); // Stabilization
  }
  
  // Capture with warm-up frames
  cameraManager.warmupCamera(3);
  camera_fb_t* fb = cameraManager.captureFrame();
  
  // Turn off flash
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_OFF);
  }
  
  if (fb) {
    response.status_code = 200;
    response.content_length = fb->len;
    response.binary_data = (uint8_t*)malloc(fb->len);
    if (response.binary_data) {
      memcpy(response.binary_data, fb->buf, fb->len);
      cameraManager.releaseFrameBuffer(fb);
    } else {
      cameraManager.releaseFrameBuffer(fb);
      response.status_code = 500;
      response.is_binary = false;
      response.content_type = "application/json";
      response.body = createErrorResponse("Out of memory");
    }
  } else {
    response.status_code = 500;
    response.is_binary = false;
    response.content_type = "application/json";
    response.body = createErrorResponse("Camera capture failed");
  }
  
  return response;
}

ApiResponse WebServerManager::handle404() {
  ApiResponse response;
  response.status_code = 404;
  response.content_type = "text/plain";
  response.is_binary = false;
  response.body = "404 Not Found";
  
  return response;
}

// JSON utilities
String WebServerManager::createJsonResponse(const char* status, const JsonDocument& data) {
  if (data.isNull()) {
    JsonDocument response;
    response["status"] = status;
    
    String result;
    serializeJson(response, result);
    return result;
  } else {
    // Return the data document as is, assuming it already contains status
    String result;
    serializeJson(data, result);
    return result;
  }
}

String WebServerManager::createErrorResponse(const char* error, int code) {
  JsonDocument response;
  response["status"] = "error";
  response["error"] = error;
  response["code"] = code;
  
  String result;
  serializeJson(response, result);
  return result;
}

bool WebServerManager::parseJsonBody(const String& body, JsonDocument& doc) {
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("JSON parsing failed: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool WebServerManager::parseRequestSettings(const JsonDocument& json, CameraSettings& settings, bool& use_flash) {
  // Set defaults
  settings.resolution = FRAMESIZE_UXGA;
  settings.brightness = 0;
  settings.contrast = 0;
  settings.saturation = 0;
  settings.exposure = 300;
  settings.gain = 0;
  settings.special_effect = 0;
  settings.wb_mode = 0;
  settings.hmirror = false;
  settings.vflip = false;
  use_flash = false;
  
  // Parse resolution
  if (json["resolution"].is<String>()) {
    String res = json["resolution"].as<String>();
    settings.resolution = cameraManager.getFrameSize(res);
  }
  
  // Parse numeric settings
  if (json["brightness"].is<int>()) settings.brightness = constrain(json["brightness"], -2, 2);
  if (json["contrast"].is<int>()) settings.contrast = constrain(json["contrast"], -2, 2);
  if (json["saturation"].is<int>()) settings.saturation = constrain(json["saturation"], -2, 2);
  if (json["exposure"].is<int>()) settings.exposure = constrain(json["exposure"], 0, 1200);
  if (json["gain"].is<int>()) settings.gain = constrain(json["gain"], 0, 30);
  if (json["special_effect"].is<int>()) settings.special_effect = constrain(json["special_effect"], 0, 6);
  if (json["wb_mode"].is<int>()) settings.wb_mode = constrain(json["wb_mode"], 0, 4);
  
  // Parse boolean settings
  if (json["hmirror"].is<bool>()) settings.hmirror = json["hmirror"].as<bool>();
  if (json["vflip"].is<bool>()) settings.vflip = json["vflip"].as<bool>();
  if (json["flash"].is<bool>()) use_flash = json["flash"].as<bool>();
  
  return true;
}

// Helper methods
void WebServerManager::logRequest(const HttpRequest& request) {
  Serial.printf("HTTP %s %s", 
               request.type == REQ_GET ? "GET" : "POST", 
               request.path.c_str());
  if (!request.query_params.isEmpty()) {
    Serial.printf("?%s", request.query_params.c_str());
  }
  Serial.println();
}

void WebServerManager::logResponse(const ApiResponse& response) {
  Serial.printf("Response: %d %s (%s)\n", 
               response.status_code,
               response.is_binary ? "Binary" : "Text",
               response.content_type.c_str());
}

String WebServerManager::extractQueryParam(const String& query_params, const String& param_name) {
  if (query_params.isEmpty()) {
    return "";
  }
  
  int start = query_params.indexOf(param_name + "=");
  if (start < 0) {
    return "";
  }
  
  start += param_name.length() + 1; // Move past "param="
  int end = query_params.indexOf("&", start);
  
  String value;
  if (end < 0) {
    value = query_params.substring(start);
  } else {
    value = query_params.substring(start, end);
  }
  
  return urlDecode(value);
}

String WebServerManager::urlDecode(const String& str) {
  String decoded = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && i + 2 < str.length()) {
      // URL decode hex characters
      String hex = str.substring(i + 1, i + 3);
      char decoded_char = (char)strtol(hex.c_str(), nullptr, 16);
      decoded += decoded_char;
      i += 2;
    } else {
      decoded += c;
    }
  }
  return decoded;
}

void WebServerManager::generateDeviceInfo(JsonDocument& doc) {
  doc["device"] = configManager.getDeviceName();
  doc["version"] = "2.1";
  doc["mode"] = "POST-Only API";
  doc["description"] = "Advanced ESP32-CAM with JSON-only endpoints";
  
  JsonObject endpoints = doc["endpoints"].to<JsonObject>();
  endpoints["snapshot"] = "POST /snapshot - Camera capture with full settings";
  endpoints["status"] = "GET /status - System status and statistics";
  endpoints["info"] = "GET / - Device information";
  
  JsonObject network = doc["network"].to<JsonObject>();
  network["ip"] = WiFi.localIP().toString();
  network["mode"] = configManager.useStaticIP() ? "Static" : "DHCP";
}

void WebServerManager::generateStatusJson(JsonDocument& doc) {
  // Flash status
  FlashStatus flash_status = flashManager.getStatus();
  JsonObject flash = doc["flash"].to<JsonObject>();
  flash["on"] = flash_status.is_on;
  flash["duty"] = flash_status.duty_cycle;
  flash["brightness_percent"] = flash_status.brightness_percent;
  
  // WiFi status
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ip"] = WiFi.localIP().toString();
  wifi["gateway"] = WiFi.gatewayIP().toString();
  wifi["subnet"] = WiFi.subnetMask().toString();
  wifi["dns"] = WiFi.dnsIP().toString();
  wifi["mac"] = WiFi.macAddress();
  wifi["ssid"] = configManager.getWiFiSSID();
  wifi["mode"] = configManager.useStaticIP() ? "Static" : "DHCP";
  wifi["rssi"] = WiFi.RSSI();
  wifi["signal_percentage"] = getWiFiSignalPercentage();
  wifi["tx_power"] = "19.5 dBm (MAXIMUM - Long Range Mode)";
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["protocol"] = getWiFiProtocol();
  wifi["speed"] = getWiFiConnectionSpeed();
  wifi["bandwidth"] = getWiFiBandwidth();
  
  // Camera status
  JsonObject camera = doc["camera"].to<JsonObject>();
  camera["resolution"] = cameraManager.getResolutionString(cameraManager.getCurrentResolution());
  camera["ready"] = cameraManager.isReady();
  camera["total_captures"] = cameraManager.getTotalCaptureCount();
  camera["failed_captures"] = cameraManager.getFailedCaptureCount();
}

String WebServerManager::getWiFiProtocol() {
  // We force 802.11b mode for maximum range
  if (WiFi.status() != WL_CONNECTED) {
    return "disconnected";
  }
  
  // We explicitly set 802.11b mode for maximum distance
  return "802.11b (2.4GHz) - MAXIMUM RANGE MODE";
}

String WebServerManager::getWiFiBandwidth() {
  if (WiFi.status() != WL_CONNECTED) {
    return "unknown";
  }
  
  // We force 802.11b mode which uses 22MHz channels
  return "22MHz (802.11b DSSS) - Maximum Range";
}

String WebServerManager::getWiFiConnectionSpeed() {
  if (WiFi.status() != WL_CONNECTED) {
    return "disconnected";
  }
  
  // We force 802.11b mode for maximum range
  int rssi = WiFi.RSSI();
  
  // 802.11b speeds based on signal strength
  if (rssi > -50) {
    return "11 Mbps (802.11b CCK) - Maximum Range";
  } else if (rssi > -60) {
    return "5.5 Mbps (802.11b CCK) - Long Range";
  } else if (rssi > -70) {
    return "2 Mbps (802.11b DQPSK) - Extended Range";
  } else {
    return "1 Mbps (802.11b DBPSK) - Maximum Distance";
  }
}

int WebServerManager::getWiFiSignalPercentage() {
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  
  int rssi = WiFi.RSSI();
  
  // Convert RSSI to percentage
  // RSSI typically ranges from -30dBm (excellent) to -80dBm (very poor)
  if (rssi >= -30) {
    return 100; // Excellent signal
  } else if (rssi <= -80) {
    return 0;   // Very poor signal
  } else {
    // Linear interpolation between -30dBm (100%) and -80dBm (0%)
    // Formula: percentage = 2 * (rssi + 80)
    // This gives us: -30dBm = 100%, -40dBm = 80%, -50dBm = 60%, -60dBm = 40%, -70dBm = 20%, -80dBm = 0%
    return 2 * (rssi + 80);
  }
}

String WebServerManager::generateWebPage() {
  // Serve the updated HTML content with toggle buttons and spinner
  return "<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>ESP32-CAM Live Stream</title>\n"
"    <style>\n"
"        * {\n"
"            margin: 0;\n"
"            padding: 0;\n"
"            box-sizing: border-box;\n"
"        }\n"
"\n"
"        body {\n"
"            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
"            background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);\n"
"            color: #fff;\n"
"            min-height: 100vh;\n"
"        }\n"
"\n"
"        .container {\n"
"            max-width: 1400px;\n"
"            margin: 0 auto;\n"
"            padding: 20px;\n"
"            display: grid;\n"
"            grid-template-columns: 2fr 1fr;\n"
"            gap: 20px;\n"
"            min-height: 100vh;\n"
"        }\n"
"\n"
"        .left-column {\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            gap: 20px;\n"
"        }\n"
"\n"
"        .video-section {\n"
"            background: rgba(255, 255, 255, 0.1);\n"
"            border-radius: 15px;\n"
"            padding: 20px;\n"
"            backdrop-filter: blur(10px);\n"
"            border: 1px solid rgba(255, 255, 255, 0.2);\n"
"        }\n"
"\n"
"        .controls-section {\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            gap: 20px;\n"
"        }\n"
"\n"
"        .control-panel, .payload-panel {\n"
"            background: rgba(255, 255, 255, 0.1);\n"
"            border-radius: 15px;\n"
"            padding: 20px;\n"
"            backdrop-filter: blur(10px);\n"
"            border: 1px solid rgba(255, 255, 255, 0.2);\n"
"        }\n"
"\n"
"        h1, h2 {\n"
"            text-align: center;\n"
"            margin-bottom: 20px;\n"
"            color: #fff;\n"
"        }\n"
"\n"
"        #stream-container {\n"
"            position: relative;\n"
"            width: 100%;\n"
"            max-width: 100%;\n"
"            border-radius: 10px;\n"
"            overflow: hidden;\n"
"            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);\n"
"        }\n"
"\n"
"        #camera-display {\n"
"            width: 100%;\n"
"            height: auto;\n"
"            display: block;\n"
"            max-height: 70vh;\n"
"            object-fit: contain;\n"
"            background: rgba(255, 255, 255, 0.1);\n"
"            border: 2px dashed rgba(255, 255, 255, 0.3);\n"
"            min-height: 300px;\n"
"        }\n"
"        \n"
"        #camera-placeholder {\n"
"            display: flex;\n"
"            justify-content: center;\n"
"            align-items: center;\n"
"            height: 300px;\n"
"            color: rgba(255, 255, 255, 0.6);\n"
"            font-size: 18px;\n"
"            text-align: center;\n"
"        }\n"
"\n"
"        .photo-overlay {\n"
"            position: absolute;\n"
"            top: 10px;\n"
"            right: 10px;\n"
"            background: rgba(0, 0, 0, 0.7);\n"
"            color: #fff;\n"
"            padding: 8px 12px;\n"
"            border-radius: 5px;\n"
"            font-size: 12px;\n"
"            font-family: monospace;\n"
"        }\n"
"\n"
"        .control-group {\n"
"            margin-bottom: 20px;\n"
"        }\n"
"\n"
"        .control-group label {\n"
"            display: block;\n"
"            margin-bottom: 8px;\n"
"            font-weight: 600;\n"
"            color: #fff;\n"
"        }\n"
"\n"
"        .slider-container {\n"
"            position: relative;\n"
"            margin-bottom: 15px;\n"
"        }\n"
"\n"
"        .slider {\n"
"            width: 100%;\n"
"            height: 6px;\n"
"            border-radius: 3px;\n"
"            background: rgba(255, 255, 255, 0.3);\n"
"            outline: none;\n"
"            -webkit-appearance: none;\n"
"            appearance: none;\n"
"        }\n"
"\n"
"        .slider::-webkit-slider-thumb {\n"
"            appearance: none;\n"
"            width: 20px;\n"
"            height: 20px;\n"
"            border-radius: 50%;\n"
"            background: #4CAF50;\n"
"            cursor: pointer;\n"
"            box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);\n"
"        }\n"
"\n"
"        .slider::-moz-range-thumb {\n"
"            width: 20px;\n"
"            height: 20px;\n"
"            border-radius: 50%;\n"
"            background: #4CAF50;\n"
"            cursor: pointer;\n"
"            border: none;\n"
"            box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);\n"
"        }\n"
"\n"
"        .slider-value {\n"
"            position: absolute;\n"
"            right: 0;\n"
"            top: -25px;\n"
"            background: #4CAF50;\n"
"            color: white;\n"
"            padding: 2px 8px;\n"
"            border-radius: 12px;\n"
"            font-size: 12px;\n"
"            font-weight: bold;\n"
"        }\n"
"\n"
"        select, button {\n"
"            width: 100%;\n"
"            padding: 12px;\n"
"            margin-bottom: 10px;\n"
"            border: none;\n"
"            border-radius: 8px;\n"
"            background: rgba(255, 255, 255, 0.2);\n"
"            color: #fff;\n"
"            font-size: 14px;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s ease;\n"
"        }\n"
"\n"
"        select option {\n"
"            background: #1e3c72;\n"
"            color: #fff;\n"
"        }\n"
"\n"
"        button {\n"
"            background: linear-gradient(45deg, #4CAF50, #45a049);\n"
"            font-weight: 600;\n"
"            text-transform: uppercase;\n"
"            letter-spacing: 0.5px;\n"
"        }\n"
"\n"
"        button:hover {\n"
"            transform: translateY(-2px);\n"
"            box-shadow: 0 4px 12px rgba(76, 175, 80, 0.3);\n"
"        }\n"
"\n"
"        button:active {\n"
"            transform: translateY(0);\n"
"        }\n"
"\n"
"        .flash-controls {\n"
"            display: grid;\n"
"            grid-template-columns: 1fr 1fr;\n"
"            gap: 10px;\n"
"        }\n"
"\n"
"        .flash-controls button {\n"
"            margin: 0;\n"
"        }\n"
"\n"
"        .payload-display {\n"
"            background: #1a1a2e;\n"
"            border-radius: 8px;\n"
"            padding: 15px;\n"
"            font-family: 'Courier New', monospace;\n"
"            font-size: 12px;\n"
"            line-height: 1.4;\n"
"            max-height: 300px;\n"
"            overflow-y: auto;\n"
"            border: 1px solid rgba(255, 255, 255, 0.1);\n"
"        }\n"
"\n"
"        .payload-display pre {\n"
"            margin: 0;\n"
"            white-space: pre-wrap;\n"
"            word-wrap: break-word;\n"
"        }\n"
"\n"
"        .status-indicator {\n"
"            display: inline-block;\n"
"            width: 10px;\n"
"            height: 10px;\n"
"            border-radius: 50%;\n"
"            margin-right: 8px;\n"
"        }\n"
"\n"
"        .status-connected {\n"
"            background: #4CAF50;\n"
"            box-shadow: 0 0 10px #4CAF50;\n"
"        }\n"
"\n"
"        .status-disconnected {\n"
"            background: #f44336;\n"
"            box-shadow: 0 0 10px #f44336;\n"
"        }\n"
"\n"
"        .timestamp {\n"
"            color: #888;\n"
"            font-size: 10px;\n"
"            margin-bottom: 10px;\n"
"        }\n"
"\n"
"        @media (max-width: 1024px) {\n"
"            .container {\n"
"                grid-template-columns: 1fr;\n"
"                gap: 15px;\n"
"            }\n"
"            \n"
"            .left-column {\n"
"                order: 1;\n"
"            }\n"
"            \n"
"            .controls-section {\n"
"                order: 2;\n"
"            }\n"
"        }\n"
"\n"
"        .loading {\n"
"            display: flex;\n"
"            justify-content: center;\n"
"            align-items: center;\n"
"            height: 300px;\n"
"            font-size: 18px;\n"
"            color: #ccc;\n"
"        }\n"
"\n"
"        .wifi-info-grid {\n"
"            display: grid;\n"
"            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));\n"
"            gap: 15px;\n"
"            margin-bottom: 20px;\n"
"        }\n"
"\n"
"        .info-item {\n"
"            background: rgba(255, 255, 255, 0.05);\n"
"            padding: 12px;\n"
"            border-radius: 8px;\n"
"            border: 1px solid rgba(255, 255, 255, 0.1);\n"
"        }\n"
"\n"
"        .info-item label {\n"
"            display: block;\n"
"            font-size: 12px;\n"
"            color: rgba(255, 255, 255, 0.7);\n"
"            margin-bottom: 5px;\n"
"            text-transform: uppercase;\n"
"            letter-spacing: 0.5px;\n"
"        }\n"
"\n"
"        .info-value {\n"
"            font-size: 14px;\n"
"            font-weight: 600;\n"
"            color: #fff;\n"
"            font-family: 'Courier New', monospace;\n"
"        }\n"
"\n"
"        .wifi-status {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"            padding: 15px;\n"
"            background: rgba(255, 255, 255, 0.05);\n"
"            border-radius: 8px;\n"
"            border: 1px solid rgba(255, 255, 255, 0.1);\n"
"        }\n"
"\n"
"        .checkbox-group {\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            gap: 10px;\n"
"        }\n"
"\n"
"        .checkbox-label {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            cursor: pointer;\n"
"            font-size: 14px;\n"
"            font-weight: normal !important;\n"
"            margin-bottom: 0 !important;\n"
"        }\n"
"\n"
"        /* Toggle control styles */\n"
"        .toggle-control {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: space-between;\n"
"            margin-bottom: 15px;\n"
"        }\n"
"\n"
"        .toggle-control label {\n"
"            margin-bottom: 0 !important;\n"
"            font-weight: 500;\n"
"            color: #fff;\n"
"        }\n"
"\n"
"        .toggle-switch {\n"
"            position: relative;\n"
"            width: 60px;\n"
"            height: 30px;\n"
"            background: rgba(255, 255, 255, 0.3);\n"
"            border-radius: 15px;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s ease;\n"
"            border: none;\n"
"            outline: none;\n"
"        }\n"
"\n"
"        .toggle-switch.on {\n"
"            background: rgba(76, 175, 80, 0.8);\n"
"        }\n"
"\n"
"        .toggle-switch::before {\n"
"            content: '';\n"
"            position: absolute;\n"
"            top: 3px;\n"
"            left: 3px;\n"
"            width: 24px;\n"
"            height: 24px;\n"
"            background: #fff;\n"
"            border-radius: 50%;\n"
"            transition: all 0.3s ease;\n"
"            box-shadow: 0 2px 6px rgba(0, 0, 0, 0.3);\n"
"        }\n"
"\n"
"        .toggle-switch.on::before {\n"
"            transform: translateX(30px);\n"
"        }\n"
"\n"
"        .toggle-switch:hover {\n"
"            transform: scale(1.05);\n"
"        }\n"
"\n"
"        .toggle-switch:active {\n"
"            transform: scale(0.95);\n"
"        }\n"
"\n"
"        /* Spinner styles */\n"
"        .spinner-overlay {\n"
"            position: absolute;\n"
"            top: 0;\n"
"            left: 0;\n"
"            right: 0;\n"
"            bottom: 0;\n"
"            background: rgba(0, 0, 0, 0.8);\n"
"            display: none;\n"
"            justify-content: center;\n"
"            align-items: center;\n"
"            border-radius: 10px;\n"
"            z-index: 10;\n"
"        }\n"
"\n"
"        .spinner-container {\n"
"            display: flex;\n"
"            flex-direction: column;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"        }\n"
"\n"
"        .spinner {\n"
"            width: 60px;\n"
"            height: 60px;\n"
"            border: 4px solid rgba(255, 255, 255, 0.3);\n"
"            border-top: 4px solid #4CAF50;\n"
"            border-radius: 50%;\n"
"            animation: spin 1s linear infinite;\n"
"            margin-bottom: 15px;\n"
"        }\n"
"\n"
"        .spinner-text {\n"
"            color: #fff;\n"
"            font-size: 16px;\n"
"            font-weight: 600;\n"
"            text-align: center;\n"
"        }\n"
"\n"
"        @keyframes spin {\n"
"            0% { transform: rotate(0deg); }\n"
"            100% { transform: rotate(360deg); }\n"
"        }\n"
"\n"
"        .wifi-info-grid {\n"
"            display: grid;\n"
"            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));\n"
"            gap: 15px;\n"
"            margin-bottom: 20px;\n"
"        }\n"
"\n"
"        .info-item {\n"
"            background: rgba(255, 255, 255, 0.05);\n"
"            padding: 12px;\n"
"            border-radius: 8px;\n"
"            border: 1px solid rgba(255, 255, 255, 0.1);\n"
"        }\n"
"\n"
"        .info-item label {\n"
"            display: block;\n"
"            font-size: 12px;\n"
"            color: rgba(255, 255, 255, 0.7);\n"
"            margin-bottom: 5px;\n"
"            text-transform: uppercase;\n"
"            letter-spacing: 0.5px;\n"
"        }\n"
"\n"
"        .info-value {\n"
"            font-size: 14px;\n"
"            font-weight: 600;\n"
"            color: #fff;\n"
"            font-family: 'Courier New', monospace;\n"
"        }\n"
"\n"
"        .wifi-status {\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: center;\n"
"            padding: 15px;\n"
"            background: rgba(255, 255, 255, 0.05);\n"
"            border-radius: 8px;\n"
"            border: 1px solid rgba(255, 255, 255, 0.1);\n"
"        }\n"
"\n"
"        @media (max-width: 768px) {\n"
"            .wifi-info-grid {\n"
"                grid-template-columns: 1fr;\n"
"            }\n"
"        }\n"
"\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <div class=\"left-column\">\n"
"            <div class=\"video-section\">\n"
"                <h1>ESP32-CAM Photo Capture</h1>\n"
"                <div id=\"stream-container\">\n"
"                    <div id=\"camera-placeholder\">\n"
"                        <div>\n"
"                            <p>Click \"Take Photo\" to capture an image</p>\n"
"                            <p style=\"font-size: 14px; margin-top: 10px; color: rgba(255, 255, 255, 0.4);\">Adjust settings below and click capture</p>\n"
"                        </div>\n"
"                    </div>\n"
"                    <img id=\"camera-display\" src=\"\" alt=\"ESP32-CAM Photo\" style=\"display: none;\">\n"
"                    <div class=\"photo-overlay\" style=\"display: none;\">\n"
"                        <span class=\"status-indicator\" id=\"connection-status\"></span>\n"
"                        <span id=\"photo-status\">Ready</span>\n"
"                    </div>\n"
"                    <div class=\"spinner-overlay\" id=\"spinner-overlay\">\n"
"                        <div class=\"spinner-container\">\n"
"                            <div class=\"spinner\"></div>\n"
"                            <div class=\"spinner-text\">Capturing Photo...</div>\n"
"                        </div>\n"
"                    </div>\n"
"                </div>\n"
"            </div>\n"
"\n"
"            <div class=\"video-section\">\n"
"                <h2>WiFi Settings</h2>\n"
"                <div class=\"wifi-info-grid\">\n"
"                    <div class=\"info-item\">\n"
"                        <label>Network Name (SSID):</label>\n"
"                        <div class=\"info-value\" id=\"wifi-ssid\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>IP Address:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-ip\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>Connection Mode:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-mode\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>Signal Strength:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-signal\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>TX Power:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-txpower\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>Gateway:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-gateway\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>MAC Address:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-mac\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>WiFi Protocol:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-protocol\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>Connection Speed:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-speed\">Loading...</div>\n"
"                    </div>\n"
"                    <div class=\"info-item\">\n"
"                        <label>Channel Bandwidth:</label>\n"
"                        <div class=\"info-value\" id=\"wifi-bandwidth\">Loading...</div>\n"
"                    </div>\n"
"                </div>\n"
"                <div class=\"wifi-status\">\n"
"                    <span class=\"status-indicator\" id=\"wifi-status-indicator\"></span>\n"
"                    <span id=\"wifi-status-text\">Checking connection...</span>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"\n"
"        <div class=\"controls-section\">\n"
"            <div class=\"control-panel\">\n"
"                <h2>Camera Controls</h2>\n"
"                \n"
"                <div class=\"control-group\">\n"
"                    <label for=\"resolution-select\">Resolution:</label>\n"
"                    <select id=\"resolution-select\">\n"
"                        <option value=\"UXGA\">UXGA (1600x1200)</option>\n"
"                        <option value=\"SXGA\">SXGA (1280x1024)</option>\n"
"                        <option value=\"XGA\">XGA (1024x768)</option>\n"
"                        <option value=\"SVGA\">SVGA (800x600)</option>\n"
"                        <option value=\"VGA\">VGA (640x480)</option>\n"
"                        <option value=\"CIF\">CIF (400x296)</option>\n"
"                        <option value=\"QVGA\">QVGA (320x240)</option>\n"
"                    </select>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Brightness:</label>\n"
"                    <div class=\"slider-container\">\n"
"                        <input type=\"range\" id=\"brightness-slider\" class=\"slider\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\">\n"
"                        <span class=\"slider-value\" id=\"brightness-value\">0</span>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Contrast:</label>\n"
"                    <div class=\"slider-container\">\n"
"                        <input type=\"range\" id=\"contrast-slider\" class=\"slider\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\">\n"
"                        <span class=\"slider-value\" id=\"contrast-value\">0</span>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Exposure:</label>\n"
"                    <div class=\"slider-container\">\n"
"                        <input type=\"range\" id=\"exposure-slider\" class=\"slider\" min=\"0\" max=\"1200\" value=\"300\" step=\"50\">\n"
"                        <span class=\"slider-value\" id=\"exposure-value\">300</span>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Saturation:</label>\n"
"                    <div class=\"slider-container\">\n"
"                        <input type=\"range\" id=\"saturation-slider\" class=\"slider\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\">\n"
"                        <span class=\"slider-value\" id=\"saturation-value\">0</span>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Gain:</label>\n"
"                    <div class=\"slider-container\">\n"
"                        <input type=\"range\" id=\"gain-slider\" class=\"slider\" min=\"0\" max=\"30\" value=\"15\" step=\"1\">\n"
"                        <span class=\"slider-value\" id=\"gain-value\">15</span>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Special Effect:</label>\n"
"                    <select id=\"special-effect-select\">\n"
"                        <option value=\"0\">None</option>\n"
"                        <option value=\"1\">Negative</option>\n"
"                        <option value=\"2\">Grayscale</option>\n"
"                        <option value=\"3\">Red Tint</option>\n"
"                        <option value=\"4\">Green Tint</option>\n"
"                        <option value=\"5\">Blue Tint</option>\n"
"                        <option value=\"6\">Sepia</option>\n"
"                    </select>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>White Balance Mode:</label>\n"
"                    <select id=\"wb-mode-select\">\n"
"                        <option value=\"0\">Auto</option>\n"
"                        <option value=\"1\">Sunny</option>\n"
"                        <option value=\"2\">Cloudy</option>\n"
"                        <option value=\"3\">Office</option>\n"
"                        <option value=\"4\">Home</option>\n"
"                    </select>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Image Options:</label>\n"
"                    <div class=\"toggle-control\">\n"
"                        <label>Horizontal Mirror</label>\n"
"                        <button class=\"toggle-switch off\" id=\"hmirror-toggle\"></button>\n"
"                    </div>\n"
"                    <div class=\"toggle-control\">\n"
"                        <label>Vertical Flip</label>\n"
"                        <button class=\"toggle-switch off\" id=\"vflip-toggle\"></button>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <div class=\"control-group\">\n"
"                    <label>Flash Control:</label>\n"
"                    <div class=\"toggle-control\">\n"
"                        <label>Flash</label>\n"
"                        <button class=\"toggle-switch off\" id=\"flash-toggle\"></button>\n"
"                    </div>\n"
"                </div>\n"
"\n"
"                <button id=\"reset-btn\" style=\"background: linear-gradient(45deg, #f44336, #d32f2f); margin-bottom: 10px;\">Reset to Defaults</button>\n"
"                <button id=\"capture-btn\">SNAPSHOT</button>\n"
"            </div>\n"
"\n"
"            <div class=\"payload-panel\">\n"
"                <h2>API Payload</h2>\n"
"                <div class=\"timestamp\" id=\"last-updated\">Last updated: Never</div>\n"
"                <div class=\"payload-display\">\n"
"                    <pre id=\"payload-content\">{\n"
"  \"resolution\": \"UXGA\",\n"
"  \"flash\": \"off\",\n"
"  \"brightness\": 0,\n"
"  \"contrast\": 0,\n"
"  \"exposure\": 300\n"
"}</pre>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"    </div>\n"
"\n"
"    <script>\n"
"        class ESP32CameraController {\n"
"            constructor() {\n"
"                this.baseUrl = 'http://192.168.50.3';\n"
"                this.isConnected = false;\n"
"                this.currentSettings = {\n"
"                    resolution: 'UXGA',\n"
"                    flash: 'off',\n"
"                    brightness: 0,\n"
"                    contrast: 0,\n"
"                    saturation: 0,\n"
"                    exposure: 300,\n"
"                    gain: 15,\n"
"                    special_effect: 0,\n"
"                    wb_mode: 0,\n"
"                    hmirror: false,\n"
"                    vflip: false\n"
"                };\n"
"                \n"
"                this.init();\n"
"            }\n"
"\n"
"            init() {\n"
"                this.bindEvents();\n"
"                this.updatePayloadDisplay();\n"
"                this.loadWiFiInfo();\n"
"            }\n"
"\n"
"            bindEvents() {\n"
"                // Resolution change\n"
"                document.getElementById('resolution-select').addEventListener('change', (e) => {\n"
"                    this.currentSettings.resolution = e.target.value;\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Brightness control\n"
"                const brightnessSlider = document.getElementById('brightness-slider');\n"
"                const brightnessValue = document.getElementById('brightness-value');\n"
"                brightnessSlider.addEventListener('input', (e) => {\n"
"                    const value = parseInt(e.target.value);\n"
"                    this.currentSettings.brightness = value;\n"
"                    brightnessValue.textContent = value;\n"
"                    this.updateCameraSetting('brightness', value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Contrast control\n"
"                const contrastSlider = document.getElementById('contrast-slider');\n"
"                const contrastValue = document.getElementById('contrast-value');\n"
"                contrastSlider.addEventListener('input', (e) => {\n"
"                    const value = parseInt(e.target.value);\n"
"                    this.currentSettings.contrast = value;\n"
"                    contrastValue.textContent = value;\n"
"                    this.updateCameraSetting('contrast', value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Exposure control\n"
"                const exposureSlider = document.getElementById('exposure-slider');\n"
"                const exposureValue = document.getElementById('exposure-value');\n"
"                exposureSlider.addEventListener('input', (e) => {\n"
"                    const value = parseInt(e.target.value);\n"
"                    this.currentSettings.exposure = value;\n"
"                    exposureValue.textContent = value;\n"
"                    this.updateCameraSetting('exposure', value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Saturation control\n"
"                const saturationSlider = document.getElementById('saturation-slider');\n"
"                const saturationValue = document.getElementById('saturation-value');\n"
"                saturationSlider.addEventListener('input', (e) => {\n"
"                    const value = parseInt(e.target.value);\n"
"                    this.currentSettings.saturation = value;\n"
"                    saturationValue.textContent = value;\n"
"                    this.updateCameraSetting('saturation', value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Gain control\n"
"                const gainSlider = document.getElementById('gain-slider');\n"
"                const gainValue = document.getElementById('gain-value');\n"
"                gainSlider.addEventListener('input', (e) => {\n"
"                    const value = parseInt(e.target.value);\n"
"                    this.currentSettings.gain = value;\n"
"                    gainValue.textContent = value;\n"
"                    this.updateCameraSetting('gain', value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Special Effect control\n"
"                document.getElementById('special-effect-select').addEventListener('change', (e) => {\n"
"                    this.currentSettings.special_effect = parseInt(e.target.value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // White Balance Mode control\n"
"                document.getElementById('wb-mode-select').addEventListener('change', (e) => {\n"
"                    this.currentSettings.wb_mode = parseInt(e.target.value);\n"
"                    this.updatePayloadDisplay();\n"
"                });\n"
"\n"
"                // Toggle button controls\n"
"                document.getElementById('flash-toggle').addEventListener('click', () => {\n"
"                    this.toggleFlash();\n"
"                });\n"
"\n"
"                document.getElementById('hmirror-toggle').addEventListener('click', () => {\n"
"                    this.toggleHMirror();\n"
"                });\n"
"\n"
"                document.getElementById('vflip-toggle').addEventListener('click', () => {\n"
"                    this.toggleVFlip();\n"
"                });\n"
"\n"
"                // Reset button\n"
"                document.getElementById('reset-btn').addEventListener('click', () => {\n"
"                    this.resetToDefaults();\n"
"                });\n"
"\n"
"                // Capture button\n"
"                document.getElementById('capture-btn').addEventListener('click', () => {\n"
"                    this.takePhoto();\n"
"                });\n"
"            }\n"
"\n"
"            async updateCameraSetting(setting, value) {\n"
"                // Camera settings are applied in real-time through the stream\n"
"                console.log(`${setting} updated to ${value}`);\n"
"            }\n"
"\n"
"            toggleFlash() {\n"
"                const isOn = this.currentSettings.flash === 'on';\n"
"                this.currentSettings.flash = isOn ? 'off' : 'on';\n"
"                this.updateToggleButton('flash-toggle', 'flash-text', 'Flash', !isOn);\n"
"                this.updatePayloadDisplay();\n"
"            }\n"
"\n"
"            toggleHMirror() {\n"
"                this.currentSettings.hmirror = !this.currentSettings.hmirror;\n"
"                this.updateToggleButton('hmirror-toggle', 'hmirror-text', 'Horizontal Mirror', this.currentSettings.hmirror);\n"
"                this.updatePayloadDisplay();\n"
"            }\n"
"\n"
"            toggleVFlip() {\n"
"                this.currentSettings.vflip = !this.currentSettings.vflip;\n"
"                this.updateToggleButton('vflip-toggle', 'vflip-text', 'Vertical Flip', this.currentSettings.vflip);\n"
"                this.updatePayloadDisplay();\n"
"            }\n"
"\n"
"            updateToggleButton(buttonId, textId, label, isOn) {\n"
"                const button = document.getElementById(buttonId);\n"
"                button.className = `toggle-switch ${isOn ? 'on' : 'off'}`;\n"
"            }\n"
"\n"
"            resetToDefaults() {\n"
"                // Reset all settings to default values\n"
"                this.currentSettings = {\n"
"                    resolution: 'UXGA',\n"
"                    flash: 'off',\n"
"                    brightness: 0,\n"
"                    contrast: 0,\n"
"                    saturation: 0,\n"
"                    exposure: 300,\n"
"                    gain: 15,\n"
"                    special_effect: 0,\n"
"                    wb_mode: 0,\n"
"                    hmirror: false,\n"
"                    vflip: false\n"
"                };\n"
"\n"
"                // Update all UI elements\n"
"                document.getElementById('resolution-select').value = 'UXGA';\n"
"                \n"
"                // Reset sliders\n"
"                document.getElementById('brightness-slider').value = 0;\n"
"                document.getElementById('brightness-value').textContent = '0';\n"
"                document.getElementById('contrast-slider').value = 0;\n"
"                document.getElementById('contrast-value').textContent = '0';\n"
"                document.getElementById('saturation-slider').value = 0;\n"
"                document.getElementById('saturation-value').textContent = '0';\n"
"                document.getElementById('exposure-slider').value = 300;\n"
"                document.getElementById('exposure-value').textContent = '300';\n"
"                document.getElementById('gain-slider').value = 15;\n"
"                document.getElementById('gain-value').textContent = '15';\n"
"                \n"
"                // Reset select dropdowns\n"
"                document.getElementById('special-effect-select').value = '0';\n"
"                document.getElementById('wb-mode-select').value = '0';\n"
"                \n"
"                // Reset toggle switches\n"
"                this.updateToggleButton('flash-toggle', null, 'Flash', false);\n"
"                this.updateToggleButton('hmirror-toggle', null, 'Horizontal Mirror', false);\n"
"                this.updateToggleButton('vflip-toggle', null, 'Vertical Flip', false);\n"
"                \n"
"                // Update payload display\n"
"                this.updatePayloadDisplay();\n"
"                \n"
"                console.log('Settings reset to defaults');\n"
"            }\n"
"\n"
"            async takePhoto() {\n"
"                const { resolution, flash, brightness, contrast, saturation, exposure, gain, special_effect, wb_mode, hmirror, vflip } = this.currentSettings;\n"
"                \n"
"                // Use POST /snapshot with flat structure (not nested)\n"
"                const url = `${this.baseUrl}/snapshot`;\n"
"                const payload = {\n"
"                    resolution: resolution,\n"
"                    flash: flash === 'on',\n"
"                    brightness: brightness,\n"
"                    contrast: contrast,\n"
"                    saturation: saturation,\n"
"                    exposure: exposure,\n"
"                    gain: gain,\n"
"                    special_effect: special_effect,\n"
"                    wb_mode: wb_mode,\n"
"                    hmirror: hmirror,\n"
"                    vflip: vflip\n"
"                };\n"
"                \n"
"                // Update UI to show capturing state\n"
"                const captureBtn = document.getElementById('capture-btn');\n"
"                const spinnerOverlay = document.getElementById('spinner-overlay');\n"
"                const originalText = captureBtn.textContent;\n"
"                captureBtn.textContent = 'Capturing...';\n"
"                captureBtn.disabled = true;\n"
"                spinnerOverlay.style.display = 'flex';\n"
"                \n"
"                try {\n"
"                    const response = await fetch(url, {\n"
"                        method: 'POST',\n"
"                        headers: {\n"
"                            'Content-Type': 'application/json'\n"
"                        },\n"
"                        body: JSON.stringify(payload)\n"
"                    });\n"
"                    \n"
"                    if (response.ok) {\n"
"                        const blob = await response.blob();\n"
"                        const imageUrl = URL.createObjectURL(blob);\n"
"                        \n"
"                        // Display the captured image\n"
"                        const imageDisplay = document.getElementById('camera-display');\n"
"                        const placeholder = document.getElementById('camera-placeholder');\n"
"                        const overlay = document.querySelector('.photo-overlay');\n"
"                        \n"
"                        imageDisplay.src = imageUrl;\n"
"                        imageDisplay.style.display = 'block';\n"
"                        placeholder.style.display = 'none';\n"
"                        overlay.style.display = 'block';\n"
"                        \n"
"                        document.getElementById('connection-status').className = 'status-indicator status-connected';\n"
"                        document.getElementById('photo-status').textContent = 'Photo captured';\n"
"                        \n"
"                        console.log('Photo captured successfully');\n"
"                        this.updatePayloadDisplay();\n"
"                        \n"
"                        // Update connection status\n"
"                        if (!this.isConnected) {\n"
"                            this.isConnected = true;\n"
"                        }\n"
"                    } else {\n"
"                        throw new Error('Failed to capture photo');\n"
"                    }\n"
"                } catch (error) {\n"
"                    console.error('Failed to capture photo:', error);\n"
"                    const overlay = document.querySelector('.photo-overlay');\n"
"                    overlay.style.display = 'block';\n"
"                    document.getElementById('connection-status').className = 'status-indicator status-disconnected';\n"
"                    document.getElementById('photo-status').textContent = 'Capture failed';\n"
"                    this.isConnected = false;\n"
"                } finally {\n"
"                    captureBtn.textContent = originalText;\n"
"                    captureBtn.disabled = false;\n"
"                    spinnerOverlay.style.display = 'none';\n"
"                }\n"
"            }\n"
"\n"
"\n"
"            async loadWiFiInfo() {\n"
"                try {\n"
"                    const response = await fetch(`${this.baseUrl}/status`);\n"
"                    if (response.ok) {\n"
"                        const data = await response.json();\n"
"                        \n"
"                        // Update WiFi information\n"
"                        document.getElementById('wifi-ssid').textContent = data.wifi.ssid;\n"
"                        document.getElementById('wifi-ip').textContent = data.wifi.ip;\n"
"                        document.getElementById('wifi-mode').textContent = data.wifi.mode;\n"
"                        document.getElementById('wifi-gateway').textContent = data.wifi.gateway;\n"
"                        document.getElementById('wifi-mac').textContent = data.wifi.mac;\n"
"                        document.getElementById('wifi-protocol').textContent = data.wifi.protocol || 'Unknown';\n"
"                        document.getElementById('wifi-speed').textContent = data.wifi.speed || 'Unknown';\n"
"                        document.getElementById('wifi-bandwidth').textContent = data.wifi.bandwidth || 'Unknown';\n"
"                        document.getElementById('wifi-txpower').textContent = data.wifi.tx_power || 'Unknown';\n"
"                        \n"
"                        // Update signal strength with visual indicator\n"
"                        const rssi = data.wifi.rssi;\n"
"                        const signalPercentage = data.wifi.signal_percentage || 0;\n"
"                        let signalQuality = 'Poor';\n"
"                        if (rssi > -50) signalQuality = 'Excellent';\n"
"                        else if (rssi > -60) signalQuality = 'Good';\n"
"                        else if (rssi > -70) signalQuality = 'Fair';\n"
"                        document.getElementById('wifi-signal').textContent = `${rssi} dBm (${signalPercentage}% - ${signalQuality})`;\n"
"                        \n"
"                        // Update connection status\n"
"                        const statusIndicator = document.getElementById('wifi-status-indicator');\n"
"                        const statusText = document.getElementById('wifi-status-text');\n"
"                        if (data.wifi.connected) {\n"
"                            statusIndicator.className = 'status-indicator status-connected';\n"
"                            statusText.textContent = 'Connected';\n"
"                        } else {\n"
"                            statusIndicator.className = 'status-indicator status-disconnected';\n"
"                            statusText.textContent = 'Disconnected';\n"
"                        }\n"
"                    }\n"
"                } catch (error) {\n"
"                    console.error('Failed to load WiFi info:', error);\n"
"                    document.getElementById('wifi-status-text').textContent = 'Error loading WiFi info';\n"
"                }\n"
"            }\n"
"\n"
"            updatePayloadDisplay() {\n"
"                const payloadContent = document.getElementById('payload-content');\n"
"                const lastUpdated = document.getElementById('last-updated');\n"
"                \n"
"                const payload = {\n"
"                    resolution: this.currentSettings.resolution,\n"
"                    flash: this.currentSettings.flash,\n"
"                    brightness: this.currentSettings.brightness,\n"
"                    contrast: this.currentSettings.contrast,\n"
"                    saturation: this.currentSettings.saturation,\n"
"                    exposure: this.currentSettings.exposure,\n"
"                    gain: this.currentSettings.gain,\n"
"                    special_effect: this.currentSettings.special_effect,\n"
"                    wb_mode: this.currentSettings.wb_mode,\n"
"                    hmirror: this.currentSettings.hmirror,\n"
"                    vflip: this.currentSettings.vflip,\n"
"                    timestamp: new Date().toISOString(),\n"
"                    api_endpoint: `${this.baseUrl}/snapshot`,\n"
"                    method: 'POST',\n"
"                    content_type: 'application/json'\n"
"                };\n"
"                \n"
"                payloadContent.textContent = JSON.stringify(payload, null, 2);\n"
"                lastUpdated.textContent = `Last updated: ${new Date().toLocaleTimeString()}`;\n"
"            }\n"
"        }\n"
"\n"
"        // Initialize the controller when the page loads\n"
"        document.addEventListener('DOMContentLoaded', () => {\n"
"            new ESP32CameraController();\n"
"        });\n"
"    </script>\n"
"</body>\n"
"</html>";
}

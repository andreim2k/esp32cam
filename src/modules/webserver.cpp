#include "webserver.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"

// Global instance
WebServerManager webServerManager;

WebServerManager::WebServerManager()
    : server(nullptr), server_running(false), server_port(80),
      total_requests(0), error_requests(0), last_request_time(0) {}

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

void WebServerManager::handleClient(WiFiClient &client) {
  if (!client.connected()) {
    client.stop();
    return;
  }

  // Reset watchdog for each client request
  esp_task_wdt_reset();

  total_requests++;
  last_request_time = millis();

  // Allocate request on heap to avoid stack overflow
  HttpRequest *request = (HttpRequest *)malloc(sizeof(HttpRequest));
  if (!request) {
    Serial.println("Failed to allocate memory for HTTP request");
    error_requests++;
    client.stop();
    return;
  }

  if (!parseHttpRequest(client, *request)) {
    Serial.println("Failed to parse HTTP request");
    error_requests++;
    client.stop();
    free(request);
    return;
  }

  logRequest(*request);

  // Process request and generate response
  ApiResponse response = processRequest(*request);

  logResponse(response);

  // Send response
  sendResponse(client, response);

  if (response.frame_buffer) {
    cameraManager.releaseFrameBuffer(response.frame_buffer);
  } else if (response.owns_binary_data && response.binary_data) {
    free(response.binary_data);
  }

  free(request);
  client.stop();
}

bool WebServerManager::parseHttpRequest(WiFiClient &client,
                                        HttpRequest &request) {
  // Reset watchdog during HTTP parsing
  esp_task_wdt_reset();

  char current_line[HTTP_BUFFER_SIZE] = {0};
  int line_pos = 0;
  bool headers_complete = false;
  int content_length = 0;

  request.type = REQ_UNKNOWN;
  request.has_content_length = false;
  request.content_length = 0;

  // Initialize char arrays
  memset(request.path, 0, sizeof(request.path));
  memset(request.query_params, 0, sizeof(request.query_params));
  memset(request.headers, 0, sizeof(request.headers));
  memset(request.body, 0, sizeof(request.body));

  // Parse headers
  unsigned long parse_start = millis();
  const unsigned long parse_timeout = 1500; // 1.5 second timeout for header parsing

  while (client.connected() && !headers_complete) {
    if ((millis() - parse_start) > parse_timeout) {
      Serial.println("HTTP header parsing timeout");
      return false;
    }

    if (client.available()) {
      char c = client.read();

      if (c == '\n') {
        current_line[line_pos] = '\0';
        if (line_pos == 0) {
          headers_complete = true;
        } else {
          // Process header line
          if (strncmp(current_line, "GET ", 4) == 0) {
            request.type = REQ_GET;
            char *space_pos = strchr(current_line + 4, ' ');
            if (space_pos) {
              *space_pos = '\0';
              char *full_path = current_line + 4;
              char *question_mark = strchr(full_path, '?');
              if (question_mark) {
                *question_mark = '\0';
                strncpy(request.path, full_path, sizeof(request.path) - 1);
                strncpy(request.query_params, question_mark + 1,
                        sizeof(request.query_params) - 1);
              } else {
                strncpy(request.path, full_path, sizeof(request.path) - 1);
                request.query_params[0] = '\0';
              }
            }
          } else if (strncmp(current_line, "POST ", 5) == 0) {
            request.type = REQ_POST;
            char *space_pos = strchr(current_line + 5, ' ');
            if (space_pos) {
              *space_pos = '\0';
              strncpy(request.path, current_line + 5, sizeof(request.path) - 1);
            }
          } else if (strncmp(current_line, "Content-Length: ", 16) == 0) {
            content_length = atoi(current_line + 16);
            request.has_content_length = true;
            request.content_length = content_length;
          }

          // Append to headers
          strncat(request.headers, current_line,
                  sizeof(request.headers) - strlen(request.headers) - 1);
          strncat(request.headers, "\n",
                  sizeof(request.headers) - strlen(request.headers) - 1);
          line_pos = 0;
        }
      } else if (c != '\r' && line_pos < sizeof(current_line) - 1) {
        current_line[line_pos++] = c;
      }
    } else {
      delay(1); // Avoid busy-looping when no data available
    }
  }

  // Read POST body if present
  if (request.type == REQ_POST && request.has_content_length &&
      content_length > 0) {
    int bytes_read = 0;
    unsigned long start_time = millis();
    const unsigned long timeout_duration = 1500; // 1.5 second timeout

    while (bytes_read < content_length &&
           (millis() - start_time) <
               timeout_duration && // Overflow-safe comparison
           client.connected() &&
           bytes_read < sizeof(request.body) - 1) {
      if (client.available()) {
        request.body[bytes_read] = (char)client.read();
        bytes_read++;
      }
    }
    request.body[bytes_read] = '\0';
  }

  return request.type != REQ_UNKNOWN;
}

/**
 * Extract HTTP header value from headers string
 * Supports formats: "Header-Name: value" or "Header-Name:value"
 */
ApiResponse WebServerManager::processRequest(const HttpRequest &request) {
  // Route to appropriate handler - Only essential endpoints
  if (strcmp(request.path, "/") == 0) {
    return handleRoot();
  } else if (strcmp(request.path, "/status") == 0) {
    return handleStatus();
  } else if (strcmp(request.path, "/snapshot") == 0) {
    return handleSnapshot(request);
  } else if (strcmp(request.path, "/wifi") == 0) {
    return handleWiFiConfig(request);
  } else {
    return handle404();
  }
}

void WebServerManager::sendResponse(WiFiClient &client,
                                    const ApiResponse &response) {
  // Send status line
  client.printf("HTTP/1.1 %d %s\r\n", response.status_code,
                response.status_code == 200   ? "OK"
                : response.status_code == 404 ? "Not Found"
                                              : "Error");

  // Send headers
  client.printf("Content-Type: %s\r\n", response.content_type);

  if (response.is_binary && response.binary_data) {
    client.printf("Content-Length: %u\r\n", response.content_length);
  } else {
    client.printf("Content-Length: %u\r\n", strlen(response.body));
  }

  // CORS headers
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();

  // Send body
  if (response.is_binary && response.binary_data) {
    if (!response.owns_binary_data) {
      size_t sent = 0;
      while (sent < response.content_length) {
        size_t to_send = (HTML_CHUNK_SIZE < response.content_length - sent) ? HTML_CHUNK_SIZE : (response.content_length - sent);
        client.write(response.binary_data + sent, to_send);
        sent += to_send;
        esp_task_wdt_reset();
      }
    } else {
      client.write(response.binary_data, response.content_length);
    }
  } else {
    client.print(response.body);
  }
  client.flush();
}

// API Endpoints
ApiResponse WebServerManager::handleRoot() {
  ApiResponse response;
  response.status_code = 200;
  strncpy(response.content_type, "text/html",
          sizeof(response.content_type) - 1);
  response.content_type[sizeof(response.content_type) - 1] = '\0';

  static const char html_content[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-CAM Control</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#121826;color:#f7fafc}.wrap{max-width:1240px;margin:0 auto;padding:16px;display:grid;grid-template-columns:minmax(0,2fr) 360px;gap:16px}.panel{background:#1f2937;border:1px solid #374151;border-radius:8px;padding:16px}.toolbar{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.field{margin:0 0 12px}.field label{display:block;margin:0 0 6px;color:#cbd5e1;font-size:13px}input,select,button{width:100%;border:1px solid #4b5563;border-radius:7px;background:#111827;color:#f9fafb;padding:10px;font-size:14px}button{cursor:pointer;background:#2563eb;border-color:#2563eb;font-weight:700}.secondary{background:#374151;border-color:#4b5563}.danger{background:#b91c1c;border-color:#b91c1c}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.stat{background:#111827;border:1px solid #374151;border-radius:7px;padding:10px}.stat b{display:block;color:#93c5fd;font-size:12px;margin-bottom:4px}.muted{color:#9ca3af}.ok{color:#86efac}.bad{color:#fca5a5}.imageBox{position:relative;min-height:320px;display:flex;align-items:center;justify-content:center;background:#0f172a;border:1px dashed #475569;border-radius:8px;overflow:hidden}.imageBox img{max-width:100%;max-height:70vh;display:none}.watermark{position:absolute;left:12px;bottom:12px;max-width:calc(100% - 24px);padding:8px 10px;border-radius:7px;background:rgba(15,23,42,.72);color:#dbeafe;font-weight:700;font-size:13px;line-height:1.25;pointer-events:none;backdrop-filter:blur(4px);box-shadow:0 4px 14px rgba(0,0,0,.25)}.watermark.error{color:#fecaca;background:rgba(127,29,29,.82)}.statusLine{margin-top:10px;color:#cbd5e1}h1,h2{margin:0 0 14px}h1{font-size:24px}h2{font-size:18px}@media(max-width:900px){.wrap{grid-template-columns:1fr}.toolbar,.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="wrap">
  <main class="panel">
    <h1>ESP32-CAM</h1>
    <div class="toolbar">
      <div class="field"><label>Resolution</label><select id="resolution"><option>UXGA</option><option>SXGA</option><option>XGA</option><option>SVGA</option><option>VGA</option><option>QVGA</option></select></div>
      <div class="field"><label>JPEG quality</label><input id="quality" type="number" min="0" max="63" value="10"></div>
      <div class="field"><label>Flash</label><select id="flash"><option value="false">Off</option><option value="true">On</option></select></div>
      <div class="field"><label>&nbsp;</label><button id="capture">Snapshot</button></div>
    </div>
    <div class="imageBox"><span id="placeholder" class="muted">No image captured yet</span><img id="photo" alt="Captured frame"><div id="captureStatus" class="watermark">Ready</div></div>
  </main>
  <aside>
    <section class="panel">
      <h2>Network</h2>
      <div class="grid">
        <div class="stat"><b>SSID</b><span id="wifiSsid">-</span></div>
        <div class="stat"><b>IP</b><span id="wifiIp">-</span></div>
        <div class="stat"><b>Mode</b><span id="wifiMode">-</span></div>
        <div class="stat"><b>Signal</b><span id="wifiSignal">-</span></div>
        <div class="stat"><b>Gateway</b><span id="wifiGateway">-</span></div>
        <div class="stat"><b>MAC</b><span id="wifiMac">-</span></div>
      </div>
      <div class="stat" style="margin-top:10px"><b>Protocol</b><span id="wifiProtocol">-</span></div>
      <div class="stat" style="margin-top:10px"><b>Speed</b><span id="wifiSpeed">-</span></div>
      <div class="stat" style="margin-top:10px"><b>Bandwidth</b><span id="wifiBandwidth">-</span></div>
    </section>
    <section class="panel" style="margin-top:16px">
      <h2>WiFi Settings</h2>
      <div class="field"><label>SSID</label><input id="wifiInputSsid" placeholder="SSID"></div>
      <div class="field"><label>Password</label><input id="wifiInputPassword" type="password" placeholder="Leave blank to keep current"></div>
      <div class="field"><label>Speed / range mode</label><select id="wifiInputBandwidth"><option value="0">Max range - 802.11b</option><option value="1">Balanced - HT20</option><option value="2">Max speed - HT40</option></select></div>
      <button id="saveWifi">Save to EEPROM and reconnect</button>
      <button id="togglePassword" class="secondary" style="margin-top:8px">Show password</button>
      <div id="wifiResult" class="statusLine"></div>
    </section>
    <section class="panel" style="margin-top:16px">
      <h2>Camera</h2>
      <div class="grid">
        <div class="stat"><b>Ready</b><span id="camReady">-</span></div>
        <div class="stat"><b>Resolution</b><span id="camResolution">-</span></div>
        <div class="stat"><b>PSRAM</b><span id="camPsram">-</span></div>
        <div class="stat"><b>FB in PSRAM</b><span id="camFb">-</span></div>
      </div>
    </section>
  </aside>
</div>
<script>
const $=id=>document.getElementById(id);
function setText(id,value){$(id).textContent=value??'-'}
function bandwidthValue(label){if((label||'').includes('HT40'))return '2';if((label||'').includes('HT20'))return '1';return '0'}
async function refreshStatus(){
  try{
    const r=await fetch('/status');
    const d=await r.json();
    setText('wifiSsid',d.wifi.ssid); setText('wifiIp',d.wifi.ip); setText('wifiMode',d.wifi.mode);
    setText('wifiSignal',`${d.wifi.rssi} dBm (${d.wifi.signal_percentage}%)`);
    setText('wifiGateway',d.wifi.gateway); setText('wifiMac',d.wifi.mac);
    setText('wifiProtocol',d.wifi.protocol); setText('wifiSpeed',d.wifi.speed); setText('wifiBandwidth',d.wifi.bandwidth);
    setText('camReady',d.camera.ready?'yes':'no'); setText('camResolution',d.camera.resolution);
    setText('camPsram',d.camera.psram_available?'yes':'no'); setText('camFb',d.camera.frame_buffers_in_psram?'yes':'no');
    $('wifiInputSsid').placeholder=d.wifi.ssid||'SSID';
    $('wifiInputBandwidth').value=bandwidthValue(d.wifi.bandwidth);
  }catch(e){setText('wifiResult','Status load failed: '+e.message)}
}
async function capture(){
  $('captureStatus').className='watermark';
  $('captureStatus').textContent='Capturing...';
  const payload={resolution:$('resolution').value,quality:parseInt($('quality').value,10),flash:$('flash').value==='true'};
  try{
    const r=await fetch('/snapshot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    if(!r.ok)throw new Error(await r.text());
    const blob=await r.blob();
    const url=URL.createObjectURL(blob);
    $('photo').src=url; $('photo').style.display='block'; $('placeholder').style.display='none';
    $('captureStatus').className='watermark';
    $('captureStatus').textContent='Captured';
    refreshStatus();
  }catch(e){$('captureStatus').className='watermark error';$('captureStatus').textContent='Capture failed: '+e.message}
}
async function saveWifi(){
  const payload={bandwidth:parseInt($('wifiInputBandwidth').value,10)};
  const ssid=$('wifiInputSsid').value.trim();
  const pass=$('wifiInputPassword').value;
  if(ssid)payload.ssid=ssid;
  if(pass)payload.password=pass;
  $('wifiResult').textContent='Saving to EEPROM...';
  try{
    const r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    const d=await r.json();
    $('wifiResult').textContent=d.message||JSON.stringify(d);
    if(d.reconnect_requested)setTimeout(refreshStatus,5000);
  }catch(e){$('wifiResult').textContent='Save request sent; reconnect may be in progress.'}
}
$('capture').addEventListener('click',capture);
$('saveWifi').addEventListener('click',saveWifi);
$('togglePassword').addEventListener('click',()=>{$('wifiInputPassword').type=$('wifiInputPassword').type==='password'?'text':'password'});
refreshStatus();
setInterval(refreshStatus,15000);
</script>
</body>
</html>
)rawliteral";

  response.is_binary = true;
  response.binary_data = (uint8_t *)html_content;
  response.owns_binary_data = false;
  response.content_length = strlen(html_content);

  return response;
}

ApiResponse WebServerManager::handleStatus() {
  ApiResponse response;
  response.status_code = 200;
  strncpy(response.content_type, "application/json",
          sizeof(response.content_type) - 1);
  response.content_type[sizeof(response.content_type) - 1] = '\0';
  response.is_binary = false;

  JsonDocument doc;
  generateStatusJson(doc);

  serializeJson(doc, response.body, sizeof(response.body));

  return response;
}

ApiResponse WebServerManager::handleSnapshot(const HttpRequest &request) {
  // This handles the legacy POST /snapshot endpoint with JSON body
  ApiResponse response;
  response.is_binary = true;
  strncpy(response.content_type, "image/jpeg",
          sizeof(response.content_type) - 1);
  response.content_type[sizeof(response.content_type) - 1] = '\0';

  if (request.type != REQ_POST) {
    response.status_code = 405; // Method not allowed
    response.is_binary = false;
    strncpy(response.content_type, "application/json",
            sizeof(response.content_type) - 1);
    response.content_type[sizeof(response.content_type) - 1] = '\0';
    createErrorResponse("Method not allowed", 405, response.body,
                        sizeof(response.body));
    return response;
  }

  // Parse JSON body
  JsonDocument json;
  if (!parseJsonBody(request.body, json)) {
    response.status_code = 400;
    response.is_binary = false;
    strncpy(response.content_type, "application/json",
            sizeof(response.content_type) - 1);
    response.content_type[sizeof(response.content_type) - 1] = '\0';
    createErrorResponse("Invalid JSON", 400, response.body,
                        sizeof(response.body));
    return response;
  }

  // Parse camera settings and flash mode
  CameraSettings settings;
  bool use_flash;
  if (!parseRequestSettings(json, settings, use_flash)) {
    response.status_code = 400;
    response.is_binary = false;
    strncpy(response.content_type, "application/json",
            sizeof(response.content_type) - 1);
    response.content_type[sizeof(response.content_type) - 1] = '\0';
    createErrorResponse("Invalid camera settings", 400, response.body,
                        sizeof(response.body));
    return response;
  }

  // Apply settings
  framesize_t original_resolution = cameraManager.getCurrentResolution();
  if (!cameraManager.applySettings(settings)) {
    response.status_code = 500;
    response.is_binary = false;
    strncpy(response.content_type, "application/json",
            sizeof(response.content_type) - 1);
    response.content_type[sizeof(response.content_type) - 1] = '\0';
    createErrorResponse("Failed to apply camera settings", 500, response.body,
                        sizeof(response.body));
    return response;
  }

  // Discard the frame buffered before applySettings() so the captured image
  // uses the new settings/resolution and is not a pre-flash frame.
  {
    camera_fb_t *stale = cameraManager.captureFrame();
    if (stale) cameraManager.releaseFrameBuffer(stale);
  }

  // Handle flash
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_MEDIUM);
    delay(150); // Stabilization
  }

  // Capture frame
  camera_fb_t *fb = cameraManager.captureFrame();

  // Turn off flash
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_OFF);
  }

  if (fb) {
    response.status_code = 200;
    response.content_length = fb->len;
    response.binary_data = fb->buf;
    response.frame_buffer = fb;
  } else {
    response.status_code = 500;
    response.is_binary = false;
    strncpy(response.content_type, "application/json",
            sizeof(response.content_type) - 1);
    response.content_type[sizeof(response.content_type) - 1] = '\0';
    createErrorResponse("Camera capture failed", 500, response.body,
                        sizeof(response.body));
  }

  return response;
}

ApiResponse WebServerManager::handleWiFiConfig(const HttpRequest &request) {
  ApiResponse response;
  response.status_code = 200;
  strncpy(response.content_type, "application/json", sizeof(response.content_type) - 1);
  response.is_binary = false;

  if (request.type != REQ_POST) {
    response.status_code = 405;
    createErrorResponse("Method not allowed", 405, response.body, sizeof(response.body));
    return response;
  }

  JsonDocument json;
  if (!parseJsonBody(request.body, json)) {
    response.status_code = 400;
    createErrorResponse("Invalid JSON", 400, response.body, sizeof(response.body));
    return response;
  }

  bool ssid_changed = false;
  bool password_changed = false;
  bool bandwidth_changed = false;
  bool any_valid = false;

  if (json.containsKey("ssid") && json["ssid"].is<const char*>()) {
    const char *s = json["ssid"];
    if (s && strlen(s) > 0 && strlen(s) <= 63) {
      ssid_changed = strcmp(s, configManager.getWiFiSSID()) != 0;
      configManager.setWiFiCredentials(s, configManager.getWiFiPassword());
      any_valid = true;
    }
  }

  if (json.containsKey("password") && json["password"].is<const char*>()) {
    const char *p = json["password"];
    if (p && strlen(p) <= 63) {
      password_changed = strcmp(p, configManager.getWiFiPassword()) != 0;
      configManager.setWiFiCredentials(configManager.getWiFiSSID(), p);
      any_valid = true;
    }
  }

  if (json.containsKey("bandwidth") && json["bandwidth"].is<int>()) {
    uint8_t bw = json["bandwidth"];
    if (bw <= WIFI_BW_MODE_HT40) {
      bandwidth_changed = (bw != configManager.getWiFiBandwidthMode());
      configManager.setWiFiBandwidthMode(bw);
      any_valid = true;
    }
  }

  if (!any_valid) {
    response.status_code = 400;
    createErrorResponse("No valid fields", 400, response.body, sizeof(response.body));
    return response;
  }

  if (ssid_changed || password_changed || bandwidth_changed) {
    if (!configManager.saveConfig()) {
      response.status_code = 500;
      createErrorResponse("Failed to save settings to EEPROM", 500, response.body, sizeof(response.body));
      return response;
    }
    wifi_reconnect_requested = true;
  }

  JsonDocument resp;
  resp["status"] = "success";

  // Build detailed message about what was saved
  char message[256] = {0};
  if (ssid_changed || password_changed || bandwidth_changed) {
    snprintf(message, sizeof(message),
             "✓ Settings saved to EEPROM%s%s%s - Device will reconnect in 2 seconds",
             ssid_changed ? " (SSID)" : "",
             password_changed ? " (Password)" : "",
             bandwidth_changed ? " (Bandwidth)" : "");
  } else {
    snprintf(message, sizeof(message), "✓ Settings unchanged - current values are already stored in EEPROM");
  }

  resp["message"] = message;
  resp["saved_to_eeprom"] = (ssid_changed || password_changed || bandwidth_changed);
  resp["reconnect_requested"] = (ssid_changed || password_changed || bandwidth_changed);
  resp["ssid_changed"] = ssid_changed;
  serializeJson(resp, response.body, sizeof(response.body));
  return response;
}

ApiResponse WebServerManager::handle404() {
  ApiResponse response;
  response.status_code = 404;
  strncpy(response.content_type, "text/plain",
          sizeof(response.content_type) - 1);
  response.content_type[sizeof(response.content_type) - 1] = '\0';
  response.is_binary = false;
  strncpy(response.body, "404 Not Found", sizeof(response.body) - 1);
  response.body[sizeof(response.body) - 1] = '\0';

  return response;
}

// JSON utilities
void WebServerManager::createJsonResponse(const char *status,
                                          JsonDocument &data, char *output,
                                          size_t max_len) {
  if (data.isNull()) {
    JsonDocument response;
    response["status"] = status;
    serializeJson(response, output, max_len);
  } else {
    // Return the data document as is, assuming it already contains status
    serializeJson(data, output, max_len);
  }
}

void WebServerManager::createErrorResponse(const char *error, int code,
                                           char *output, size_t max_len) {
  JsonDocument response;
  response["status"] = "error";
  response["error"] = error;
  response["code"] = code;

  serializeJson(response, output, max_len);
}

bool WebServerManager::parseJsonBody(const char *body, JsonDocument &doc) {
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("JSON parsing failed: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool WebServerManager::parseRequestSettings(const JsonDocument &json,
                                            CameraSettings &settings,
                                            bool &use_flash) {
  // Set defaults
  settings.resolution = cameraManager.isReady()
                            ? cameraManager.getCurrentResolution()
                            : configManager.getDefaultResolution();
  settings.resolution = cameraManager.getSafeFrameSize(settings.resolution);
  settings.jpeg_quality = configManager.getJPEGQuality();
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
  if (json["resolution"].is<const char *>()) {
    const char *res = json["resolution"].as<const char *>();
    settings.resolution =
        cameraManager.getSafeFrameSize(cameraManager.getFrameSize(String(res)));
  }

  // Parse numeric settings
  if (json["quality"].is<int>())
    settings.jpeg_quality = constrain(json["quality"].as<int>(), 0, 63);
  if (json["brightness"].is<int>())
    settings.brightness = constrain(json["brightness"], -2, 2);
  if (json["contrast"].is<int>())
    settings.contrast = constrain(json["contrast"], -2, 2);
  if (json["saturation"].is<int>())
    settings.saturation = constrain(json["saturation"], -2, 2);
  if (json["exposure"].is<int>())
    settings.exposure = constrain(json["exposure"], 0, 1200);
  if (json["gain"].is<int>())
    settings.gain = constrain(json["gain"], 0, 30);
  if (json["special_effect"].is<int>())
    settings.special_effect = constrain(json["special_effect"], 0, 6);
  if (json["wb_mode"].is<int>())
    settings.wb_mode = constrain(json["wb_mode"], 0, 4);

  // Parse boolean settings
  if (json["hmirror"].is<bool>())
    settings.hmirror = json["hmirror"].as<bool>();
  if (json["vflip"].is<bool>())
    settings.vflip = json["vflip"].as<bool>();
  if (json["flash"].is<bool>())
    use_flash = json["flash"].as<bool>();

  return true;
}

// Helper methods
void WebServerManager::logRequest(const HttpRequest &request) {
  Serial.printf("HTTP %s %s", request.type == REQ_GET ? "GET" : "POST",
                request.path);
  if (strlen(request.query_params) > 0) {
    Serial.printf("?%s", request.query_params);
  }
  Serial.println();
}

void WebServerManager::logResponse(const ApiResponse &response) {
  Serial.printf("Response: %d %s (%s)\n", response.status_code,
                response.is_binary ? "Binary" : "Text", response.content_type);
}

void WebServerManager::extractQueryParam(const char *query_params,
                                         const char *param_name, char *output,
                                         size_t max_len) {
  output[0] = '\0';

  if (strlen(query_params) == 0) {
    return;
  }

  char search_str[128];
  snprintf(search_str, sizeof(search_str), "%s=", param_name);

  char *start = strstr(query_params, search_str);
  if (!start) {
    return;
  }

  start += strlen(search_str); // Move past "param="
  char *end = strchr(start, '&');

  size_t len;
  if (end) {
    len = end - start;
  } else {
    len = strlen(start);
  }

  if (len >= max_len) {
    len = max_len - 1;
  }

  strncpy(output, start, len);
  output[len] = '\0';

  urlDecode(output, output, max_len);
}

void WebServerManager::urlDecode(const char *str, char *output,
                                 size_t max_len) {
  size_t input_len = strlen(str);
  size_t output_pos = 0;

  for (size_t i = 0; i < input_len && output_pos < max_len - 1; i++) {
    char c = str[i];
    if (c == '+') {
      output[output_pos++] = ' ';
    } else if (c == '%' && i + 2 < input_len) {
      // URL decode hex characters with validation
      char hex[3] = {str[i + 1], str[i + 2], '\0'};
      char *endptr;
      long decoded_val = strtol(hex, &endptr, 16);

      // Validate hex input
      if (endptr == hex || *endptr != '\0' || decoded_val < 0 ||
          decoded_val > 255) {
        // Invalid hex, skip this character
        output[output_pos++] = c;
        continue;
      }

      char decoded_char = (char)decoded_val;
      output[output_pos++] = decoded_char;
      i += 2;
    } else {
      output[output_pos++] = c;
    }
  }
  output[output_pos] = '\0';
}

void WebServerManager::generateDeviceInfo(JsonDocument &doc) {
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

void WebServerManager::generateStatusJson(JsonDocument &doc) {
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
  // Show the actual connected SSID, not the configured one
  wifi["ssid"] = WiFi.SSID().c_str();
  wifi["mode"] = configManager.useStaticIP() ? "Static" : "DHCP";
  wifi["rssi"] = WiFi.RSSI();
  wifi["signal_percentage"] = getWiFiSignalPercentage();
  wifi["tx_power"] = "19.5 dBm (MAXIMUM - ESP32 Regulatory Limit)";
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  char protocol[128], speed[128], bandwidth[128];
  getWiFiProtocol(protocol, sizeof(protocol));
  getWiFiConnectionSpeed(speed, sizeof(speed));
  getWiFiBandwidth(bandwidth, sizeof(bandwidth));

  wifi["protocol"] = protocol;
  wifi["speed"] = speed;
  wifi["bandwidth"] = bandwidth;

  // Camera status
  JsonObject camera = doc["camera"].to<JsonObject>();
  char resolution_str[32];
  cameraManager.getResolutionString(cameraManager.getCurrentResolution(),
                                    resolution_str, sizeof(resolution_str));
  camera["resolution"] = resolution_str;
  camera["ready"] = cameraManager.isReady();
  camera["psram_available"] = cameraManager.isPSRAMAvailable();
  camera["frame_buffers_in_psram"] = cameraManager.usesPSRAMFrameBuffers();
  camera["total_captures"] = cameraManager.getTotalCaptureCount();
  camera["failed_captures"] = cameraManager.getFailedCaptureCount();
}

void WebServerManager::getWiFiProtocol(char *output, size_t max_len) {
  if (WiFi.status() != WL_CONNECTED) {
    strncpy(output, "disconnected", max_len - 1);
    output[max_len - 1] = '\0';
    return;
  }

  // Report protocol based on actual bandwidth mode setting
  uint8_t bwMode = configManager.getWiFiBandwidthMode();
  if (bwMode == WIFI_BW_MODE_11B) {
    strncpy(output, "802.11b (2.4GHz) - MAXIMUM RANGE MODE", max_len - 1);
  } else {
    // HT20 and HT40 use 802.11bgn
    strncpy(output, "802.11bgn (2.4GHz) - Mixed Mode", max_len - 1);
  }
  output[max_len - 1] = '\0';
}

void WebServerManager::getWiFiBandwidth(char *output, size_t max_len) {
  uint8_t bwMode = configManager.getWiFiBandwidthMode();

  switch (bwMode) {
    case WIFI_BW_MODE_HT20:
      strncpy(output, "⚖️ Balanced Speed - HT20 (20MHz)", max_len - 1);
      break;
    case WIFI_BW_MODE_HT40:
      strncpy(output, "⚡ Max Speed - HT40 (40MHz)", max_len - 1);
      break;
    default: // WIFI_BW_MODE_11B
      strncpy(output, "📡 Max Range - 802.11b (22MHz)", max_len - 1);
      break;
  }

  output[max_len - 1] = '\0';
}

void WebServerManager::getWiFiConnectionSpeed(char *output, size_t max_len) {
  if (WiFi.status() != WL_CONNECTED) {
    strncpy(output, "disconnected", max_len - 1);
    output[max_len - 1] = '\0';
    return;
  }

  int rssi = WiFi.RSSI();
  uint8_t bwMode = configManager.getWiFiBandwidthMode();

  // Speed varies by bandwidth mode and signal strength
  if (bwMode == WIFI_BW_MODE_HT40) {
    // HT40 achieves higher speeds (150 Mbps max)
    if (rssi > -50) {
      snprintf(output, max_len, "~80-150 Mbps (802.11n HT40) - Excellent");
    } else if (rssi > -60) {
      snprintf(output, max_len, "~40-80 Mbps (802.11n HT40) - Good");
    } else if (rssi > -70) {
      snprintf(output, max_len, "~20-40 Mbps (802.11n HT40) - Fair");
    } else {
      snprintf(output, max_len, "~5-20 Mbps (802.11n HT40) - Weak");
    }
  } else if (bwMode == WIFI_BW_MODE_HT20) {
    // HT20 achieves medium speeds (72 Mbps max)
    if (rssi > -50) {
      snprintf(output, max_len, "~36-72 Mbps (802.11n HT20) - Excellent");
    } else if (rssi > -60) {
      snprintf(output, max_len, "~18-36 Mbps (802.11n HT20) - Good");
    } else if (rssi > -70) {
      snprintf(output, max_len, "~9-18 Mbps (802.11n HT20) - Fair");
    } else {
      snprintf(output, max_len, "~2-9 Mbps (802.11n HT20) - Weak");
    }
  } else {
    // 802.11b mode for maximum range (11 Mbps max)
    if (rssi > -50) {
      snprintf(output, max_len, "11 Mbps (802.11b CCK) - Maximum Range");
    } else if (rssi > -60) {
      snprintf(output, max_len, "5.5 Mbps (802.11b CCK) - Long Range");
    } else if (rssi > -70) {
      snprintf(output, max_len, "2 Mbps (802.11b DQPSK) - Extended Range");
    } else {
      snprintf(output, max_len, "1 Mbps (802.11b DBPSK) - Maximum Distance");
    }
  }
  output[max_len - 1] = '\0';
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
    return 0; // Very poor signal
  } else {
    // Linear interpolation between -30dBm (100%) and -80dBm (0%)
    // Formula: percentage = 2 * (rssi + 80)
    // This gives us: -30dBm = 100%, -40dBm = 80%, -50dBm = 60%, -60dBm = 40%,
    // -70dBm = 20%, -80dBm = 0%
    return 2 * (rssi + 80);
  }
}

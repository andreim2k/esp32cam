#include "webserver.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "../version.h"

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
  } else if (strcmp(request.path, "/reset") == 0) {
    return handleReset(request);
  } else {
    return handle404();
  }
}

void WebServerManager::sendResponse(WiFiClient &client,
                                    const ApiResponse &response) {
  // Send status line
  const char *reason = "Error";
  if (response.status_code == 200) reason = "OK";
  else if (response.status_code == 400) reason = "Bad Request";
  else if (response.status_code == 404) reason = "Not Found";
  else if (response.status_code == 405) reason = "Method Not Allowed";
  else if (response.status_code == 500) reason = "Internal Server Error";

  client.printf("HTTP/1.1 %d %s\r\n", response.status_code, reason);

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

  // Send body (chunked for all binary data to keep watchdog alive)
  if (response.is_binary && response.binary_data) {
    size_t sent = 0;
    while (sent < response.content_length) {
      size_t to_send = (HTML_CHUNK_SIZE < response.content_length - sent) ? HTML_CHUNK_SIZE : (response.content_length - sent);
      client.write(response.binary_data + sent, to_send);
      sent += to_send;
      esp_task_wdt_reset();
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
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>ESP32-CAM Control</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#121826;color:#f7fafc;line-height:1.4}
.wrap{max-width:1240px;margin:0 auto;padding:16px;display:grid;grid-template-columns:minmax(0,2fr) 360px;gap:16px}
main{display:grid;gap:16px}
.panel{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:16px;height:fit-content}
.captureHead,.panelHead{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:16px}
.field{margin:0 0 12px}
.field label,.rangeLabel{display:block;margin:0 0 6px;color:#cbd5e1;font-size:12px;font-weight:600}
.rangeLabel{display:flex;align-items:center;justify-content:space-between;gap:6px}
.value{min-width:38px;text-align:right;color:#f8fafc;background:#0f172a;border:1px solid #334155;border-radius:5px;padding:2px 6px;font-size:11px;font-weight:700;font-variant-numeric:tabular-nums}
input,select,button{width:100%;border:1px solid #4b5563;border-radius:8px;background:#111827;color:#f9fafb;padding:10px;font-size:14px;-webkit-appearance:none}
select{background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23f9fafb' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M6 9l6 6 6-6'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 10px center;background-size:14px;padding-right:32px}
input[type=range]{height:28px;padding:0;accent-color:#3b82f6;cursor:pointer}
button{cursor:pointer;background:#2563eb;border-color:#2563eb;font-weight:700;transition:all 0.2s;display:flex;align-items:center;justify-content:center;min-height:42px}
button:hover{opacity:0.9;transform:translateY(-1px)}
button:active{transform:translateY(0)}
button:disabled{opacity:0.5;cursor:not-allowed;transform:none}
.secondary{background:#374151;border-color:#4b5563}
.smallBtn{width:auto;padding:6px 14px;font-size:12px;min-height:34px}
.grid{display:grid;grid-template-columns:repeat(auto-fit, minmax(140px, 1fr));gap:10px}
.settingsStack{display:grid;gap:16px}
.settingsBlock{border-top:1px solid #334155;padding-top:16px}
.settingsBlock:first-child{border-top:0;padding-top:0}
.sectionTitle{margin:0 0 10px;color:#93c5fd;font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:1px}
.imageBox{position:relative;background:#0f172a;border-radius:8px;min-height:200px;display:flex;align-items:center;justify-content:center;overflow:hidden;border:1px solid #334155;aspect-ratio:4/3}
#photo{width:100%;height:100%;object-fit:contain;display:none}
.watermark{position:absolute;bottom:10px;right:10px;background:rgba(15,23,42,0.85);backdrop-filter:blur(4px);padding:4px 10px;border-radius:5px;font-size:10px;font-weight:700;color:#f8fafc;border:1px solid #334155;z-index:10;pointer-events:none}
.error{color:#f87171;border-color:#ef4444}
.muted{color:#94a3b8;font-size:13px}
.stat{display:flex;flex-direction:column;gap:1px;padding:8px;background:#111827;border-radius:8px;border:1px solid #334155}
.stat b{font-size:9px;color:#94a3b8;text-transform:uppercase;letter-spacing:0.5px}
.stat span{font-size:12px;font-weight:600;word-break:break-all}
.statusLine{margin-top:12px;font-size:12px;padding:10px;border-radius:8px;background:#1e1b4b;color:#c7d2fe;display:none;border:1px solid #3730a3}
.check{display:flex;align-items:center;gap:10px;cursor:pointer;padding:6px 0;user-select:none;font-size:13px}
.check input{width:18px;height:18px;margin:0;border-radius:5px}
.payloadBox{margin:0;max-height:160px;overflow:auto;white-space:pre-wrap;background:#0f172a;border:1px solid #334155;border-radius:8px;color:#bfdbfe;padding:10px;font:11px/1.4 ui-monospace,monospace}
.copyBtn{position:absolute;top:6px;right:6px;width:auto;padding:2px 8px;font-size:10px;background:#334155;border-color:#475569;min-height:24px}
footer{margin-top:24px;padding:16px;text-align:center;border-top:1px solid #334155;color:#64748b;font-size:11px}

@media (max-width: 960px) {
  .wrap { grid-template-columns: 1fr; }
  aside { order: 2; }
  main { order: 1; }
}

@media (max-width: 640px) {
  .wrap { padding: 12px; }
  .panel { padding: 12px; }
  .settingsGrid { grid-template-columns: 1fr 1fr !important; gap: 10px !important; }
  .grid { grid-template-columns: 1fr 1fr !important; gap: 8px !important; }
  .captureHead { flex-direction: column; align-items: flex-start; gap: 8px; }
  .captureHead h1 { font-size: 18px !important; }
  .captureHead button { width: 100%; }
  .stat span { font-size: 11px; }
}
</style>
</head>
<body>
<div class="wrap">
  <main>
    <section class="panel">
      <div class="captureHead">
        <div>
          <h1 style="margin:0;font-size:22px">Screen Capture</h1>
          <div class="muted">Live ESP32-CAM output</div>
        </div>
        <button id="capture">Take Snapshot</button>
      </div>
      <div class="imageBox">
        <span id="placeholder" class="muted">No image captured</span>
        <img id="photo" alt="Captured frame">
        <div id="captureStatus" class="watermark">Ready</div>
      </div>
    </section>
    <section class="panel">
      <div class="panelHead">
        <h2 style="margin:0;font-size:18px">Camera Settings</h2>
        <button id="resetCamera" class="secondary smallBtn">Defaults</button>
      </div>
      <div class="settingsStack">
        <div class="settingsBlock">
          <div class="sectionTitle">Capture Configuration</div>
          <div class="settingsGrid" style="display:grid;grid-template-columns:repeat(3, 1fr);gap:12px">
            <div class="field">
              <label>Resolution</label>
              <select id="resolution">
                <option>UXGA (1600x1200)</option>
                <option>SXGA (1280x1024)</option>
                <option>XGA (1024x768)</option>
                <option>SVGA (800x600)</option>
                <option>VGA (640x480)</option>
                <option>CIF (400x296)</option>
                <option>QVGA (320x240)</option>
                <option>HQVGA (240x176)</option>
              </select>
            </div>
            <div class="field"><label class="rangeLabel"><span>Quality</span><span class="value" id="qualityVal">10</span></label><input id="quality" type="range" min="10" max="63" value="10"></div>
            <div class="field"><label>Flash</label><select id="flash"><option value="false">Off</option><option value="true">On</option></select></div>
          </div>
        </div>
        <div class="settingsBlock">
          <div class="sectionTitle">Image Tuning</div>
          <div class="settingsGrid" style="display:grid;grid-template-columns:repeat(auto-fit, minmax(160px, 1fr));gap:12px">
            <div class="field"><label class="rangeLabel"><span>Brightness</span><span class="value" id="brightnessVal">0</span></label><input id="brightness" type="range" min="-2" max="2" value="0"></div>
            <div class="field"><label class="rangeLabel"><span>Contrast</span><span class="value" id="contrastVal">0</span></label><input id="contrast" type="range" min="-2" max="2" value="0"></div>
            <div class="field"><label class="rangeLabel"><span>Saturation</span><span class="value" id="saturationVal">0</span></label><input id="saturation" type="range" min="-2" max="2" value="0"></div>
            <div class="field"><label class="rangeLabel"><span>Exposure</span><span class="value" id="exposureVal">300</span></label><input id="exposure" type="range" min="0" max="1200" step="10" value="300"></div>
            <div class="field"><label class="rangeLabel"><span>Gain</span><span class="value" id="gainVal">0</span></label><input id="gain" type="range" min="0" max="30" value="0"></div>
          </div>
        </div>
        <div class="settingsBlock">
          <div class="sectionTitle">Advanced Processing</div>
          <div class="settingsGrid" style="display:grid;grid-template-columns:repeat(auto-fit, minmax(180px, 1fr));gap:16px;align-items:start">
            <div class="field" style="margin:0"><label>White Balance</label><select id="wbMode"><option value="0">Auto</option><option value="1">Sunny</option><option value="2">Cloudy</option><option value="3">Office</option><option value="4">Home</option></select></div>
            <div class="field" style="margin:0"><label>Special Effect</label><select id="specialEffect"><option value="0">None</option><option value="1">Negative</option><option value="2">Grayscale</option><option value="3">Red tint</option><option value="4">Green tint</option><option value="5">Blue tint</option><option value="6">Sepia</option></select></div>
            <div style="display:flex;flex-direction:column;gap:4px">
              <label class="check"><input id="hmirror" type="checkbox"><span>Horizontal Mirror</span></label>
              <label class="check"><input id="vflip" type="checkbox"><span>Vertical Flip</span></label>
            </div>
          </div>
        </div>
        <div class="settingsBlock">
          <div class="sectionTitle">API Payload Preview</div>
          <div style="position:relative">
            <pre id="payloadText" class="payloadBox"></pre>
            <button id="copyPayload" class="secondary copyBtn">Copy</button>
          </div>
        </div>
      </div>
    </section>
  </main>
  <aside>
    <section class="panel">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:14px">
        <h2 style="margin:0;font-size:16px">Network Status</h2>
        <button id="refreshStatus" class="secondary smallBtn" style="min-width:60px">Refresh</button>
      </div>
      <div class="grid">
        <div class="stat"><b>SSID</b><span id="wifiSsid">-</span></div>
        <div class="stat"><b>IP</b><span id="wifiIp">-</span></div>
        <div class="stat"><b>Signal</b><span id="wifiSignal">-</span></div>
        <div class="stat"><b>Mode</b><span id="wifiMode">-</span></div>
      </div>
      <div class="stat" style="margin-top:10px"><b>MAC</b><span id="wifiMac">-</span></div>
      <div class="stat" style="margin-top:10px"><b>Link Details</b><span id="wifiProtocol">-</span></div>
      <div class="stat" style="margin-top:10px"><b>Bandwidth Mode</b><span id="wifiBandwidth">-</span></div>
    </section>
    <section class="panel" style="margin-top:16px">
      <h2 style="margin:0 0 14px;font-size:16px">WiFi Configuration</h2>
      <div class="field"><label>SSID</label><input id="wifiInputSsid" placeholder="New SSID"></div>
      <div class="field"><label>Password</label><input id="wifiInputPassword" type="password" placeholder="New Password"></div>
      <div class="field"><label>Performance</label><select id="wifiInputBandwidth"><option value="0">Max Range (802.11b)</option><option value="1">Balanced (HT20)</option><option value="2">Max Speed (HT40)</option></select></div>
      <button id="saveWifi">Update & Reconnect</button>
      <button id="togglePassword" class="secondary" style="margin-top:8px">Show Password</button>
      <button id="resetDevice" class="secondary" style="margin-top:8px;background:#7f1d1d;border-color:#991b1b">Reset Device</button>
      <div id="wifiResult" class="statusLine"></div>
    </section>
    <section class="panel" style="margin-top:16px">
      <h2 style="margin:0 0 14px;font-size:16px">Hardware Information</h2>
      <div class="grid">
        <div class="stat"><b>Sensor</b><span id="camReady">-</span></div>
        <div class="stat"><b>Resolution</b><span id="camResolution">-</span></div>
        <div class="stat"><b>PSRAM</b><span id="camPsram">-</span></div>
        <div class="stat"><b>Buffer</b><span id="camFb">-</span></div>
      </div>
      <div class="stat" style="margin-top:10px"><b>Activity</b><span id="camCaptures">-</span></div>
    </section>
  </aside>
</div>
<footer>
  Build: <span id="buildNum">-</span> &bull; <span id="buildTime">-</span>
</footer>
<script>
const $=id=>document.getElementById(id);
function setText(id,value){const el=$(id);if(el)el.textContent=value??'-'}
function bandwidthValue(label){if((label||'').includes('HT40'))return '2';if((label||'').includes('HT20'))return '1';return '0'}
const cameraDefaults={resolution:'UXGA (1600x1200)',flash:'false',wbMode:'0',quality:10,brightness:0,contrast:0,saturation:0,exposure:300,gain:0,specialEffect:'0',hmirror:false,vflip:false};
const rangeFields=['quality','brightness','contrast','saturation','exposure','gain'];
function syncRangeLabel(id){const el=$(id+'Val');if(el)el.textContent=$(id).value}
function applyCameraDefaults(){
  $('resolution').value=cameraDefaults.resolution;
  $('flash').value=cameraDefaults.flash;
  $('wbMode').value=cameraDefaults.wbMode;
  $('specialEffect').value=cameraDefaults.specialEffect;
  rangeFields.forEach(id=>{$(id).value=cameraDefaults[id];syncRangeLabel(id)});
  $('hmirror').checked=cameraDefaults.hmirror;
  $('vflip').checked=cameraDefaults.vflip;
  updatePayloadPreview();
}
function cameraPayload(){
  return {
    resolution:$('resolution').value,
    quality:parseInt($('quality').value,10),
    flash:$('flash').value==='true',
    brightness:parseInt($('brightness').value,10),
    contrast:parseInt($('contrast').value,10),
    saturation:parseInt($('saturation').value,10),
    exposure:parseInt($('exposure').value,10),
    gain:parseInt($('gain').value,10),
    special_effect:parseInt($('specialEffect').value,10),
    wb_mode:parseInt($('wbMode').value,10),
    hmirror:$('hmirror').checked,
    vflip:$('vflip').checked
  };
}
function updatePayloadPreview(){$('payloadText').textContent=JSON.stringify(cameraPayload(),null,2)}
let firstLoad=true;
async function refreshStatus(){
  try{
    const r=await fetch('/status');
    const d=await r.json();
    setText('wifiSsid',d.wifi.ssid);
    setText('wifiIp',d.wifi.ip);
    setText('wifiMode',d.wifi.mode);
    setText('wifiSignal',`${d.wifi.rssi} dBm (${d.wifi.signal_percentage}%)`);
    setText('wifiMac',d.wifi.mac);
    setText('wifiProtocol',d.wifi.protocol);
    setText('wifiBandwidth',d.wifi.bandwidth);
    setText('camReady',d.camera.ready?'Active':'Offline');
    setText('camResolution',d.camera.resolution);
    setText('camPsram',d.camera.psram_available?'Detected':'No');
    setText('camFb',d.camera.frame_buffers_in_psram?'PSRAM':'Internal');
    setText('camCaptures',`${d.camera.total_captures} captured / ${d.camera.failed_captures} failed`);
    setText('buildNum',d.build.number);
    setText('buildTime',d.build.timestamp);
    $('wifiInputSsid').placeholder=d.wifi.ssid||'New SSID';
    $('wifiInputBandwidth').value=bandwidthValue(d.wifi.bandwidth);
    if(firstLoad&&d.camera.ready){
      if(d.camera.resolution)$('resolution').value=d.camera.resolution;
      if(d.camera.quality!==undefined)$('quality').value=d.camera.quality;
      if(d.camera.brightness!==undefined)$('brightness').value=d.camera.brightness;
      if(d.camera.contrast!==undefined)$('contrast').value=d.camera.contrast;
      if(d.camera.saturation!==undefined)$('saturation').value=d.camera.saturation;
      if(d.camera.exposure!==undefined)$('exposure').value=d.camera.exposure;
      if(d.camera.gain!==undefined)$('gain').value=d.camera.gain;
      if(d.camera.special_effect!==undefined)$('specialEffect').value=d.camera.special_effect.toString();
      if(d.camera.wb_mode!==undefined)$('wbMode').value=d.camera.wb_mode.toString();
      if(d.camera.hmirror!==undefined)$('hmirror').checked=d.camera.hmirror;
      if(d.camera.vflip!==undefined)$('vflip').checked=d.camera.vflip;
      rangeFields.forEach(syncRangeLabel);
      updatePayloadPreview();
      firstLoad=false;
    }
  }catch(e){console.error(e)}
}
async function capture(){
  const btn=$('capture');
  btn.disabled=true;
  $('captureStatus').textContent='Capturing...';
  const payload=cameraPayload();
  updatePayloadPreview();
  try{
    const r=await fetch('/snapshot',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    if(!r.ok)throw new Error('Capture failed');
    const blob=await r.blob();
    const url=URL.createObjectURL(blob);
    $('photo').src=url;
    $('photo').style.display='block';
    $('placeholder').style.display='none';
    $('captureStatus').textContent='Captured';
    setTimeout(refreshStatus,100);
  }catch(e){
    $('captureStatus').textContent='Error';
    $('captureStatus').classList.add('error');
  }finally{
    btn.disabled=false;
  }
}
async function resetDevice(){
  const res=$('wifiResult');
  res.style.display='block';
  res.textContent='Restarting...';
  try{
    await fetch('/reset',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
  }catch(e){}
  let attempts=0;
  const poll=setInterval(async()=>{
    attempts++;
    if(attempts>20){clearInterval(poll);res.textContent='Device did not come back online';return;}
    try{const r=await fetch('/status');if(r.ok){clearInterval(poll);window.location.reload();}}catch(e){}
  },2000);
}
async function saveWifi(){
  const payload={bandwidth:parseInt($('wifiInputBandwidth').value,10)};
  const ssid=$('wifiInputSsid').value.trim();
  const pass=$('wifiInputPassword').value;
  if(ssid)payload.ssid=ssid;
  if(pass)payload.password=pass;
  const res=$('wifiResult');
  res.style.display='block';
  res.textContent='Saving...';
  try{
    const r=await fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
    const d=await r.json();
    res.textContent=d.message||'Success';
    if(d.reconnect_requested)setTimeout(()=>window.location.reload(),5000);
  }catch(e){res.textContent='Sent. Reconnecting...'}
}
$('capture').addEventListener('click',capture);
$('saveWifi').addEventListener('click',saveWifi);
$('resetDevice').addEventListener('click',resetDevice);
$('refreshStatus').addEventListener('click',refreshStatus);
$('resetCamera').addEventListener('click',applyCameraDefaults);
$('copyPayload').addEventListener('click',()=>{
  navigator.clipboard.writeText($('payloadText').textContent);
  const oldText=$('copyPayload').textContent;
  $('copyPayload').textContent='Copied';
  setTimeout(()=>$('copyPayload').textContent=oldText,2000);
});
rangeFields.forEach(id=>$(id).addEventListener('input',()=>{syncRangeLabel(id);updatePayloadPreview()}));
['resolution','flash','wbMode','specialEffect','hmirror','vflip'].forEach(id=>$(id).addEventListener('change',updatePayloadPreview));
$('togglePassword').addEventListener('click',()=>{
  const pw=$('wifiInputPassword');
  pw.type=pw.type==='password'?'text':'password';
});
rangeFields.forEach(syncRangeLabel);
updatePayloadPreview();
refreshStatus();
setInterval(refreshStatus,30000);
</script>
</body>
</html>
)rawliteral";
response.is_binary = true;
  response.binary_data = (uint8_t *)html_content;
  response.owns_binary_data = false;
  response.content_length = sizeof(html_content) - 1;

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

  // Store original resolution to restore after capture
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

  // Discard 2 frames buffered during applySettings() to ensure clean output.
  for (int i = 0; i < 2; i++) {
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

  // Restore original resolution if it was changed
  if (original_resolution != cameraManager.getCurrentResolution()) {
    cameraManager.setResolution(original_resolution);
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

  if (json["ssid"].is<const char*>()) {
    const char *s = json["ssid"];
    if (s && strlen(s) > 0 && strlen(s) <= 63) {
      ssid_changed = strcmp(s, configManager.getWiFiSSID()) != 0;
      configManager.setWiFiCredentials(s, configManager.getWiFiPassword());
      any_valid = true;
    }
  }

  if (json["password"].is<const char*>()) {
    const char *p = json["password"];
    if (p && strlen(p) <= 63) {
      password_changed = strcmp(p, configManager.getWiFiPassword()) != 0;
      configManager.setWiFiCredentials(configManager.getWiFiSSID(), p);
      any_valid = true;
    }
  }

  if (json["bandwidth"].is<int>()) {
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

ApiResponse WebServerManager::handleReset(const HttpRequest &request) {
  ApiResponse response;
  response.status_code = 200;
  strncpy(response.content_type, "application/json",
          sizeof(response.content_type) - 1);
  response.content_type[sizeof(response.content_type) - 1] = '\0';
  response.is_binary = false;

  if (request.type != REQ_POST) {
    response.status_code = 405;
    createErrorResponse("Method not allowed", 405, response.body,
                        sizeof(response.body));
    return response;
  }

  JsonDocument resp;
  resp["status"] = "success";
  resp["message"] = "Device is restarting...";
  serializeJson(resp, response.body, sizeof(response.body));

  // Trigger ESP32 restart after sending response
  ESP.restart();
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
    settings.jpeg_quality = constrain(json["quality"].as<int>(), 10, 63);
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
  endpoints["reset"] = "POST /reset - Reboot the ESP32 device";
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
  flash["threshold"] = configManager.getFlashThreshold();

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

  // Current settings
  CameraSettings settings = cameraManager.getCurrentSettings();
  camera["quality"] = settings.jpeg_quality;
  camera["brightness"] = settings.brightness;
  camera["contrast"] = settings.contrast;
  camera["saturation"] = settings.saturation;
  camera["exposure"] = settings.exposure;
  camera["gain"] = settings.gain;
  camera["special_effect"] = settings.special_effect;
  camera["wb_mode"] = settings.wb_mode;
  camera["hmirror"] = settings.hmirror;
  camera["vflip"] = settings.vflip;

  // Build info
  JsonObject build = doc["build"].to<JsonObject>();
  build["number"] = BUILD_NUMBER;
  build["timestamp"] = BUILD_TIMESTAMP;
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

#include "esp_camera.h"
#include <WiFi.h>

// ===================
// WiFi Configuration
// ===================
const char* ssid = "MNZ";
const char* password = "debianhusk1";

// ===================
// Camera Configuration (AI Thinker ESP32-CAM)
// ===================
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

WiFiServer server(80);

void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
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
  // Drop down frame size for higher initial frame rate
  if(config.pixel_format == PIXFORMAT_JPEG){
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  WiFiClient client = server.available();
  
  if (client) {
    String currentLine = "";
    String request = "";
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // HTTP request ended, process it
            if (request.indexOf("GET / ") >= 0) {
              // Serve main page
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();
              client.print(INDEX_HTML);
            }
            else if (request.indexOf("GET /capture") >= 0) {
              // Serve single image
              camera_fb_t * fb = esp_camera_fb_get();
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
            else if (request.indexOf("GET /stream") >= 0) {
              // Start video stream
              client.println("HTTP/1.1 200 OK");
              client.printf("Content-Type: %s\r\n", _STREAM_CONTENT_TYPE);
              client.println("Connection: close");
              client.println();
              
              while (client.connected()) {
                camera_fb_t * fb = esp_camera_fb_get();
                if (!fb) {
                  Serial.println("Camera capture failed");
                  break;
                }
                
                client.print(_STREAM_BOUNDARY);
                client.printf(_STREAM_PART, fb->len);
                client.write(fb->buf, fb->len);
                esp_camera_fb_return(fb);
                
                if (!client.connected()) {
                  break;
                }
                delay(30); // ~30 FPS
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
  delay(1);
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>ESP32-CAM</title>
        <style>
            body{font-family:Arial,Helvetica,sans-serif;background:#181818;color:#EFEFEF;font-size:16px}
            h2{font-size:18px}
            section.main{display:flex}
            #menu,section.main{flex-direction:column}
            #menu{background:#E6E6FA;color:#000;width:340px;min-height:100vh;padding:8px}
            #content{display:flex;flex-wrap:wrap;align-items:stretch}
            figure{padding:0px;margin:0;-webkit-margin-before:0;margin-block-start:0;-webkit-margin-after:0;margin-block-end:0;-webkit-margin-start:0;margin-inline-start:0;-webkit-margin-end:0;margin-inline-end:0}
            figure img{display:block;width:100%;height:auto;border-radius:4px;margin-top:8px}
            @media (min-width: 800px) {
                #content{display:flex;flex-wrap:nowrap;align-items:stretch}
                figure img{display:block;max-width:100%;max-height:calc(100vh - 40px);width:auto;height:auto}
                figure{padding:0px;margin:0;display:flex;align-items:center;justify-content:center}
            }
            section#buttons{display:flex;flex-wrap:nowrap;justify-content:space-between}
            #nav-toggle{cursor:pointer;display:block}
            .input-group{display:flex;flex-wrap:nowrap;line-height:22px;margin:5px 0}
            .input-group>label{display:inline-block;padding-right:10px;min-width:47%}
            .input-group input,.input-group select{flex-grow:1}
            .range-max,.range-min{display:inline-block}
            .range-min{padding-right:10px}
            button{display:block;margin:5px;padding:0 12px;border:0;line-height:28px;cursor:pointer;color:#fff;background:#ff3034;border-radius:5px;font-size:16px;outline:0}
            button:hover{background:#ff494d}
            button:active{background:#f21c21}
            button.disabled{cursor:default;background:#a0a0a0}
            input[type=range]{-webkit-appearance:none;width:100%;height:22px;outline:0;background:0 0}
            input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:22px;height:22px;border-radius:50%;background:#ff3034;cursor:pointer}
            input[type=range]::-moz-range-thumb{width:22px;height:22px;border-radius:50%;background:#ff3034;cursor:pointer}
            input[type=checkbox]{width:auto;display:inline-block;position:relative;right:6px}
            select{padding:4px}
            .switch{display:inline-block;position:relative;width:60px;height:34px}
            .switch input{opacity:0;width:0;height:0}
            .slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;-webkit-transition:.4s;transition:.4s}
            .slider:before{position:absolute;content:"";height:26px;width:26px;left:4px;bottom:4px;background-color:white;-webkit-transition:.4s;transition:.4s}
            input:checked + .slider{background-color:#ff3034}
            input:focus + .slider{box-shadow:0 0 1px #ff3034}
            input:checked + .slider:before{-webkit-transform:translateX(26px);-ms-transform:translateX(26px);transform:translateX(26px)}
            .slider.round{border-radius:34px}
            .slider.round:before{border-radius:50%}
            .hidden{display:none}
        </style>
    </head>
    <body>
        <section class="main">
            <div id="content">
                <div>
                    <section id="buttons">
                        <button id="get-still">Get Still</button>
                        <button id="toggle-stream">Start Stream</button>
                    </section>
                    <div id="stream-container" class="image-container hidden">
                        <div class="close" id="close-stream">×</div>
                        <img id="stream" src="">
                    </div>
                </div>
            </div>
        </section>

        <script>
        document.addEventListener('DOMContentLoaded', function (event) {
            var baseHost = document.location.origin
            var streamUrl = baseHost + '/stream'

            const hide = el => {
              el.classList.add('hidden')
            }
            const show = el => {
              el.classList.remove('hidden')
            }

            const disable = el => {
              el.classList.add('disabled')
              el.disabled = true
            }

            const enable = el => {
              el.classList.remove('disabled')
              el.disabled = false
            }

            const updateConfig = el => {
              let value
              switch (el.type) {
                case 'checkbox':
                  value = el.checked ? 1 : 0
                  break
                case 'range':
                case 'select-one':
                  value = el.value
                  break
                case 'button':
                case 'submit':
                  value = '1'
                  break
                default:
                  return
              }

              const query = `${baseHost}/control?var=${el.id}&val=${value}`

              fetch(query)
                .then(response => {
                  console.log(`request to ${query} finished, status: ${response.status}`)
                })
            }

            document
              .querySelectorAll('.close')
              .forEach(el => {
                el.onclick = () => {
                  hide(el.parentNode)
                }
              })

            // read initial values
            fetch(`${baseHost}/status`)
              .then(function (response) {
                return response.json()
              })
              .then(function (state) {
                document
                  .querySelectorAll('.default-action')
                  .forEach(el => {
                    updateConfig(el)
                  })
              })

            const view = document.getElementById('stream')
            const viewContainer = document.getElementById('stream-container')
            const stillButton = document.getElementById('get-still')
            const streamButton = document.getElementById('toggle-stream')
            const closeButton = document.getElementById('close-stream')

            const stopStream = () => {
              window.stop();
              streamButton.innerHTML = 'Start Stream'
            }

            const startStream = () => {
              view.src = `${streamUrl}`
              show(viewContainer)
              streamButton.innerHTML = 'Stop Stream'
            }

            // Attach actions to buttons
            stillButton.onclick = () => {
              stopStream()
              view.src = `${baseHost}/capture?_cb=${Date.now()}`
              show(viewContainer)
            }

            closeButton.onclick = () => {
              stopStream()
              hide(viewContainer)
            }

            streamButton.onclick = () => {
              const streamEnabled = streamButton.innerHTML === 'Stop Stream'
              if (streamEnabled) {
                stopStream()
                hide(viewContainer)
              } else {
                startStream()
              }
            }

            // Attach default on change action
            document
              .querySelectorAll('.default-action')
              .forEach(el => {
                el.onchange = () => updateConfig(el)
              })
        })
        </script>
    </body>
</html>
)rawliteral";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

void startCameraServer(){
    server.begin();
}

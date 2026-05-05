#include "camera.h"
#include "flash.h"
#include "esp_heap_caps.h"
#include "soc/soc_memory_types.h"

// Global instance
CameraManager cameraManager;

CameraManager::CameraManager() :
  camera_ready(false),
  current_resolution(FRAMESIZE_UXGA),
  capture_count(0),
  failed_capture_count(0),
  last_capture_time(0),
  last_frame_size(0),
  frame_buffers_in_psram(false),
  frame_buffer_active(false) {
  
  // Initialize default settings
  default_settings.resolution = FRAMESIZE_UXGA;
  default_settings.jpeg_quality = 10;
  default_settings.brightness = 0;
  default_settings.contrast = 0;
  default_settings.saturation = 0;
  default_settings.exposure = 300;
  default_settings.gain = 0;
  default_settings.special_effect = 0;
  default_settings.wb_mode = 0;
  default_settings.hmirror = false;
  default_settings.vflip = false;
}

void CameraManager::deinit() {
  if (!camera_ready) return;
  esp_camera_deinit();
  camera_ready = false;
  Serial.println("Camera deinitialized (PSRAM buffers freed)");
}

bool CameraManager::begin(uint8_t jpeg_quality, framesize_t default_resolution) {
  Serial.println("Initializing camera...");
  framesize_t safe_resolution = getSafeFrameSize(default_resolution);
  default_settings.resolution = safe_resolution;
  default_settings.jpeg_quality = constrain(jpeg_quality, 10, 63);
  
  // Retry camera initialization up to 3 times
  const int max_retries = 3;
  for (int retry = 0; retry < max_retries; retry++) {
    if (retry > 0) {
      Serial.printf("Camera initialization retry %d/%d...\n", retry + 1, max_retries);
      delay(1000); // Wait before retry
    }
    
    if (configureCamera(jpeg_quality, safe_resolution)) {
      current_resolution = safe_resolution;
      if (initializeCameraSensor()) {
        camera_ready = true;
        
        Serial.println("Camera initialization complete");
        printCameraInfo();
        return true;
      } else {
        Serial.printf("Camera sensor initialization failed (attempt %d)\n", retry + 1);
      }
    } else {
      Serial.printf("Camera configuration failed (attempt %d)\n", retry + 1);
    }
  }
  
  Serial.println("CRITICAL: Camera initialization failed after all retries");
  camera_ready = false;
  return false;
}

bool CameraManager::configureCamera(uint8_t jpeg_quality, framesize_t resolution) {
  framesize_t safe_resolution = getSafeFrameSize(resolution);
  bool psram_available = psramFound();
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
  config.frame_size = safe_resolution;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = psram_available ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = psram_available ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = jpeg_quality;
  config.fb_count = psram_available ? 2 : 1;

  bool using_psram_buffers = psram_available &&
                             config.fb_location == CAMERA_FB_IN_PSRAM;
  Serial.printf("PSRAM Available: %s\n", psram_available ? "Yes" : "No");
  Serial.printf("Frame buffer location: %s\n",
                using_psram_buffers ? "PSRAM" : "DRAM");
  Serial.printf("Frame buffer count: %d\n", config.fb_count);

  // Power-cycle the sensor to recover from soft-reset stuck states (OTA reboot, etc.)
  if (PWDN_GPIO_NUM >= 0) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // power down
    delay(10);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // power up
    delay(100);
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    frame_buffers_in_psram = false;
    return false;
  }

  frame_buffers_in_psram = using_psram_buffers;
  return true;
}

framesize_t CameraManager::getSafeFrameSize(framesize_t resolution) {
  if (!psramFound() && resolution > FRAMESIZE_VGA) {
    char requested[32];
    getResolutionString(resolution, requested, sizeof(requested));
    Serial.printf("Requested resolution %s requires PSRAM; using VGA instead\n",
                  requested);
    return FRAMESIZE_VGA;
  }

  return resolution;
}

bool CameraManager::initializeCameraSensor() {
  sensor_t * s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("Failed to get camera sensor");
    return false;
  }

  // Initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }

  // Set frame size for streaming
  if(s->pixformat == PIXFORMAT_JPEG){
    s->set_framesize(s, current_resolution);
  }

  // Additional optimizations for real-time streaming
  s->set_brightness(s, 0);     // 0 = default
  s->set_contrast(s, 2);       // Increased contrast for text clarity
  s->set_saturation(s, 1);     // Slight saturation boost
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

  return true;
}

// Resolution management
framesize_t CameraManager::getFrameSize(const String& size_param) {
  // Handle both short names (UXGA) and UI format (UXGA (1600x1200))
  if (size_param.startsWith("UXGA")) return FRAMESIZE_UXGA;   // 1600x1200
  if (size_param.startsWith("SXGA")) return FRAMESIZE_SXGA;   // 1280x1024
  if (size_param.startsWith("XGA")) return FRAMESIZE_XGA;     // 1024x768
  if (size_param.startsWith("SVGA")) return FRAMESIZE_SVGA;   // 800x600
  if (size_param.startsWith("VGA")) return FRAMESIZE_VGA;     // 640x480
  if (size_param.startsWith("CIF")) return FRAMESIZE_CIF;     // 400x296
  if (size_param.startsWith("QVGA")) return FRAMESIZE_QVGA;   // 320x240
  if (size_param.startsWith("HQVGA")) return FRAMESIZE_HQVGA; // 240x176
  return FRAMESIZE_VGA; // Default
}

void CameraManager::getResolutionString(framesize_t resolution, char* output, size_t max_len) {
  const char* resolution_str;
  switch(resolution) {
    case FRAMESIZE_UXGA: resolution_str = "UXGA (1600x1200)"; break;
    case FRAMESIZE_SXGA: resolution_str = "SXGA (1280x1024)"; break;
    case FRAMESIZE_XGA: resolution_str = "XGA (1024x768)"; break;
    case FRAMESIZE_SVGA: resolution_str = "SVGA (800x600)"; break;
    case FRAMESIZE_VGA: resolution_str = "VGA (640x480)"; break;
    case FRAMESIZE_CIF: resolution_str = "CIF (400x296)"; break;
    case FRAMESIZE_QVGA: resolution_str = "QVGA (320x240)"; break;
    case FRAMESIZE_HQVGA: resolution_str = "HQVGA (240x176)"; break;
    default: resolution_str = "Unknown"; break;
  }
  strncpy(output, resolution_str, max_len - 1);
  output[max_len - 1] = '\0';
}

bool CameraManager::setResolution(framesize_t resolution) {
  if (!camera_ready) return false;
  framesize_t safe_resolution = getSafeFrameSize(resolution);
  
  sensor_t* s = getSensor();
  if (!s) return false;
  
  if (s->set_framesize(s, safe_resolution) != 0) {
    Serial.printf("Failed to set resolution to %d\n", safe_resolution);
    return false;
  }

  current_resolution = safe_resolution;

  // The OV2640 needs time to flush the old geometry after resolution change.
  delay(200);

  // Discard 2 stale frames to get a clean frame at the new resolution.
  for (int i = 0; i < 2; i++) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) esp_camera_fb_return(stale);
  }

  char resolution_str[32];
  getResolutionString(safe_resolution, resolution_str, sizeof(resolution_str));
  Serial.printf("Resolution changed to: %s\n", resolution_str);
  return true;
}

framesize_t CameraManager::getCurrentResolution() {
  return current_resolution;
}

// Camera capture
camera_fb_t* CameraManager::captureFrame() {
  if (!camera_ready) {
    logCaptureResult(CAPTURE_CAMERA_NOT_READY);
    return nullptr;
  }
  
  if (frame_buffer_active) {
    Serial.println("WARNING: Previous frame buffer not released");
    logCaptureResult(CAPTURE_FAILED);
    return nullptr;
  }
  
  camera_fb_t* fb = nullptr;
  for (int attempt = 0; attempt < 2 && !fb; attempt++) {
    if (attempt > 0) delay(50);
    fb = esp_camera_fb_get();
  }

  if (fb) {
    if (frame_buffers_in_psram && !esp_ptr_external_ram(fb->buf)) {
      Serial.println("WARNING: Expected camera frame buffer in PSRAM, but buffer is not external RAM");
    }
    frame_buffer_active = true;
    capture_count++;
    last_capture_time = millis();
    last_frame_size = fb->len;
    logCaptureResult(CAPTURE_SUCCESS);
  } else {
    failed_capture_count++;
    logCaptureResult(CAPTURE_FAILED);
  }
  
  return fb;
}


CaptureResult CameraManager::captureToBuffer(uint8_t** buffer, size_t* buffer_size, bool use_flash) {
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_MEDIUM);
    delay(100);
  }
  camera_fb_t* fb = captureFrame();
  if (use_flash) {
    flashManager.setFlashDuty(FLASH_OFF);
  }
  if (!fb) {
    return CAPTURE_FAILED;
  }

  // Allocate copied captures in PSRAM when available.
  *buffer = psramFound() ? (uint8_t*)ps_malloc(fb->len) : nullptr;
  if (!*buffer) {
    *buffer = (uint8_t*)malloc(fb->len);
  }
  if (!*buffer) {
    releaseFrameBuffer(fb);
    return CAPTURE_OUT_OF_MEMORY;
  }

  memcpy(*buffer, fb->buf, fb->len);
  *buffer_size = fb->len;

  releaseFrameBuffer(fb);
  return CAPTURE_SUCCESS;
}

void CameraManager::releaseFrameBuffer(camera_fb_t* fb) {
  if (fb && frame_buffer_active) {
    esp_camera_fb_return(fb);
    frame_buffer_active = false;
  } else if (fb && !frame_buffer_active) {
    Serial.println("WARNING: Attempted to release frame buffer that wasn't active");
    esp_camera_fb_return(fb); // Still release it to prevent leaks
  }
}

// Camera settings
bool CameraManager::applySettings(const CameraSettings& settings) {
  if (!camera_ready) {
    return false;
  }

  CameraSettings safe_settings = settings;
  safe_settings.resolution = getSafeFrameSize(settings.resolution);
  safe_settings.jpeg_quality = constrain(settings.jpeg_quality, 10, 63);

  if (!validateSettings(safe_settings)) {
    return false;
  }
  
  sensor_t* s = getSensor();
  if (!s) return false;
  
  char resolution_str[32];
  getResolutionString(safe_settings.resolution, resolution_str, sizeof(resolution_str));
  
  // Apply resolution first
  if (safe_settings.resolution != current_resolution) {
    if (!setResolution(safe_settings.resolution)) {
      return false;
    }
  }
  
  if (s->set_quality(s, safe_settings.jpeg_quality) != 0) {
    Serial.printf("Failed to set JPEG quality to %d\n", safe_settings.jpeg_quality);
    return false;
  }

  // Apply basic image settings
  s->set_brightness(s, constrain(safe_settings.brightness, -2, 2));
  s->set_contrast(s, constrain(safe_settings.contrast, -2, 2));
  s->set_saturation(s, constrain(safe_settings.saturation, -2, 2));
  s->set_special_effect(s, constrain(safe_settings.special_effect, 0, 6));
  
  // Apply white balance settings
  if (safe_settings.wb_mode == 0) {
    s->set_whitebal(s, 1); // Enable auto white balance
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
  } else {
    s->set_whitebal(s, 0); // Disable auto white balance
    s->set_wb_mode(s, constrain(safe_settings.wb_mode, 0, 4));
  }
  
  // Apply gain control
  if (safe_settings.gain > 0) {
    s->set_gain_ctrl(s, 0); // Disable auto gain
    s->set_agc_gain(s, constrain(safe_settings.gain, 0, 30)); // Set manual gain
  } else {
    s->set_gain_ctrl(s, 1); // Enable auto gain
  }
  
  // Apply orientation settings
  s->set_hmirror(s, safe_settings.hmirror ? 1 : 0);
  s->set_vflip(s, safe_settings.vflip ? 1 : 0);
  
  // Use auto exposure for high resolutions to prevent corruption
  if (safe_settings.resolution <= FRAMESIZE_VGA && safe_settings.exposure > 0) {
    // Only use manual exposure for smaller resolutions
    s->set_exposure_ctrl(s, 0); // 0 = disable auto exposure
    s->set_aec_value(s, constrain(safe_settings.exposure, 0, 1200)); // Set manual exposure value
    s->set_aec2(s, 0); // Disable AEC2
    Serial.println("Manual exposure enabled (small resolution)");
  } else {
    // Use auto exposure for high resolutions
    s->set_exposure_ctrl(s, 1); // 1 = enable auto exposure
    s->set_aec2(s, 1); // Enable AEC2
    Serial.println("Auto exposure enabled (high resolution protection)");
  }
  
  Serial.printf("Applied camera settings - Res: %s, Quality: %d, Brightness: %d, Contrast: %d, Gain: %d\n",
               resolution_str, 
               safe_settings.jpeg_quality, safe_settings.brightness,
               safe_settings.contrast, safe_settings.gain);
  
  return true;
}

bool CameraManager::resetToDefaults() {
  return applySettings(default_settings);
}

CameraSettings CameraManager::getCurrentSettings() {
  sensor_t* s = getSensor();
  CameraSettings settings = default_settings;
  
  if (s) {
    settings.resolution = current_resolution;
    // Note: ESP32 camera doesn't provide getters for all settings
    // So we return the last known values or defaults
  }
  
  return settings;
}

// Individual setting controls
bool CameraManager::setBrightness(int8_t brightness) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_brightness(s, constrain(brightness, -2, 2)) == 0;
}

bool CameraManager::setContrast(int8_t contrast) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_contrast(s, constrain(contrast, -2, 2)) == 0;
}

bool CameraManager::setSaturation(int8_t saturation) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_saturation(s, constrain(saturation, -2, 2)) == 0;
}

bool CameraManager::setJPEGQuality(uint8_t quality) {
  if (!camera_ready || quality < 10 || quality > 63) return false;
  sensor_t* s = getSensor();
  return s && s->set_quality(s, quality) == 0;
}

bool CameraManager::setExposure(uint16_t exposure) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  if (!s) return false;
  
  if (exposure == 0) {
    // Enable auto exposure
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
  } else {
    // Set manual exposure
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, constrain(exposure, 0, 1200));
    s->set_aec2(s, 0);
  }
  
  return true;
}

bool CameraManager::setGain(uint8_t gain) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  if (!s) return false;
  
  if (gain == 0) {
    // Enable auto gain
    s->set_gain_ctrl(s, 1);
  } else {
    // Set manual gain
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, constrain(gain, 0, 30));
  }
  
  return true;
}

bool CameraManager::setSpecialEffect(uint8_t effect) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_special_effect(s, constrain(effect, 0, 6)) == 0;
}

bool CameraManager::setWhiteBalance(uint8_t wb_mode) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  if (!s) return false;
  
  if (wb_mode == 0) {
    // Auto white balance
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
  } else {
    // Manual white balance
    s->set_whitebal(s, 0);
    s->set_wb_mode(s, constrain(wb_mode, 0, 4));
  }
  
  return true;
}

bool CameraManager::setHorizontalMirror(bool enable) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_hmirror(s, enable ? 1 : 0) == 0;
}

bool CameraManager::setVerticalFlip(bool enable) {
  if (!camera_ready) return false;
  sensor_t* s = getSensor();
  return s && s->set_vflip(s, enable ? 1 : 0) == 0;
}

// Utility functions
void CameraManager::printCameraInfo() {
  if (!camera_ready) {
    Serial.println("Camera not ready");
    return;
  }
  
  sensor_t* s = getSensor();
  if (s) {
    Serial.println("========== Camera Information ==========");
    Serial.printf("Camera ID: 0x%02X\n", s->id.PID);
    char resolution_str[32];
    getResolutionString(current_resolution, resolution_str, sizeof(resolution_str));
    Serial.printf("Current Resolution: %s\n", resolution_str);
    Serial.printf("PSRAM Available: %s\n", psramFound() ? "Yes" : "No");
    if (psramFound()) {
      Serial.printf("PSRAM Size: %u bytes\n", ESP.getPsramSize());
      Serial.printf("PSRAM Free: %u bytes\n", ESP.getFreePsram());
    }
    Serial.printf("Frame Buffers: %s\n",
                  frame_buffers_in_psram ? "PSRAM" : "DRAM");
    Serial.printf("Total Captures: %u\n", capture_count);
    Serial.printf("Failed Captures: %u\n", failed_capture_count);
    Serial.printf("Success Rate: %.1f%%\n", 
                 capture_count > 0 ? ((float)(capture_count - failed_capture_count) / capture_count) * 100.0 : 0.0);
    Serial.printf("Last Frame Size: %u bytes\n", last_frame_size);
    Serial.println("=======================================");
  }
}

// Private methods
sensor_t* CameraManager::getSensor() {
  return esp_camera_sensor_get();
}

bool CameraManager::validateSettings(const CameraSettings& settings) {
  // Validate ranges
  if (settings.brightness < -2 || settings.brightness > 2) return false;
  if (settings.contrast < -2 || settings.contrast > 2) return false;
  if (settings.saturation < -2 || settings.saturation > 2) return false;
  if (settings.exposure > 1200) return false;
  if (settings.jpeg_quality < 10 || settings.jpeg_quality > 63) return false;
  if (settings.gain > 30) return false;
  if (settings.special_effect > 6) return false;
  if (settings.wb_mode > 4) return false;
  if (settings.resolution < FRAMESIZE_96X96 || settings.resolution > FRAMESIZE_UXGA) return false;
  
  return true;
}

void CameraManager::logCaptureResult(CaptureResult result) {
  switch (result) {
    case CAPTURE_SUCCESS:
      // Success logged elsewhere to avoid spam
      break;
    case CAPTURE_FAILED:
      Serial.println("Capture failed");
      break;
    case CAPTURE_OUT_OF_MEMORY:
      Serial.println("Capture failed: Out of memory");
      break;
    case CAPTURE_INVALID_RESOLUTION:
      Serial.println("Capture failed: Invalid resolution");
      break;
    case CAPTURE_CAMERA_NOT_READY:
      Serial.println("Capture failed: Camera not ready");
      break;
  }
}




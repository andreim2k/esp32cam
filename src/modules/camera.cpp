#include "camera.h"

// Global instance
CameraManager cameraManager;

CameraManager::CameraManager() : 
  camera_ready(false),
  current_resolution(FRAMESIZE_UXGA),
  original_resolution(FRAMESIZE_UXGA),
  capture_count(0),
  failed_capture_count(0),
  last_capture_time(0),
  last_frame_size(0) {
  
  // Initialize default settings
  default_settings.resolution = FRAMESIZE_UXGA;
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

bool CameraManager::begin(uint8_t jpeg_quality, framesize_t default_resolution) {
  Serial.println("Initializing camera...");
  
  if (!configureCamera(jpeg_quality, default_resolution)) {
    Serial.println("Failed to configure camera");
    return false;
  }
  
  if (!initializeCameraSensor()) {
    Serial.println("Failed to initialize camera sensor");
    return false;
  }
  
  current_resolution = default_resolution;
  original_resolution = default_resolution;
  camera_ready = true;
  
  Serial.println("Camera initialization complete");
  printCameraInfo();
  
  return true;
}

bool CameraManager::configureCamera(uint8_t jpeg_quality, framesize_t resolution) {
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
  config.frame_size = resolution;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = jpeg_quality;
  config.fb_count = 2; // Double buffering

  // If PSRAM IC present, init with UXGA resolution and higher JPEG quality
  if(config.pixel_format == PIXFORMAT_JPEG){
    if(psramFound()){
      config.jpeg_quality = jpeg_quality;
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
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  return true;
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

  return true;
}

// Resolution management
framesize_t CameraManager::getFrameSize(const String& size_param) {
  if (size_param == "UXGA") return FRAMESIZE_UXGA;   // 1600x1200
  if (size_param == "SXGA") return FRAMESIZE_SXGA;   // 1280x1024
  if (size_param == "XGA") return FRAMESIZE_XGA;     // 1024x768
  if (size_param == "SVGA") return FRAMESIZE_SVGA;   // 800x600
  if (size_param == "VGA") return FRAMESIZE_VGA;     // 640x480
  if (size_param == "CIF") return FRAMESIZE_CIF;     // 400x296
  if (size_param == "QVGA") return FRAMESIZE_QVGA;   // 320x240
  if (size_param == "HQVGA") return FRAMESIZE_HQVGA; // 240x176
  return FRAMESIZE_VGA; // Default
}

String CameraManager::getResolutionString(framesize_t resolution) {
  switch(resolution) {
    case FRAMESIZE_UXGA: return "UXGA (1600x1200)";
    case FRAMESIZE_SXGA: return "SXGA (1280x1024)";
    case FRAMESIZE_XGA: return "XGA (1024x768)";
    case FRAMESIZE_SVGA: return "SVGA (800x600)";
    case FRAMESIZE_VGA: return "VGA (640x480)";
    case FRAMESIZE_CIF: return "CIF (400x296)";
    case FRAMESIZE_QVGA: return "QVGA (320x240)";
    case FRAMESIZE_HQVGA: return "HQVGA (240x176)";
    default: return "Unknown";
  }
}

bool CameraManager::setResolution(framesize_t resolution) {
  if (!camera_ready) return false;
  
  sensor_t* s = getSensor();
  if (!s) return false;
  
  if (s->set_framesize(s, resolution) != 0) {
    Serial.printf("Failed to set resolution to %d\n", resolution);
    return false;
  }
  
  current_resolution = resolution;
  Serial.printf("Resolution changed to: %s\n", getResolutionString(resolution).c_str());
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
  
  camera_fb_t* fb = esp_camera_fb_get();
  
  if (fb) {
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

camera_fb_t* CameraManager::captureWithFlash(bool use_flash) {
  if (!camera_ready) {
    logCaptureResult(CAPTURE_CAMERA_NOT_READY);
    return nullptr;
  }
  
  if (use_flash) {
    Serial.println("FLASH CAPTURE: Starting synchronized flash capture");
    
    // Flash control will be handled by FlashManager
    // Just add stabilization delay for flash
    delay(200);
    
    // Discard warm-up frames for consistent exposure
    Serial.println("FLASH CAPTURE: Warming up camera...");
    for (int i = 0; i < 2; i++) {
      camera_fb_t* warmup = esp_camera_fb_get();
      if (warmup) {
        esp_camera_fb_return(warmup);
      }
      delay(100);
    }
    
    Serial.println("FLASH CAPTURE: Capturing final frame");
  } else {
    Serial.println("NORMAL CAPTURE: Warming up camera...");
    
    // Discard warm-up frames for consistent exposure and focus
    for (int i = 0; i < 1; i++) {
      camera_fb_t* warmup = esp_camera_fb_get();
      if (warmup) {
        esp_camera_fb_return(warmup);
      }
      delay(50);
    }
    
    Serial.println("NORMAL CAPTURE: Capturing final frame");
  }
  
  // Capture the final frame
  camera_fb_t* fb = esp_camera_fb_get();
  
  if (fb) {
    capture_count++;
    last_capture_time = millis();
    last_frame_size = fb->len;
    logCaptureResult(CAPTURE_SUCCESS);
    Serial.println("Capture complete");
  } else {
    failed_capture_count++;
    logCaptureResult(CAPTURE_FAILED);
    Serial.println("Capture failed");
  }
  
  return fb;
}

CaptureResult CameraManager::captureToBuffer(uint8_t** buffer, size_t* buffer_size, bool use_flash) {
  camera_fb_t* fb = captureWithFlash(use_flash);
  if (!fb) {
    return CAPTURE_FAILED;
  }
  
  // Allocate buffer and copy data
  *buffer = (uint8_t*)malloc(fb->len);
  if (!*buffer) {
    esp_camera_fb_return(fb);
    return CAPTURE_OUT_OF_MEMORY;
  }
  
  memcpy(*buffer, fb->buf, fb->len);
  *buffer_size = fb->len;
  
  esp_camera_fb_return(fb);
  return CAPTURE_SUCCESS;
}

void CameraManager::releaseFrameBuffer(camera_fb_t* fb) {
  if (fb) {
    esp_camera_fb_return(fb);
  }
}

// Camera settings
bool CameraManager::applySettings(const CameraSettings& settings) {
  if (!camera_ready || !validateSettings(settings)) {
    return false;
  }
  
  sensor_t* s = getSensor();
  if (!s) return false;
  
  // Store original resolution to restore later if needed
  framesize_t original_res = current_resolution;
  
  // Apply resolution first
  if (settings.resolution != current_resolution) {
    if (!setResolution(settings.resolution)) {
      return false;
    }
  }
  
  // Apply basic image settings
  s->set_brightness(s, constrain(settings.brightness, -2, 2));
  s->set_contrast(s, constrain(settings.contrast, -2, 2));
  s->set_saturation(s, constrain(settings.saturation, -2, 2));
  s->set_special_effect(s, constrain(settings.special_effect, 0, 6));
  
  // Apply white balance settings
  if (settings.wb_mode == 0) {
    s->set_whitebal(s, 1); // Enable auto white balance
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
  } else {
    s->set_whitebal(s, 0); // Disable auto white balance
    s->set_wb_mode(s, constrain(settings.wb_mode, 0, 4));
  }
  
  // Apply gain control
  if (settings.gain > 0) {
    s->set_gain_ctrl(s, 0); // Disable auto gain
    s->set_agc_gain(s, constrain(settings.gain, 0, 30)); // Set manual gain
  } else {
    s->set_gain_ctrl(s, 1); // Enable auto gain
  }
  
  // Apply orientation settings
  s->set_hmirror(s, settings.hmirror ? 1 : 0);
  s->set_vflip(s, settings.vflip ? 1 : 0);
  
  // Use auto exposure for high resolutions to prevent corruption
  if (settings.resolution <= FRAMESIZE_VGA && settings.exposure > 0) {
    // Only use manual exposure for smaller resolutions
    s->set_exposure_ctrl(s, 0); // 0 = disable auto exposure
    s->set_aec_value(s, constrain(settings.exposure, 0, 1200)); // Set manual exposure value
    s->set_aec2(s, 0); // Disable AEC2
    Serial.println("Manual exposure enabled (small resolution)");
  } else {
    // Use auto exposure for high resolutions
    s->set_exposure_ctrl(s, 1); // 1 = enable auto exposure
    s->set_aec2(s, 1); // Enable AEC2
    Serial.println("Auto exposure enabled (high resolution protection)");
  }
  
  Serial.printf("Applied camera settings - Res: %s, Brightness: %d, Contrast: %d, Gain: %d\n", 
               getResolutionString(settings.resolution).c_str(), 
               settings.brightness, settings.contrast, settings.gain);
  
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
bool CameraManager::warmupCamera(int frames) {
  if (!camera_ready) return false;
  
  Serial.printf("Warming up camera with %d frames...\n", frames);
  
  for (int i = 0; i < frames; i++) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) {
      esp_camera_fb_return(warmup);
      Serial.printf("Warm-up frame %d/%d completed\n", i+1, frames);
    } else {
      Serial.printf("Warm-up frame %d/%d failed\n", i+1, frames);
      return false;
    }
    delay(100);
  }
  
  Serial.println("Camera warm-up complete");
  return true;
}

void CameraManager::printCameraInfo() {
  if (!camera_ready) {
    Serial.println("Camera not ready");
    return;
  }
  
  sensor_t* s = getSensor();
  if (s) {
    Serial.println("========== Camera Information ==========");
    Serial.printf("Camera ID: 0x%02X\n", s->id.PID);
    Serial.printf("Current Resolution: %s\n", getResolutionString(current_resolution).c_str());
    Serial.printf("PSRAM Available: %s\n", psramFound() ? "Yes" : "No");
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
#include "flash.h"

// Global instance
FlashManager flashManager;

FlashManager::FlashManager() : 
  flash_ready(false),
  current_duty(0),
  light_threshold(100),
  last_light_level(255),
  last_light_check(0),
  cached_light_result(true),
  activation_count(0),
  last_activation_time(0) {
}

bool FlashManager::begin(uint8_t threshold) {
  Serial.println("Initializing flash control...");
  
  light_threshold = threshold;
  
  if (!initializePWM()) {
    Serial.println("Failed to initialize flash PWM");
    return false;
  }
  
  // Start with flash OFF
  setFlashDuty(FLASH_OFF);
  
  flash_ready = true;
  Serial.printf("Flash LED initialized on GPIO%d with PWM control\n", FLASH_LED_PIN);
  Serial.printf("Light threshold set to: %d\n", light_threshold);
  
  return true;
}

bool FlashManager::initializePWM() {
  // Setup LEDC for PWM control of flash LED
  if (ledcSetup(FLASH_CH, FLASH_FREQ, FLASH_RESOLUTION) == 0) {
    Serial.printf("Failed to setup LEDC channel %d\n", FLASH_CH);
    return false;
  }
  
  ledcAttachPin(FLASH_LED_PIN, FLASH_CH);
  ledcWrite(FLASH_CH, 0); // Start with flash OFF
  
  Serial.printf("PWM initialized: Channel=%d, Freq=%dHz, Resolution=%d-bit\n", 
               FLASH_CH, FLASH_FREQ, FLASH_RESOLUTION);
  
  return true;
}

// Flash control
bool FlashManager::setFlash(bool enable) {
  if (!flash_ready) return false;
  
  uint8_t duty = enable ? FLASH_HIGH : FLASH_OFF;
  return setFlashDuty(duty);
}

bool FlashManager::setFlashDuty(uint8_t duty) {
  if (!flash_ready || !validateDutyRange(duty)) {
    return false;
  }
  
  bool was_off = (current_duty == 0);
  current_duty = duty;
  
  ledcWrite(FLASH_CH, duty);
  
  // Update statistics
  if (duty > 0 && was_off) {
    activation_count++;
    last_activation_time = millis();
    Serial.printf("Flash activated: duty=%d (%d%%)\n", duty, (duty * 100) / 255);
  } else if (duty == 0 && !was_off) {
    Serial.println("Flash deactivated");
  }
  
  return true;
}

bool FlashManager::setFlashPreset(const char* preset) {
  uint8_t duty = FLASH_OFF;
  
  if (strcmp(preset, "off") == 0) {
    duty = FLASH_OFF;
  } else if (strcmp(preset, "low") == 0) {
    duty = FLASH_LOW;
  } else if (strcmp(preset, "medium") == 0) {
    duty = FLASH_MEDIUM;
  } else if (strcmp(preset, "high") == 0) {
    duty = FLASH_HIGH;
  } else {
    Serial.printf("Unknown flash preset: %s\n", preset);
    return false;
  }
  
  bool result = setFlashDuty(duty);
  if (result) {
    Serial.printf("Flash preset '%s' applied: duty=%d\n", preset, duty);
  }
  
  return result;
}

// Light detection
bool FlashManager::isLightLow() {
  if (!flash_ready) {
    return true; // Default to flash ON for safety
  }
  
  // Use cached result if recent
  if (millis() - last_light_check < LIGHT_CHECK_INTERVAL) {
    return cached_light_result;
  }
  
  // Get a frame for light analysis using CameraManager
  camera_fb_t* fb = cameraManager.captureFrame();
  if (!fb) {
    Serial.println("Light check failed: Could not capture frame");
    cached_light_result = true; // Default to flash ON if can't check
    last_light_check = millis();
    return cached_light_result;
  }
  
  bool result = isLightLow(fb);
  cameraManager.releaseFrameBuffer(fb);
  
  return result;
}

bool FlashManager::isLightLow(camera_fb_t* fb) {
  if (!fb || fb->len < 1000) {
    Serial.println("Light analysis failed: Invalid frame buffer");
    return true; // Default to flash ON for safety
  }
  
  uint8_t brightness = analyzeBrightness(fb);
  last_light_level = brightness;
  last_light_check = millis();
  
  bool is_low = brightness < light_threshold;
  cached_light_result = is_low;
  
  Serial.printf("Light level: %d (threshold: %d) -> %s\n", 
               brightness, light_threshold, is_low ? "LOW" : "BRIGHT");
  
  return is_low;
}

uint8_t FlashManager::analyzeBrightness(camera_fb_t* fb) {
  if (!fb || fb->len == 0) {
    return 0;
  }
  
  // Sample pixels from center area for brightness analysis
  uint32_t brightness_sum = 0;
  int sample_size = min(500, (int)(fb->len / 8));
  int start_offset = fb->len / 4; // Start from center area
  int samples_taken = 0;
  
  // Sample every 4th pixel for speed optimization
  for (int i = start_offset; i < start_offset + sample_size && i < (int)fb->len; i += 4) {
    brightness_sum += fb->buf[i];
    samples_taken++;
  }
  
  if (samples_taken == 0) {
    return 0;
  }
  
  uint8_t avg_brightness = brightness_sum / samples_taken;
  return avg_brightness;
}

void FlashManager::setLightThreshold(uint8_t threshold) {
  light_threshold = threshold;
  Serial.printf("Light threshold updated to: %d\n", threshold);
  
  // Clear cached result to force re-check
  last_light_check = 0;
}

// Auto flash logic
bool FlashManager::shouldUseFlash() {
  return isLightLow();
}

bool FlashManager::shouldUseFlash(camera_fb_t* fb) {
  return isLightLow(fb);
}

FlashMode FlashManager::determineFlashMode(const char* mode_param) {
  if (strcmp(mode_param, "1") == 0 || strcmp(mode_param, "on") == 0 || strcmp(mode_param, "true") == 0) {
    return FLASH_MODE_ON;
  } else if (strcmp(mode_param, "auto") == 0) {
    return FLASH_MODE_AUTO;
  } else {
    return FLASH_MODE_OFF;
  }
}

// Status and statistics
FlashStatus FlashManager::getStatus() const {
  FlashStatus status;
  status.is_on = (current_duty > 0);
  status.duty_cycle = current_duty;
  status.brightness_percent = (current_duty * 100) / 255;
  status.mode = status.is_on ? FLASH_MODE_ON : FLASH_MODE_OFF;
  status.last_activation = last_activation_time;
  status.activation_count = activation_count;
  
  return status;
}

void FlashManager::printFlashInfo() {
  if (!flash_ready) {
    Serial.println("Flash not ready");
    return;
  }
  
  FlashStatus status = getStatus();
  
  Serial.println("========== Flash Information ==========");
  Serial.printf("Flash State: %s\n", status.is_on ? "ON" : "OFF");
  Serial.printf("Duty Cycle: %d/255 (%d%%)\n", status.duty_cycle, status.brightness_percent);
  Serial.printf("Light Threshold: %d\n", light_threshold);
  Serial.printf("Last Light Level: %d\n", last_light_level);
  Serial.printf("Total Activations: %u\n", activation_count);
  Serial.printf("Last Activation: %lu ms ago\n", 
               last_activation_time > 0 ? millis() - last_activation_time : 0);
  Serial.println("======================================");
}

// Synchronized capture support
void FlashManager::prepareForCapture() {
  if (!flash_ready) return;
  
  // This method can be used to prepare flash for capture
  // For now, just log the preparation
  Serial.println("Flash prepared for capture");
}

void FlashManager::finishCapture() {
  if (!flash_ready) return;
  
  // Turn off flash after capture if it was auto-activated
  // This provides a clean end to the capture process
  Serial.println("Flash capture finished");
}

bool FlashManager::captureWithAutoFlash(camera_fb_t** fb) {
  if (!flash_ready || !fb) {
    return false;
  }
  
  // Check if flash is needed
  bool use_flash = shouldUseFlash();
  
  if (use_flash) {
    Serial.println("Auto-flash: Activating flash for low light");
    setFlashDuty(FLASH_MEDIUM);
    delay(200); // Stabilization delay
  }
  
  // Capture frame using CameraManager
  *fb = cameraManager.captureFrame();
  
  if (use_flash) {
    // Keep flash on briefly for exposure, then turn off
    delay(100);
    setFlashDuty(FLASH_OFF);
    Serial.println("Auto-flash: Deactivated after capture");
  }
  
  return (*fb != nullptr);
}

// Private methods
bool FlashManager::validateDutyRange(uint8_t duty) {
  // Allow full range 0-255
  return true;
}
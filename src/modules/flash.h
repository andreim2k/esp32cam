#ifndef FLASH_H
#define FLASH_H

#include <Arduino.h>
#include "esp_camera.h"

// Flash Control Configuration
#define FLASH_LED_PIN     4   // Built-in flash LED
#define FLASH_CH          2   // LEDC channel for PWM flash control
#define FLASH_FREQ        5000 // PWM frequency in Hz
#define FLASH_RESOLUTION  8   // PWM resolution (8-bit = 0-255)
#define LIGHT_CHECK_INTERVAL 1000 // Light check interval in ms

// Flash presets
#define FLASH_OFF         0
#define FLASH_LOW         64
#define FLASH_MEDIUM      128
#define FLASH_HIGH        255

enum FlashMode {
  FLASH_MODE_OFF = 0,
  FLASH_MODE_ON = 1,
  FLASH_MODE_AUTO = 2
};

struct FlashStatus {
  bool is_on;
  uint8_t duty_cycle;
  uint8_t brightness_percent;
  FlashMode mode;
  unsigned long last_activation;
  uint32_t activation_count;
};

class FlashManager {
public:
  FlashManager();
  
  // Initialization
  bool begin(uint8_t threshold = 100);
  bool isReady() const { return flash_ready; }
  
  // Flash control
  bool setFlash(bool enable);
  bool setFlashDuty(uint8_t duty);
  bool setFlashPreset(const char* preset);
  uint8_t getCurrentDuty() const { return current_duty; }
  bool isFlashOn() const { return current_duty > 0; }
  
  // Light detection
  bool isLightLow();
  bool isLightLow(camera_fb_t* fb);
  uint8_t getLastLightLevel() const { return last_light_level; }
  void setLightThreshold(uint8_t threshold);
  uint8_t getLightThreshold() const { return light_threshold; }
  
  // Auto flash logic
  bool shouldUseFlash();
  bool shouldUseFlash(camera_fb_t* fb);
  FlashMode determineFlashMode(const char* mode_param);
  
  // Status and statistics
  FlashStatus getStatus() const;
  void printFlashInfo();
  
  // Synchronized capture support
  void prepareForCapture();
  void finishCapture();
  bool captureWithAutoFlash(camera_fb_t** fb);

private:
  bool flash_ready;
  uint8_t current_duty;
  uint8_t light_threshold;
  uint8_t last_light_level;
  unsigned long last_light_check;
  bool cached_light_result;
  uint32_t activation_count;
  unsigned long last_activation_time;
  
  // Internal methods
  bool initializePWM();
  uint8_t analyzeBrightness(camera_fb_t* fb);
  void updateLightCache();
  bool validateDutyRange(uint8_t duty);
};

// Global flash manager instance
extern FlashManager flashManager;

#endif // FLASH_H
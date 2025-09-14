#ifndef CAMERA_H
#define CAMERA_H

#include <Arduino.h>
#include "esp_camera.h"

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

enum CaptureResult {
  CAPTURE_SUCCESS = 0,
  CAPTURE_FAILED = 1,
  CAPTURE_OUT_OF_MEMORY = 2,
  CAPTURE_INVALID_RESOLUTION = 3,
  CAPTURE_CAMERA_NOT_READY = 4
};

struct CameraSettings {
  framesize_t resolution;
  int8_t brightness;    // -2 to +2
  int8_t contrast;      // -2 to +2
  int8_t saturation;    // -2 to +2
  uint16_t exposure;    // 0-1200
  uint8_t gain;         // 0-30
  uint8_t special_effect; // 0-6
  uint8_t wb_mode;      // 0-4 (white balance mode)
  bool hmirror;         // horizontal mirror
  bool vflip;           // vertical flip
};

class CameraManager {
public:
  CameraManager();
  
  // Initialization
  bool begin(uint8_t jpeg_quality = 10, framesize_t default_resolution = FRAMESIZE_UXGA);
  bool isReady() const { return camera_ready; }
  
  // Resolution management
  framesize_t getFrameSize(const String& size_param);
  String getResolutionString(framesize_t resolution);
  bool setResolution(framesize_t resolution);
  framesize_t getCurrentResolution();
  
  // Camera capture
  camera_fb_t* captureFrame();
  camera_fb_t* captureWithFlash(bool use_flash);
  CaptureResult captureToBuffer(uint8_t** buffer, size_t* buffer_size, bool use_flash = false);
  void releaseFrameBuffer(camera_fb_t* fb);
  
  // Camera settings
  bool applySettings(const CameraSettings& settings);
  bool resetToDefaults();
  CameraSettings getCurrentSettings();
  
  // Individual setting controls
  bool setBrightness(int8_t brightness);
  bool setContrast(int8_t contrast);
  bool setSaturation(int8_t saturation);
  bool setExposure(uint16_t exposure);
  bool setGain(uint8_t gain);
  bool setSpecialEffect(uint8_t effect);
  bool setWhiteBalance(uint8_t wb_mode);
  bool setHorizontalMirror(bool enable);
  bool setVerticalFlip(bool enable);
  
  // Statistics and diagnostics
  uint32_t getTotalCaptureCount() const { return capture_count; }
  uint32_t getFailedCaptureCount() const { return failed_capture_count; }
  unsigned long getLastCaptureTime() const { return last_capture_time; }
  size_t getLastFrameSize() const { return last_frame_size; }
  
  // Utility functions
  bool warmupCamera(int frames = 3);
  void printCameraInfo();

private:
  bool camera_ready;
  framesize_t current_resolution;
  framesize_t original_resolution;
  uint32_t capture_count;
  uint32_t failed_capture_count;
  unsigned long last_capture_time;
  size_t last_frame_size;
  
  // Default camera settings
  CameraSettings default_settings;
  
  // Internal methods
  bool initializeCameraSensor();
  bool configureCamera(uint8_t jpeg_quality, framesize_t resolution);
  sensor_t* getSensor();
  bool validateSettings(const CameraSettings& settings);
  void logCaptureResult(CaptureResult result);
};

// Global camera manager instance
extern CameraManager cameraManager;

#endif // CAMERA_H
#ifndef CONFIG_H
#define CONFIG_H

#include "esp_camera.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>

// EEPROM Memory Layout
#define EEPROM_SIZE 512
#define CONFIG_MAGIC 0xCAFE
#define CONFIG_VERSION 1

// Memory offsets
#define OFFSET_MAGIC 0
#define OFFSET_VERSION 2
#define OFFSET_WIFI_SSID 4
#define OFFSET_WIFI_PASSWORD 68
#define OFFSET_API_KEY 132
#define OFFSET_USE_STATIC_IP 196
#define OFFSET_STATIC_IP 197
#define OFFSET_GATEWAY 201
#define OFFSET_SUBNET 205
#define OFFSET_DNS_PRIMARY 209
#define OFFSET_DNS_SECONDARY 213
#define OFFSET_DEVICE_NAME 217
#define OFFSET_JPEG_QUALITY 281
#define OFFSET_DEFAULT_RESOLUTION 282
#define OFFSET_FLASH_THRESHOLD 283

// String field sizes
#define SSID_MAX_LEN 64
#define PASSWORD_MAX_LEN 64
#define API_KEY_MAX_LEN 64
#define DEVICE_NAME_MAX_LEN 64

// Default configuration values
#define DEFAULT_SSID "ESP32CAM_Config"
#define DEFAULT_PASSWORD "configure123"
#define DEFAULT_API_KEY "esp32cam-default-key"
#define DEFAULT_DEVICE_NAME "ESP32-CAM-Server"
#define DEFAULT_JPEG_QUALITY 10
#define DEFAULT_RESOLUTION FRAMESIZE_UXGA
#define DEFAULT_FLASH_THRESHOLD 100

struct Configuration {
  // Network settings
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_password[PASSWORD_MAX_LEN];
  bool use_static_ip;
  IPAddress static_ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns_primary;
  IPAddress dns_secondary;

  // Security settings
  char api_key[API_KEY_MAX_LEN];

  // Device settings
  char device_name[DEVICE_NAME_MAX_LEN];
  uint8_t jpeg_quality;
  framesize_t default_resolution;
  uint8_t flash_threshold;
};

class ConfigManager {
public:
  ConfigManager();
  bool begin();
  bool loadConfig();
  bool saveConfig();
  void resetToDefaults();

  // Getters
  const Configuration &getConfig() const { return config; }
  const char *getWiFiSSID() const { return config.wifi_ssid; }
  const char *getWiFiPassword() const { return config.wifi_password; }
  const char *getAPIKey() const { return config.api_key; }
  const char *getDeviceName() const { return config.device_name; }
  bool useStaticIP() const { return config.use_static_ip; }
  IPAddress getStaticIP() const { return config.static_ip; }
  IPAddress getGateway() const { return config.gateway; }
  IPAddress getSubnet() const { return config.subnet; }
  IPAddress getPrimaryDNS() const { return config.dns_primary; }
  IPAddress getSecondaryDNS() const { return config.dns_secondary; }
  uint8_t getJPEGQuality() const { return config.jpeg_quality; }
  framesize_t getDefaultResolution() const { return config.default_resolution; }
  uint8_t getFlashThreshold() const { return config.flash_threshold; }

  // Setters
  bool setWiFiCredentials(const char *ssid, const char *password);
  bool setAPIKey(const char *key);
  bool setStaticIP(const IPAddress &ip, const IPAddress &gateway,
                   const IPAddress &subnet);
  bool setDNS(const IPAddress &primary, const IPAddress &secondary);
  bool setDeviceName(const char *name);
  bool setJPEGQuality(uint8_t quality);
  bool setDefaultResolution(framesize_t resolution);
  bool setFlashThreshold(uint8_t threshold);
  bool setUseStaticIP(bool use_static);

  // Validation
  bool isValidConfig() const;
  bool isAPIKeyValid(const char *provided_key) const;

  // Configuration mode (for initial setup)
  bool isFirstBoot() const;
  void enterConfigMode();
  void exitConfigMode();

private:
  Configuration config;
  bool config_loaded;

  void writeString(int offset, const char *str, int max_len);
  void readString(int offset, char *str, int max_len);
  void writeUint8(int offset, uint8_t value);
  uint8_t readUint8(int offset);
  void writeUint16(int offset, uint16_t value);
  uint16_t readUint16(int offset);
  void writeIPAddress(int offset, const IPAddress &addr);
  IPAddress readIPAddress(int offset);
  bool validateConfiguration() const;
};

// Global configuration manager instance
extern ConfigManager configManager;

#endif // CONFIG_H
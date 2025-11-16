#include "config.h"

// Global instance
ConfigManager configManager;

ConfigManager::ConfigManager() : config_loaded(false) {
  memset(&config, 0, sizeof(Configuration));
}

bool ConfigManager::begin() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialize EEPROM");
    return false;
  }

  Serial.println("ConfigManager initialized");
  return loadConfig();
}

bool ConfigManager::loadConfig() {
  // Check magic number and version
  uint16_t magic = readUint16(OFFSET_MAGIC);
  uint16_t version = readUint16(OFFSET_VERSION);

  if (magic != CONFIG_MAGIC || version != CONFIG_VERSION) {
    Serial.printf("Invalid config magic=0x%04X version=%d, using defaults\n",
                  magic, version);
    resetToDefaults();
    return saveConfig(); // Save defaults to EEPROM
  }

  // Load configuration from EEPROM
  readString(OFFSET_WIFI_SSID, config.wifi_ssid, SSID_MAX_LEN);
  readString(OFFSET_WIFI_PASSWORD, config.wifi_password, PASSWORD_MAX_LEN);
  readString(OFFSET_API_KEY, config.api_key, API_KEY_MAX_LEN);
  readString(OFFSET_DEVICE_NAME, config.device_name, DEVICE_NAME_MAX_LEN);

  config.use_static_ip = readUint8(OFFSET_USE_STATIC_IP);
  config.static_ip = readIPAddress(OFFSET_STATIC_IP);
  config.gateway = readIPAddress(OFFSET_GATEWAY);
  config.subnet = readIPAddress(OFFSET_SUBNET);
  config.dns_primary = readIPAddress(OFFSET_DNS_PRIMARY);
  config.dns_secondary = readIPAddress(OFFSET_DNS_SECONDARY);

  config.jpeg_quality = readUint8(OFFSET_JPEG_QUALITY);
  config.default_resolution = (framesize_t)readUint8(OFFSET_DEFAULT_RESOLUTION);
  config.flash_threshold = readUint8(OFFSET_FLASH_THRESHOLD);

  if (!validateConfiguration()) {
    Serial.println("Configuration validation failed, using defaults");
    resetToDefaults();
    return saveConfig();
  }

  config_loaded = true;
  Serial.println("Configuration loaded successfully");
  Serial.printf("WiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("Device Name: %s\n", config.device_name);
  Serial.printf("Static IP: %s\n",
                config.use_static_ip ? "Enabled" : "Disabled");

  return true;
}

bool ConfigManager::saveConfig() {
  Serial.println("Saving configuration to EEPROM...");

  // Write magic number and version
  writeUint16(OFFSET_MAGIC, CONFIG_MAGIC);
  writeUint16(OFFSET_VERSION, CONFIG_VERSION);

  // Write configuration data
  writeString(OFFSET_WIFI_SSID, config.wifi_ssid, SSID_MAX_LEN);
  writeString(OFFSET_WIFI_PASSWORD, config.wifi_password, PASSWORD_MAX_LEN);
  writeString(OFFSET_API_KEY, config.api_key, API_KEY_MAX_LEN);
  writeString(OFFSET_DEVICE_NAME, config.device_name, DEVICE_NAME_MAX_LEN);

  writeUint8(OFFSET_USE_STATIC_IP, config.use_static_ip ? 1 : 0);
  writeIPAddress(OFFSET_STATIC_IP, config.static_ip);
  writeIPAddress(OFFSET_GATEWAY, config.gateway);
  writeIPAddress(OFFSET_SUBNET, config.subnet);
  writeIPAddress(OFFSET_DNS_PRIMARY, config.dns_primary);
  writeIPAddress(OFFSET_DNS_SECONDARY, config.dns_secondary);

  writeUint8(OFFSET_JPEG_QUALITY, config.jpeg_quality);
  writeUint8(OFFSET_DEFAULT_RESOLUTION, (uint8_t)config.default_resolution);
  writeUint8(OFFSET_FLASH_THRESHOLD, config.flash_threshold);

  if (!EEPROM.commit()) {
    Serial.println("Failed to commit EEPROM changes");
    return false;
  }

  Serial.println("Configuration saved successfully");
  return true;
}

void ConfigManager::resetToDefaults() {
  Serial.println("Resetting configuration to defaults...");

  // Set default WiFi SSID and password
  strncpy(config.wifi_ssid, DEFAULT_SSID, SSID_MAX_LEN - 1);
  strncpy(config.wifi_password, DEFAULT_PASSWORD, PASSWORD_MAX_LEN - 1);

  // Use default API key
  strncpy(config.api_key, DEFAULT_API_KEY, API_KEY_MAX_LEN - 1);
  Serial.printf("Using default API key: %s\n", config.api_key);

  strncpy(config.device_name, DEFAULT_DEVICE_NAME, DEVICE_NAME_MAX_LEN - 1);

  // Default to DHCP (no static IP)
  config.use_static_ip = false;
  config.static_ip = IPAddress(0, 0, 0, 0);
  config.gateway = IPAddress(0, 0, 0, 0);
  config.subnet = IPAddress(0, 0, 0, 0);
  config.dns_primary = IPAddress(8, 8, 8, 8);
  config.dns_secondary = IPAddress(8, 8, 4, 4);

  config.jpeg_quality = DEFAULT_JPEG_QUALITY;
  config.default_resolution = DEFAULT_RESOLUTION;
  config.flash_threshold = DEFAULT_FLASH_THRESHOLD;

  // Null-terminate strings
  config.wifi_ssid[SSID_MAX_LEN - 1] = '\0';
  config.wifi_password[PASSWORD_MAX_LEN - 1] = '\0';
  config.api_key[API_KEY_MAX_LEN - 1] = '\0';
  config.device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
}

// Setters
bool ConfigManager::setWiFiCredentials(const char *ssid, const char *password) {
  if (!ssid || !password || strlen(ssid) >= SSID_MAX_LEN ||
      strlen(password) >= PASSWORD_MAX_LEN) {
    return false;
  }

  strncpy(config.wifi_ssid, ssid, SSID_MAX_LEN - 1);
  strncpy(config.wifi_password, password, PASSWORD_MAX_LEN - 1);
  config.wifi_ssid[SSID_MAX_LEN - 1] = '\0';
  config.wifi_password[PASSWORD_MAX_LEN - 1] = '\0';

  return true;
}

bool ConfigManager::setAPIKey(const char *key) {
  if (!key || strlen(key) >= API_KEY_MAX_LEN) {
    return false;
  }

  strncpy(config.api_key, key, API_KEY_MAX_LEN - 1);
  config.api_key[API_KEY_MAX_LEN - 1] = '\0';

  return true;
}

bool ConfigManager::setStaticIP(const IPAddress &ip, const IPAddress &gateway,
                                const IPAddress &subnet) {
  config.static_ip = ip;
  config.gateway = gateway;
  config.subnet = subnet;
  return true;
}

bool ConfigManager::setDNS(const IPAddress &primary,
                           const IPAddress &secondary) {
  config.dns_primary = primary;
  config.dns_secondary = secondary;
  return true;
}

bool ConfigManager::setDeviceName(const char *name) {
  if (!name || strlen(name) >= DEVICE_NAME_MAX_LEN) {
    return false;
  }

  strncpy(config.device_name, name, DEVICE_NAME_MAX_LEN - 1);
  config.device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';

  return true;
}

bool ConfigManager::setJPEGQuality(uint8_t quality) {
  if (quality > 63)
    return false; // JPEG quality range is 0-63
  config.jpeg_quality = quality;
  return true;
}

bool ConfigManager::setDefaultResolution(framesize_t resolution) {
  if (resolution < FRAMESIZE_96X96 || resolution > FRAMESIZE_UXGA) {
    return false;
  }
  config.default_resolution = resolution;
  return true;
}

bool ConfigManager::setFlashThreshold(uint8_t threshold) {
  config.flash_threshold = threshold;
  return true;
}

bool ConfigManager::setUseStaticIP(bool use_static) {
  config.use_static_ip = use_static;
  return true;
}

// Validation
bool ConfigManager::isValidConfig() const {
  return config_loaded && validateConfiguration();
}

bool ConfigManager::isAPIKeyValid(const char *provided_key) const {
  if (!provided_key || !config_loaded) {
    return false;
  }

  // Constant time comparison to prevent timing attacks
  size_t provided_len = strlen(provided_key);
  size_t config_len = strlen(config.api_key);

  if (provided_len != config_len) {
    return false;
  }

  uint8_t result = 0;
  for (size_t i = 0; i < config_len; i++) {
    result |= provided_key[i] ^ config.api_key[i];
  }

  return result == 0;
}

bool ConfigManager::isFirstBoot() const {
  uint16_t magic = const_cast<ConfigManager *>(this)->readUint16(OFFSET_MAGIC);
  return magic != CONFIG_MAGIC;
}

void ConfigManager::enterConfigMode() {
  Serial.println("Entering WiFi configuration mode...");
  WiFi.mode(WIFI_AP);

  // Use default password for config AP
  WiFi.softAP(DEFAULT_SSID, DEFAULT_PASSWORD);
  Serial.printf("Configuration AP started: SSID=%s, Password=%s\n",
                DEFAULT_SSID, DEFAULT_PASSWORD);
  Serial.printf("Connect and visit http://192.168.4.1 to configure\n");
  // Basic HTTP server for config (integrate with webServerManager)
  Serial.println("Basic config server starting...");
  delay(1000);
}

void ConfigManager::exitConfigMode() {
  Serial.println("Exiting configuration mode...");
  WiFi.mode(WIFI_OFF);
  Serial.println("Configuration mode exited.");

  // Save any pending config if implemented
  if (isValidConfig()) {
    saveConfig();
  }
}

// Private helper methods
void ConfigManager::writeString(int offset, const char *str, int max_len) {
  int len = strlen(str);
  if (len >= max_len)
    len = max_len - 1;

  for (int i = 0; i < len; i++) {
    EEPROM.write(offset + i, str[i]);
  }
  // Null terminate
  for (int i = len; i < max_len; i++) {
    EEPROM.write(offset + i, 0);
  }
}

void ConfigManager::readString(int offset, char *str, int max_len) {
  for (int i = 0; i < max_len; i++) {
    str[i] = EEPROM.read(offset + i);
    if (str[i] == 0)
      break;
  }
  str[max_len - 1] = '\0'; // Ensure null termination
}

void ConfigManager::writeUint8(int offset, uint8_t value) {
  EEPROM.write(offset, value);
}

uint8_t ConfigManager::readUint8(int offset) { return EEPROM.read(offset); }

void ConfigManager::writeUint16(int offset, uint16_t value) {
  EEPROM.write(offset, value & 0xFF);
  EEPROM.write(offset + 1, (value >> 8) & 0xFF);
}

uint16_t ConfigManager::readUint16(int offset) {
  uint16_t value = EEPROM.read(offset);
  value |= (EEPROM.read(offset + 1) << 8);
  return value;
}

void ConfigManager::writeIPAddress(int offset, const IPAddress &addr) {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(offset + i, addr[i]);
  }
}

IPAddress ConfigManager::readIPAddress(int offset) {
  return IPAddress(EEPROM.read(offset), EEPROM.read(offset + 1),
                   EEPROM.read(offset + 2), EEPROM.read(offset + 3));
}

bool ConfigManager::validateConfiguration() const {
  // Validate WiFi SSID is not empty
  if (strlen(config.wifi_ssid) == 0) {
    return false;
  }

  // Validate API key is not empty
  if (strlen(config.api_key) == 0) {
    return false;
  }

  // Validate JPEG quality range
  if (config.jpeg_quality > 63) {
    return false;
  }

  // Validate resolution enum
  if (config.default_resolution < FRAMESIZE_96X96 ||
      config.default_resolution > FRAMESIZE_UXGA) {
    return false;
  }

  // Validate static IP configuration if enabled
  if (config.use_static_ip) {
    if (config.static_ip == IPAddress(0, 0, 0, 0) ||
        config.gateway == IPAddress(0, 0, 0, 0) ||
        config.subnet == IPAddress(0, 0, 0, 0)) {
      return false;
    }
  }

  return true;
}

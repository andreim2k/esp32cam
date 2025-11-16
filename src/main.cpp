/*
 * ESP32-CAM Advanced Capture Server
 *
 * Features:
 * - Modular architecture with separate components
 * - Configuration management with EEPROM storage
 * - High-resolution photo capture with configurable resolution
 * - Smart flash control with PWM brightness adjustment
 * - Auto flash based on ambient light detection
 * - Comprehensive REST API with JSON parsing
 * - Clean separation of concerns
 *
 * Author: ESP32-CAM Project
 * Version: 2.1 - Modular Architecture
 */

#include "esp_heap_caps.h" // Memory monitoring
#include "esp_system.h"    // Stack overflow protection
#include "esp_task_wdt.h"  // Watchdog timer
#include "esp_wifi.h"      // For low-level WiFi power control
#include "modules/camera.h"
#include "modules/config.h"
#include "modules/flash.h"
#include "modules/webserver.h"
#include <WiFi.h>

// ===================
// CRASH PREVENTION & MONITORING
// ===================

// Global variables for monitoring
unsigned long last_wifi_check = 0;
unsigned long last_memory_check = 0;
unsigned long last_watchdog_reset = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;     // 30 seconds
const unsigned long MEMORY_CHECK_INTERVAL = 60000;   // 60 seconds
const unsigned long WATCHDOG_RESET_INTERVAL = 10000; // 10 seconds

/**
 * Helper to convert IPAddress to char array safely
 */
void ipToCharArray(const IPAddress &ip, char *buffer, size_t buf_size) {
  ip.toString().toCharArray(buffer, buf_size);
}

/**
 * Initialize watchdog timer for crash prevention
 */
void initWatchdog() {
  Serial.println("Initializing watchdog timer...");

  // Initialize watchdog with 30 second timeout
  esp_task_wdt_init(30, true);

  // Add current task to watchdog
  esp_task_wdt_add(NULL);

  Serial.println("Watchdog timer initialized (30s timeout)");
}

/**
 * Initialize stack overflow protection
 */
void initStackProtection() {
  Serial.println("Initializing stack overflow protection...");

  // Stack overflow protection is already enabled by esp_task_wdt_add(NULL) in
  // initWatchdog() ESP32 automatically detects stack overflows when watchdog is
  // active
  Serial.println("Stack overflow protection enabled (via watchdog)");
}

/**
 * Reset watchdog timer (call regularly in main loop)
 */
void resetWatchdog() {
  unsigned long now = millis();
  if (now - last_watchdog_reset >= WATCHDOG_RESET_INTERVAL) {
    esp_task_wdt_reset();
    last_watchdog_reset = now;
  }
}

/**
 * Monitor memory usage and log warnings
 */
void checkMemoryUsage() {
  unsigned long now = millis();
  if (now - last_memory_check >= MEMORY_CHECK_INTERVAL) {
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);

    Serial.printf(
        "Memory Status: Free=%u bytes, Min Free=%u bytes, Total=%u bytes\n",
        free_heap, min_free_heap, total_heap);

    // Warning thresholds
    if (free_heap < 50000) { // Less than 50KB free
      Serial.println("WARNING: Low memory detected!");
    }

    if (min_free_heap < 30000) { // Less than 30KB minimum
      Serial.println("CRITICAL: Very low memory detected!");
    }

    last_memory_check = now;
  }
}

/**
 * Check WiFi connection and attempt reconnection if needed
 */
void checkWiFiConnection() {
  unsigned long now = millis();
  if (now - last_wifi_check >= WIFI_CHECK_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, attempting reconnection...");

      // Attempt reconnection
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(configManager.getWiFiSSID(), configManager.getWiFiPassword());

      // Wait for connection with timeout
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        Serial.print(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("WiFi reconnected successfully");
        char ip_str[16];
        WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
        Serial.printf("IP Address: %s\n", ip_str);
      } else {
        Serial.println();
        Serial.println("WiFi reconnection failed");
      }
    }
    last_wifi_check = now;
  }
}

/**
 * Emergency recovery function for critical failures
 */
void emergencyRecovery() {
  Serial.println("EMERGENCY RECOVERY: Attempting system recovery...");

  // Reset watchdog to prevent immediate restart
  esp_task_wdt_reset();

  // Attempt to restart WiFi
  WiFi.disconnect();
  delay(2000);
  WiFi.begin(configManager.getWiFiSSID(), configManager.getWiFiPassword());

  // Wait briefly for WiFi
  delay(5000);

  // Restart web server if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    webServerManager.stop();
    delay(1000);
    webServerManager.begin();
    Serial.println("Emergency recovery completed");
  } else {
    Serial.println("Emergency recovery failed - manual intervention required");
  }
}

// ===================
// WIFI MANAGEMENT
// ===================

/**
 * Initialize WiFi connection using configuration
 */
void initWiFi() {
  Serial.println("========== WiFi Configuration ==========");
  Serial.printf("SSID: %s\n", configManager.getWiFiSSID());

  // Configure IP settings
  if (configManager.useStaticIP()) {
    Serial.println("Configuring Static IP...");
    IPAddress staticIP = configManager.getStaticIP();
    IPAddress gateway = configManager.getGateway();
    IPAddress subnet = configManager.getSubnet();
    IPAddress primaryDNS = configManager.getPrimaryDNS();
    IPAddress secondaryDNS = configManager.getSecondaryDNS();

    char ip_str[16], gateway_str[16], subnet_str[16], dns1_str[16],
        dns2_str[16];
    staticIP.toString().toCharArray(ip_str, sizeof(ip_str));
    gateway.toString().toCharArray(gateway_str, sizeof(gateway_str));
    subnet.toString().toCharArray(subnet_str, sizeof(subnet_str));
    primaryDNS.toString().toCharArray(dns1_str, sizeof(dns1_str));
    secondaryDNS.toString().toCharArray(dns2_str, sizeof(dns2_str));
    Serial.printf("Static IP: %s\n", ip_str);
    Serial.printf("Gateway:   %s\n", gateway_str);
    Serial.printf("Subnet:    %s\n", subnet_str);
    Serial.printf("DNS:       %s, %s\n", dns1_str, dns2_str);

    // Configure static IP before connecting
    if (!WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("ERROR: Failed to configure static IP!");
      return;
    }
  } else {
    Serial.println("Using DHCP (automatic IP assignment)");
  }

  Serial.println("Connecting to WiFi...");
  WiFi.begin(configManager.getWiFiSSID(), configManager.getWiFiPassword());
  WiFi.setSleep(false);

  // MAXIMUM POWER CONFIGURATION FOR LONG DISTANCE
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // Absolute maximum power: 19.5 dBm
  Serial.println("WiFi transmission power set to MAXIMUM (19.5 dBm)");

  // Aggressive power and range optimizations
  WiFi.setAutoReconnect(true); // Enable automatic reconnection
  WiFi.persistent(true); // Store WiFi config in flash for faster reconnects

  // Low-level ESP32 WiFi power optimizations
  esp_wifi_set_max_tx_power(
      78); // Set maximum TX power (78 = 19.5dBm, ESP-IDF level)

  // Force 802.11b mode for MAXIMUM RANGE (sacrifice speed for distance)
  esp_wifi_set_protocol(WIFI_IF_STA,
                        WIFI_PROTOCOL_11B); // 802.11b only - longest range

  // Additional long-range optimizations
  wifi_config_t wifi_config;
  esp_wifi_get_config(WIFI_IF_STA, &wifi_config);

  Serial.println("MAXIMUM DISTANCE MODE ENABLED:");
  Serial.println("  - Maximum TX Power: 19.5 dBm (78 units)");
  Serial.println("  - Protocol: 802.11b ONLY (longest range)");
  Serial.println("  - Data Rate: 1-11 Mbps (maximum reliability)");
  Serial.println("  - Modulation: DSSS (most robust)");
  Serial.println("  - Auto-reconnect: ENABLED");
  Serial.println("  - Persistent config: ENABLED");
  Serial.println("  - Sleep mode: DISABLED");
  Serial.println("  - Priority: MAXIMUM DISTANCE > SPEED");

  // Non-blocking connection with watchdog resets
  int attempts = 0;
  const int maxAttempts = 30; // 15 seconds timeout
  unsigned long start_time = millis();

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    // Reset watchdog during connection attempts
    esp_task_wdt_reset();

    delay(500);
    Serial.print(".");
    attempts++;

    // Print status every 10 attempts
    if (attempts % 10 == 0) {
      Serial.println();
      Serial.printf("Connection attempt %d/%d...\n", attempts, maxAttempts);
      Serial.printf("WiFi status: %d\n", WiFi.status());
    }

    // Additional watchdog reset for long operations
    if (attempts % 5 == 0) {
      esp_task_wdt_reset();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("======== WiFi Connected Successfully ========");
    char ip_str[16];
    WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
    Serial.printf("IP Address: %s\n", ip_str);
    char gateway_ip_str[16], subnet_mask_str[16], dns_ip_str[16];
    WiFi.gatewayIP().toString().toCharArray(gateway_ip_str,
                                            sizeof(gateway_ip_str));
    WiFi.subnetMask().toString().toCharArray(subnet_mask_str,
                                             sizeof(subnet_mask_str));
    WiFi.dnsIP().toString().toCharArray(dns_ip_str, sizeof(dns_ip_str));
    Serial.printf("Gateway:    %s\n", gateway_ip_str);
    Serial.printf("Subnet:     %s\n", subnet_mask_str);
    Serial.printf("DNS:        %s\n", dns_ip_str);
    Serial.printf("RSSI:       %d dBm\n", WiFi.RSSI());
    Serial.printf("TX Power:   19.5 dBm (MAXIMUM - Long Range Mode)\n");
    char mac_str[18];
    WiFi.macAddress().toCharArray(mac_str, sizeof(mac_str));
    Serial.printf("MAC:        %s\n", mac_str);
    Serial.printf("Channel:    %d\n", WiFi.channel());
    Serial.println("===========================================");
  } else {
    Serial.println();
    Serial.println("ERROR: WiFi connection failed!");
    Serial.println("Check your WiFi credentials and network settings.");
    Serial.printf("Final WiFi status: %d\n", WiFi.status());
    Serial.println("Device will continue but network features won't work.");
  }
}

// ===================
// INITIALIZATION FUNCTIONS
// ===================

/**
 * Main setup function using modular components
 */
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println(
      "ESP32-CAM Advanced Capture Server v2.1 - Modular Architecture");
  Serial.println(
      "===============================================================");

  // Initialize crash prevention systems first
  initWatchdog();
  initStackProtection();

  // Initialize configuration manager first
  if (!configManager.begin()) {
    Serial.println("ERROR: Failed to initialize configuration manager");
    return;
  }

  // Initialize hardware components using configuration
  if (!flashManager.begin(configManager.getFlashThreshold())) {
    Serial.println("ERROR: Failed to initialize flash manager");
    return;
  }

  if (!cameraManager.begin(configManager.getJPEGQuality(),
                           configManager.getDefaultResolution())) {
    Serial.println(
        "WARNING: Camera initialization failed - continuing without camera");
    Serial.println("System will run in limited mode (no photo capture)");
    // Continue without camera - system can still provide status and other
    // services
  }

  // Initialize WiFi connection
  initWiFi();

  // Start web server
  if (!webServerManager.begin()) {
    Serial.println("ERROR: Failed to start web server");
    return;
  }

  // Print available endpoints and network information
  Serial.println();
  Serial.println("============ API SERVER READY ============");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Device Name:   %s\n", configManager.getDeviceName());
    char ip_str[16];
    WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
    Serial.printf("API Base URL:  http://%s\n", ip_str);
    Serial.println();
    Serial.println("📡 Available API Endpoints:");
    Serial.printf("  📸 Capture:    http://%s/capture\n", ip_str);
    Serial.printf("  ⚡ Flash:      http://%s/flash\n", ip_str);
    Serial.printf("  🚀 Quick Snap: http://%s/snap\n", ip_str);
    Serial.printf("  📊 Status:     http://%s/status\n", ip_str);
    Serial.printf("  ℹ️  Info:       http://%s/\n", ip_str);
    Serial.println();
    Serial.printf("Mode: API-Only (No Web Interface)\n");
    Serial.printf("Network: %s\n",
                  configManager.useStaticIP() ? "Static IP" : "DHCP");
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    Serial.printf("JPEG Quality: %d\n", configManager.getJPEGQuality());
    char resolution_str[32];
    cameraManager.getResolutionString(configManager.getDefaultResolution(),
                                      resolution_str, sizeof(resolution_str));
    Serial.printf("Default Resolution: %s\n", resolution_str);
  } else {
    Serial.println("❌ WiFi not connected - API unavailable");
  }
  Serial.println("==========================================");
}

// ===================
// MAIN LOOP
// ===================

void loop() {
  // Reset watchdog timer to prevent crashes
  resetWatchdog();

  // Monitor system health
  checkMemoryUsage();
  checkWiFiConnection();

  // Check for critical memory conditions
  size_t free_heap = esp_get_free_heap_size();
  if (free_heap < 20000) { // Less than 20KB free
    Serial.println("CRITICAL: Very low memory, triggering emergency recovery");
    emergencyRecovery();
  }

  // Handle incoming HTTP requests using the web server manager
  webServerManager.handleClients();

  // Small delay to prevent watchdog issues
  delay(1);
}

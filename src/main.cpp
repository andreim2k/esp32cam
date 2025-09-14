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

#include <WiFi.h>
#include "modules/config.h"
#include "modules/camera.h"
#include "modules/flash.h"
#include "modules/webserver.h"

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
    
    Serial.printf("Static IP: %s\n", staticIP.toString().c_str());
    Serial.printf("Gateway:   %s\n", gateway.toString().c_str());
    Serial.printf("Subnet:    %s\n", subnet.toString().c_str());
    Serial.printf("DNS:       %s, %s\n", primaryDNS.toString().c_str(), secondaryDNS.toString().c_str());
    
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

  // Connection timeout and status monitoring
  int attempts = 0;
  const int maxAttempts = 30; // 15 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Print status every 10 attempts
    if (attempts % 10 == 0) {
      Serial.println();
      Serial.printf("Connection attempt %d/%d...\n", attempts, maxAttempts);
      Serial.printf("WiFi status: %d\n", WiFi.status());
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("======== WiFi Connected Successfully ========");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway:    %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet:     %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("DNS:        %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("RSSI:       %d dBm\n", WiFi.RSSI());
    Serial.printf("MAC:        %s\n", WiFi.macAddress().c_str());
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
  Serial.println("ESP32-CAM Advanced Capture Server v2.1 - Modular Architecture");
  Serial.println("===============================================================");
  
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
  
  if (!cameraManager.begin(configManager.getJPEGQuality(), configManager.getDefaultResolution())) {
    Serial.println("ERROR: Failed to initialize camera manager");
    return;
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
    Serial.printf("API Base URL:  http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println();
    Serial.println("📡 Available API Endpoints:");
    Serial.printf("  📸 Capture:    http://%s/capture\n", WiFi.localIP().toString().c_str());
    Serial.printf("  ⚡ Flash:      http://%s/flash\n", WiFi.localIP().toString().c_str());
    Serial.printf("  🚀 Quick Snap: http://%s/snap\n", WiFi.localIP().toString().c_str());
    Serial.printf("  📊 Status:     http://%s/status\n", WiFi.localIP().toString().c_str());
    Serial.printf("  ℹ️  Info:       http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println();
    Serial.printf("Mode: API-Only (No Web Interface)\n");
    Serial.printf("Network: %s\n", configManager.useStaticIP() ? "Static IP" : "DHCP");
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    Serial.printf("JPEG Quality: %d\n", configManager.getJPEGQuality());
    Serial.printf("Default Resolution: %s\n", 
                 cameraManager.getResolutionString(configManager.getDefaultResolution()).c_str());
  } else {
    Serial.println("❌ WiFi not connected - API unavailable");
  }
  Serial.println("==========================================");
}

// ===================
// MAIN LOOP
// ===================

void loop() {
  // Handle incoming HTTP requests using the web server manager
  webServerManager.handleClients();
  
  // Small delay to prevent watchdog issues
  delay(1);
}

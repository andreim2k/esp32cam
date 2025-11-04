void ConfigManager::enterConfigMode() {
  Serial.println("Entering WiFi configuration mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32CAM_Config", "configure123");
  Serial.printf("Configuration AP started: SSID=ESP32CAM_Config, Password=configure123\n");
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
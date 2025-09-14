#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "camera.h"
#include "flash.h"
#include "config.h"

#define HTTP_BUFFER_SIZE 1024
#define JSON_BUFFER_SIZE 2048

enum RequestType {
  REQ_GET,
  REQ_POST,
  REQ_UNKNOWN
};

struct HttpRequest {
  RequestType type;
  String path;
  String query_params;
  String headers;
  String body;
  bool has_content_length;
  int content_length;
};

struct ApiResponse {
  int status_code;
  String content_type;
  String body;
  size_t content_length;
  uint8_t* binary_data;
  bool is_binary;
};

class WebServerManager {
public:
  WebServerManager();
  
  // Server management
  bool begin(uint16_t port = 80);
  void handleClients();
  bool isRunning() const { return server_running; }
  void stop();
  
  // Request handling
  void handleClient(WiFiClient& client);
  bool parseHttpRequest(WiFiClient& client, HttpRequest& request);
  ApiResponse processRequest(const HttpRequest& request);
  void sendResponse(WiFiClient& client, const ApiResponse& response);
  
  // API endpoints
  ApiResponse handleRoot();
  ApiResponse handleStatus();
  ApiResponse handleSnapshot(const HttpRequest& request);
  ApiResponse handle404();
  
  // JSON utilities
  String createJsonResponse(const char* status, const JsonDocument& data = JsonDocument());
  String createErrorResponse(const char* error, int code = 500);
  bool parseJsonBody(const String& body, JsonDocument& doc);
  
  // Web page generation
  String generateWebPage();
  
  // Camera settings from JSON
  bool parseRequestSettings(const JsonDocument& json, CameraSettings& settings, bool& use_flash);
  
  // Statistics
  uint32_t getTotalRequests() const { return total_requests; }
  uint32_t getErrorRequests() const { return error_requests; }
  unsigned long getLastRequestTime() const { return last_request_time; }

private:
  WiFiServer* server;
  bool server_running;
  uint16_t server_port;
  uint32_t total_requests;
  uint32_t error_requests;
  unsigned long last_request_time;
  
  // Internal helpers
  void logRequest(const HttpRequest& request);
  void logResponse(const ApiResponse& response);
  String extractQueryParam(const String& query_params, const String& param_name);
  String urlDecode(const String& str);
  void addCorsHeaders(String& headers);
  void generateDeviceInfo(JsonDocument& doc);
  void generateStatusJson(JsonDocument& doc);
};

// Global web server manager instance
extern WebServerManager webServerManager;

#endif // WEBSERVER_H
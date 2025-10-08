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
#define MAX_PATH_LENGTH 256
#define MAX_HEADERS_LENGTH 2048
#define MAX_BODY_LENGTH 4096
#define MAX_QUERY_LENGTH 512

enum RequestType {
  REQ_GET,
  REQ_POST,
  REQ_UNKNOWN
};

struct HttpRequest {
  RequestType type;
  char path[MAX_PATH_LENGTH];
  char query_params[MAX_QUERY_LENGTH];
  char headers[MAX_HEADERS_LENGTH];
  char body[MAX_BODY_LENGTH];
  bool has_content_length;
  int content_length;
};

struct ApiResponse {
  int status_code;
  char content_type[64];
  char body[JSON_BUFFER_SIZE];
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
  void createJsonResponse(const char* status, JsonDocument& data, char* output, size_t max_len);
  void createErrorResponse(const char* error, int code, char* output, size_t max_len);
  bool parseJsonBody(const char* body, JsonDocument& doc);
  
  // Web page generation
  void generateWebPage(char* output, size_t max_len);
  
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
  void extractQueryParam(const char* query_params, const char* param_name, char* output, size_t max_len);
  void urlDecode(const char* str, char* output, size_t max_len);
  void addCorsHeaders(char* headers, size_t max_len);
  void generateDeviceInfo(JsonDocument& doc);
  void generateStatusJson(JsonDocument& doc);
  
  // WiFi protocol detection helpers
  void getWiFiProtocol(char* output, size_t max_len);
  void getWiFiConnectionSpeed(char* output, size_t max_len);
  void getWiFiBandwidth(char* output, size_t max_len);
  int getWiFiSignalPercentage();
};

// Global web server manager instance
extern WebServerManager webServerManager;

#endif // WEBSERVER_H
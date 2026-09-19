#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../include/httplib.h"
#include "../include/json.hpp"

// Forward declarations
class EntropyCollector;
class Logger;

/**
 * @brief HTTP Server for the PseudoQuantum Entropy Service
 *
 * Provides REST API endpoints for:
 * - Entropy retrieval
 * - Cryptographic key generation
 * - AES-GCM encryption/decryption
 * - Statistical analysis
 * - System status
 * - Configuration
 *
 * Uses cpp-httplib for HTTP handling.
 */
class WebServer {
public:
  /**
   * @brief Constructor
   * @param entropyCollector Reference to the entropy collector
   * @param port HTTP server port (default: 8080)
   * @param frontendPath Path to frontend static files
   */
  WebServer(EntropyCollector &entropyCollector, unsigned int port = 8080,
            const std::string &frontendPath = "../../frontend");

  /**
   * @brief Destructor
   */
  ~WebServer();

  /**
   * @brief Start the HTTP server
   * @return true if started successfully
   */
  bool start();

  /**
   * @brief Stop the HTTP server
   */
  void stop();

  /**
   * @brief Check if the server is running
   * @return true if running
   */
  bool isRunning() const;

  /**
   * @brief Get the server port
   * @return unsigned int Port number
   */
  unsigned int getPort() const;

  /**
   * @brief Set the server port
   * @param port Port number
   */
  void setPort(unsigned int newPort);

  /**
   * @brief Get the frontend path
   * @return std::string Frontend path
   */
  std::string getFrontendPath() const;

  /**
   * @brief Set the frontend path
   * @param path Frontend path
   */
  void setFrontendPath(const std::string &path);

  /**
   * @brief Get the number of active connections
   * @return size_t Connection count
   */
  size_t getActiveConnections() const;

  /**
   * @brief Get the total number of requests served
   * @return size_t Request count
   */
  size_t getTotalRequests() const;

private:
  /**
   * @brief Setup all HTTP routes
   */
  void setupRoutes();

  /**
   * @brief Serve static files
   * @param req HTTP request
   * @param res HTTP response
   */
  void serveStaticFile(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle GET /status
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleStatus(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle GET /entropy
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleGetEntropy(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /keygen
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleKeyGen(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /encrypt
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleEncrypt(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /decrypt
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleDecrypt(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /test
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleTest(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /settings
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleSettings(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle GET /settings
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleGetSettings(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle POST /reseed
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleReseed(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Handle GET /health
   * @param req HTTP request
   * @param res HTTP response
   */
  void handleHealth(const httplib::Request &req, httplib::Response &res);

  /**
   * @brief Create a JSON error response
   * @param code Error code
   * @param message Error message
   * @return std::string JSON response
   */
  std::string errorResponse(int code, const std::string &message);

  /**
   * @brief Create a JSON success response
   * @param data Data to include
   * @return std::string JSON response
   */
  std::string successResponse(const nlohmann::json &data);

  /**
   * @brief Log a request
   * @param method HTTP method
   * @param path Request path
   * @param status Response status
   */
  void logRequest(const std::string &method, const std::string &path,
                  int status);

  /**
   * @brief Get a random nonce/IV as hex string
   * @param length Number of bytes
   * @return std::string Hex string
   */
  std::string generateRandomHex(size_t length);

  /**
   * @brief Hex encode bytes
   * @param data Bytes to encode
   * @return std::string Hex string
   */
  std::string hexEncode(const std::vector<uint8_t> &data);

  /**
   * @brief Hex decode string
   * @param hex Hex string to decode
   * @return std::vector<uint8_t> Decoded bytes
   */
  std::vector<uint8_t> hexDecode(const std::string &hex);

  /**
   * @brief Base64 encode bytes
   * @param data Bytes to encode
   * @return std::string Base64 string
   */
  std::string base64Encode(const std::vector<uint8_t> &data);

  /**
   * @brief Base64 decode string
   * @param encoded Base64 string to decode
   * @return std::vector<uint8_t> Decoded bytes
   */
  std::vector<uint8_t> base64Decode(const std::string &encoded);

  // References
  EntropyCollector &entropyCollector;

  // Server configuration
  unsigned int port;
  std::string frontendPath;
  std::string host;

  // Server instance
  std::unique_ptr<httplib::Server> server;
  std::atomic<bool> running;
  std::thread serverThread;

  // Statistics
  std::atomic<size_t> activeConnections;
  std::atomic<size_t> totalRequests;

  // Thread safety
  mutable std::mutex mutex;

  // Constants
  static constexpr const char *API_VERSION = "v0.2.alpha.build.01";
  static constexpr size_t MAX_ENTROPY_REQUEST = 1024 * 1024;  // 1MB
  static constexpr size_t MAX_UPLOAD_SIZE = 10 * 1024 * 1024; // 10MB
  static constexpr unsigned int DEFAULT_PORT = 8080;
};

// Include httplib must be in the implementation, but we need the forward
// declaration The actual include will be in WebServer.cpp
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include "json.hpp"

/**
 * @brief Configuration manager for the PseudoQuantum Entropy Service
 * 
 * Features:
 * - JSON-based configuration file
 * - Default values for all settings
 * - Environment variable overrides (optional)
 * - Thread-safe access
 * - Runtime updates
 * - Validation
 * 
 * Usage:
 * @code
 * Config& config = Config::instance();
 * config.load("config/backend_config.json");
 * std::string port = config.get<std::string>("serial.port");
 * uint32_t baudRate = config.get<uint32_t>("serial.baud_rate");
 * @endcode
 */
class Config {
public:
    /**
     * @brief Get the singleton instance
     * @return Config& Reference to the config instance
     */
    static Config& instance();

    /**
     * @brief Load configuration from a JSON file
     * 
     * @param filename Path to configuration file
     * @return true if loaded successfully
     */
    bool load(const std::string& filename);

    /**
     * @brief Merge configuration from a JSON file into the existing config
     * 
     * @param filename Path to configuration file
     * @return true if merged successfully
     */
    bool merge(const std::string& filename);

    /**
     * @brief Load configuration from a JSON string
     * 
     * @param jsonString JSON string containing configuration
     * @return true if loaded successfully
     */
    bool loadFromString(const std::string& jsonString);

    /**
     * @brief Save current configuration to a JSON file
     * 
     * @param filename Path to save configuration to
     * @return true if saved successfully
     */
    bool save(const std::string& filename) const;

    /**
     * @brief Save current configuration to a JSON string
     * 
     * @return std::string JSON string
     */
    std::string saveToString() const;

    /**
     * @brief Reload configuration from the last loaded file
     * 
     * @return true if reloaded successfully
     */
    bool reload();

    /**
     * @brief Reset configuration to default values
     */
    void resetToDefaults();

    /**
     * @brief Get a configuration value by key
     * 
     * @tparam T Type of the value (string, int, uint32_t, bool, etc.)
     * @param key Dot-separated key path (e.g., "serial.port")
     * @param defaultValue Default value if key doesn't exist
     * @return T The configuration value
     */
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T()) const;

    /**
     * @brief Get a configuration value by key (must exist)
     * 
     * @tparam T Type of the value
     * @param key Dot-separated key path
     * @return T The configuration value
     * @throws std::runtime_error if key doesn't exist
     */
    template<typename T>
    T getRequired(const std::string& key) const;

    /**
     * @brief Set a configuration value by key
     * 
     * @tparam T Type of the value
     * @param key Dot-separated key path
     * @param value Value to set
     */
    template<typename T>
    void set(const std::string& key, const T& value);

    /**
     * @brief Check if a key exists in the configuration
     * 
     * @param key Dot-separated key path
     * @return true if key exists
     */
    bool hasKey(const std::string& key) const;

    /**
     * @brief Get all keys in the configuration
     * 
     * @return std::vector<std::string> List of all keys
     */
    std::vector<std::string> getAllKeys() const;

    /**
     * @brief Get the path to the current configuration file
     * 
     * @return std::string File path
     */
    std::string getConfigFilePath() const;

    /**
     * @brief Check if configuration has been loaded
     * 
     * @return true if loaded
     */
    bool isLoaded() const;

    /**
     * @brief Validate the current configuration
     * 
     * @return true if valid
     */
    bool validate() const;

    /**
     * @brief Get validation errors
     * 
     * @return std::vector<std::string> List of error messages
     */
    std::vector<std::string> getValidationErrors() const;

    // Convenience getters for common configuration values

    /**
     * @brief Get the serial port name
     * @return std::string Port name (e.g., "COM3", "/dev/ttyUSB0")
     */
    std::string getSerialPort() const;

    /**
     * @brief Get the serial baud rate
     * @return unsigned int Baud rate (e.g., 921600)
     */
    unsigned int getSerialBaudRate() const;

    /**
     * @brief Get the serial timeout in milliseconds
     * @return unsigned int Timeout in ms
     */
    unsigned int getSerialTimeout() const;

    /**
     * @brief Get the entropy source type
     * @return std::string "hardware" or "openssl"
     */
    std::string getEntropySource() const;

    /**
     * @brief Get the entropy pool buffer size
     * @return size_t Buffer size in bytes
     */
    size_t getPoolBufferSize() const;

    /**
     * @brief Get the entropy pool refill threshold
     * @return size_t Refill threshold in bytes
     */
    size_t getPoolRefillThreshold() const;

    /**
     * @brief Get the HTTP server port
     * @return unsigned int Port number
     */
    unsigned int getHttpPort() const;

    /**
     * @brief Get the log file path
     * @return std::string Log file path
     */
    std::string getLogFilePath() const;

    /**
     * @brief Get the log level
     * @return std::string Log level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
     */
    std::string getLogLevel() const;

    /**
     * @brief Get the test mode for statistical analysis
     * @return std::string "quick" or "full"
     */
    std::string getTestMode() const;

    /**
     * @brief Get the path to the static frontend files
     * @return std::string Frontend path
     */
    std::string getFrontendPath() const;

private:
    /**
     * @brief Private constructor (singleton)
     */
    Config();

    /**
     * @brief Private destructor
     */
    ~Config() = default;

    /**
     * @brief Parse a JSON string into the configuration map
     * 
     * @param jsonString JSON string to parse
     * @return true if parsed successfully
     */
    bool parseJson(const std::string& jsonString);

    /**
     * @brief Flatten a nested JSON object into dot-separated keys
     * 
     * @param prefix Current key prefix
     * @param value JSON value to flatten
     */
    void flattenJson(const std::string& prefix, const nlohmann::json& value);

    /**
     * @brief Get a JSON value by key
     * 
     * @param key Dot-separated key path
     * @return nlohmann::json JSON value
     * @throws std::runtime_error if key doesn't exist
     */
    nlohmann::json getJsonValue(const std::string& key) const;

    /**
     * @brief Set a JSON value by key
     * 
     * @param key Dot-separated key path
     * @param value JSON value to set
     */
    void setJsonValue(const std::string& key, const nlohmann::json& value);

    /**
     * @brief Create default configuration
     */
    void createDefaults();

    /**
     * @brief Validate a specific configuration value
     * 
     * @param key Key to validate
     * @param value Value to validate
     * @return true if valid
     */
    bool validateValue(const std::string& key, const nlohmann::json& value) const;

    // Configuration storage
    nlohmann::json config;
    std::string configFilePath;
    bool loaded;
    mutable std::mutex mutex;

    // Validation errors
    mutable std::vector<std::string> validationErrors;

    // Default configuration
    static nlohmann::json createDefaultConfig();

    // Constants
    static constexpr const char* DEFAULT_CONFIG_FILE = "config/backend_config.json";
};

// Template implementations (must be in header)

template<typename T>
T Config::get(const std::string& key, const T& defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        auto jsonValue = getJsonValue(key);
        return jsonValue.get<T>();
    } catch (const std::exception&) {
        return defaultValue;
    }
}

template<typename T>
T Config::getRequired(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        auto jsonValue = getJsonValue(key);
        return jsonValue.get<T>();
    } catch (const std::exception& e) {
        throw std::runtime_error("Required configuration key missing: " + key + " - " + e.what());
    }
}

template<typename T>
void Config::set(const std::string& key, const T& value) {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        nlohmann::json jsonValue = value;
        setJsonValue(key, jsonValue);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to set configuration key: " + key + " - " + e.what());
    }
}
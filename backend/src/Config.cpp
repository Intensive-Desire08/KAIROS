#include "Config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>

// Singleton instance
Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config()
    : loaded(false) {
    createDefaults();
}

bool Config::load(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[Config] Failed to open config file: " << filename << std::endl;
            return false;
        }
        
        nlohmann::json jsonData;
        file >> jsonData;
        file.close();
        
        {
            std::lock_guard<std::mutex> lock(mutex);
            // Flatten the JSON
            config = nlohmann::json::object();
            flattenJson("", jsonData);
            
            configFilePath = filename;
            loaded = true;
        }

        // Validate after load, outside the mutex to avoid re-entrant locking
        validate();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Config] Failed to load config: " << e.what() << std::endl;
        return false;
    }
}

bool Config::merge(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        nlohmann::json jsonData;
        file >> jsonData;
        file.close();
        
        {
            std::lock_guard<std::mutex> lock(mutex);
            // Flatten the JSON into the existing config
            flattenJson("", jsonData);
        }

        // Validate after load
        validate();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Config] Failed to merge config: " << e.what() << std::endl;
        return false;
    }
}

bool Config::loadFromString(const std::string& jsonString) {
    try {
        nlohmann::json jsonData = nlohmann::json::parse(jsonString);
        
        {
            std::lock_guard<std::mutex> lock(mutex);
            // Flatten the JSON
            config = nlohmann::json::object();
            flattenJson("", jsonData);
            
            loaded = true;
            configFilePath = "";
        }

        // Validate after load, outside the mutex to avoid re-entrant locking
        validate();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Config] Failed to parse config JSON: " << e.what() << std::endl;
        return false;
    }
}

bool Config::save(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        // Unflatten the configuration
        nlohmann::json unflattened = nlohmann::json::object();
        for (auto it = config.begin(); it != config.end(); ++it) {
            const std::string key = it.key();
            const nlohmann::json& value = it.value();

            // Parse key into path segments
            std::vector<std::string> segments;
            size_t start = 0;
            while (start <= key.size()) {
                size_t pos = key.find('.', start);
                if (pos == std::string::npos) {
                    segments.push_back(key.substr(start));
                    break;
                }
                segments.push_back(key.substr(start, pos - start));
                start = pos + 1;
            }
            
            // Build nested JSON
            nlohmann::json* current = &unflattened;
            for (size_t i = 0; i < segments.size(); ++i) {
                if (i == segments.size() - 1) {
                    (*current)[segments[i]] = value;
                } else {
                    if (!current->contains(segments[i])) {
                        (*current)[segments[i]] = nlohmann::json::object();
                    }
                    current = &(*current)[segments[i]];
                }
            }
        }
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << unflattened.dump(4); // Pretty print with 4 spaces
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[Config] Failed to save config: " << e.what() << std::endl;
        return false;
    }
}

std::string Config::saveToString() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        // Unflatten the configuration
        nlohmann::json unflattened = nlohmann::json::object();
        for (auto it = config.begin(); it != config.end(); ++it) {
            const std::string key = it.key();
            const nlohmann::json& value = it.value();

            // Parse key into path segments
            std::vector<std::string> segments;
            size_t start = 0;
            while (start <= key.size()) {
                size_t pos = key.find('.', start);
                if (pos == std::string::npos) {
                    segments.push_back(key.substr(start));
                    break;
                }
                segments.push_back(key.substr(start, pos - start));
                start = pos + 1;
            }
            
            // Build nested JSON
            nlohmann::json* current = &unflattened;
            for (size_t i = 0; i < segments.size(); ++i) {
                if (i == segments.size() - 1) {
                    (*current)[segments[i]] = value;
                } else {
                    if (!current->contains(segments[i])) {
                        (*current)[segments[i]] = nlohmann::json::object();
                    }
                    current = &(*current)[segments[i]];
                }
            }
        }
        
        return unflattened.dump(4);
        
    } catch (const std::exception& e) {
        std::cerr << "[Config] Failed to save config to string: " << e.what() << std::endl;
        return "{}";
    }
}

bool Config::reload() {
    if (configFilePath.empty()) {
        return false;
    }
    return load(configFilePath);
}

void Config::resetToDefaults() {
    std::lock_guard<std::mutex> lock(mutex);
    config = createDefaultConfig();
    loaded = true;
    validationErrors.clear();
}

bool Config::hasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex);
    return config.contains(key);
}

std::vector<std::string> Config::getAllKeys() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::vector<std::string> keys;
    for (auto& [key, _] : config.items()) {
        keys.push_back(key);
    }
    return keys;
}

std::string Config::getConfigFilePath() const {
    std::lock_guard<std::mutex> lock(mutex);
    return configFilePath;
}

bool Config::isLoaded() const {
    std::lock_guard<std::mutex> lock(mutex);
    return loaded;
}

bool Config::validate() const {
    validationErrors.clear();
    
    // Validate serial configuration
    if (hasKey("serial.port")) {
        std::string port = get<std::string>("serial.port", "");
        if (port.empty()) {
            validationErrors.push_back("serial.port cannot be empty");
        }
    }
    
    if (hasKey("serial.baud_rate")) {
        unsigned int baud = get<unsigned int>("serial.baud_rate", 0);
        std::vector<unsigned int> validBaudRates = {9600, 19200, 38400, 57600, 115200, 230400};
        if (std::find(validBaudRates.begin(), validBaudRates.end(), baud) == validBaudRates.end()) {
            validationErrors.push_back("serial.baud_rate must be a valid value (9600, 19200, 38400, 57600, 115200, 230400)");
        }
    }
    
    // Validate entropy source
    if (hasKey("entropy.source")) {
        std::string source = get<std::string>("entropy.source", "");
        if (source != "hardware" && source != "openssl" && source != "auto") {
            validationErrors.push_back("entropy.source must be 'hardware', 'openssl', or 'auto'");
        }
    }
    
    // Validate pool size
    if (hasKey("entropy.pool_buffer_size")) {
        size_t size = get<size_t>("entropy.pool_buffer_size", 0);
        if (size < 256 || size > 1024 * 1024) {
            validationErrors.push_back("entropy.pool_buffer_size must be between 256 and 1048576");
        }
    }
    
    // Validate HTTP port
    if (hasKey("http.port")) {
        unsigned int port = get<unsigned int>("http.port", 0);
        if (port < 1024 || port > 65535) {
            validationErrors.push_back("http.port must be between 1024 and 65535");
        }
    }
    
    // Validate log level
    if (hasKey("log.level")) {
        std::string level = get<std::string>("log.level", "");
        std::vector<std::string> validLevels = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        if (std::find(validLevels.begin(), validLevels.end(), level) == validLevels.end()) {
            validationErrors.push_back("log.level must be one of: TRACE, DEBUG, INFO, WARN, ERROR, FATAL");
        }
    }
    
    return validationErrors.empty();
}

std::vector<std::string> Config::getValidationErrors() const {
    return validationErrors;
}

// Convenience getters

std::string Config::getSerialPort() const {
    return get<std::string>("serial.port", "COM3");
}

unsigned int Config::getSerialBaudRate() const {
    return get<unsigned int>("serial.baud_rate", 115200);
}

unsigned int Config::getSerialTimeout() const {
    return get<unsigned int>("serial.timeout_ms", 5000);
}

std::string Config::getEntropySource() const {
    return get<std::string>("entropy.source", "auto");
}

size_t Config::getPoolBufferSize() const {
    return get<size_t>("entropy.pool_buffer_size", 4096);
}

size_t Config::getPoolRefillThreshold() const {
    return get<size_t>("entropy.pool_refill_threshold", 2048);
}

unsigned int Config::getHttpPort() const {
    return get<unsigned int>("http.port", 8080);
}

std::string Config::getLogFilePath() const {
    return get<std::string>("log.file", "logs/backend.log");
}

std::string Config::getLogLevel() const {
    return get<std::string>("log.level", "INFO");
}

std::string Config::getTestMode() const {
    return get<std::string>("analysis.test_mode", "quick");
}

std::string Config::getFrontendPath() const {
    return get<std::string>("server.frontend_path", "../../frontend");
}

nlohmann::json Config::getJsonValue(const std::string& key) const {
    if (!config.contains(key)) {
        throw std::runtime_error("Key not found: " + key);
    }
    return config[key];
}

void Config::setJsonValue(const std::string& key, const nlohmann::json& value) {
    config[key] = value;
}

void Config::flattenJson(const std::string& prefix, const nlohmann::json& value) {
    if (value.is_object()) {
        for (auto& [key, val] : value.items()) {
            std::string newKey = prefix.empty() ? key : prefix + "." + key;
            flattenJson(newKey, val);
        }
    } else {
        config[prefix] = value;
    }
}

void Config::createDefaults() {
    config = createDefaultConfig();
    loaded = true;
}

nlohmann::json Config::createDefaultConfig() {
    nlohmann::json defaults = nlohmann::json::object();

    defaults["serial"] = nlohmann::json::object();
    defaults["serial"]["port"] = "COM3";
    defaults["serial"]["baud_rate"] = 115200;
    defaults["serial"]["timeout_ms"] = 5000;

    defaults["entropy"] = nlohmann::json::object();
    defaults["entropy"]["source"] = "auto";
    defaults["entropy"]["pool_buffer_size"] = 4096;
    defaults["entropy"]["pool_refill_threshold"] = 2048;

    defaults["http"] = nlohmann::json::object();
    defaults["http"]["port"] = 8080;

    defaults["log"] = nlohmann::json::object();
    defaults["log"]["file"] = "logs/backend.log";
    defaults["log"]["level"] = "INFO";
    defaults["log"]["console_output"] = true;

    defaults["analysis"] = nlohmann::json::object();
    defaults["analysis"]["test_mode"] = "quick";
    defaults["analysis"]["test_size"] = 1024;

    defaults["server"] = nlohmann::json::object();
    defaults["server"]["frontend_path"] = "../../frontend";
    defaults["server"]["max_upload_size"] = 10485760;

    defaults["crypto"] = nlohmann::json::object();
    defaults["crypto"]["key_size"] = 256;
    defaults["crypto"]["salt_size"] = 32;
    defaults["crypto"]["iterations"] = 100000;
    
    return defaults;
}

bool Config::validateValue(const std::string& key, const nlohmann::json& value) const {
    // Add specific validation rules here if needed
    return true;
}
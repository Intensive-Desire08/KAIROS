#include "WebServer.h"
#include "EntropyCollector.h"
#include "Config.h"
#include "crypto/KeyGenerator.h"
#include "crypto/Encryptor.h"
#include "crypto/Hasher.h"

#include <httplib.h>
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <vector>

// Base64 utilities
static const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

WebServer::WebServer(
    EntropyCollector& entropyCollectorRef,
    unsigned int portValue,
    const std::string& frontendPathValue)
    : entropyCollector(entropyCollectorRef)
    , port(portValue)
    , frontendPath(frontendPathValue)
    , host("0.0.0.0")
    , server(nullptr)
    , running(false)
    , activeConnections(0)
    , totalRequests(0) {
    
    // Ensure frontend path is found regardless of execution working directory
    std::vector<std::string> candidates = {
        frontendPathValue,
        "frontend",
        "./frontend",
        "../frontend",
        "../../frontend",
        "../../../frontend"
    };
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
            this->frontendPath = candidate;
            break;
        }
    }
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (running) {
        std::cout << "[WebServer] WebServer already running" << std::endl;
        return true;
    }

    try {
        // Create server instance
        server = std::make_unique<httplib::Server>();
        
        // Configure server
        server->set_keep_alive_max_count(10);
        server->set_read_timeout(30);
        server->set_write_timeout(30);
        server->set_idle_interval(5);
        
        // Set up routes
        setupRoutes();
        
        // Start server in background thread
        running = true;
        serverThread = std::thread([this]() {
            std::cout << "[WebServer] Starting on " << host << ":" << port << std::endl;
            std::cout << "[WebServer] Serving frontend from: " << frontendPath << std::endl;
            
            try {
                        if (!server->listen(host.c_str(), static_cast<int>(port))) {
                    std::cerr << "[WebServer] Failed to start on port " << port << std::endl;
                    running = false;
                }
            } catch (const std::exception& e) {
                std::cerr << "[WebServer] Error: " << e.what() << std::endl;
                running = false;
            }
        });
        
        // Wait a moment for server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (!running) {
            std::cerr << "[WebServer] Failed to start" << std::endl;
            if (serverThread.joinable()) {
                serverThread.join();
            }
            server.reset();
            return false;
        }
        
        std::cout << "[WebServer] Started successfully on port " << port << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[WebServer] Start error: " << e.what() << std::endl;
        if (serverThread.joinable()) {
            serverThread.join();
        }
        server.reset();
        return false;
    }
}

void WebServer::stop() {
    if (!running) {
        return;
    }
    
    std::cout << "[WebServer] Stopping..." << std::endl;
    running = false;
    
    if (server) {
        server->stop();
    }
    
    if (serverThread.joinable()) {
        serverThread.join();
    }
    
    server.reset();
    std::cout << "[WebServer] Stopped" << std::endl;
}

bool WebServer::isRunning() const {
    return running;
}

unsigned int WebServer::getPort() const {
    return port;
}

void WebServer::setPort(unsigned int newPort) {
    if (running) {
        std::cerr << "[WebServer] Cannot change port while server is running" << std::endl;
        return;
    }
    this->port = newPort;
}

std::string WebServer::getFrontendPath() const {
    return frontendPath;
}

void WebServer::setFrontendPath(const std::string& path) {
    if (running) {
        std::cerr << "[WebServer] Cannot change frontend path while server is running" << std::endl;
        return;
    }
    this->frontendPath = path;
}

size_t WebServer::getActiveConnections() const {
    return activeConnections;
}

size_t WebServer::getTotalRequests() const {
    return totalRequests;
}

void WebServer::setupRoutes() {
    if (!server) return;
    
    // Static file serving with fallback to index.html for SPA routing
    server->set_mount_point("/", frontendPath);
    
    // Serve index.html for root
    server->Get("/", [this](const httplib::Request& req, httplib::Response& res) {
        serveStaticFile(req, res);
    });
    
    // API routes
    server->Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
        handleStatus(req, res);
    });
    
    server->Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        handleHealth(req, res);
    });
    
    server->Get("/entropy", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetEntropy(req, res);
    });
    
    server->Post("/keygen", [this](const httplib::Request& req, httplib::Response& res) {
        handleKeyGen(req, res);
    });
    
    server->Post("/encrypt", [this](const httplib::Request& req, httplib::Response& res) {
        handleEncrypt(req, res);
    });
    
    server->Post("/decrypt", [this](const httplib::Request& req, httplib::Response& res) {
        handleDecrypt(req, res);
    });
    
    server->Post("/test", [this](const httplib::Request& req, httplib::Response& res) {
        handleTest(req, res);
    });
    
    server->Get("/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetSettings(req, res);
    });
    
    server->Post("/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handleSettings(req, res);
    });
    
    server->Post("/reseed", [this](const httplib::Request& req, httplib::Response& res) {
        handleReseed(req, res);
    });
    
    // Catch-all for SPA routing - serve index.html for any unmatched routes
    server->Get(R"(.*)", [this](const httplib::Request& req, httplib::Response& res) {
        serveStaticFile(req, res);
    });
}

void WebServer::serveStaticFile(const httplib::Request& req, httplib::Response& res) {
    std::string path = req.path;
    
    // If path is root or ends with /, serve index.html
    if (path == "/" || path == "/index.html") {
        path = "/index.html";
    }
    
    // Build full file path
    std::string filePath = frontendPath + path;
    
    // Check if file exists
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        // For SPA, serve index.html for routes that don't exist as files
        std::string indexPath = frontendPath + "/index.html";
        std::ifstream indexFile(indexPath, std::ios::binary);
        if (indexFile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(indexFile)),
                                std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
            logRequest("GET", req.path, 200);
            return;
        }
        
        res.status = 404;
        res.set_content("File not found", "text/plain");
        logRequest("GET", req.path, 404);
        return;
    }
    
    // Read file content
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    // Determine content type
    std::string contentType = "application/octet-stream";
    if (endsWith(path, ".html")) contentType = "text/html";
    else if (endsWith(path, ".css")) contentType = "text/css";
    else if (endsWith(path, ".js")) contentType = "application/javascript";
    else if (endsWith(path, ".json")) contentType = "application/json";
    else if (endsWith(path, ".png")) contentType = "image/png";
    else if (endsWith(path, ".jpg") || endsWith(path, ".jpeg")) contentType = "image/jpeg";
    else if (endsWith(path, ".svg")) contentType = "image/svg+xml";
    else if (endsWith(path, ".ico")) contentType = "image/x-icon";
    
    res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
    res.set_content(content, contentType);
    logRequest("GET", req.path, 200);
}

void WebServer::handleStatus(const httplib::Request& req, httplib::Response& res) {
    logRequest("GET", req.path, 200);
    
    nlohmann::json response;
    response["status"] = "ok";
    response["version"] = API_VERSION;
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Entropy collector status
    nlohmann::json entropyStatus;
    entropyStatus["initialized"] = entropyCollector.isInitialized();
    entropyStatus["source"] = entropyCollector.getSourceName();
    entropyStatus["source_type"] = entropyCollector.getSourceType();
    entropyStatus["available_bytes"] = entropyCollector.getAvailableBytes();
    entropyStatus["total_generated"] = entropyCollector.getTotalBytesGenerated();
    entropyStatus["pool_size"] = entropyCollector.getPoolSize();
    entropyStatus["speed"] = entropyCollector.getSpeed();
    entropyStatus["hardware_available"] = entropyCollector.isHardwareAvailable();
    entropyStatus["openssl_available"] = entropyCollector.isOpenSSLAvailable();
    entropyStatus["whitening"] = entropyCollector.getWhiteningAlgorithm();
    entropyStatus["preferred_source"] = entropyCollector.getPreferredSourceType();
    response["entropy"] = entropyStatus;
    
    // Server status
    nlohmann::json serverStatus;
    serverStatus["running"] = running.load();
    serverStatus["port"] = port;
    serverStatus["active_connections"] = activeConnections.load();
    serverStatus["total_requests"] = totalRequests.load();
    response["server"] = serverStatus;
    
    res.set_content(response.dump(2), "application/json");
}

void WebServer::handleHealth(const httplib::Request& req, httplib::Response& res) {
    logRequest("GET", req.path, 200);
    
    bool healthy = running && entropyCollector.isInitialized();
    
    nlohmann::json response;
    response["status"] = healthy ? "healthy" : "unhealthy";
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    res.set_content(response.dump(2), "application/json");
}

void WebServer::handleGetEntropy(const httplib::Request& req, httplib::Response& res) {
    logRequest("GET", req.path, 200);
    
    try {
        // Get number of bytes from query parameter
        size_t numBytes = 32; // Default
        if (req.has_param("bytes")) {
            numBytes = std::stoul(req.get_param_value("bytes"));
            if (numBytes > MAX_ENTROPY_REQUEST) {
                res.status = 400;
                res.set_content(errorResponse(400, "Requested bytes exceeds maximum (" + 
                              std::to_string(MAX_ENTROPY_REQUEST) + ")"), "application/json");
                return;
            }
        }
        
        // Get entropy
        std::vector<uint8_t> entropy = entropyCollector.getEntropy(numBytes);
        
        // Return as hex
        nlohmann::json response;
        response["status"] = "ok";
        response["bytes"] = numBytes;
        response["data"] = hexEncode(entropy);
        response["hex"] = hexEncode(entropy); // Alias for convenience
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleKeyGen(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid JSON"), "application/json");
            return;
        }
        
        // Get parameters
        size_t keySize = static_cast<size_t>(request.value("key_size", 32u));
        std::string password = request.value("password", "");
        bool deriveKey = !password.empty();
        
        nlohmann::json response;
        response["status"] = "ok";
        
        if (deriveKey) {
            // Derive key from password
            size_t saltSize = static_cast<size_t>(request.value("salt_size", 32u));
            unsigned int iterations = request.value("iterations", 100000u);
            
            std::vector<uint8_t> salt = KeyGenerator::generateSalt(saltSize);
            std::vector<uint8_t> key = KeyGenerator::deriveKey(password, salt, keySize);
            
            response["key"] = hexEncode(key);
            response["salt"] = hexEncode(salt);
            response["iterations"] = iterations;
            response["key_size"] = keySize;
            response["derived"] = true;
            
        } else {
            // Generate random key
            std::vector<uint8_t> key = KeyGenerator::generateKey(keySize);
            response["key"] = hexEncode(key);
            response["key_size"] = keySize;
            response["derived"] = false;
        }
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleEncrypt(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid JSON"), "application/json");
            return;
        }
        
        // Get required parameters
        if (!request.contains("data") || !request.contains("password")) {
            res.status = 400;
            res.set_content(errorResponse(400, "Missing required fields: data, password"), "application/json");
            return;
        }
        
        std::vector<uint8_t> data = hexDecode(request["data"].get<std::string>());
        std::string password = request["password"].get<std::string>();
        
        // Generate random salt (32 bytes) and IV (12 bytes)
        std::vector<uint8_t> salt = KeyGenerator::generateSalt(32);
        std::vector<uint8_t> iv = Encryptor::generateIV(12);
        
        // Derive 32-byte key
        std::vector<uint8_t> key = KeyGenerator::deriveKey(password, salt, 32);
        
        // Encrypt
        auto result = Encryptor::encrypt(data, key, iv);
        
        // Assemble .pqe file content: [salt(32) | iv(12) | tag(16) | ciphertext]
        std::vector<uint8_t> pqeFile;
        pqeFile.reserve(salt.size() + iv.size() + result.tag.size() + result.ciphertext.size());
        pqeFile.insert(pqeFile.end(), salt.begin(), salt.end());
        pqeFile.insert(pqeFile.end(), iv.begin(), iv.end());
        pqeFile.insert(pqeFile.end(), result.tag.begin(), result.tag.end());
        pqeFile.insert(pqeFile.end(), result.ciphertext.begin(), result.ciphertext.end());
        
        nlohmann::json response;
        response["status"] = "ok";
        response["ciphertext"] = hexEncode(pqeFile);
        response["iv"] = hexEncode(iv);
        response["tag"] = hexEncode(result.tag);
        response["key"] = hexEncode(key);
        response["key_size"] = key.size() * 8;
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleDecrypt(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid JSON"), "application/json");
            return;
        }
        
        // Get required parameters
        if (!request.contains("ciphertext") || !request.contains("password")) {
            res.status = 400;
            res.set_content(errorResponse(400, "Missing required fields: ciphertext, password"), "application/json");
            return;
        }
        
        std::vector<uint8_t> pqeFile = hexDecode(request["ciphertext"].get<std::string>());
        std::string password = request["password"].get<std::string>();
        
        // Validate .pqe file structure size (32 salt + 12 iv + 16 tag = 60 bytes minimum header)
        if (pqeFile.size() < 60) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid .pqe file: too small"), "application/json");
            return;
        }
        
        // Extract components
        std::vector<uint8_t> salt(pqeFile.begin(), pqeFile.begin() + 32);
        std::vector<uint8_t> iv(pqeFile.begin() + 32, pqeFile.begin() + 44);
        std::vector<uint8_t> tag(pqeFile.begin() + 44, pqeFile.begin() + 60);
        std::vector<uint8_t> actual_ciphertext(pqeFile.begin() + 60, pqeFile.end());
        
        // Derive 32-byte key
        std::vector<uint8_t> key = KeyGenerator::deriveKey(password, salt, 32);
        
        // Decrypt
        std::vector<uint8_t> plaintext = Encryptor::decrypt(actual_ciphertext, key, iv, tag);
        
        nlohmann::json response;
        response["status"] = "ok";
        response["data"] = hexEncode(plaintext);
        response["plaintext"] = hexEncode(plaintext);
        response["iv"] = hexEncode(iv);
        response["tag"] = hexEncode(tag);
        response["key"] = hexEncode(key);
        response["key_size"] = key.size() * 8;
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleTest(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid JSON"), "application/json");
            return;
        }
        
        size_t numBytes = static_cast<size_t>(request.value("bytes", 4096u));
        std::string mode = request.value("mode", "quick");
        size_t runs = static_cast<size_t>(request.value("runs", 100u));

        size_t totalBytesToFetch = numBytes;
        if (mode == "exhaustive") {
            if (runs < 10) runs = 10;
            if (runs > 200) runs = 200;
            totalBytesToFetch = numBytes * runs;
            if (totalBytesToFetch > MAX_ENTROPY_REQUEST) {
                totalBytesToFetch = MAX_ENTROPY_REQUEST;
                runs = totalBytesToFetch / (numBytes > 0 ? numBytes : 1);
                if (runs == 0) {
                    runs = 10;
                    numBytes = totalBytesToFetch / runs;
                }
            }
        } else {
            if (numBytes > MAX_ENTROPY_REQUEST) {
                res.status = 400;
                res.set_content(errorResponse(400, "Requested bytes exceeds maximum (1MB)"), "application/json");
                return;
            }
        }
        
        std::vector<uint8_t> data = entropyCollector.getEntropy(totalBytesToFetch);

        std::filesystem::path analyzerScript;
        std::filesystem::path searchDir = std::filesystem::current_path();
        for (int depth = 0; depth < 12; ++depth) {
            std::filesystem::path candidate = searchDir / "analyzer" / "entropy_analyzer.py";
            if (std::filesystem::exists(candidate)) {
                analyzerScript = candidate;
                break;
            }
            auto parent = searchDir.parent_path();
            if (parent == searchDir) {
                break;
            }
            searchDir = parent;
        }

        std::string pythonExe = "python";
        std::filesystem::path projectRoot = analyzerScript.empty() ? std::filesystem::current_path() : analyzerScript.parent_path().parent_path();
        std::filesystem::path pythonCandidate = projectRoot / ".venv" / "Scripts" / "python.exe";
        if (std::filesystem::exists(pythonCandidate)) {
            pythonExe = pythonCandidate.string();
        } else {
            std::filesystem::path unixPython = "/usr/bin/python3";
            if (std::filesystem::exists(unixPython)) {
                pythonExe = unixPython.string();
            }
        }

        if (!analyzerScript.empty()) {
            auto tempFile = std::filesystem::temp_directory_path() / ("pqe_entropy_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".bin");
            std::ofstream inputFile(tempFile, std::ios::binary);
            if (inputFile.is_open()) {
                inputFile.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                inputFile.close();
            }

            std::string command = pythonExe + " \"" + analyzerScript.string() + "\" --mode " + mode;
            if (mode == "exhaustive") {
                command += " --runs " + std::to_string(runs) + " --sample-bytes " + std::to_string(numBytes);
            }
            command += " < \"" + tempFile.string() + "\"";

            FILE* pipe = popen(command.c_str(), "r");
            std::string analyzerOutput;
            if (pipe) {
                char buffer[4096];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    analyzerOutput += buffer;
                }
                pclose(pipe);
            }

            if (std::filesystem::exists(tempFile)) {
                std::filesystem::remove(tempFile);
            }

            try {
                if (!analyzerOutput.empty()) {
                    nlohmann::json analyzerJson = nlohmann::json::parse(analyzerOutput);
                    nlohmann::json response;
                    response["status"] = analyzerJson.value("status", "ok");
                    response["mode"] = analyzerJson.value("mode", mode);
                    response["timestamp"] = analyzerJson.value("timestamp", "");
                    response["total_bytes"] = analyzerJson.value("total_bytes", data.size());
                    if (mode == "exhaustive") {
                        response["overall_verdict"] = analyzerJson.value("overall_verdict", "PASSED");
                        response["overall_passed"] = analyzerJson.value("overall_passed", true);
                        response["overall_pass_rate_percent"] = analyzerJson.value("overall_pass_rate_percent", 100.0);
                        response["min_pass_threshold_percent"] = analyzerJson.value("min_pass_threshold_percent", 96.0);
                        response["runs"] = analyzerJson.value("runs", runs);
                        response["sample_bytes"] = analyzerJson.value("sample_bytes", numBytes);
                        response["exhaustive"] = analyzerJson;
                        response["statistics"] = nlohmann::json::array();
                        if (analyzerJson.contains("tests") && analyzerJson["tests"].is_object()) {
                            for (auto& [key, val] : analyzerJson["tests"].items()) {
                                nlohmann::json item = val;
                                item["key"] = key;
                                item["pass"] = val.value("passed", true);
                                item["p_value"] = val.value("uniformity_p_value", 1.0);
                                response["statistics"].push_back(item);
                            }
                        }
                    } else {
                        response["overall_verdict"] = analyzerJson.value("overall_verdict", "PASSED");
                        response["statistics"] = analyzerJson.value("test_results", nlohmann::json::array());
                    }
                    std::vector<uint8_t> previewData = (data.size() > 512) ? std::vector<uint8_t>(data.begin(), data.begin() + 512) : data;
                    response["data"] = hexEncode(previewData);
                    response["analysis"] = analyzerJson;
                    res.set_content(response.dump(2), "application/json");
                    return;
                }
            } catch (const std::exception&) {
                // Fall back below if the analyzer output is invalid.
            }
        }

        // Fallback statistics if the analyzer is unavailable or fails.
        nlohmann::json stats;
        stats["byte_count"] = data.size();

        std::unordered_map<uint8_t, size_t> freq;
        for (uint8_t byte : data) {
            freq[byte]++;
        }

        double expected = static_cast<double>(data.size()) / 256.0;
        double chiSquare = 0.0;
        for (size_t i = 0; i < 256; ++i) {
            double observed = static_cast<double>(freq[static_cast<uint8_t>(i)]);
            chiSquare += ((observed - expected) * (observed - expected)) / expected;
        }

        stats["chi_square"] = chiSquare;
        stats["expected_per_byte"] = expected;
        stats["unique_bytes"] = freq.size();
        stats["entropy_estimate"] = 0.0;

        size_t ones = 0;
        for (uint8_t byte : data) {
            for (int i = 0; i < 8; ++i) {
                if (byte & (1 << i)) ones++;
            }
        }
        double bitRatio = static_cast<double>(ones) / static_cast<double>(data.size() * 8u);
        stats["monobit_ratio"] = bitRatio;
        stats["monobit_pass"] = (bitRatio > 0.45 && bitRatio < 0.55);
        
        nlohmann::json response;
        response["status"] = "ok";
        response["mode"] = mode;
        response["statistics"] = stats;
        response["data"] = hexEncode(data);
        response["analysis_note"] = "Fallback summary used because the Python analyzer was unavailable.";
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleGetSettings(const httplib::Request& req, httplib::Response& res) {
    logRequest("GET", req.path, 200);
    
    nlohmann::json settings;
    settings["port"] = entropyCollector.getPort();
    settings["baud_rate"] = entropyCollector.getBaudRate();
    settings["pool_size"] = entropyCollector.getPoolSize();
    settings["available_bytes"] = entropyCollector.getAvailableBytes();
    settings["source_type"] = entropyCollector.getSourceType();
    settings["preferred_source"] = entropyCollector.getPreferredSourceType();
    settings["source_name"] = entropyCollector.getSourceName();
    settings["whitening"] = entropyCollector.getWhiteningAlgorithm();
    settings["initialized"] = entropyCollector.isInitialized();
    settings["total_generated"] = entropyCollector.getTotalBytesGenerated();
    
    res.set_content(settings.dump(2), "application/json");
}

void WebServer::handleSettings(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        nlohmann::json request;
        try {
            request = nlohmann::json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(errorResponse(400, "Invalid JSON"), "application/json");
            return;
        }
        
        std::string responseMsg = "";
        
        // Update settings
        if (request.contains("port")) {
            std::string newPort = request["port"].get<std::string>();
            if (!newPort.empty() && newPort != entropyCollector.getPort()) {
                entropyCollector.setPort(newPort);
                Config::instance().set("serial.port", newPort);
                responseMsg += "Port updated. ";
            }
        }
        
        if (request.contains("baud_rate")) {
            unsigned int baud = request["baud_rate"].get<unsigned int>();
            if (baud > 0 && baud != entropyCollector.getBaudRate()) {
                entropyCollector.setBaudRate(baud);
                Config::instance().set("serial.baud_rate", baud);
                responseMsg += "Baud rate updated. ";
            }
        }
        
        if (request.contains("source_type")) {
            std::string source = request["source_type"].get<std::string>();
            if (!source.empty()) {
                if (entropyCollector.switchSourceType(source)) {
                    Config::instance().set("entropy.source", source);
                    responseMsg += "Source switched to " + source + ". ";
                } else {
                    Config::instance().set("entropy.source", source);
                    responseMsg += "Hardware source set as preferred (awaiting device response). ";
                }
            }
        }

        if (request.contains("whitening")) {
            std::string whitening = request["whitening"].get<std::string>();
            if (!whitening.empty()) {
                entropyCollector.setWhiteningAlgorithm(whitening);
                Config::instance().set("entropy.whitening", whitening);
                responseMsg += "Whitening algorithm switched to " + whitening + ". ";
            }
        }
        
        if (request.contains("reseed") && request["reseed"].get<bool>()) {
            entropyCollector.refillPool();
            responseMsg += "Pool refilled. ";
        }

        try {
            Config::instance().save("config/backend_config.json");
        } catch (...) {}
        
        nlohmann::json response;
        response["status"] = "ok";
        response["message"] = responseMsg.empty() ? "No changes applied" : responseMsg;
        
        res.set_content(response.dump(2), "application/json");
        
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::handleReseed(const httplib::Request& req, httplib::Response& res) {
    logRequest("POST", req.path, 200);
    
    try {
        if (entropyCollector.refillPool()) {
            nlohmann::json response;
            response["status"] = "ok";
            response["message"] = "Entropy pool refilled";
            response["available_bytes"] = entropyCollector.getAvailableBytes();
            res.set_content(response.dump(2), "application/json");
        } else {
            res.status = 500;
            res.set_content(errorResponse(500, "Failed to refill entropy pool"), "application/json");
        }
    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(errorResponse(500, e.what()), "application/json");
    }
}

void WebServer::logRequest(const std::string& method, const std::string& path, int status) {
    totalRequests++;
    std::cout << "[WebServer] HTTP " << method << " " << path << " -> " << status << std::endl;
}

std::string WebServer::errorResponse(int code, const std::string& message) {
    nlohmann::json response;
    response["status"] = "error";
    response["code"] = code;
    response["message"] = message;
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return response.dump(2);
}

std::string WebServer::hexEncode(const std::vector<uint8_t>& data) {
    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}

std::vector<uint8_t> WebServer::hexDecode(const std::string& hex) {
    std::vector<uint8_t> result;
    result.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        result.push_back(static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)));
    }
    return result;
}

std::string WebServer::base64Encode(const std::vector<uint8_t>& data) {
    std::string result;
    int val = 0;
    int valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(BASE64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        result.push_back(BASE64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

std::vector<uint8_t> WebServer::base64Decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    int val = 0;
    int valb = -8;
    for (char c : encoded) {
        if (c == '=') break;
        auto pos = BASE64_CHARS.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}
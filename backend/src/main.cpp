#include "Config.h"
#include "EntropyCollector.h"
#include "Logger.h"
#include "WebServer.h"
#include "build_config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <iomanip>
#include <fstream>
#include <vector>

// Fix for Windows: ERROR macro conflicts with logger
#ifdef _WIN32
    #include <windows.h>
    #undef ERROR
#endif

namespace {

// ============================================================================
// Global State
// ============================================================================

std::atomic<bool> g_running{true};

// ============================================================================
// Signal Handlers
// ============================================================================

#ifdef _WIN32
    BOOL WINAPI consoleHandler(DWORD signal) {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
            std::cout << "\n[main] Shutdown signal received..." << std::endl;
            g_running = false;
            return TRUE;
        }
        return FALSE;
    }
#else
    void signalHandler(int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            std::cout << "\n[main] Shutdown signal received..." << std::endl;
            g_running = false;
        }
    }
#endif

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Parse string to Logger::Level
 */
Logger::Level parseLogLevel(const std::string& level) {
    if (level == "TRACE") return Logger::Level::TRACE;
    if (level == "DEBUG") return Logger::Level::DEBUG;
    if (level == "INFO")  return Logger::Level::INFO;
    if (level == "WARN")  return Logger::Level::WARN;
    if (level == "ERROR") return Logger::Level::ERR;
    if (level == "FATAL") return Logger::Level::FATAL;
    if (level == "OFF")   return Logger::Level::OFF;
    return Logger::Level::INFO;
}

/**
 * @brief Print application banner
 */
void printBanner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════════╗
║                                                               ║
║     ██████╗ ███████╗██╗   ██╗██████╗  ██████╗                 ║
║     ██╔══██╗██╔════╝██║   ██║██╔══██╗██╔═══██╗                ║
║     ██████╔╝█████╗  ██║   ██║██████╔╝██║   ██║                ║
║     ██╔═══╝ ██╔══╝  ██║   ██║██╔══██╗██║   ██║                ║
║     ██║     ███████╗╚██████╔╝██║  ██║╚██████╔╝                ║
║     ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝                 ║
║                                                               ║
║     PseudoQuantum Entropy Service v1.0                        ║
║     Hardware-Backed Entropy with Cryptographic Applications   ║
║                                                               ║
╚═══════════════════════════════════════════════════════════════╝
    )" << std::endl;
}

/**
 * @brief Print startup configuration
 */
void printConfiguration(const Config& config) {
    std::cout << "\n[main] Configuration:" << std::endl;
    std::cout << "  ├── Serial Port:     " << config.getSerialPort() << std::endl;
    std::cout << "  ├── Baud Rate:       " << config.getSerialBaudRate() << std::endl;
    std::cout << "  ├── Entropy Source:  " << config.getEntropySource() << std::endl;
    std::cout << "  ├── Pool Size:       " << config.getPoolBufferSize() << " bytes" << std::endl;
    std::cout << "  ├── HTTP Port:       " << config.getHttpPort() << std::endl;
    std::cout << "  ├── Log Level:       " << config.getLogLevel() << std::endl;
    std::cout << "  ├── Log File:        " << config.getLogFilePath() << std::endl;
    std::cout << "  └── Frontend Path:   " << config.getFrontendPath() << std::endl;
    std::cout << std::endl;
}

} // namespace

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    // ------------------------------------------------------------------------
    // 0. Set console to UTF-8 for Windows
    // ------------------------------------------------------------------------
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ------------------------------------------------------------------------
    // 1. Print Banner
    // ------------------------------------------------------------------------
    printBanner();

    // ------------------------------------------------------------------------
    // 2. Setup Signal Handlers
    // ------------------------------------------------------------------------
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        std::cerr << "[main] Failed to set console control handler" << std::endl;
        return 1;
    }
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    // ------------------------------------------------------------------------
    // 3. Load Configuration
    // ------------------------------------------------------------------------
    auto& config = Config::instance();
    std::string configPath = "config/backend_config.json";
    
    // Allow config override via command line
    if (argc > 1) {
        configPath = argv[1];
        std::cout << "[main] Using config file: " << configPath << std::endl;
    } else {
        std::vector<std::string> searchPaths = {
            "config/backend_config.json",
            "../config/backend_config.json",
            "../../config/backend_config.json",
            "../../../config/backend_config.json"
        };
        for (const auto& path : searchPaths) {
            std::ifstream f(path);
            if (f.good()) {
                configPath = path;
                break;
            }
        }
    }

    if (!config.load(configPath)) {
        std::cerr << "[main] Failed to load config from: " << configPath << std::endl;
        std::cout << "[main] Using default configuration" << std::endl;
        config.resetToDefaults();
    }

    std::string loggingConfigPath = configPath;
    size_t pos = loggingConfigPath.find("backend_config.json");
    if (pos != std::string::npos) {
        loggingConfigPath.replace(pos, 19, "logging_config.json");
    } else {
        loggingConfigPath = "config/logging_config.json";
    }
    
    if (config.merge(loggingConfigPath)) {
        std::cout << "[main] Merged logging config from: " << loggingConfigPath << std::endl;
    } else {
        std::cerr << "[main] Failed to merge logging config from: " << loggingConfigPath << std::endl;
    }

    // ------------------------------------------------------------------------
    // 4. Initialize Logger
    // ------------------------------------------------------------------------
    auto& logger = Logger::instance();
    bool loggerInitialized = logger.initialize(
        config.getLogFilePath(),
        config.get<bool>("log.console_output", true),
        parseLogLevel(config.getLogLevel())
    );

    if (!loggerInitialized) {
        std::cerr << "[main] Failed to initialize logger" << std::endl;
        return 1;
    }

    LOG_INFO("=== PseudoQuantumEntropy Backend Starting ===");
    LOG_INFO("Build: " + std::string(BUILD_TYPE) + " - " + std::string(PLATFORM_NAME));
    LOG_INFO("Config loaded from: " + configPath);

    // ------------------------------------------------------------------------
    // 5. Print Configuration
    // ------------------------------------------------------------------------
    printConfiguration(config);

    // ------------------------------------------------------------------------
    // 6. Initialize Entropy Collector
    // ------------------------------------------------------------------------
    LOG_INFO("Initializing entropy collector...");
    
    EntropyCollector entropyCollector;
    const std::string entropySource = config.getEntropySource();
    const std::string serialPort = config.getSerialPort();
    const unsigned int baudRate = config.getSerialBaudRate();
    const size_t bufferSize = config.getPoolBufferSize();
    const std::string whiteningAlgo = config.get<std::string>("entropy.whitening", "lfsr");
    entropyCollector.setWhiteningAlgorithm(whiteningAlgo);

    bool entropyInitialized = false;
    std::string sourceType = "auto";

    try {
        if (entropySource == "hardware") {
            LOG_INFO("Forcing hardware entropy source...");
            entropyInitialized = entropyCollector.initializeWithSource(
                "hardware", serialPort, baudRate, bufferSize
            );
            sourceType = "hardware";
        } else if (entropySource == "openssl") {
            LOG_INFO("Forcing OpenSSL entropy source...");
            entropyInitialized = entropyCollector.initializeWithSource(
                "openssl", serialPort, baudRate, bufferSize
            );
            sourceType = "openssl";
        } else {
            LOG_INFO("Auto-detecting entropy source (hardware preferred, OpenSSL fallback)...");
            entropyInitialized = entropyCollector.initialize(
                serialPort, baudRate, bufferSize
            );
            sourceType = entropyCollector.getSourceType();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during entropy initialization: " + std::string(e.what()));
        entropyInitialized = false;
    }

    if (!entropyInitialized) {
        LOG_ERROR("Failed to initialize entropy collector");
        logger.shutdown();
        return 1;
    }

    LOG_INFO("Entropy source initialized: " + entropyCollector.getSourceName());
    LOG_INFO("Source type: " + sourceType);
    LOG_INFO("Pool size: " + std::to_string(entropyCollector.getPoolSize()) + " bytes");

    // ------------------------------------------------------------------------
    // 7. Start Web Server
    // ------------------------------------------------------------------------
    LOG_INFO("Starting web server...");

    WebServer server(
        entropyCollector,
        config.getHttpPort(),
        config.getFrontendPath()
    );

    bool serverStarted = false;
    try {
        serverStarted = server.start();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during web server start: " + std::string(e.what()));
        serverStarted = false;
    }

    if (!serverStarted) {
        LOG_ERROR("Failed to start web server on port " + std::to_string(config.getHttpPort()));
        entropyCollector.shutdown();
        logger.shutdown();
        return 1;
    }

    LOG_INFO("Web server started on port " + std::to_string(config.getHttpPort()));
    LOG_INFO("Frontend path: " + config.getFrontendPath());

    // ------------------------------------------------------------------------
    // 8. Ready & Running
    // ------------------------------------------------------------------------
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  PseudoQuantumEntropy v" << PROJECT_VERSION << " - RUNNING" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "  Web interface   : http://localhost:" << config.getHttpPort() << std::endl;
    std::cout << "  Entropy source  : " << entropyCollector.getSourceName() << std::endl;
    std::cout << "  Status          : Press Ctrl+C to stop" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << std::endl;

    LOG_INFO("Backend is now running");
    LOG_INFO("Available at: http://localhost:" + std::to_string(config.getHttpPort()));

    // ------------------------------------------------------------------------
    // 9. Main Loop
    // ------------------------------------------------------------------------
    while (g_running.load()) {
        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Optional: Periodic health check or stats
        // Could log pool status every N seconds
    }

    // ------------------------------------------------------------------------
    // 10. Graceful Shutdown
    // ------------------------------------------------------------------------
    std::cout << "\n[main] Shutting down..." << std::endl;
    LOG_INFO("Shutdown signal received - stopping services...");

    // Stop web server first (stops accepting new connections)
    LOG_INFO("Stopping web server...");
    server.stop();
    LOG_INFO("Web server stopped");

    // Stop entropy collector (stops background threads)
    LOG_INFO("Stopping entropy collector...");
    entropyCollector.shutdown();
    LOG_INFO("Entropy collector stopped");

    // Flush and stop logger
    LOG_INFO("PseudoQuantumEntropy backend stopped");
    logger.shutdown();

    std::cout << "[main] Goodbye!" << std::endl;
    return 0;
}

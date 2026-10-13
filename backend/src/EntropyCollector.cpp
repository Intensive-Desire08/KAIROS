#include "Logger.h"
#include "EntropyCollector.h"
#include "entropy/SerialEntropySource.h"
#include "entropy/OpenSSLEntropySource.h"
#include "EntropyPool.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

EntropyCollector::EntropyCollector()
    : port("COM3")
    , baudRate(921600)
    , bufferSize(DEFAULT_BUFFER_SIZE)
    , initialized(false)
    , activeSourceType("none")
    , activeSourceName("none")
    , hardwareAvailable(false)
    , openSSLAvailable(false)
    , monitorRunning(false) {
}

EntropyCollector::~EntropyCollector() {
    shutdown();
}

std::vector<std::string> EntropyCollector::detectSources() {
    std::vector<std::string> sources;
    
    // Check if OpenSSL is available (always true if linked)
    openSSLAvailable = true;
    sources.push_back("openssl");
    
    // Check if hardware is available by trying to open the port
    // We'll do a quick test without fully initializing
    try {
        SerialEntropySource testSource(port, baudRate, 1000); // Short timeout
        if (testSource.initialize()) {
            hardwareAvailable = true;
            sources.push_back("hardware");
            testSource.shutdown();
        }
    } catch (const std::exception& e) {
        hardwareAvailable = false;
        { std::stringstream ss; ss << "[EntropyCollector] Hardware not detected: " << e.what(); LOG_INFO(ss.str()); }
    }
    
    std::cout << "[EntropyCollector] Detected sources: ";
    for (const auto& source : sources) {
        std::cout << source << " ";
    }
    std::cout << std::endl;
    
    return sources;
}

bool EntropyCollector::initialize(
    const std::string& port,
    unsigned int baudRate,
    size_t bufferSize) {
    
    if (initialized) {
        { std::stringstream ss; ss << "[EntropyCollector] Already initialized"; LOG_INFO(ss.str()); }
        return true;
    }
    
    this->port = port;
    this->baudRate = baudRate;
    this->bufferSize = bufferSize > 0 ? bufferSize : DEFAULT_BUFFER_SIZE;
    
    // Detect available sources
    detectSources();
    
    // Try hardware first
    if (hardwareAvailable) {
        { std::stringstream ss; ss << "[EntropyCollector] Attempting to initialize hardware source..."; LOG_INFO(ss.str()); }
        if (initializeHardware(port, baudRate)) {
            if (startPool(bufferSize)) {
                initialized = true;
                { std::stringstream ss; ss << "[EntropyCollector] Initialized with hardware source: " << activeSourceName; LOG_INFO(ss.str()); }
                
                // Start monitor loop to detect disconnects and reconnects
                monitorRunning = true;
                monitorThread = std::thread(&EntropyCollector::sourceMonitorLoop, this);
                return true;
            }
        }
    }
    
    // Fallback to OpenSSL
    { std::stringstream ss; ss << "[EntropyCollector] Hardware unavailable, falling back to OpenSSL..."; LOG_INFO(ss.str()); }
    if (initializeOpenSSL()) {
        if (startPool(bufferSize)) {
            initialized = true;
            { std::stringstream ss; ss << "[EntropyCollector] Initialized with OpenSSL fallback"; LOG_INFO(ss.str()); }
            
            // Start monitor loop
            monitorRunning = true;
            monitorThread = std::thread(&EntropyCollector::sourceMonitorLoop, this);
            return true;
        }
    }
    
    { std::stringstream ss; ss << "[EntropyCollector] Failed to initialize any entropy source!"; LOG_ERROR(ss.str()); }
    return false;
}

bool EntropyCollector::initializeWithSource(
    const std::string& sourceType,
    const std::string& port,
    unsigned int baudRate,
    size_t bufferSize) {
    
    if (initialized) {
        { std::stringstream ss; ss << "[EntropyCollector] Already initialized"; LOG_INFO(ss.str()); }
        return true;
    }
    
    this->port = port;
    this->baudRate = baudRate;
    this->bufferSize = bufferSize > 0 ? bufferSize : DEFAULT_BUFFER_SIZE;
    
    bool success = false;
    
    if (sourceType == "hardware") {
        success = initializeHardware(port, baudRate);
    } else if (sourceType == "openssl") {
        success = initializeOpenSSL();
    } else {
        { std::stringstream ss; ss << "[EntropyCollector] Unknown source type: " << sourceType; LOG_ERROR(ss.str()); }
        return false;
    }
    
    if (!success) {
        { std::stringstream ss; ss << "[EntropyCollector] Failed to initialize " << sourceType; LOG_ERROR(ss.str()); }
        return false;
    }
    
    if (!startPool(bufferSize)) {
        { std::stringstream ss; ss << "[EntropyCollector] Failed to start entropy pool"; LOG_ERROR(ss.str()); }
        return false;
    }
    
    initialized = true;
    { std::stringstream ss; ss << "[EntropyCollector] Initialized with " << sourceType << " source: " << activeSourceName; LOG_INFO(ss.str()); }
    
    // Start monitor loop
    monitorRunning = true;
    monitorThread = std::thread(&EntropyCollector::sourceMonitorLoop, this);
    return true;
}

void EntropyCollector::shutdown() {
    if (monitorRunning) {
        monitorRunning = false;
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }

    if (entropyPool) {
        entropyPool->stop();
        entropyPool.reset();
    }
    
    if (hardwareSource) {
        hardwareSource->shutdown();
        hardwareSource.reset();
    }

    if (openSSLSource) {
        openSSLSource->shutdown();
        openSSLSource.reset();
    }

    entropySource.reset();
    
    initialized = false;
    activeSourceType = "none";
    activeSourceName = "none";
    
    { std::stringstream ss; ss << "[EntropyCollector] Shutdown complete"; LOG_INFO(ss.str()); }
}

std::vector<uint8_t> EntropyCollector::getEntropy(size_t numBytes) {
    if (!initialized || !entropyPool) {
        throw EntropyException("EntropyCollector not initialized");
    }
    
    return entropyPool->getEntropy(numBytes);
}

uint8_t EntropyCollector::getByte() {
    if (!initialized || !entropyPool) {
        throw EntropyException("EntropyCollector not initialized");
    }
    
    return entropyPool->getByte();
}

size_t EntropyCollector::tryGetEntropy(size_t numBytes, std::vector<uint8_t>& buffer) {
    if (!initialized || !entropyPool) {
        return 0;
    }
    
    return entropyPool->tryGetEntropy(numBytes, buffer);
}

std::string EntropyCollector::getSourceName() const {
    return activeSourceName;
}

std::string EntropyCollector::getSourceType() const {
    return activeSourceType;
}

bool EntropyCollector::isHardwareAvailable() const {
    return hardwareAvailable;
}

bool EntropyCollector::isOpenSSLAvailable() const {
    return openSSLAvailable;
}

bool EntropyCollector::isInitialized() const {
    return initialized;
}

size_t EntropyCollector::getTotalBytesGenerated() const {
    if (!entropyPool) {
        return 0;
    }
    return entropyPool->getTotalBytesGenerated();
}

double EntropyCollector::getSpeed() const {
    if (!entropyPool) {
        return 0.0;
    }
    return entropyPool->getSpeed();
}

void EntropyCollector::resetSpeed() {
    if (entropyPool) {
        entropyPool->resetSpeed();
    }
}

void EntropyCollector::resetByteCounter() {
    if (entropySource) {
        entropySource->resetByteCounter();
    }
}

size_t EntropyCollector::getPoolSize() const {
    if (!entropyPool) {
        return 0;
    }
    return entropyPool->getBufferSize();
}

size_t EntropyCollector::getAvailableBytes() const {
    if (!entropyPool) {
        return 0;
    }
    return entropyPool->available();
}

bool EntropyCollector::refillPool() {
    if (!entropyPool) {
        return false;
    }
    return entropyPool->refill();
}

void EntropyCollector::setPort(const std::string& port) {
    this->port = port;
}

void EntropyCollector::setBaudRate(unsigned int baudRate) {
    this->baudRate = baudRate;
}

void EntropyCollector::setBufferSize(size_t bufferSize) {
    this->bufferSize = bufferSize > 0 ? bufferSize : DEFAULT_BUFFER_SIZE;
}

std::string EntropyCollector::getPort() const {
    return port;
}

unsigned int EntropyCollector::getBaudRate() const {
    return baudRate;
}

bool EntropyCollector::initializeHardware(const std::string& port, unsigned int baudRate) {
    try {
        hardwareSource = std::make_shared<SerialEntropySource>(port, baudRate);
        
        if (hardwareSource->initialize()) {
            if (configuredWhitening == "sha256") {
                hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::SHA256);
            } else {
                hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::LFSR);
            }
            entropySource = hardwareSource;
            activeSourceType = "hardware";
            activeSourceName = hardwareSource->getSourceName();
            hardwareAvailable = true;
            return true;
        }
    } catch (const std::exception& e) {
        { std::stringstream ss; ss << "[EntropyCollector] Hardware initialization error: " << e.what(); LOG_ERROR(ss.str()); }
    }
    
    return false;
}

bool EntropyCollector::initializeOpenSSL() {
    try {
        if (!openSSLSource) {
            openSSLSource = std::make_shared<OpenSSLEntropySource>();
            openSSLSource->initialize();
        }
        
        entropySource = openSSLSource;
        activeSourceType = "openssl";
        activeSourceName = openSSLSource->getSourceName();
        openSSLAvailable = true;
        return true;
    } catch (const std::exception& e) {
        { std::stringstream ss; ss << "[EntropyCollector] OpenSSL initialization error: " << e.what(); LOG_ERROR(ss.str()); }
    }
    
    return false;
}

bool EntropyCollector::startPool(size_t bufferSize) {
    if (!entropySource) {
        return false;
    }
    
    try {
        entropyPool = std::make_unique<EntropyPool>(entropySource, bufferSize, REFILL_THRESHOLD);
        return entropyPool->start();
    } catch (const std::exception& e) {
        { std::stringstream ss; ss << "[EntropyCollector] Pool creation error: " << e.what(); LOG_ERROR(ss.str()); }
        return false;
    }
}

void EntropyCollector::cleanup() {
    if (entropyPool) {
        entropyPool->stop();
        entropyPool.reset();
    }
    
    if (hardwareSource) {
        hardwareSource->shutdown();
        hardwareSource.reset();
    }

    if (openSSLSource) {
        openSSLSource->shutdown();
        openSSLSource.reset();
    }

    entropySource.reset();
}

void EntropyCollector::sourceMonitorLoop() {
    int failedCount = 0;
    int probeInterval = 0;
    while (monitorRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!monitorRunning) break;

        // If user manually forced OpenSSL mode in Settings, do not auto-switch to hardware
        if (preferredSourceType == "openssl") {
            continue;
        }

        if (activeSourceType == "hardware") {
            bool isAvail = false;
            if (hardwareSource) {
                isAvail = hardwareSource->isAvailable();
            }

            if (!isAvail) {
                failedCount++;
                if (failedCount >= 2) {
                    failedCount = 0;
                    if (entropyPool) {
                        entropyPool->resetSpeed();
                    }
                    { std::stringstream ss; ss << "[EntropyCollector] Hardware paused or disconnected. Falling back to OpenSSL..."; LOG_INFO(ss.str()); }
                    
                    // If the cable was physically unplugged (port invalid), shut down the handle
                    if (hardwareSource && !hardwareSource->isPortOpen()) {
                        try {
                            hardwareSource->shutdown();
                            hardwareSource.reset();
                        } catch (...) {}
                    } else if (hardwareSource) {
                        // Port is still open (device paused via button); flush stale bytes
                        hardwareSource->flushBuffer();
                    }

                    hardwareAvailable = false;

                    if (initializeOpenSSL()) {
                        if (entropyPool) {
                            entropyPool->setSource(openSSLSource);
                        }
                        { std::stringstream ss; ss << "[EntropyCollector] Successfully switched to OpenSSL fallback"; LOG_INFO(ss.str()); }
                    }
                }
            } else {
                failedCount = 0;
            }
        } else if (activeSourceType == "openssl") {
            failedCount = 0;
            // Case 1: Hardware was paused via button (port is still open)
            // Check if user pressed button to resume (new bytes arrived) without re-opening port or toggling DTR/RTS!
            if (hardwareSource && hardwareSource->isPortOpen()) {
                if (hardwareSource->hasIncomingData()) {
                    if (hardwareSource->resumeFromPause()) {
                        if (entropyPool) {
                            entropyPool->resetSpeed();
                        }
                        { std::stringstream ss; ss << "[EntropyCollector] Hardware source resumed! Switching back..."; LOG_INFO(ss.str()); }
                        if (configuredWhitening == "sha256") {
                            hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::SHA256);
                        } else {
                            hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::LFSR);
                        }
                        entropySource = hardwareSource;
                        activeSourceType = "hardware";
                        activeSourceName = hardwareSource->getSourceName();
                        hardwareAvailable = true;
                        if (entropyPool) {
                            entropyPool->setSource(hardwareSource);
                        }
                    }
                }
            } else {
                // Case 2: Hardware was physically unplugged (no open port)
                // Probe every 2 seconds (4 * 500ms) to check if USB is plugged back in
                probeInterval++;
                if (probeInterval >= 4) {
                    probeInterval = 0;
                    try {
                        auto testSource = std::make_shared<SerialEntropySource>(port, baudRate, 1000);
                        if (testSource->initialize() && testSource->isAvailable()) {
                            if (entropyPool) {
                                entropyPool->resetSpeed();
                            }
                            { std::stringstream ss; ss << "[EntropyCollector] Hardware reconnected! Switching back..."; LOG_INFO(ss.str()); }
                            if (configuredWhitening == "sha256") {
                                testSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::SHA256);
                            } else {
                                testSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::LFSR);
                            }
                            hardwareSource = testSource;
                            entropySource = hardwareSource;
                            activeSourceType = "hardware";
                            activeSourceName = testSource->getSourceName();
                            hardwareAvailable = true;
                            if (entropyPool) {
                                entropyPool->setSource(hardwareSource);
                            }
                        } else {
                            testSource->shutdown();
                        }
                    } catch (const std::exception&) {
                        // Still disconnected
                    }
                }
            }
        }
    }
}

bool EntropyCollector::switchSourceType(const std::string& sourceType) {
    std::string target = sourceType;
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

    if (target == "openssl" || target == "software") {
        preferredSourceType = "openssl";
        if (!openSSLSource) {
            initializeOpenSSL();
        }
        if (openSSLSource) {
            entropySource = openSSLSource;
            activeSourceType = "openssl";
            activeSourceName = openSSLSource->getSourceName();
            if (entropyPool) {
                entropyPool->resetSpeed();
                entropyPool->setSource(openSSLSource);
            }
            { std::stringstream ss; ss << "[EntropyCollector] User switched source to OpenSSL software mode"; LOG_INFO(ss.str()); }
            return true;
        }
        return false;
    } else if (target == "hardware" || target == "esp32") {
        preferredSourceType = "hardware";
        bool hwReady = false;
        if (hardwareSource && hardwareSource->isPortOpen() && hardwareSource->isAvailable()) {
            hwReady = true;
        } else if (hardwareSource && hardwareSource->isPortOpen()) {
            hwReady = hardwareSource->resumeFromPause();
        } else {
            hwReady = initializeHardware(port, baudRate);
        }

        if (hwReady && hardwareSource) {
            if (configuredWhitening == "sha256") {
                hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::SHA256);
            } else {
                hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::LFSR);
            }
            entropySource = hardwareSource;
            activeSourceType = "hardware";
            activeSourceName = hardwareSource->getSourceName();
            hardwareAvailable = true;
            if (entropyPool) {
                entropyPool->resetSpeed();
                entropyPool->setSource(hardwareSource);
            }
            { std::stringstream ss; ss << "[EntropyCollector] User switched source to ESP32 hardware mode"; LOG_INFO(ss.str()); }
            return true;
        } else {
            { std::stringstream ss; ss << "[EntropyCollector] Hardware mode requested, but ESP32 device is not currently responsive"; LOG_WARN(ss.str()); }
            return false;
        }
    } else if (target == "auto") {
        preferredSourceType = "auto";
        { std::stringstream ss; ss << "[EntropyCollector] Source selection restored to auto"; LOG_INFO(ss.str()); }
        return true;
    }

    return false;
}

std::string EntropyCollector::getPreferredSourceType() const {
    return preferredSourceType;
}

void EntropyCollector::setWhiteningAlgorithm(const std::string& algo) {
    std::string lower = algo;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("sha") != std::string::npos) {
        configuredWhitening = "sha256";
        if (hardwareSource) {
            hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::SHA256);
        }
    } else {
        configuredWhitening = "lfsr";
        if (hardwareSource) {
            hardwareSource->setWhiteningAlgorithm(SerialEntropySource::WhiteningAlgorithm::LFSR);
        }
    }
    { std::stringstream ss; ss << "[EntropyCollector] Whitening algorithm configured: " << configuredWhitening; LOG_INFO(ss.str()); }
}

std::string EntropyCollector::getWhiteningAlgorithm() const {
    if (hardwareSource) {
        auto algo = hardwareSource->getWhiteningAlgorithm();
        return algo == SerialEntropySource::WhiteningAlgorithm::SHA256 ? "sha256" : "lfsr";
    }
    return configuredWhitening;
}
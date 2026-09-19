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
    , baudRate(115200)
    , bufferSize(DEFAULT_BUFFER_SIZE)
    , initialized(false)
    , activeSourceType("none")
    , activeSourceName("none")
    , hardwareAvailable(false)
    , openSSLAvailable(false) {
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
        std::cout << "[EntropyCollector] Hardware not detected: " << e.what() << std::endl;
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
        std::cout << "[EntropyCollector] Already initialized" << std::endl;
        return true;
    }
    
    this->port = port;
    this->baudRate = baudRate;
    this->bufferSize = bufferSize > 0 ? bufferSize : DEFAULT_BUFFER_SIZE;
    
    // Detect available sources
    detectSources();
    
    // Try hardware first
    if (hardwareAvailable) {
        std::cout << "[EntropyCollector] Attempting to initialize hardware source..." << std::endl;
        if (initializeHardware(port, baudRate)) {
            if (startPool(bufferSize)) {
                initialized = true;
                std::cout << "[EntropyCollector] Initialized with hardware source: " << activeSourceName << std::endl;
                return true;
            }
        }
    }
    
    // Fallback to OpenSSL
    std::cout << "[EntropyCollector] Hardware unavailable, falling back to OpenSSL..." << std::endl;
    if (initializeOpenSSL()) {
        if (startPool(bufferSize)) {
            initialized = true;
            std::cout << "[EntropyCollector] Initialized with OpenSSL fallback" << std::endl;
            return true;
        }
    }
    
    std::cerr << "[EntropyCollector] Failed to initialize any entropy source!" << std::endl;
    return false;
}

bool EntropyCollector::initializeWithSource(
    const std::string& sourceType,
    const std::string& port,
    unsigned int baudRate,
    size_t bufferSize) {
    
    if (initialized) {
        std::cout << "[EntropyCollector] Already initialized" << std::endl;
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
        std::cerr << "[EntropyCollector] Unknown source type: " << sourceType << std::endl;
        return false;
    }
    
    if (!success) {
        std::cerr << "[EntropyCollector] Failed to initialize " << sourceType << std::endl;
        return false;
    }
    
    if (!startPool(bufferSize)) {
        std::cerr << "[EntropyCollector] Failed to start entropy pool" << std::endl;
        return false;
    }
    
    initialized = true;
    std::cout << "[EntropyCollector] Initialized with " << sourceType << " source: " << activeSourceName << std::endl;
    return true;
}

void EntropyCollector::shutdown() {
    if (entropyPool) {
        entropyPool->stop();
        entropyPool.reset();
    }
    
    if (entropySource) {
        entropySource->shutdown();
        entropySource.reset();
    }
    
    initialized = false;
    activeSourceType = "none";
    activeSourceName = "none";
    
    std::cout << "[EntropyCollector] Shutdown complete" << std::endl;
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
        auto source = std::make_shared<SerialEntropySource>(port, baudRate);
        
        if (source->initialize()) {
            entropySource = source;
            activeSourceType = "hardware";
            activeSourceName = source->getSourceName();
            hardwareAvailable = true;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[EntropyCollector] Hardware initialization error: " << e.what() << std::endl;
    }
    
    return false;
}

bool EntropyCollector::initializeOpenSSL() {
    try {
        auto source = std::make_shared<OpenSSLEntropySource>();
        
        if (source->initialize()) {
            entropySource = source;
            activeSourceType = "openssl";
            activeSourceName = source->getSourceName();
            openSSLAvailable = true;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[EntropyCollector] OpenSSL initialization error: " << e.what() << std::endl;
    }
    
    return false;
}

bool EntropyCollector::startPool(size_t bufferSize) {
    if (!entropySource) {
        return false;
    }
    
    try {
        entropyPool = std::make_unique<EntropyPool>(
            entropySource,
            bufferSize,
            bufferSize / 2  // Refill at 50%
        );
        
        if (entropyPool->start()) {
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[EntropyCollector] Pool start error: " << e.what() << std::endl;
    }
    
    entropyPool.reset();
    return false;
}

void EntropyCollector::cleanup() {
    if (entropyPool) {
        entropyPool->stop();
        entropyPool.reset();
    }
    
    if (entropySource) {
        entropySource->shutdown();
        entropySource.reset();
    }
}
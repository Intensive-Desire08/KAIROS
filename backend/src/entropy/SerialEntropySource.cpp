#include "SerialEntropySource.h"
#include <iostream>
#include <chrono>
#include <thread>

#if PQT_HAS_BOOST_ASIO
    #include <array>
    #include <boost/asio/read.hpp>
    #include <boost/asio/write.hpp>
    #include <boost/asio/read_until.hpp>

using namespace boost::asio;
#endif

SerialEntropySource::SerialEntropySource(const std::string& port, unsigned int baudRate, unsigned int timeoutMs)
    : portName(port)
    , baudRate(baudRate)
    , timeoutMs(timeoutMs)
#if PQT_HAS_BOOST_ASIO
    , serialPort(ioContext)
#endif
    , initialized(false)
    , available(false)
    , bytesGenerated(0) {
}

SerialEntropySource::~SerialEntropySource() {
    shutdown();
}

bool SerialEntropySource::initialize() {
    std::lock_guard<std::mutex> lock(mutex);

#if !PQT_HAS_BOOST_ASIO
    std::cerr << "[SerialEntropySource] Boost.Asio is unavailable; hardware serial entropy is disabled" << std::endl;
    available = false;
    initialized = true;
    return false;
#else
    
    try {
        // Open the serial port
        serialPort.open(portName);
        
        // Set baud rate
        serialPort.set_option(serial_port_base::baud_rate(baudRate));
        serialPort.set_option(serial_port_base::character_size(8));
        serialPort.set_option(serial_port_base::stop_bits(serial_port_base::stop_bits::one));
        serialPort.set_option(serial_port_base::parity(serial_port_base::parity::none));
        serialPort.set_option(serial_port_base::flow_control(serial_port_base::flow_control::none));
        
        // Flush any pending data
        flushBuffer();
        
        // Test if hardware is responsive
        available = testConnection();
        
        if (available) {
            std::cout << "[SerialEntropySource] Connected to ESP32 on " << portName << std::endl;
        } else {
            std::cout << "[SerialEntropySource] Warning: No response from ESP32 on " << portName << std::endl;
            // Still mark as initialized but not available
        }
        
        initialized = true;
        return available;
        
    } catch (const std::exception& e) {
        std::cerr << "[SerialEntropySource] Failed to open port " << portName << ": " << e.what() << std::endl;
        available = false;
        initialized = true; // Still mark as initialized so we can fallback
        return false;
    }
#endif
}

void SerialEntropySource::shutdown() {
    std::lock_guard<std::mutex> lock(mutex);

#if !PQT_HAS_BOOST_ASIO
    available = false;
    initialized = false;
    return;
#endif
    
    if (serialPort.is_open()) {
        try {
            serialPort.close();
            std::cout << "[SerialEntropySource] Serial port closed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[SerialEntropySource] Error closing port: " << e.what() << std::endl;
        }
    }
    available = false;
    initialized = false;
}

bool SerialEntropySource::isAvailable() const {
    return available && initialized;
}

std::string SerialEntropySource::getSourceName() const {
    return "ESP32 Hardware TRNG (Dual Photodiode)";
}

std::string SerialEntropySource::getSourceType() const {
    return "hardware";
}

std::vector<uint8_t> SerialEntropySource::getEntropy(size_t numBytes) {
    if (!isAvailable()) {
        throw EntropyException("Serial entropy source not available");
    }

#if !PQT_HAS_BOOST_ASIO
    (void)numBytes;
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    
    std::vector<uint8_t> result;
    result.reserve(numBytes);
    
    for (size_t i = 0; i < numBytes; ++i) {
        result.push_back(getByte());
    }
    
    return result;
#endif
}

uint8_t SerialEntropySource::getByte() {
    if (!isAvailable()) {
        throw EntropyException("Serial entropy source not available");
    }

#if !PQT_HAS_BOOST_ASIO
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    
    std::lock_guard<std::mutex> lock(mutex);
    return readEntropyPacket();
#endif
}

void SerialEntropySource::setTimeout(unsigned int timeoutMs) {
    this->timeoutMs = timeoutMs;
}

unsigned int SerialEntropySource::getTimeout() const {
    return timeoutMs;
}

void SerialEntropySource::flushBuffer() {
#if !PQT_HAS_BOOST_ASIO
    return;
#else
    if (!serialPort.is_open()) {
        return;
    }

    discardBuffer.clear();
#endif
}

size_t SerialEntropySource::getTotalBytesGenerated() const {
    return bytesGenerated;
}

void SerialEntropySource::resetByteCounter() {
    bytesGenerated = 0;
}

bool SerialEntropySource::supportsReseeding() const {
    return false;
}

void SerialEntropySource::reseed() {
    throw EntropyException("Reseeding not supported for hardware entropy source");
}

bool SerialEntropySource::testConnection() {
#if !PQT_HAS_BOOST_ASIO
    return false;
#else
    try {
        // Send a simple test command or just try to read
        // ESP32 continuously outputs entropy, so just try to sync
        return syncToMarker();
    } catch (const std::exception& e) {
        std::cerr << "[SerialEntropySource] Connection test failed: " << e.what() << std::endl;
        return false;
    }
#endif
}

bool SerialEntropySource::syncToMarker() {
#if !PQT_HAS_BOOST_ASIO
    return false;
#else
    const unsigned int startTime = static_cast<unsigned int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    
    while (true) {
        // Check timeout
        const unsigned int currentTime = static_cast<unsigned int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        
        if (currentTime - startTime > SYNC_TIMEOUT_MS) {
            return false; // Timeout
        }
        
        uint8_t byte = readByte();
        if (byte == SYNC_MARKER) {
            return true; // Found sync marker
        }
    }
#endif
}

uint8_t SerialEntropySource::readByte() {
#if !PQT_HAS_BOOST_ASIO
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    if (!serialPort.is_open()) {
        throw EntropyException("Serial port not open");
    }
    
    uint8_t byte = 0;
    boost::system::error_code ec;
    size_t bytesRead = serialPort.read_some(boost::asio::buffer(&byte, 1), ec);
    
    if (ec) {
        throw EntropyException("Failed to read from serial port: " + ec.message());
    }
    
    if (bytesRead != 1) {
        throw EntropyException("Timeout reading from serial port");
    }
    
    return byte;
#endif
}

uint8_t SerialEntropySource::readEntropyPacket() {
#if !PQT_HAS_BOOST_ASIO
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    const unsigned int startTime = static_cast<unsigned int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    
    // First, find sync marker
    while (true) {
        const unsigned int currentTime = static_cast<unsigned int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        
        if (currentTime - startTime > timeoutMs) {
            throw EntropyException("Timeout waiting for sync marker");
        }
        
        uint8_t byte = readByte();
        if (byte == SYNC_MARKER) {
            // Found sync marker, now read the entropy byte
            uint8_t entropyByte = readByte();
            bytesGenerated++;
            return entropyByte;
        }
    }
#endif
}
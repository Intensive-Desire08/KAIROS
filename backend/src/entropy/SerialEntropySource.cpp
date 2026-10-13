#include "../Logger.h"
#include "SerialEntropySource.h"
#include <iostream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <openssl/sha.h>

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
    , bytesGenerated(0)
    , lastByteReceivedTimeMs(0)
    , whiteningAlgorithm(WhiteningAlgorithm::LFSR)
    , lfsrState(LFSR_INITIAL_SEED) {
}

void SerialEntropySource::setWhiteningAlgorithm(WhiteningAlgorithm algo) {
    std::lock_guard<std::mutex> lock(mutex);
    whiteningAlgorithm = algo;
}

SerialEntropySource::WhiteningAlgorithm SerialEntropySource::getWhiteningAlgorithm() const {
    std::lock_guard<std::mutex> lock(mutex);
    return whiteningAlgorithm;
}

SerialEntropySource::~SerialEntropySource() {
    shutdown();
}

bool SerialEntropySource::initialize() {
    std::lock_guard<std::mutex> lock(mutex);

#if !PQT_HAS_BOOST_ASIO
    { std::stringstream ss; ss << "[SerialEntropySource] Boost.Asio is unavailable; hardware serial entropy is disabled"; LOG_ERROR(ss.str()); }
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
        
#ifdef _WIN32
        // Prevent infinite blocking on Windows when hardware is disconnected
        HANDLE handle = serialPort.native_handle();
        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.ReadTotalTimeoutConstant = timeoutMs > 0 ? timeoutMs : 500;
        SetCommTimeouts(handle, &timeouts);

        // Disable DTR and RTS so opening the port does not toggle auto-reset on ESP32
        DCB dcb;
        if (GetCommState(handle, &dcb)) {
            dcb.fDtrControl = DTR_CONTROL_DISABLE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            SetCommState(handle, &dcb);
        }
#endif

        // Flush any pending data
        flushBuffer();
        
        // Test if hardware is responsive
        available = testConnection();
        
        if (available) {
            lastByteReceivedTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
            { std::stringstream ss; ss << "[SerialEntropySource] Connected to ESP32 on " << portName; LOG_INFO(ss.str()); }
        } else {
            // Background reconnect probes while device is paused/silent arrive here; avoid log spam
        }
        
        initialized = true;
        return available;
        
    } catch (const std::exception& e) {
        // Suppress terminal output for continuous polling when disconnected
        // { std::stringstream ss; ss << "[SerialEntropySource] Failed to open port " << portName << ": " << e.what(); LOG_ERROR(ss.str()); }
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
            boost::system::error_code ec;
            serialPort.cancel(ec);
            ioContext.poll();
            serialPort.close(ec);
            { std::stringstream ss; ss << "[SerialEntropySource] Serial port closed"; LOG_INFO(ss.str()); }
        } catch (const std::exception& e) {
            { std::stringstream ss; ss << "[SerialEntropySource] Error closing port: " << e.what(); LOG_ERROR(ss.str()); }
        }
    }
    available = false;
    initialized = false;
}

bool SerialEntropySource::isAvailable() const {
    if (!available.load() || !initialized.load()) {
        return false;
    }

#if PQT_HAS_BOOST_ASIO
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock()) {
        if (!serialPort.is_open()) {
            available.store(false);
            return false;
        }

#ifdef _WIN32
        HANDLE handle = const_cast<boost::asio::serial_port&>(serialPort).native_handle();
        if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
            available.store(false);
            return false;
        }

        DWORD commErrors = 0;
        COMSTAT comStat;
        if (!ClearCommError(handle, &commErrors, &comStat)) {
            // Physical device disconnected / handle invalidated by Windows
            available.store(false);
            return false;
        }
#endif
    }
#endif

    int64_t lastRx = lastByteReceivedTimeMs.load();
    if (lastRx > 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        if (now - lastRx > 1200) {
            available.store(false);
            return false;
        }
    }

    return available.load();
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
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<uint8_t> result;
    result.reserve(numBytes);
    
    for (size_t i = 0; i < numBytes; ++i) {
        result.push_back(readEntropyPacket());
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

#ifdef _WIN32
    HANDLE handle = serialPort.native_handle();
    if (handle != INVALID_HANDLE_VALUE && handle != NULL) {
        PurgeComm(handle, PURGE_RXCLEAR | PURGE_RXABORT);
    }
#endif

    rxHead = 0;
    rxTail = 0;
    packetHead = 0;
    packetTail = 0;
    discardBuffer.clear();
#endif
}

bool SerialEntropySource::isPortOpen() const {
#if !PQT_HAS_BOOST_ASIO
    return false;
#else
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!serialPort.is_open()) {
        return false;
    }

#ifdef _WIN32
    HANDLE handle = const_cast<boost::asio::serial_port&>(serialPort).native_handle();
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        return false;
    }

    DWORD commErrors = 0;
    COMSTAT comStat;
    if (!ClearCommError(handle, &commErrors, &comStat)) {
        return false;
    }
#endif

    return true;
#endif
}

bool SerialEntropySource::hasIncomingData() {
#if !PQT_HAS_BOOST_ASIO
    return false;
#else
    if (rxHead < rxTail) {
        return true;
    }

    if (!serialPort.is_open()) {
        return false;
    }

#ifdef _WIN32
    HANDLE handle = serialPort.native_handle();
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        return false;
    }

    DWORD commErrors = 0;
    COMSTAT comStat;
    if (!ClearCommError(handle, &commErrors, &comStat)) {
        return false;
    }

    return comStat.cbInQue > 0;
#else
    return false;
#endif
#endif
}

bool SerialEntropySource::resumeFromPause() {
#if !PQT_HAS_BOOST_ASIO
    return false;
#else
    std::lock_guard<std::mutex> lock(mutex);
    flushBuffer();
    try {
        if (syncToMarker()) {
            initialized.store(true);
            available.store(true);
            lastByteReceivedTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
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
        { std::stringstream ss; ss << "[SerialEntropySource] Connection test failed: " << e.what(); LOG_ERROR(ss.str()); }
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
        
        uint8_t b1 = readByte();
        if (b1 == SYNC_MARKER_1) {
            uint8_t b2 = readByte();
            if (b2 == SYNC_MARKER_2) {
                // Found 0xAA 0x55, read payload into packetBuffer to lock first packet
                uint8_t rawPayload[CHUNK_PAYLOAD_SIZE];
                for (size_t i = 0; i < CHUNK_PAYLOAD_SIZE; ++i) {
                    rawPayload[i] = readByte();
                }
                whitenPayload(rawPayload, packetBuffer.data(), packetTail);
                packetHead = 0;
                return true;
            }
        }
    }
#endif
}

uint8_t SerialEntropySource::readByte() {
#if !PQT_HAS_BOOST_ASIO
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    // If we have buffered bytes available in user space, return immediately
    if (rxHead < rxTail) {
        return rxBuffer[rxHead++];
    }

    if (!serialPort.is_open()) {
        available.store(false);
        initialized.store(false);
        throw EntropyException("Serial port not open");
    }

#ifdef _WIN32
    HANDLE handle = serialPort.native_handle();
    DWORD commErrors = 0;
    COMSTAT comStat;
    if (!ClearCommError(handle, &commErrors, &comStat)) {
        available.store(false);
        initialized.store(false);
        throw EntropyException("Serial device disconnected");
    }
#endif
    
    rxHead = 0;
    rxTail = 0;
    boost::system::error_code read_ec;
    size_t bytesRead = 0;
    
    try {
        ioContext.restart();

        serialPort.async_read_some(boost::asio::buffer(rxBuffer.data(), rxBuffer.size()),
            [&](const boost::system::error_code& ec, size_t n) {
                read_ec = ec;
                bytesRead = n;
            });
            
        ioContext.run_for(std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 500));
    } catch (const std::exception& e) {
        available.store(false);
        initialized.store(false);
        throw EntropyException(std::string("Exception during read: ") + e.what());
    }
    
    if (bytesRead == 0) {
        try { 
            boost::system::error_code cancel_ec;
            serialPort.cancel(cancel_ec); 
            ioContext.poll();
        } catch (...) {}
        available.store(false);
        throw EntropyException("Timeout reading from serial port");
    }
    
    if (read_ec) {
        available.store(false);
        throw EntropyException("Failed to read from serial port: " + read_ec.message());
    }
    
    lastByteReceivedTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());
    
    rxTail = bytesRead;
    return rxBuffer[rxHead++];
#endif
}

uint8_t SerialEntropySource::readEntropyPacket() {
#if !PQT_HAS_BOOST_ASIO
    throw EntropyException("Serial entropy source unavailable in this build");
#else
    // If we have cached whitened bytes from the current chunk, return immediately
    if (packetHead < packetTail) {
        bytesGenerated++;
        return packetBuffer[packetHead++];
    }

    const unsigned int startTime = static_cast<unsigned int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );

    // Synchronize to next [0xAA][0x55] frame
    while (true) {
        const unsigned int currentTime = static_cast<unsigned int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );

        if (currentTime - startTime > timeoutMs) {
            available.store(false);
            throw EntropyException("Timeout waiting for packet sync header [0xAA][0x55]");
        }

        uint8_t b1 = readByte();
        if (b1 == SYNC_MARKER_1) {
            uint8_t b2 = readByte();
            if (b2 == SYNC_MARKER_2) {
                // Locked frame sync: read full 64-byte payload
                uint8_t rawPayload[CHUNK_PAYLOAD_SIZE];
                for (size_t i = 0; i < CHUNK_PAYLOAD_SIZE; ++i) {
                    rawPayload[i] = readByte();
                }
                whitenPayload(rawPayload, packetBuffer.data(), packetTail);
                packetHead = 0;
                bytesGenerated++;
                return packetBuffer[packetHead++];
            }
            // If b2 was also 0xAA, check next byte in case it is 0x55
            if (b2 == SYNC_MARKER_1) {
                uint8_t b3 = readByte();
                if (b3 == SYNC_MARKER_2) {
                    uint8_t rawPayload[CHUNK_PAYLOAD_SIZE];
                    for (size_t i = 0; i < CHUNK_PAYLOAD_SIZE; ++i) {
                        rawPayload[i] = readByte();
                    }
                    whitenPayload(rawPayload, packetBuffer.data(), packetTail);
                    packetHead = 0;
                    bytesGenerated++;
                    return packetBuffer[packetHead++];
                }
            }
        }
    }
#endif
}

void SerialEntropySource::whitenPayloadLFSR(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength) {
    for (size_t i = 0; i < CHUNK_PAYLOAD_SIZE; ++i) {
        uint8_t mask = 0;
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t lsb = lfsrState & 1;
            lfsrState >>= 1;
            if (lsb) {
                lfsrState ^= LFSR_POLYNOMIAL;
            }
            mask = static_cast<uint8_t>((mask << 1) | lsb);
        }
        outPayload[i] = static_cast<uint8_t>(rawPayload[i] ^ mask);
    }
    outLength = CHUNK_PAYLOAD_SIZE;
}

void SerialEntropySource::whitenPayloadSHA256(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength) {
    SHA256(rawPayload, CHUNK_PAYLOAD_SIZE, outPayload);
    outLength = SHA256_WHITENED_BYTES;
}

void SerialEntropySource::whitenPayload(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength) {
    if (whiteningAlgorithm == WhiteningAlgorithm::SHA256) {
        whitenPayloadSHA256(rawPayload, outPayload, outLength);
    } else {
        whitenPayloadLFSR(rawPayload, outPayload, outLength);
    }
}
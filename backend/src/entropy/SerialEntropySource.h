#pragma once

#include "IEntropySource.h"
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include <cstdint>
#include <string>

#if __has_include(<boost/asio.hpp>)
    #include <boost/asio.hpp>
    #include <boost/asio/serial_port.hpp>
    #define PQT_HAS_BOOST_ASIO 1
#else
    #define PQT_HAS_BOOST_ASIO 0
#endif

/**
 * @brief Hardware entropy source using ESP32 dual photodiode TRNG
 * 
 * Communicates with the ESP32 over USB serial.
 * The ESP32 outputs entropy bytes with a sync marker (0xAA).
 * 
 * Protocol:
 * - Baud Rate: 115200
 * - Format: [0xAA][Random Byte]
 * - The 0xAA sync marker ensures proper byte alignment
 * 
 * Features:
 * - Automatic hardware detection
 * - Sync marker validation
 * - Error recovery on desync
 * - Thread-safe access
 * - Timeout protection
 */
class SerialEntropySource : public IEntropySource {
public:
    /**
     * @brief Constructor
     * @param port Serial port name (e.g., "COM3" on Windows, "/dev/ttyUSB0" on Linux)
     * @param baudRate Communication speed (default: 115200)
     * @param timeout Read timeout in milliseconds (default: 5000ms)
     */
    explicit SerialEntropySource(
        const std::string& port,
        unsigned int baudRate = 115200,
        unsigned int timeoutMs = 5000
    );

    /**
     * @brief Destructor - ensures serial port is closed
     */
    ~SerialEntropySource() override;

    /**
     * @brief Get entropy bytes from the hardware source
     * 
     * Reads entropy bytes from the ESP32 over serial.
     * Each byte is validated with the 0xAA sync marker.
     * 
     * @param numBytes Number of random bytes to generate
     * @return std::vector<uint8_t> Vector containing the entropy bytes
     * @throws EntropyException if reading fails or timeout occurs
     */
    std::vector<uint8_t> getEntropy(size_t numBytes) override;

    /**
     * @brief Get a single byte of entropy
     * @return uint8_t A random byte
     * @throws EntropyException if reading fails
     */
    uint8_t getByte() override;

    /**
     * @brief Check if the hardware is available and responsive
     * 
     * Tests by attempting to read a single byte with a short timeout.
     * 
     * @return true if hardware is connected and responsive
     * @return false if hardware is unavailable
     */
    bool isAvailable() const override;

    /**
     * @brief Get the human-readable name
     * @return std::string "ESP32 Hardware TRNG"
     */
    std::string getSourceName() const override;

    /**
     * @brief Get the source type identifier
     * @return std::string "hardware"
     */
    std::string getSourceType() const override;

    /**
     * @brief Initialize the serial connection
     * 
     * Opens the serial port and attempts to communicate with the ESP32.
     * 
     * @return true if initialization succeeded
     * @return false if initialization failed
     */
    bool initialize() override;

    /**
     * @brief Shutdown the serial connection
     * 
     * Closes the serial port and frees resources.
     */
    void shutdown() override;

    /**
     * @brief Get the total number of bytes generated
     * @return size_t Total bytes generated since initialization
     */
    size_t getTotalBytesGenerated() const override;

    /**
     * @brief Reset the byte counter
     */
    void resetByteCounter() override;

    /**
     * @brief Check if the source supports reseeding
     * @return false - hardware source does not support reseeding
     */
    bool supportsReseeding() const override;

    /**
     * @brief Reseed (not supported for hardware)
     * @throws EntropyException always throws
     */
    void reseed() override;

    /**
     * @brief Set the read timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setTimeout(unsigned int timeoutMs);

    /**
     * @brief Get the current read timeout
     * @return unsigned int Timeout in milliseconds
     */
    unsigned int getTimeout() const;

    /**
     * @brief Flush the serial buffer
     * Clears any pending data in the serial port buffer.
     */
    void flushBuffer();

private:
    /**
     * @brief Test the connection to the ESP32
     * @return true if connection is working
     */
    bool testConnection();

    /**
     * @brief Find and validate the sync marker
     * 
     * Reads from the serial port until 0xAA is found,
     * or timeout occurs.
     * 
     * @return true if sync marker found
     * @return false if timeout occurred
     */
    bool syncToMarker();

    /**
     * @brief Read a single byte from the serial port
     * @return uint8_t The byte read
     * @throws EntropyException if read fails or timeout occurs
     */
    uint8_t readByte();

    /**
     * @brief Read a complete entropy packet from the ESP32
     * 
     * Reads until we get a valid 0xAA sync marker,
     * then reads the following data byte.
     * 
     * @return uint8_t The random byte
     * @throws EntropyException if sync marker not found
     */
    uint8_t readEntropyPacket();

    /**
     * @brief Write data to the serial port (for possible future commands)
     * @param data Data to write
     * @throws EntropyException if write fails
     */
    void writeBytes(const std::vector<uint8_t>& data);

    /**
     * @brief Check if the serial port is open and valid
     * @return true if the port is open
     */
    bool isPortOpen() const;

    // Serial port configuration
    std::string portName;
    unsigned int baudRate;
    unsigned int timeoutMs;

    // Serial communication
#if PQT_HAS_BOOST_ASIO
    boost::asio::io_context ioContext;
    boost::asio::serial_port serialPort;
#endif

    // State management
    std::atomic<bool> initialized;
    std::atomic<bool> available;
    std::atomic<size_t> bytesGenerated;

    // Thread safety
    mutable std::mutex mutex;

    // Buffer for flushing
#if PQT_HAS_BOOST_ASIO
    std::vector<char> discardBuffer;
    boost::system::error_code errorCode;
#endif

    // Constants
    static constexpr uint8_t SYNC_MARKER = 0xAA;
    static constexpr size_t PACKET_SIZE = 2; // [0xAA][Random Byte]
    static constexpr unsigned int DEFAULT_TIMEOUT_MS = 5000;
    static constexpr unsigned int SYNC_TIMEOUT_MS = 1000;
};
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
 * - Baud Rate: 921600
 * - Format: [0xAA][0x55][64 raw bytes] (66-byte chunked frames)
 * - Dual sync marker [0xAA][0x55] ensures robust frame alignment
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
     * @param baudRate Communication speed (default: 921600)
     * @param timeout Read timeout in milliseconds (default: 5000ms)
     */
    explicit SerialEntropySource(
        const std::string& port,
        unsigned int baudRate = 921600,
        unsigned int timeoutMs = 1000
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

    /**
     * @brief Check if the serial port is open and valid (USB connected)
     * @return true if the port is open and device is connected
     */
    bool isPortOpen() const;

    /**
     * @brief Check if new incoming bytes are waiting in the serial buffer
     * @return true if bytes are available in the input queue
     */
    bool hasIncomingData();

    /**
     * @brief Whitening algorithm selection
     */
    enum class WhiteningAlgorithm {
        LFSR,   ///< 32-bit Galois LFSR polynomial XOR (1:1 throughput, ~14.7 KB/s)
        SHA256  ///< NIST SP 800-90B SHA-256 block extraction (2:1 ratio, ~7.3 KB/s)
    };

    /**
     * @brief Set the active whitening algorithm
     * @param algo WhiteningAlgorithm::LFSR or WhiteningAlgorithm::SHA256
     */
    void setWhiteningAlgorithm(WhiteningAlgorithm algo);

    /**
     * @brief Get the active whitening algorithm
     * @return WhiteningAlgorithm Current algorithm
     */
    WhiteningAlgorithm getWhiteningAlgorithm() const;

    /**
     * @brief Resume entropy reception after a pause
     * @return true if sync lock succeeded and new bytes are ready
     */
    bool resumeFromPause();

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
     * @brief Apply 32-bit Galois LFSR whitening to a 64-byte raw payload (1:1 byte mapping)
     * @param rawPayload Pointer to 64 raw bytes received from hardware
     * @param outPayload Destination buffer for whitened bytes (64 bytes)
     * @param outLength Reference receiving number of output bytes generated (64 bytes)
     */
    void whitenPayloadLFSR(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength);

    /**
     * @brief Apply SHA-256 cryptographic whitening to a 64-byte raw payload (NIST SP 800-90B 2:1 extraction)
     * @param rawPayload Pointer to 64 raw bytes received from hardware
     * @param outPayload Destination buffer for conditioned bytes (at least 32 bytes)
     * @param outLength Reference receiving number of output bytes generated (32 bytes)
     */
    void whitenPayloadSHA256(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength);

    /**
     * @brief Dispatch whitening to the active algorithm (LFSR or SHA256)
     * @param rawPayload Pointer to 64 raw bytes received from hardware
     * @param outPayload Destination buffer for whitened bytes
     * @param outLength Reference receiving number of output bytes generated
     */
    void whitenPayload(const uint8_t* rawPayload, uint8_t* outPayload, size_t& outLength);

    /**
     * @brief Write data to the serial port (for possible future commands)
     * @param data Data to write
     * @throws EntropyException if write fails
     */
    void writeBytes(const std::vector<uint8_t>& data);

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
    mutable std::atomic<bool> initialized;
    mutable std::atomic<bool> available;
    std::atomic<size_t> bytesGenerated;
    mutable std::atomic<int64_t> lastByteReceivedTimeMs{0};

    // Whitening algorithm selection (defaults to LFSR to satisfy Gateway 12 KB/s minimum threshold)
    WhiteningAlgorithm whiteningAlgorithm = WhiteningAlgorithm::LFSR;

    // 32-bit Galois LFSR state (taps 32, 22, 2, 1)
    uint32_t lfsrState = 0xACE1u;
    static constexpr uint32_t LFSR_POLYNOMIAL = 0x80200003u;
    static constexpr uint32_t LFSR_INITIAL_SEED = 0xACE1u;

    // SHA-256 Whitening Output Size (NIST SP 800-90B 2:1 extraction: 64 raw bytes -> 32 conditioned bytes)
    static constexpr size_t SHA256_WHITENED_BYTES = 32;

    // Thread safety
    mutable std::mutex mutex;

    // Buffer for flushing
#if PQT_HAS_BOOST_ASIO
    std::vector<char> discardBuffer;
    boost::system::error_code errorCode;
#endif

    // Internal receive buffer for high-throughput batched reads
    static constexpr size_t RX_BUFFER_SIZE = 2048;
    std::array<uint8_t, RX_BUFFER_SIZE> rxBuffer{};
    size_t rxHead = 0;
    size_t rxTail = 0;

    // 64-byte Chunked Protocol Framing
    static constexpr uint8_t SYNC_MARKER_1 = 0xAA;
    static constexpr uint8_t SYNC_MARKER_2 = 0x55;
    static constexpr size_t CHUNK_PAYLOAD_SIZE = 64;
    static constexpr size_t PACKET_SIZE = 66; // [0xAA][0x55][64 Bytes Payload]
    static constexpr unsigned int DEFAULT_TIMEOUT_MS = 1000;
    static constexpr unsigned int SYNC_TIMEOUT_MS = 1000;

    // Whitened packet chunk buffer
    std::array<uint8_t, CHUNK_PAYLOAD_SIZE> packetBuffer{};
    size_t packetHead = 0;
    size_t packetTail = 0;
};
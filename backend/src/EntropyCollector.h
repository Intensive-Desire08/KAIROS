#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>

// Forward declarations
class IEntropySource;
class EntropyPool;

/**
 * @brief Manages entropy source selection and provides entropy to the application
 * 
 * The EntropyCollector is the main entry point for entropy in the system.
 * It handles:
 * - Detecting available entropy sources (hardware first, then software)
 * - Initializing the chosen source
 * - Managing the EntropyPool
 * - Providing a unified interface for entropy requests
 * 
 * Typical usage:
 * @code
 * EntropyCollector collector;
 * collector.detectSources();
 * collector.initialize("COM3", 115200);
 * auto bytes = collector.getEntropy(32);
 * @endcode
 */
class EntropyCollector {
public:
    /**
     * @brief Constructor
     */
    EntropyCollector();

    /**
     * @brief Destructor
     */
    ~EntropyCollector();

    /**
     * @brief Detect and list all available entropy sources
     * 
     * @return std::vector<std::string> List of source names
     */
    std::vector<std::string> detectSources();

    /**
     * @brief Initialize the entropy collector with automatic source selection
     * 
     * Tries hardware first, falls back to OpenSSL if hardware is unavailable.
     * 
     * @param port Serial port name (e.g., "COM3" on Windows)
     * @param baudRate Serial baud rate (default: 115200)
     * @param bufferSize Size of entropy pool buffer (default: 4096)
     * @return true if initialization succeeded
     */
    bool initialize(
        const std::string& port = "COM3",
        unsigned int baudRate = 115200,
        size_t bufferSize = 4096
    );

    /**
     * @brief Initialize with a specific entropy source
     * 
     * @param sourceType Type of source ("hardware" or "openssl")
     * @param port Serial port name (only used for hardware)
     * @param baudRate Serial baud rate (only used for hardware)
     * @param bufferSize Size of entropy pool buffer
     * @return true if initialization succeeded
     */
    bool initializeWithSource(
        const std::string& sourceType,
        const std::string& port = "COM3",
        unsigned int baudRate = 115200,
        size_t bufferSize = 4096
    );

    /**
     * @brief Shutdown the entropy collector
     */
    void shutdown();

    /**
     * @brief Get entropy bytes (blocking)
     * 
     * @param numBytes Number of bytes to get
     * @return std::vector<uint8_t> Entropy bytes
     * @throws EntropyException if not initialized or source unavailable
     */
    std::vector<uint8_t> getEntropy(size_t numBytes);

    /**
     * @brief Get a single byte of entropy (blocking)
     * 
     * @return uint8_t Random byte
     * @throws EntropyException if not initialized or source unavailable
     */
    uint8_t getByte();

    /**
     * @brief Try to get entropy without blocking
     * 
     * @param numBytes Number of bytes requested
     * @param buffer Output buffer to fill
     * @return size_t Actual number of bytes read
     */
    size_t tryGetEntropy(size_t numBytes, std::vector<uint8_t>& buffer);

    /**
     * @brief Get the current entropy source name
     * @return std::string Source name
     */
    std::string getSourceName() const;

    /**
     * @brief Get the current source type
     * @return std::string Source type ("hardware" or "openssl")
     */
    std::string getSourceType() const;

    /**
     * @brief Check if hardware entropy is available
     * @return true if hardware source is available
     */
    bool isHardwareAvailable() const;

    /**
     * @brief Check if OpenSSL entropy is available
     * @return true if OpenSSL source is available
     */
    bool isOpenSSLAvailable() const;

    /**
     * @brief Check if the collector is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

    /**
     * @brief Get the total bytes generated
     * @return size_t Total bytes generated
     */
    size_t getTotalBytesGenerated() const;

    /**
     * @brief Reset the byte counter
     */
    void resetByteCounter();

    /**
     * @brief Get the entropy pool size
     * @return size_t Pool size in bytes
     */
    size_t getPoolSize() const;

    /**
     * @brief Get the number of bytes available in the pool
     * @return size_t Available bytes
     */
    size_t getAvailableBytes() const;

    /**
     * @brief Force a refill of the entropy pool
     * @return true if refill succeeded
     */
    bool refillPool();

    /**
     * @brief Set the serial port to use for hardware
     * @param port Serial port name
     */
    void setPort(const std::string& port);

    /**
     * @brief Set the baud rate for hardware serial
     * @param baudRate Baud rate
     */
    void setBaudRate(unsigned int baudRate);

    /**
     * @brief Set the entropy pool buffer size
     * @param bufferSize Buffer size in bytes
     */
    void setBufferSize(size_t bufferSize);

    /**
     * @brief Get the current serial port
     * @return std::string Port name
     */
    std::string getPort() const;

    /**
     * @brief Get the current baud rate
     * @return unsigned int Baud rate
     */
    unsigned int getBaudRate() const;

private:
    /**
     * @brief Initialize the hardware entropy source
     * @param port Serial port
     * @param baudRate Baud rate
     * @return true if successful
     */
    bool initializeHardware(const std::string& port, unsigned int baudRate);

    /**
     * @brief Initialize the OpenSSL entropy source
     * @return true if successful
     */
    bool initializeOpenSSL();

    /**
     * @brief Create and start the entropy pool
     * @param bufferSize Buffer size
     * @return true if successful
     */
    bool startPool(size_t bufferSize);

    /**
     * @brief Clean up resources
     */
    void cleanup();

    // Entropy source
    std::shared_ptr<IEntropySource> entropySource;
    std::unique_ptr<EntropyPool> entropyPool;

    // Configuration
    std::string port;
    unsigned int baudRate;
    size_t bufferSize;

    // State
    bool initialized;
    std::string activeSourceType;
    std::string activeSourceName;

    // Hardware detection status
    bool hardwareAvailable;
    bool openSSLAvailable;

    // Constants
    static constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
    static constexpr size_t REFILL_THRESHOLD = 2048;
};
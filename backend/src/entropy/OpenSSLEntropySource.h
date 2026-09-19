#pragma once

#include "IEntropySource.h"
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/conf.h>

#include <atomic>
#include <mutex>
#include <cstdint>

/**
 * @brief Software entropy source using OpenSSL RAND_bytes()
 * 
 * This serves as a fallback when hardware entropy is unavailable.
 * Uses OpenSSL's cryptographically secure PRNG.
 * 
 * Features:
 * - Cryptographically secure random generation
 * - Automatic fallback (always available)
 * - Thread-safe
 * - Seeded by the operating system
 */
class OpenSSLEntropySource : public IEntropySource {
public:
    /**
     * @brief Constructor
     * 
     * Initializes OpenSSL's random number generator.
     * The OS automatically seeds the PRNG using:
     * - /dev/urandom on Linux
     * - CryptGenRandom on Windows
     * - getentropy on macOS
     */
    explicit OpenSSLEntropySource();

    /**
     * @brief Destructor
     */
    ~OpenSSLEntropySource() override = default;

    /**
     * @brief Get entropy bytes from OpenSSL
     * 
     * Uses RAND_bytes() to generate cryptographically secure
     * pseudo-random bytes.
     * 
     * @param numBytes Number of random bytes to generate
     * @return std::vector<uint8_t> Vector containing the random bytes
     * @throws EntropyException if RAND_bytes() fails
     */
    std::vector<uint8_t> getEntropy(size_t numBytes) override;

    /**
     * @brief Get a single byte of entropy
     * @return uint8_t A random byte
     * @throws EntropyException if generation fails
     */
    uint8_t getByte() override;

    /**
     * @brief Check if OpenSSL is available
     * 
     * OpenSSL is always available as long as the library is linked.
     * 
     * @return true always (software fallback never fails)
     */
    bool isAvailable() const override;

    /**
     * @brief Get the human-readable name
     * @return std::string "OpenSSL PRNG"
     */
    std::string getSourceName() const override;

    /**
     * @brief Get the source type identifier
     * @return std::string "openssl"
     */
    std::string getSourceType() const override;

    /**
     * @brief Initialize the OpenSSL entropy source
     * 
     * Verifies that OpenSSL is correctly initialized.
     * 
     * @return true if initialization succeeded
     * @return false if initialization failed
     */
    bool initialize() override;

    /**
     * @brief Shutdown the entropy source
     * 
     * OpenSSL cleanup (optional).
     */
    void shutdown() override;

    /**
     * @brief Get the total number of bytes generated
     * @return size_t Total bytes generated
     */
    size_t getTotalBytesGenerated() const override;

    /**
     * @brief Reset the byte counter
     */
    void resetByteCounter() override;

    /**
     * @brief Check if the source supports reseeding
     * @return true - OpenSSL supports reseeding
     */
    bool supportsReseeding() const override;

    /**
     * @brief Reseed the OpenSSL PRNG
     * 
     * Forces the PRNG to reseed from the OS entropy sources.
     * 
     * @throws EntropyException if reseeding fails
     */
    void reseed() override;

    /**
     * @brief Get OpenSSL error string
     * @return std::string Human-readable error
     */
    static std::string getOpenSSLErrorString();

    /**
     * @brief Set custom entropy for seeding (advanced)
     * 
     * Adds additional entropy to the PRNG pool.
     * 
     * @param entropy Additional entropy bytes
     * @throws EntropyException if adding entropy fails
     */
    void addEntropy(const std::vector<uint8_t>& entropy);

private:
    /**
     * @brief Internal method to generate random bytes
     * @param buffer Output buffer
     * @param numBytes Number of bytes to generate
     * @throws EntropyException if generation fails
     */
    void generateBytes(uint8_t* buffer, size_t numBytes);

    /**
     * @brief Check if OpenSSL is initialized
     * @return true if OpenSSL is ready
     */
    bool isOpenSSLReady() const;

    // State management
    std::atomic<bool> initialized;
    std::atomic<size_t> bytesGenerated;

    // Thread safety
    mutable std::mutex mutex;

    // Status
    bool openSSLReady;

    // Constants
    static constexpr size_t MAX_REQUEST_SIZE = 1024 * 1024; // 1MB max per request
    static constexpr size_t DEFAULT_SEED_SIZE = 32;
};
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>

#include "../entropy/IEntropySource.h"

/**
 * @brief Cryptographic key generation utilities
 * 
 * Provides:
 * - Random key generation using entropy pool
 * - PBKDF2-SHA256 key derivation from passwords
 * - Salt generation
 */
class KeyGenerator {
public:
    /**
     * @brief Generate a random cryptographic key
     * 
     * @param keySize Size of key in bytes (default: 32 for AES-256)
     * @return std::vector<uint8_t> Random key bytes
     * @throws EntropyException if entropy source is unavailable
     */
    static std::vector<uint8_t> generateKey(size_t keySize = 32);

    /**
     * @brief Derive a key from a password using PBKDF2-SHA256
     * 
     * @param password Password string
     * @param salt Salt bytes (should be at least 16 bytes)
     * @param keySize Desired key size in bytes (default: 32)
     * @param iterations Number of PBKDF2 iterations (default: 100000)
     * @return std::vector<uint8_t> Derived key
     * @throws std::runtime_error if derivation fails
     */
    static std::vector<uint8_t> deriveKey(
        const std::string& password,
        const std::vector<uint8_t>& salt,
        size_t keySize = 32,
        unsigned int iterations = 100000
    );

    /**
     * @brief Generate a cryptographically secure salt
     * 
     * @param saltSize Size of salt in bytes (default: 32)
     * @return std::vector<uint8_t> Random salt
     * @throws EntropyException if entropy source is unavailable
     */
    static std::vector<uint8_t> generateSalt(size_t saltSize = 32);

    /**
     * @brief Generate a random initialization vector (IV)
     * 
     * @param ivSize Size of IV in bytes (default: 12 for GCM)
     * @return std::vector<uint8_t> Random IV
     * @throws EntropyException if entropy source is unavailable
     */
    static std::vector<uint8_t> generateIV(size_t ivSize = 12);

    /**
     * @brief Generate a random nonce
     * 
     * @param nonceSize Size of nonce in bytes (default: 12)
     * @return std::vector<uint8_t> Random nonce
     */
    static std::vector<uint8_t> generateNonce(size_t nonceSize = 12);

    /**
     * @brief Check if a key size is valid for AES
     * 
     * @param keySize Key size in bytes
     * @return true if valid (16, 24, or 32 bytes)
     */
    static bool isValidKeySize(size_t keySize);

    /**
     * @brief Get the recommended iterations for PBKDF2
     * 
     * @return unsigned int Recommended iteration count
     */
    static unsigned int getRecommendedIterations();

    /**
     * @brief Set the entropy source for key generation
     * 
     * @param entropySource Pointer to the entropy source
     */
    static void setEntropySource(std::shared_ptr<IEntropySource> source);

private:
    /**
     * @brief Get entropy bytes from the configured source
     * 
     * @param numBytes Number of bytes to get
     * @return std::vector<uint8_t> Entropy bytes
     */
    static std::vector<uint8_t> getEntropy(size_t numBytes);

    // Entropy source (set by EntropyCollector)
    static std::shared_ptr<IEntropySource> entropySource;
    static std::mutex mutex;

    // Constants
    static constexpr unsigned int DEFAULT_ITERATIONS = 100000;
    static constexpr size_t MIN_SALT_SIZE = 16;
    static constexpr size_t RECOMMENDED_SALT_SIZE = 32;
};
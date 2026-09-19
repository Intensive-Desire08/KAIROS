#pragma once

#include <vector>
#include <cstdint>
#include <string>

#include <openssl/evp.h>

/**
 * @brief Cryptographic hashing utilities
 * 
 * Provides:
 * - SHA-256 hashing
 * - SHA-384 hashing
 * - SHA-512 hashing
 * - HMAC-SHA256
 * - Hex encoding/decoding
 */
class Hasher {
public:
    /**
     * @brief Hash algorithm types
     */
    enum class Algorithm {
        SHA256,
        SHA384,
        SHA512
    };

    /**
     * @brief Compute SHA-256 hash
     * 
     * @param data Data to hash
     * @return std::vector<uint8_t> 32-byte hash
     */
    static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);

    /**
     * @brief Compute SHA-256 hash of a string
     * 
     * @param data String to hash
     * @return std::vector<uint8_t> 32-byte hash
     */
    static std::vector<uint8_t> sha256(const std::string& data);

    /**
     * @brief Compute SHA-384 hash
     * 
     * @param data Data to hash
     * @return std::vector<uint8_t> 48-byte hash
     */
    static std::vector<uint8_t> sha384(const std::vector<uint8_t>& data);

    /**
     * @brief Compute SHA-512 hash
     * 
     * @param data Data to hash
     * @return std::vector<uint8_t> 64-byte hash
     */
    static std::vector<uint8_t> sha512(const std::vector<uint8_t>& data);

    /**
     * @brief Compute hash using specified algorithm
     * 
     * @param data Data to hash
     * @param algorithm Hash algorithm to use
     * @return std::vector<uint8_t> Hash result
     */
    static std::vector<uint8_t> hash(
        const std::vector<uint8_t>& data,
        Algorithm algorithm = Algorithm::SHA256
    );

    /**
     * @brief Compute HMAC-SHA256
     * 
     * @param data Data to authenticate
     * @param key HMAC key
     * @return std::vector<uint8_t> HMAC result (32 bytes)
     */
    static std::vector<uint8_t> hmacSHA256(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& key
    );

    /**
     * @brief Compute HMAC-SHA256 of a string
     * 
     * @param data String to authenticate
     * @param key HMAC key
     * @return std::vector<uint8_t> HMAC result (32 bytes)
     */
    static std::vector<uint8_t> hmacSHA256(
        const std::string& data,
        const std::vector<uint8_t>& key
    );

    /**
     * @brief Convert bytes to hex string
     * 
     * @param data Bytes to convert
     * @return std::string Hex string
     */
    static std::string toHex(const std::vector<uint8_t>& data);

    /**
     * @brief Convert hex string to bytes
     * 
     * @param hex Hex string to convert
     * @return std::vector<uint8_t> Bytes
     * @throws std::runtime_error if hex string is invalid
     */
    static std::vector<uint8_t> fromHex(const std::string& hex);

    /**
     * @brief Get the size of a hash algorithm output
     * 
     * @param algorithm Hash algorithm
     * @return size_t Hash size in bytes
     */
    static size_t getHashSize(Algorithm algorithm);

    /**
     * @brief Verify a hash against data
     * 
     * @param data Data to verify
     * @param expectedHash Expected hash
     * @param algorithm Hash algorithm to use
     * @return true if hash matches
     */
    static bool verify(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& expectedHash,
        Algorithm algorithm = Algorithm::SHA256
    );

private:
    /**
     * @brief Internal hash computation
     * 
     * @param data Data to hash
     * @param algorithm Hash algorithm
     * @return std::vector<uint8_t> Hash result
     */
    static std::vector<uint8_t> hashInternal(
        const std::vector<uint8_t>& data,
        const EVP_MD* md
    );

    /**
     * @brief Get the EVP_MD for an algorithm
     * 
     * @param algorithm Hash algorithm
     * @return const EVP_MD* OpenSSL message digest
     */
    static const EVP_MD* getMessageDigest(Algorithm algorithm);

    // Constants
    static constexpr size_t SHA256_SIZE = 32;
    static constexpr size_t SHA384_SIZE = 48;
    static constexpr size_t SHA512_SIZE = 64;
};
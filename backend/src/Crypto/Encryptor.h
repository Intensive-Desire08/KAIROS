#pragma once

#include <vector>
#include <cstdint>
#include <string>

#include <openssl/evp.h>

/**
 * @brief AES-GCM encryption/decryption utilities
 * 
 * Provides:
 * - AES-128/192/256 GCM encryption
 * - AES-128/192/256 GCM decryption
 * - IV generation
 * - Authentication tag handling
 * 
 * The encryption result includes the authentication tag appended
 * to the ciphertext for simplicity.
 */
class Encryptor {
public:
    /**
     * @brief Encryption result containing ciphertext and authentication tag
     */
    struct EncryptResult {
        std::vector<uint8_t> ciphertext;  // Encrypted data
        std::vector<uint8_t> tag;         // Authentication tag (16 bytes)
        
        /**
         * @brief Combine ciphertext and tag into a single vector
         * @return std::vector<uint8_t> Combined data
         */
        std::vector<uint8_t> combined() const {
            std::vector<uint8_t> result = ciphertext;
            result.insert(result.end(), tag.begin(), tag.end());
            return result;
        }
    };

    /**
     * @brief Encrypt data using AES-GCM
     * 
     * @param plaintext Data to encrypt
     * @param key Encryption key (16, 24, or 32 bytes)
     * @param iv Initialization vector (12 bytes recommended for GCM)
     * @param aad Additional authenticated data (optional)
     * @return EncryptResult Contains ciphertext and authentication tag
     * @throws std::runtime_error if encryption fails
     */
    static EncryptResult encrypt(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& aad = {}
    );

    /**
     * @brief Encrypt data using AES-GCM (combined output)
     * 
     * @param plaintext Data to encrypt
     * @param key Encryption key (16, 24, or 32 bytes)
     * @param iv Initialization vector (12 bytes recommended for GCM)
     * @param aad Additional authenticated data (optional)
     * @return std::vector<uint8_t> Combined ciphertext + tag
     * @throws std::runtime_error if encryption fails
     */
    static std::vector<uint8_t> encryptCombined(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& aad = {}
    );

    /**
     * @brief Decrypt data using AES-GCM
     * 
     * @param ciphertext Encrypted data
     * @param key Decryption key (16, 24, or 32 bytes)
     * @param iv Initialization vector used for encryption
     * @param tag Authentication tag (16 bytes)
     * @param aad Additional authenticated data (optional)
     * @return std::vector<uint8_t> Decrypted plaintext
     * @throws std::runtime_error if decryption fails (bad tag)
     */
    static std::vector<uint8_t> decrypt(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& tag,
        const std::vector<uint8_t>& aad = {}
    );

    /**
     * @brief Decrypt data using AES-GCM (combined input)
     * 
     * @param combined Combined ciphertext + tag
     * @param key Decryption key (16, 24, or 32 bytes)
     * @param iv Initialization vector used for encryption
     * @param aad Additional authenticated data (optional)
     * @return std::vector<uint8_t> Decrypted plaintext
     * @throws std::runtime_error if decryption fails (bad tag)
     */
    static std::vector<uint8_t> decryptCombined(
        const std::vector<uint8_t>& combined,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& aad = {}
    );

    /**
     * @brief Generate a random IV for AES-GCM
     * 
     * @param ivSize Size of IV in bytes (default: 12)
     * @return std::vector<uint8_t> Random IV
     */
    static std::vector<uint8_t> generateIV(size_t ivSize = 12);

    /**
     * @brief Check if a key size is valid for AES
     * 
     * @param keySize Key size in bytes
     * @return true if valid (16, 24, or 32 bytes)
     */
    static bool isValidKeySize(size_t keySize);

    /**
     * @brief Get the recommended IV size for GCM
     * 
     * @return size_t Recommended IV size (12 bytes)
     */
    static size_t getRecommendedIVSize();

    /**
     * @brief Get the authentication tag size for GCM
     * 
     * @return size_t Tag size (16 bytes)
     */
    static size_t getTagSize();

private:
    /**
     * @brief Internal encryption implementation
     */
    static EncryptResult encryptInternal(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& aad
    );

    /**
     * @brief Internal decryption implementation
     */
    static std::vector<uint8_t> decryptInternal(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& tag,
        const std::vector<uint8_t>& aad
    );

    /**
     * @brief Get the OpenSSL cipher for a key size
     * 
     * @param keySize Key size in bytes
     * @return const EVP_CIPHER* OpenSSL cipher
     */
    static const EVP_CIPHER* getCipher(size_t keySize);

    // Constants
    static constexpr size_t RECOMMENDED_IV_SIZE = 12;
    static constexpr size_t TAG_SIZE = 16;
};
#include "KeyGenerator.h"
#include "../entropy/IEntropySource.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <mutex>

// Static member initialization
std::shared_ptr<IEntropySource> KeyGenerator::entropySource;
std::mutex KeyGenerator::mutex;

std::vector<uint8_t> KeyGenerator::generateKey(size_t keySize) {
    if (!isValidKeySize(keySize)) {
        throw std::runtime_error("Invalid key size: " + std::to_string(keySize) + 
                                 " bytes. Must be 16, 24, or 32 bytes.");
    }
    
    return getEntropy(keySize);
}

std::vector<uint8_t> KeyGenerator::deriveKey(
    const std::string& password,
    const std::vector<uint8_t>& salt,
    size_t keySize,
    unsigned int iterations) {
    
    if (password.empty()) {
        throw std::runtime_error("Password cannot be empty");
    }
    
    if (salt.size() < MIN_SALT_SIZE) {
        throw std::runtime_error("Salt must be at least " + 
                                std::to_string(MIN_SALT_SIZE) + " bytes");
    }
    
    if (!isValidKeySize(keySize)) {
        throw std::runtime_error("Invalid key size: " + std::to_string(keySize) + 
                                 " bytes. Must be 16, 24, or 32 bytes.");
    }
    
    std::vector<uint8_t> key(keySize);
    
    // Use PKCS5_PBKDF2_HMAC for PBKDF2 with SHA-256
    int result = PKCS5_PBKDF2_HMAC(
        password.c_str(),
        static_cast<int>(password.length()),
        salt.data(),
        static_cast<int>(salt.size()),
        static_cast<int>(iterations),
        EVP_sha256(),
        static_cast<int>(keySize),
        key.data()
    );
    
    if (result != 1) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        throw std::runtime_error("PBKDF2 key derivation failed: " + std::string(errBuf));
    }
    
    return key;
}

std::vector<uint8_t> KeyGenerator::generateSalt(size_t saltSize) {
    if (saltSize < MIN_SALT_SIZE) {
        saltSize = MIN_SALT_SIZE;
    }
    return getEntropy(saltSize);
}

std::vector<uint8_t> KeyGenerator::generateIV(size_t ivSize) {
    return getEntropy(ivSize);
}

std::vector<uint8_t> KeyGenerator::generateNonce(size_t nonceSize) {
    return getEntropy(nonceSize);
}

bool KeyGenerator::isValidKeySize(size_t keySize) {
    return (keySize == 16 || keySize == 24 || keySize == 32);
}

unsigned int KeyGenerator::getRecommendedIterations() {
    return DEFAULT_ITERATIONS;
}

void KeyGenerator::setEntropySource(std::shared_ptr<IEntropySource> source) {
    std::lock_guard<std::mutex> lock(mutex);
    entropySource = source;
}

std::vector<uint8_t> KeyGenerator::getEntropy(size_t numBytes) {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::vector<uint8_t> result(numBytes);
    
    if (entropySource && entropySource->isAvailable()) {
        // Use the configured entropy source
        result = entropySource->getEntropy(numBytes);
    } else {
        // Fallback to OpenSSL RAND_bytes
        if (RAND_bytes(result.data(), static_cast<int>(numBytes)) != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            throw std::runtime_error("RAND_bytes failed: " + std::string(errBuf));
        }
    }
    
    return result;
}
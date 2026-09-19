#include "Encryptor.h"
#include "../entropy/IEntropySource.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <cstring>
#include <iostream>

Encryptor::EncryptResult Encryptor::encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& aad) {
    
    return encryptInternal(plaintext, key, iv, aad);
}

std::vector<uint8_t> Encryptor::encryptCombined(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& aad) {
    
    auto result = encryptInternal(plaintext, key, iv, aad);
    return result.combined();
}

std::vector<uint8_t> Encryptor::decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& tag,
    const std::vector<uint8_t>& aad) {
    
    return decryptInternal(ciphertext, key, iv, tag, aad);
}

std::vector<uint8_t> Encryptor::decryptCombined(
    const std::vector<uint8_t>& combined,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& aad) {
    
    if (combined.size() < TAG_SIZE) {
        throw std::runtime_error("Combined data too short for tag");
    }
    
    // Split combined data into ciphertext and tag
    size_t ciphertextSize = combined.size() - TAG_SIZE;
    auto offset = static_cast<std::vector<uint8_t>::difference_type>(ciphertextSize);
    std::vector<uint8_t> ciphertext(combined.begin(), combined.begin() + offset);
    std::vector<uint8_t> tag(combined.begin() + offset, combined.end());
    
    return decryptInternal(ciphertext, key, iv, tag, aad);
}

std::vector<uint8_t> Encryptor::generateIV(size_t ivSize) {
    std::vector<uint8_t> iv(ivSize);
    if (RAND_bytes(iv.data(), static_cast<int>(ivSize)) != 1) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        throw std::runtime_error("Failed to generate IV: " + std::string(errBuf));
    }
    return iv;
}

bool Encryptor::isValidKeySize(size_t keySize) {
    return (keySize == 16 || keySize == 24 || keySize == 32);
}

size_t Encryptor::getRecommendedIVSize() {
    return RECOMMENDED_IV_SIZE;
}

size_t Encryptor::getTagSize() {
    return TAG_SIZE;
}

Encryptor::EncryptResult Encryptor::encryptInternal(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& aad) {
    
    // Validate inputs
    if (!isValidKeySize(key.size())) {
        throw std::runtime_error("Invalid key size: " + std::to_string(key.size()) + 
                                 " bytes. Must be 16, 24, or 32 bytes.");
    }
    
    if (iv.empty()) {
        throw std::runtime_error("IV cannot be empty");
    }
    
    // Get the cipher
    const EVP_CIPHER* cipher = getCipher(key.size());
    if (!cipher) {
        throw std::runtime_error("Unsupported key size");
    }
    
    // Create and initialize the context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create encryption context");
    }
    
    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, cipher, nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }
    
    // Add AAD if provided
    if (!aad.empty()) {
        int outLen = 0;
        if (EVP_EncryptUpdate(ctx, nullptr, &outLen, aad.data(), 
                             static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to add AAD");
        }
    }
    
    // Allocate buffer for ciphertext (same size as plaintext plus block)
    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    int totalLen = 0;
    
    // Encrypt
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen, 
                             plaintext.data(), 
                             static_cast<int>(plaintext.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption failed");
        }
        totalLen = outLen;
    }
    
    // Finalize encryption
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + totalLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Final encryption failed");
    }
    totalLen += outLen;
    
    // Resize ciphertext to actual size
    ciphertext.resize(static_cast<std::vector<uint8_t>::size_type>(totalLen));
    
    // Get authentication tag
    std::vector<uint8_t> tag(TAG_SIZE);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, TAG_SIZE, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to get authentication tag");
    }
    
    // Clean up
    EVP_CIPHER_CTX_free(ctx);
    
    return {ciphertext, tag};
}

std::vector<uint8_t> Encryptor::decryptInternal(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& tag,
    const std::vector<uint8_t>& aad) {
    
    // Validate inputs
    if (!isValidKeySize(key.size())) {
        throw std::runtime_error("Invalid key size: " + std::to_string(key.size()) + 
                                 " bytes. Must be 16, 24, or 32 bytes.");
    }
    
    if (iv.empty()) {
        throw std::runtime_error("IV cannot be empty");
    }
    
    if (tag.size() != TAG_SIZE) {
        throw std::runtime_error("Tag must be " + std::to_string(TAG_SIZE) + " bytes");
    }
    
    // Get the cipher
    const EVP_CIPHER* cipher = getCipher(key.size());
    if (!cipher) {
        throw std::runtime_error("Unsupported key size");
    }
    
    // Create and initialize the context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create decryption context");
    }
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, cipher, nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize decryption");
    }
    
    // Add AAD if provided
    if (!aad.empty()) {
        int outLen = 0;
        if (EVP_DecryptUpdate(ctx, nullptr, &outLen, aad.data(), 
                             static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to add AAD during decryption");
        }
    }
    
    // Allocate buffer for plaintext (same size as ciphertext plus block)
    std::vector<uint8_t> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    int totalLen = 0;
    
    // Decrypt
    if (!ciphertext.empty()) {
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &outLen, 
                             ciphertext.data(), 
                             static_cast<int>(ciphertext.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption failed");
        }
        totalLen = outLen;
    }
    
    // Set the expected tag before finalizing
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, TAG_SIZE, 
                           const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set authentication tag");
    }
    
    // Finalize decryption (this verifies the tag)
    int result = EVP_DecryptFinal_ex(ctx, plaintext.data() + totalLen, &outLen);
    
    // Clean up
    EVP_CIPHER_CTX_free(ctx);
    
    if (result != 1) {
        // Tag verification failed
        throw std::runtime_error("Authentication failed - data may have been tampered with");
    }
    
    totalLen += outLen;
    
    // Resize plaintext to actual size
    plaintext.resize(static_cast<std::vector<uint8_t>::size_type>(totalLen));
    
    return plaintext;
}

const EVP_CIPHER* Encryptor::getCipher(size_t keySize) {
    switch (keySize) {
        case 16: return EVP_aes_128_gcm();
        case 24: return EVP_aes_192_gcm();
        case 32: return EVP_aes_256_gcm();
        default: return nullptr;
    }
}
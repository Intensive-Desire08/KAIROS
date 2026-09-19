#include "Hasher.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <iomanip>

std::vector<uint8_t> Hasher::sha256(const std::vector<uint8_t>& data) {
    return hashInternal(data, EVP_sha256());
}

std::vector<uint8_t> Hasher::sha256(const std::string& data) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return sha256(bytes);
}

std::vector<uint8_t> Hasher::sha384(const std::vector<uint8_t>& data) {
    return hashInternal(data, EVP_sha384());
}

std::vector<uint8_t> Hasher::sha512(const std::vector<uint8_t>& data) {
    return hashInternal(data, EVP_sha512());
}

std::vector<uint8_t> Hasher::hash(
    const std::vector<uint8_t>& data,
    Algorithm algorithm) {
    
    const EVP_MD* md = getMessageDigest(algorithm);
    if (!md) {
        throw std::runtime_error("Unsupported hash algorithm");
    }
    return hashInternal(data, md);
}

std::vector<uint8_t> Hasher::hmacSHA256(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& key) {
    
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    unsigned int resultLen = 0;
    
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             data.data(), data.size(),
             result.data(), &resultLen) == nullptr) {
        throw std::runtime_error("HMAC-SHA256 computation failed");
    }
    
    result.resize(resultLen);
    return result;
}

std::vector<uint8_t> Hasher::hmacSHA256(
    const std::string& data,
    const std::vector<uint8_t>& key) {
    
    std::vector<uint8_t> bytes(data.begin(), data.end());
    return hmacSHA256(bytes, key);
}

std::string Hasher::toHex(const std::vector<uint8_t>& data) {
    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}

std::vector<uint8_t> Hasher::fromHex(const std::string& hex) {
    if (hex.length() % 2 != 0) {
        throw std::runtime_error("Invalid hex string length");
    }
    
    std::vector<uint8_t> result;
    result.reserve(hex.length() / 2);
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        char high = hex[i];
        char low = hex[i + 1];
        
        auto hexChar = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            throw std::runtime_error("Invalid hex character");
        };
        
        result.push_back((hexChar(high) << 4) | hexChar(low));
    }
    
    return result;
}

size_t Hasher::getHashSize(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::SHA256: return SHA256_SIZE;
        case Algorithm::SHA384: return SHA384_SIZE;
        case Algorithm::SHA512: return SHA512_SIZE;
        default: return 0;
    }
}

bool Hasher::verify(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& expectedHash,
    Algorithm algorithm) {
    
    auto computed = hash(data, algorithm);
    if (computed.size() != expectedHash.size()) {
        return false;
    }
    
    // Constant-time comparison to prevent timing attacks
    int result = 0;
    for (size_t i = 0; i < computed.size(); ++i) {
        result |= computed[i] ^ expectedHash[i];
    }
    return result == 0;
}

std::vector<uint8_t> Hasher::hashInternal(
    const std::vector<uint8_t>& data,
    const EVP_MD* md) {
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create hash context");
    }
    
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize hash");
    }
    
    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update hash");
    }
    
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    unsigned int resultLen = 0;
    
    if (EVP_DigestFinal_ex(ctx, result.data(), &resultLen) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize hash");
    }
    
    EVP_MD_CTX_free(ctx);
    
    result.resize(resultLen);
    return result;
}

const EVP_MD* Hasher::getMessageDigest(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::SHA256: return EVP_sha256();
        case Algorithm::SHA384: return EVP_sha384();
        case Algorithm::SHA512: return EVP_sha512();
        default: return nullptr;
    }
}
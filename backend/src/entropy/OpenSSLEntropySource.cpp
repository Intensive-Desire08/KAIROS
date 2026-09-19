#include "OpenSSLEntropySource.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <windows.h>
    #include <wincrypt.h>
#else
    #include <unistd.h>
#endif

// Initialize OpenSSL - must be called before any crypto operations
static bool opensslInitialized = false;

static void initializeOpenSSL() {
    if (!opensslInitialized) {
        // Load OpenSSL error strings
        ERR_load_crypto_strings();
        OpenSSL_add_all_algorithms();
        
        // Seed the PRNG with OS entropy if available
        #ifdef _WIN32
            HCRYPTPROV hProv;
            if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
                BYTE seed[32];
                if (CryptGenRandom(hProv, sizeof(seed), seed)) {
                    RAND_seed(seed, sizeof(seed));
                }
                CryptReleaseContext(hProv, 0);
            }
        #else
            // On Unix-like systems, OpenSSL seeds from /dev/urandom automatically
        #endif
        
        opensslInitialized = true;
    }
}

OpenSSLEntropySource::OpenSSLEntropySource() 
    : initialized(false)
    , bytesGenerated(0)
    , openSSLReady(false) {
}

bool OpenSSLEntropySource::initialize() {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        // Initialize OpenSSL
        initializeOpenSSL();
        openSSLReady = true;
        initialized = true;
        
        std::cout << "[OpenSSLEntropySource] OpenSSL PRNG initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[OpenSSLEntropySource] Failed to initialize: " << e.what() << std::endl;
        initialized = false;
        openSSLReady = false;
        return false;
    }
}

void OpenSSLEntropySource::shutdown() {
    std::lock_guard<std::mutex> lock(mutex);
    
    initialized = false;
    openSSLReady = false;
    bytesGenerated = 0;
}

bool OpenSSLEntropySource::isAvailable() const {
    return initialized && openSSLReady;
}

std::string OpenSSLEntropySource::getSourceName() const {
    return "OpenSSL PRNG (Software Fallback)";
}

std::string OpenSSLEntropySource::getSourceType() const {
    return "openssl";
}

std::vector<uint8_t> OpenSSLEntropySource::getEntropy(size_t numBytes) {
    if (!isAvailable()) {
        throw EntropyException("OpenSSL entropy source not initialized");
    }
    
    std::vector<uint8_t> result(numBytes);
    generateBytes(result.data(), numBytes);
    return result;
}

uint8_t OpenSSLEntropySource::getByte() {
    if (!isAvailable()) {
        throw EntropyException("OpenSSL entropy source not initialized");
    }
    
    uint8_t byte;
    generateBytes(&byte, 1);
    return byte;
}

void OpenSSLEntropySource::generateBytes(uint8_t* buffer, size_t numBytes) {
    if (!openSSLReady || !initialized) {
        throw EntropyException("OpenSSL entropy source not ready");
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    
    if (numBytes == 0) {
        return;
    }
    
    // Use OpenSSL's RAND_bytes for cryptographically secure random bytes
    int result = RAND_bytes(buffer, static_cast<int>(numBytes));
    
    if (result != 1) {
        // RAND_bytes failed - get error
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        throw EntropyException("RAND_bytes failed: " + std::string(errBuf));
    }
    
    bytesGenerated += numBytes;
}

void OpenSSLEntropySource::addEntropy(const std::vector<uint8_t>& entropy) {
    if (entropy.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    
    // Add custom entropy to the PRNG pool
    RAND_seed(entropy.data(), static_cast<int>(entropy.size()));
    std::cout << "[OpenSSLEntropySource] Added " << entropy.size() << " bytes of custom entropy" << std::endl;
}

void OpenSSLEntropySource::reseed() {
    if (!isAvailable()) {
        throw EntropyException("OpenSSL entropy source not available for reseeding");
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    
    // Force reseed from OS entropy sources
    #ifdef _WIN32
        HCRYPTPROV hProv;
        if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            BYTE seed[64];
            if (CryptGenRandom(hProv, sizeof(seed), seed)) {
                RAND_seed(seed, sizeof(seed));
            }
            CryptReleaseContext(hProv, 0);
        }
    #else
        // On Unix, just call RAND_poll() to gather more entropy
        RAND_poll();
    #endif
    
    std::cout << "[OpenSSLEntropySource] Reseeded PRNG" << std::endl;
}

size_t OpenSSLEntropySource::getTotalBytesGenerated() const {
    return bytesGenerated;
}

void OpenSSLEntropySource::resetByteCounter() {
    std::lock_guard<std::mutex> lock(mutex);
    bytesGenerated = 0;
}

bool OpenSSLEntropySource::supportsReseeding() const {
    return true;
}

std::string OpenSSLEntropySource::getOpenSSLErrorString() {
    unsigned long err = ERR_get_error();
    if (err == 0) {
        return "No error";
    }
    char errBuf[256];
    ERR_error_string_n(err, errBuf, sizeof(errBuf));
    return std::string(errBuf);
}

bool OpenSSLEntropySource::isOpenSSLReady() const {
    return openSSLReady;
}
#include "Logger.h"
#include "EntropyPool.h"
#include "entropy/IEntropySource.h"
#include <iostream>
#include <algorithm>

EntropyPool::EntropyPool(
    std::shared_ptr<IEntropySource> source,
    size_t bufferSize,
    size_t refillThreshold)
    : entropySource(source)
    , buffer(bufferSize)
    , head(0)
    , tail(0)
    , count(0)
    , bufferSize(bufferSize)
    , refillThreshold(refillThreshold > bufferSize ? bufferSize : refillThreshold)
    , running(false)
    , stopped(false)
    , totalBytesGenerated(0)
    , currentSpeed(0.0)
    , windowBytesCollected(0)
    , windowStartTime(std::chrono::steady_clock::now())
    , speedHoldoffUntil(std::chrono::steady_clock::now())
    , collectorThread() {
    
    if (!entropySource) {
        throw EntropyException("EntropyPool: Invalid entropy source (nullptr)");
    }
}

EntropyPool::~EntropyPool() {
    stop();
}

bool EntropyPool::start() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (running) {
        return true; // Already running
    }
    
    if (!entropySource || !entropySource->isAvailable()) {
        { std::stringstream ss; ss << "[EntropyPool] Cannot start: entropy source not available"; LOG_ERROR(ss.str()); }
        return false;
    }
    
    // Reset state
    head = 0;
    tail = 0;
    count = 0;
    totalBytesGenerated = 0;
    currentSpeed = 0.0;
    windowBytesCollected = 0;
    windowStartTime = std::chrono::steady_clock::now();
    speedHoldoffUntil = std::chrono::steady_clock::now();
    stopped = false;
    running = true;
    
    // Start background thread
    collectorThread = std::thread(&EntropyPool::collectionThread, this);
    
    std::cout << "[EntropyPool] Started with buffer size: " << bufferSize 
              << " bytes, source: " << entropySource->getSourceName() << std::endl;
    
    return true;
}

void EntropyPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running) {
            return;
        }
        running = false;
        stopped = true;
    }
    
    // Wake up any waiting threads
    cv.notify_all();
    refillCV.notify_all();
    
    // Wait for thread to finish
    if (collectorThread.joinable()) {
        collectorThread.join();
    }
    
    { std::stringstream ss; ss << "[EntropyPool] Stopped"; LOG_INFO(ss.str()); }
}

std::vector<uint8_t> EntropyPool::getEntropy(size_t numBytes) {
    if (numBytes == 0) {
        return {};
    }
    
    std::vector<uint8_t> result;
    result.reserve(numBytes);

    while (result.size() < numBytes) {
        std::unique_lock<std::mutex> lock(mutex);
        
        while (count == 0 && !stopped && running) {
            cv.wait(lock);
        }
        
        if (stopped || !running) {
            if (result.empty()) {
                throw EntropyException("EntropyPool is stopped");
            }
            break;
        }
        
        size_t needed = numBytes - result.size();
        size_t toRead = std::min(needed, count);
        auto chunk = readBytes(toRead);
        result.insert(result.end(), chunk.begin(), chunk.end());

        if (needsRefill()) {
            refillCV.notify_one();
        }
    }
    
    return result;
}

uint8_t EntropyPool::getByte() {
    auto bytes = getEntropy(1);
    return bytes[0];
}

size_t EntropyPool::tryGetEntropy(size_t numBytes, std::vector<uint8_t>& buffer) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (stopped || !running) {
        return 0;
    }
    
    size_t bytesToRead = std::min(numBytes, count);
    if (bytesToRead == 0) {
        return 0;
    }
    
    auto bytes = readBytes(bytesToRead);
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    
    // If we're below the refill threshold, wake up the collector
    if (needsRefill()) {
        refillCV.notify_one();
    }
    
    return bytesToRead;
}

size_t EntropyPool::available() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count;
}

size_t EntropyPool::getTotalBytesGenerated() const {
    return totalBytesGenerated;
}

double EntropyPool::getSpeed() const {
    return currentSpeed;
}

bool EntropyPool::isRunning() const {
    return running;
}

bool EntropyPool::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count == 0;
}

bool EntropyPool::isFull() const {
    std::lock_guard<std::mutex> lock(mutex);
    return count == bufferSize;
}

size_t EntropyPool::getBufferSize() const {
    return bufferSize;
}

void EntropyPool::setRefillThreshold(size_t threshold) {
    std::lock_guard<std::mutex> lock(mutex);
    refillThreshold = std::min(threshold, bufferSize);
}

size_t EntropyPool::getRefillThreshold() const {
    return refillThreshold;
}

bool EntropyPool::refill() {
    return collectEntropy();
}

void EntropyPool::resetSpeed() {
    std::lock_guard<std::mutex> lock(mutex);
    currentSpeed.store(0.0);
    windowBytesCollected.store(0);
    windowStartTime = std::chrono::steady_clock::now();
    speedHoldoffUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
}

void EntropyPool::setSource(std::shared_ptr<IEntropySource> source) {
    if (!source) {
        throw EntropyException("EntropyPool: Invalid entropy source (nullptr)");
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    entropySource = source;
    currentSpeed.store(0.0);
    windowBytesCollected.store(0);
    windowStartTime = std::chrono::steady_clock::now();
    speedHoldoffUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    { std::stringstream ss; ss << "[EntropyPool] Source changed to: " << entropySource->getSourceName(); LOG_INFO(ss.str()); }

    // Wake up collector thread immediately to switch to new source
    refillCV.notify_all();
    cv.notify_all();
}

std::string EntropyPool::getSourceName() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entropySource ? entropySource->getSourceName() : "No source";
}

void EntropyPool::collectionThread() {
    { std::stringstream ss; ss << "[EntropyPool] Collection thread started"; LOG_INFO(ss.str()); }
    
    while (running) {
        bool needRefill = false;
        std::shared_ptr<IEntropySource> source;
        {
            std::lock_guard<std::mutex> lock(mutex);
            needRefill = needsRefill();
            source = entropySource;
        }
        
        if (!source || !source->isAvailable()) {
            currentSpeed.store(0.0);
            windowBytesCollected.store(0);
            windowStartTime = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        bool isHardware = (source->getSourceType() == "hardware");
        
        if (needRefill || isHardware) {
            // Collect entropy without holding pool mutex during I/O
            bool success = collectEntropy();
            
            if (!success) {
                currentSpeed.store(0.0);
                windowBytesCollected.store(0);
                windowStartTime = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else if (!needRefill && isHardware) {
                // Buffer is full; yield CPU slice so other threads can run without artificially throttling serial stream
                std::this_thread::yield();
            }
        } else {
            // Buffer is full and source is software (OpenSSL).
            // Benchmark periodically to keep speed metric fresh without high CPU usage.
            try {
                auto start = std::chrono::steady_clock::now();
                std::vector<uint8_t> entropy = source->getEntropy(COLLECTION_BATCH_SIZE);
                auto end = std::chrono::steady_clock::now();
                
                if (!entropy.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        addBytes(entropy);
                        totalBytesGenerated += entropy.size();
                    }
                    cv.notify_all();
                    
                    auto now = std::chrono::steady_clock::now();
                    if (now < speedHoldoffUntil) {
                        currentSpeed.store(0.0);
                    } else {
                        std::chrono::duration<double> elapsed = end - start;
                        if (elapsed.count() > 0) {
                            double inst_speed = entropy.size() / elapsed.count();
                            double curr = currentSpeed.load();
                            currentSpeed.store(curr == 0.0 ? inst_speed : (0.2 * inst_speed + 0.8 * curr));
                        }
                    }
                }
            } catch (...) {
                currentSpeed.store(0.0);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    { std::stringstream ss; ss << "[EntropyPool] Collection thread stopped"; LOG_INFO(ss.str()); }
}

bool EntropyPool::collectEntropy() {
    std::shared_ptr<IEntropySource> source;
    {
        std::lock_guard<std::mutex> lock(mutex);
        source = entropySource;
    }
    
    if (!source) {
        return false;
    }
    
    if (!source->isAvailable()) {
        currentSpeed.store(0.0);
        windowBytesCollected.store(0);
        windowStartTime = std::chrono::steady_clock::now();
        return false;
    }
    
    try {
        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> entropy = source->getEntropy(COLLECTION_BATCH_SIZE);
        auto end = std::chrono::steady_clock::now();
        
        if (!entropy.empty()) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                addBytes(entropy);
                totalBytesGenerated += entropy.size();
            }
            cv.notify_all();
            
            auto now = std::chrono::steady_clock::now();
            if (now < speedHoldoffUntil) {
                currentSpeed.store(0.0);
            } else {
                if (source->getSourceType() == "hardware") {
                    // For hardware, calculate real continuous throughput over 1-second sliding window!
                    windowBytesCollected += entropy.size();
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - windowStartTime).count();
                    if (elapsedMs >= 1000) {
                        double realSpeed = (static_cast<double>(windowBytesCollected.load()) * 1000.0) / static_cast<double>(elapsedMs);
                        double curr = currentSpeed.load();
                        currentSpeed.store(curr == 0.0 ? realSpeed : (0.3 * realSpeed + 0.7 * curr));
                        windowBytesCollected.store(0);
                        windowStartTime = now;
                    }
                } else {
                    std::chrono::duration<double> elapsed = end - start;
                    if (elapsed.count() > 0) {
                        double inst_speed = entropy.size() / elapsed.count();
                        double curr = currentSpeed.load();
                        currentSpeed.store(curr == 0.0 ? inst_speed : (0.2 * inst_speed + 0.8 * curr));
                    }
                }
            }
            
            return true;
        }
        
        currentSpeed.store(0.0);
        windowBytesCollected.store(0);
        windowStartTime = std::chrono::steady_clock::now();
        return false;
        
    } catch (const std::exception& e) {
        { std::stringstream ss; ss << "[EntropyPool] Error collecting entropy: " << e.what(); LOG_ERROR(ss.str()); }
        currentSpeed.store(0.0);
        windowBytesCollected.store(0);
        windowStartTime = std::chrono::steady_clock::now();
        return false;
    }
}

void EntropyPool::addBytes(const std::vector<uint8_t>& data) {
    // Assumes lock is held
    if (data.empty()) {
        return;
    }
    
    // Circular FIFO insertion with overwrite when full
    for (uint8_t byte : data) {
        buffer[tail] = byte;
        tail = (tail + 1) % bufferSize;
        if (count < bufferSize) {
            count++;
        } else {
            // Buffer is full: advance head to overwrite oldest byte
            head = (head + 1) % bufferSize;
        }
    }
    
    // Notify waiting readers
    cv.notify_all();
}

std::vector<uint8_t> EntropyPool::readBytes(size_t numBytes) {
    // Assumes lock is held
    if (numBytes == 0 || count == 0) {
        return {};
    }
    
    size_t bytesToRead = std::min(numBytes, count);
    std::vector<uint8_t> result(bytesToRead);
    
    for (size_t i = 0; i < bytesToRead; ++i) {
        result[i] = buffer[head];
        head = (head + 1) % bufferSize;
    }
    
    count -= bytesToRead;
    
    // If we're below the refill threshold, wake up the collector
    if (needsRefill()) {
        refillCV.notify_one();
    }
    
    return result;
}

bool EntropyPool::needsRefill() const {
    // Assumes lock is held
    return count < refillThreshold && running;
}
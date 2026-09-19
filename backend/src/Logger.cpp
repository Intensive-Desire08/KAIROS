#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
    #include <windows.h>
    #undef ERROR
#else
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

// Static member initialization
std::unique_ptr<Logger> Logger::instance_;
std::once_flag Logger::initFlag_;

Logger& Logger::instance() {
    std::call_once(initFlag_, []() {
        instance_.reset(new Logger());
    });
    return *instance_;
}

Logger::Logger()
    : consoleOutput(true)
    , fileOutput(false)
    , minLevel(Level::INFO)
    , maxFileSize(10 * 1024 * 1024)  // 10MB
    , maxBackupFiles(3)
    , initialized_(false)
    , entryCount_(0) {
}

Logger::~Logger() {
    shutdown();
}

bool Logger::initialize(
    const std::string& logFilePathValue,
    bool consoleOutput,
    Level minLevel,
    size_t maxFileSize,
    unsigned int maxBackupFiles) {

    {
        std::lock_guard<std::mutex> lock(mutex);

        if (initialized_) {
            return true;
        }

        this->logFilePath = logFilePathValue;
        this->consoleOutput = consoleOutput;
        this->minLevel = minLevel;
        this->maxFileSize = maxFileSize;
        this->maxBackupFiles = maxBackupFiles;

        // Open log file if specified
        if (!logFilePath.empty()) {
            // Create directory if it doesn't exist
            std::filesystem::path path(logFilePath);
            auto parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent)) {
                std::filesystem::create_directories(parent);
            }

            logFile.open(logFilePath, std::ios::out | std::ios::app);
            if (!logFile.is_open()) {
                std::cerr << "[Logger] Failed to open log file: " << logFilePath << std::endl;
                fileOutput = false;
            } else {
                fileOutput = true;
            }
        }

        initialized_ = true;
    }

    std::cout << "[Logger] Logger initialized (console="
              << (consoleOutput ? "on" : "off")
              << ", file=" << (fileOutput ? "on" : "off")
              << ", level=" << levelToString(minLevel) << ')'
              << std::endl;

    return true;
}

void Logger::shutdown() {

    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!initialized_) {
            return;
        }

        if (logFile.is_open()) {
            logFile.flush();
            logFile.close();
        }

        initialized_ = false;
    }

    std::cout << "[Logger] Logger shutting down..." << std::endl;
}

bool Logger::isInitialized() const {
    return initialized_;
}

void Logger::setMinLevel(Level level) {
    std::lock_guard<std::mutex> lock(mutex);
    minLevel = level;
}

Logger::Level Logger::getMinLevel() const {
    std::lock_guard<std::mutex> lock(mutex);
    return minLevel;
}

void Logger::setConsoleOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    consoleOutput = enabled;
}

void Logger::setFileOutput(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    fileOutput = enabled;
    
    if (fileOutput && !logFile.is_open() && !logFilePath.empty()) {
        logFile.open(logFilePath, std::ios::out | std::ios::app);
    }
}

void Logger::trace(const std::string& message) {
    log(Level::TRACE, message);
}

void Logger::debug(const std::string& message) {
    log(Level::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(Level::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(Level::WARN, message);
}

void Logger::error(const std::string& message) {
    log(Level::ERROR, message);
}

void Logger::fatal(const std::string& message) {
    log(Level::FATAL, message);
}

void Logger::log(Level level, const std::string& message) {
    if (!initialized_ || level < minLevel) {
        return;
    }
    
    std::string formatted = formatMessage(level, message);
    writeMessage(formatted);
}

void Logger::logWithLocation(
    Level level,
    const std::string& message,
    const std::string& file,
    int line,
    const std::string& function) {
    
    if (!initialized_ || level < minLevel) {
        return;
    }
    
    // Extract just the filename from the full path
    std::string filename = file;
    size_t pos = file.find_last_of("/\\");
    if (pos != std::string::npos) {
        filename = file.substr(pos + 1);
    }
    
    std::string location = filename + ":" + std::to_string(line) + " in " + function;
    std::string fullMessage = message + " [" + location + "]";
    
    std::string formatted = formatMessage(level, fullMessage);
    writeMessage(formatted);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(mutex);
    if (logFile.is_open()) {
        logFile.flush();
    }
    std::cout.flush();
    std::cerr.flush();
}

size_t Logger::getEntryCount() const {
    return entryCount_;
}

bool Logger::clearLogFile() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (!fileOutput || !logFile.is_open()) {
        return false;
    }
    
    logFile.close();
    std::ofstream clearFile(logFilePath, std::ios::out | std::ios::trunc);
    if (!clearFile.is_open()) {
        return false;
    }
    clearFile.close();
    
    logFile.open(logFilePath, std::ios::out | std::ios::app);
    entryCount_ = 0;
    
    return true;
}

std::string Logger::getLogFilePath() const {
    std::lock_guard<std::mutex> lock(mutex);
    return logFilePath;
}

std::string Logger::formatMessage(
    Level level,
    const std::string& message,
    const std::string& file,
    int line,
    const std::string& function) {
    
    std::ostringstream oss;
    oss << getTimestamp() << " ";
    oss << "[" << levelToString(level) << "] ";
    oss << message;
    
    return oss.str();
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, DATE_FORMAT);
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string Logger::levelToString(Level level) const {
    switch (level) {
        case Level::TRACE: return "TRACE";
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
        case Level::FATAL: return "FATAL";
        case Level::OFF:   return "OFF";
        default:           return "UNKNOWN";
    }
}

void Logger::writeMessage(const std::string& formattedMessage) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // Extract level from message for console color
    Level level = Level::INFO;
    if (formattedMessage.find("[TRACE]") != std::string::npos) level = Level::TRACE;
    else if (formattedMessage.find("[DEBUG]") != std::string::npos) level = Level::DEBUG;
    else if (formattedMessage.find("[INFO]") != std::string::npos) level = Level::INFO;
    else if (formattedMessage.find("[WARN]") != std::string::npos) level = Level::WARN;
    else if (formattedMessage.find("[ERROR]") != std::string::npos) level = Level::ERROR;
    else if (formattedMessage.find("[FATAL]") != std::string::npos) level = Level::FATAL;
    
    // Write to console
    if (consoleOutput) {
        writeToConsole(formattedMessage, level);
    }
    
    // Write to file
    if (fileOutput && logFile.is_open()) {
        writeToFile(formattedMessage);
    }
    
    entryCount_++;
}

void Logger::writeToConsole(const std::string& message, Level level) {
    // Use cerr for ERROR and FATAL, cout for everything else
    if (level >= Level::ERROR) {
        std::cerr << message << std::endl;
    } else {
        // If console supports colors, use them
#ifdef _WIN32
        // Windows console doesn't support ANSI colors by default
        std::cout << message << std::endl;
#else
        // Unix-like systems support ANSI colors
        if (level <= Level::WARN) {
            // Get color based on level
            std::string color = getColorCode(level);
            std::cout << color << message << getResetColor() << std::endl;
        } else {
            std::cout << message << std::endl;
        }
#endif
    }
}

void Logger::writeToFile(const std::string& message) {
    if (!logFile.is_open()) {
        return;
    }
    
    logFile << message << std::endl;
    
    // Check if we need to rotate
    if (logFile.tellp() > static_cast<std::streampos>(maxFileSize)) {
        rotateLogFile();
    }
}

void Logger::rotateLogFile() {
    if (!fileOutput || logFilePath.empty()) {
        return;
    }
    
    logFile.close();
    
    // Rename existing log file to backup
    for (unsigned int i = maxBackupFiles; i > 0; --i) {
        std::string oldName = logFilePath + "." + std::to_string(i - 1);
        std::string newName = logFilePath + "." + std::to_string(i);
        
        // If old backup exists and we're not at max, rename it
        if (std::filesystem::exists(oldName) && i < maxBackupFiles) {
            std::filesystem::rename(oldName, newName);
        } else if (i == maxBackupFiles && std::filesystem::exists(oldName)) {
            // Remove the oldest backup
            std::filesystem::remove(oldName);
        }
    }
    
    // Rename current log file to .0
    if (std::filesystem::exists(logFilePath)) {
        std::filesystem::rename(logFilePath, logFilePath + ".0");
    }
    
    // Create new log file
    logFile.open(logFilePath, std::ios::out | std::ios::app);
}

std::string Logger::getColorCode(Level level) const {
    switch (level) {
        case Level::TRACE: return "\033[37m";  // White
        case Level::DEBUG: return "\033[36m";  // Cyan
        case Level::INFO:  return "\033[32m";  // Green
        case Level::WARN:  return "\033[33m";  // Yellow
        case Level::ERROR: return "\033[31m";  // Red
        case Level::FATAL: return "\033[1;31m"; // Bold Red
        default:           return "\033[0m";   // Reset
    }
}

std::string Logger::getResetColor() const {
    return "\033[0m";
}
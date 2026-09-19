#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <atomic>
#include <memory>
#include <sstream>

/**
 * @brief Thread-safe logging system with multiple output destinations
 * 
 * Features:
 * - Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
 * - Console output (stdout/stderr)
 * - File output with rotation support
 * - Thread-safe
 * - Timestamped entries
 * - Configurable log level filtering
 * - Source location logging (file, line, function)
 * 
 * Usage:
 * @code
 * Logger::instance().info("Application started");
 * Logger::instance().error("Failed to open port: " + errorMsg);
 * @endcode
 */
class Logger {
public:
    /**
     * @brief Log severity levels
     */
    enum class Level {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5,
        OFF = 6
    };

    /**
     * @brief Get the singleton instance
     * @return Logger& Reference to the logger instance
     */
    static Logger& instance();

    /**
     * @brief Initialize the logger
     * 
     * @param logFile Path to log file (empty for no file output)
     * @param consoleOutput Enable console output (default: true)
     * @param minLevel Minimum log level to output (default: INFO)
     * @param maxFileSize Maximum file size before rotation (default: 10MB)
     * @param maxBackupFiles Number of backup files to keep (default: 3)
     * @return true if initialization succeeded
     */
    bool initialize(
        const std::string& logFile = "",
        bool consoleOutput = true,
        Level minLevel = Level::INFO,
        size_t maxFileSize = 10 * 1024 * 1024,  // 10MB
        unsigned int maxBackupFiles = 3
    );

    /**
     * @brief Shutdown the logger
     */
    void shutdown();

    /**
     * @brief Check if logger is initialized
     * @return true if initialized
     */
    bool isInitialized() const;

    /**
     * @brief Set the minimum log level
     * @param level Minimum level to output
     */
    void setMinLevel(Level level);

    /**
     * @brief Get the current minimum log level
     * @return Level Current minimum level
     */
    Level getMinLevel() const;

    /**
     * @brief Enable or disable console output
     * @param enabled true to enable console output
     */
    void setConsoleOutput(bool enabled);

    /**
     * @brief Enable or disable file output
     * @param enabled true to enable file output
     */
    void setFileOutput(bool enabled);

    /**
     * @brief Log a message at TRACE level
     * @param message Message to log
     */
    void trace(const std::string& message);

    /**
     * @brief Log a message at DEBUG level
     * @param message Message to log
     */
    void debug(const std::string& message);

    /**
     * @brief Log a message at INFO level
     * @param message Message to log
     */
    void info(const std::string& message);

    /**
     * @brief Log a message at WARN level
     * @param message Message to log
     */
    void warn(const std::string& message);

    /**
     * @brief Log a message at ERROR level
     * @param message Message to log
     */
    void error(const std::string& message);

    /**
     * @brief Log a message at FATAL level
     * @param message Message to log
     */
    void fatal(const std::string& message);

    /**
     * @brief Log a message with explicit level
     * @param level Log level
     * @param message Message to log
     */
    void log(Level level, const std::string& message);

    /**
     * @brief Log a message with source location information
     * @param level Log level
     * @param message Message to log
     * @param file Source file name
     * @param line Source line number
     * @param function Source function name
     */
    void logWithLocation(
        Level level,
        const std::string& message,
        const std::string& file,
        int line,
        const std::string& function
    );

    /**
     * @brief Get the current log file path
     * @return std::string Path to current log file
     */
    std::string getLogFilePath() const;

    /**
     * @brief Get the total number of log entries
     * @return size_t Total entries logged
     */
    size_t getEntryCount() const;

    /**
     * @brief Clear the log file
     * @return true if successful
     */
    bool clearLogFile();

    /**
     * @brief Flush the log buffer
     */
    void flush();

private:
    /**
     * @brief Private constructor (singleton)
     */
    Logger();

public:
    /**
     * @brief Destructor
     */
    ~Logger();

private:

    /**
     * @brief Format a log message
     * @param level Log level
     * @param message Message to log
     * @param file Source file (optional)
     * @param line Source line (optional)
     * @param function Source function (optional)
     * @return std::string Formatted log message
     */
    std::string formatMessage(
        Level level,
        const std::string& message,
        const std::string& file = "",
        int line = 0,
        const std::string& function = ""
    );

    /**
     * @brief Get the current timestamp as a string
     * @return std::string Formatted timestamp
     */
    std::string getTimestamp() const;

    /**
     * @brief Get the level name as a string
     * @param level Log level
     * @return std::string Level name
     */
    std::string levelToString(Level level) const;

    /**
     * @brief Write a formatted message to all outputs
     * @param formattedMessage The formatted message to write
     */
    void writeMessage(const std::string& formattedMessage);

    /**
     * @brief Write to console
     * @param message Message to write
     * @param level Log level (for color coding)
     */
    void writeToConsole(const std::string& message, Level level);

    /**
     * @brief Write to file
     * @param message Message to write
     */
    void writeToFile(const std::string& message);

    /**
     * @brief Rotate log file if needed
     */
    void rotateLogFile();

    /**
     * @brief Get the color code for a log level
     * @param level Log level
     * @return std::string ANSI color code
     */
    std::string getColorCode(Level level) const;

    /**
     * @brief Get the reset color code
     * @return std::string ANSI reset code
     */
    std::string getResetColor() const;

    // Singleton instance
    static std::unique_ptr<Logger> instance_;
    static std::once_flag initFlag_;

    // Configuration
    std::string logFilePath;
    bool consoleOutput;
    bool fileOutput;
    Level minLevel;
    size_t maxFileSize;
    unsigned int maxBackupFiles;

    // State
    std::atomic<bool> initialized_;
    std::atomic<size_t> entryCount_;
    std::ofstream logFile;

    // Thread safety
    mutable std::mutex mutex;

    // Constants
    static constexpr const char* DATE_FORMAT = "%Y-%m-%d %H:%M:%S";
    static constexpr size_t BUFFER_FLUSH_THRESHOLD = 100;

    // Disable copy and move
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
};

/**
 * @brief Convenience macros for logging with source location
 */
#define LOG_TRACE(msg) Logger::instance().logWithLocation(Logger::Level::TRACE, msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_DEBUG(msg) Logger::instance().logWithLocation(Logger::Level::DEBUG, msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(msg) Logger::instance().logWithLocation(Logger::Level::INFO, msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARN(msg) Logger::instance().logWithLocation(Logger::Level::WARN, msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(msg) Logger::instance().logWithLocation(Logger::Level::ERROR, msg, __FILE__, __LINE__, __FUNCTION__)
#define LOG_FATAL(msg) Logger::instance().logWithLocation(Logger::Level::FATAL, msg, __FILE__, __LINE__, __FUNCTION__)

/**
 * @brief Macro for conditional logging
 */
#define LOG_IF(level, condition, msg) \
    if (condition) Logger::instance().log(level, msg)

/**
 * @brief Macro for logging with a specific level
 */
#define LOG_LEVEL(level, msg) Logger::instance().log(level, msg)
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <random>

/**
 * @brief Utility functions for common operations
 * 
 * Provides:
 * - File I/O operations
 * - String manipulation
 * - Time utilities
 * - Byte/hex conversion
 * - Platform detection
 */
class Helpers {
public:
    // ========================================================================
    // File I/O
    // ========================================================================

    /**
     * @brief Check if a file exists
     * @param filename Path to file
     * @return true if file exists
     */
    static bool fileExists(const std::string& filename);

    /**
     * @brief Get file size in bytes
     * @param filename Path to file
     * @return size_t File size, 0 if file doesn't exist
     */
    static size_t getFileSize(const std::string& filename);

    /**
     * @brief Read entire file into a byte vector
     * @param filename Path to file
     * @return std::vector<uint8_t> File contents
     * @throws std::runtime_error if file cannot be read
     */
    static std::vector<uint8_t> readFile(const std::string& filename);

    /**
     * @brief Read entire file as a string
     * @param filename Path to file
     * @return std::string File contents
     * @throws std::runtime_error if file cannot be read
     */
    static std::string readFileAsString(const std::string& filename);

    /**
     * @brief Write bytes to a file (overwrites if exists)
     * @param filename Path to file
     * @param data Bytes to write
     * @return true if successful
     */
    static bool writeFile(const std::string& filename, const std::vector<uint8_t>& data);

    /**
     * @brief Write string to a file (overwrites if exists)
     * @param filename Path to file
     * @param content String to write
     * @return true if successful
     */
    static bool writeFile(const std::string& filename, const std::string& content);

    /**
     * @brief Append bytes to a file
     * @param filename Path to file
     * @param data Bytes to append
     * @return true if successful
     */
    static bool appendFile(const std::string& filename, const std::vector<uint8_t>& data);

    /**
     * @brief Create a directory (and parent directories if needed)
     * @param path Directory path
     * @return true if successful or directory already exists
     */
    static bool createDirectory(const std::string& path);

    /**
     * @brief Get the directory part of a path
     * @param path Full path
     * @return std::string Directory path
     */
    static std::string getDirectory(const std::string& path);

    /**
     * @brief Get the filename part of a path
     * @param path Full path
     * @return std::string Filename
     */
    static std::string getFilename(const std::string& path);

    /**
     * @brief Get the file extension
     * @param path Full path
     * @return std::string Extension (without dot)
     */
    static std::string getExtension(const std::string& path);

    // ========================================================================
    // String Manipulation
    // ========================================================================

    /**
     * @brief Convert bytes to hex string
     * @param data Bytes to convert
     * @return std::string Hex string
     */
    static std::string toHex(const std::vector<uint8_t>& data);

    /**
     * @brief Convert hex string to bytes
     * @param hex Hex string to convert
     * @return std::vector<uint8_t> Bytes
     * @throws std::runtime_error if hex string is invalid
     */
    static std::vector<uint8_t> fromHex(const std::string& hex);

    /**
     * @brief Convert bytes to string
     * @param data Bytes to convert
     * @return std::string String
     */
    static std::string toString(const std::vector<uint8_t>& data);

    /**
     * @brief Convert string to bytes
     * @param str String to convert
     * @return std::vector<uint8_t> Bytes
     */
    static std::vector<uint8_t> toBytes(const std::string& str);

    /**
     * @brief Trim whitespace from both ends of a string
     * @param str String to trim
     * @return std::string Trimmed string
     */
    static std::string trim(const std::string& str);

    /**
     * @brief Split a string by delimiter
     * @param str String to split
     * @param delimiter Delimiter character
     * @return std::vector<std::string> Split parts
     */
    static std::vector<std::string> split(const std::string& str, char delimiter);

    /**
     * @brief Join strings with a delimiter
     * @param parts Strings to join
     * @param delimiter Delimiter character
     * @return std::string Joined string
     */
    static std::string join(const std::vector<std::string>& parts, const std::string& delimiter);

    /**
     * @brief Check if string starts with a prefix
     * @param str String to check
     * @param prefix Prefix to look for
     * @return true if string starts with prefix
     */
    static bool startsWith(const std::string& str, const std::string& prefix);

    /**
     * @brief Check if string ends with a suffix
     * @param str String to check
     * @param suffix Suffix to look for
     * @return true if string ends with suffix
     */
    static bool endsWith(const std::string& str, const std::string& suffix);

    /**
     * @brief Replace all occurrences of a substring
     * @param str String to modify
     * @param from Substring to replace
     * @param to Replacement substring
     * @return std::string Modified string
     */
    static std::string replace(const std::string& str, const std::string& from, const std::string& to);

    /**
     * @brief Convert string to lowercase
     * @param str String to convert
     * @return std::string Lowercase string
     */
    static std::string toLower(const std::string& str);

    /**
     * @brief Convert string to uppercase
     * @param str String to convert
     * @return std::string Uppercase string
     */
    static std::string toUpper(const std::string& str);

    // ========================================================================
    // Time Utilities
    // ========================================================================

    /**
     * @brief Get current timestamp as string
     * @param format strftime format (default: "%Y-%m-%d %H:%M:%S")
     * @return std::string Timestamp string
     */
    static std::string getTimestamp(const std::string& format = "%Y-%m-%d %H:%M:%S");

    /**
     * @brief Get current timestamp in milliseconds
     * @return int64_t Milliseconds since epoch
     */
    static int64_t getTimestampMs();

    /**
     * @brief Format a duration as a human-readable string
     * @param ms Duration in milliseconds
     * @return std::string Human-readable duration
     */
    static std::string formatDuration(int64_t ms);

    /**
     * @brief Sleep for a specified number of milliseconds
     * @param ms Milliseconds to sleep
     */
    static void sleepMs(int64_t ms);

    // ========================================================================
    // Random Utilities
    // ========================================================================

    /**
     * @brief Generate a random integer in a range
     * @tparam T Integer type
     * @param min Minimum value (inclusive)
     * @param max Maximum value (inclusive)
     * @return T Random value
     */
    template<typename T>
    static T randomInt(T min, T max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<T> dist(min, max);
        return dist(gen);
    }

    /**
     * @brief Generate a random double in a range
     * @param min Minimum value (inclusive)
     * @param max Maximum value (exclusive)
     * @return double Random value
     */
    static double randomDouble(double min, double max);

    // ========================================================================
    // Platform Detection
    // ========================================================================

    /**
     * @brief Get the operating system name
     * @return std::string OS name ("Windows", "Linux", "macOS", "Unknown")
     */
    static std::string getOSName();

    /**
     * @brief Check if running on Windows
     * @return true if Windows
     */
    static bool isWindows();

    /**
     * @brief Check if running on Linux
     * @return true if Linux
     */
    static bool isLinux();

    /**
     * @brief Check if running on macOS
     * @return true if macOS
     */
    static bool isMacOS();

    /**
     * @brief Get the number of CPU cores
     * @return unsigned int Number of cores
     */
    static unsigned int getCpuCores();

    /**
     * @brief Get the total system memory in bytes
     * @return size_t Total memory
     */
    static size_t getSystemMemory();

    // ========================================================================
    // Serial Port Helpers
    // ========================================================================

    /**
     * @brief Get a list of available serial ports
     * @return std::vector<std::string> List of port names
     */
    static std::vector<std::string> getAvailableSerialPorts();

    /**
     * @brief Check if a serial port exists
     * @param port Port name (e.g., "COM3", "/dev/ttyUSB0")
     * @return true if port exists
     */
    static bool serialPortExists(const std::string& port);

private:
    /**
     * @brief Internal helper to read file as binary
     */
    static std::vector<uint8_t> readFileInternal(const std::string& filename, bool binary);

    /**
     * @brief Internal helper to get the OS name from preprocessor macros
     */
    static std::string getOSNameInternal();
};

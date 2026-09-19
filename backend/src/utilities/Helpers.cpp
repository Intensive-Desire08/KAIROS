#include "Helpers.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
    #include <setupapi.h>
    #include <devguid.h>
    #include <regstr.h>
    #pragma comment(lib, "setupapi.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #ifdef __APPLE__
        #include <mach/mach.h>
    #else
        #include <sys/sysinfo.h>
    #endif
#endif

// ============================================================================
// File I/O
// ============================================================================

bool Helpers::fileExists(const std::string& filename) {
    return std::filesystem::exists(filename);
}

size_t Helpers::getFileSize(const std::string& filename) {
    try {
        return std::filesystem::file_size(filename);
    } catch (const std::exception&) {
        return 0;
    }
}

std::vector<uint8_t> Helpers::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    if (size > 0) {
        file.read(reinterpret_cast<char*>(data.data()), size);
    }
    file.close();
    
    return data;
}

std::string Helpers::readFileAsString(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return buffer.str();
}

bool Helpers::writeFile(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    file.close();
    
    return true;
}

bool Helpers::writeFile(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    file.close();
    return true;
}

bool Helpers::appendFile(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        return false;
    }
    
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    file.close();
    
    return true;
}

bool Helpers::createDirectory(const std::string& path) {
    try {
        if (std::filesystem::exists(path)) {
            return std::filesystem::is_directory(path);
        }
        return std::filesystem::create_directories(path);
    } catch (const std::exception&) {
        return false;
    }
}

std::string Helpers::getDirectory(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::string Helpers::getFilename(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::string Helpers::getExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    return ext;
}

// ============================================================================
// String Manipulation
// ============================================================================

std::string Helpers::toHex(const std::vector<uint8_t>& data) {
    static const char* hex = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}

std::vector<uint8_t> Helpers::fromHex(const std::string& hex) {
    if (hex.length() % 2 != 0) {
        throw std::runtime_error("Invalid hex string length");
    }
    
    std::vector<uint8_t> result;
    result.reserve(hex.length() / 2);
    
    auto hexChar = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("Invalid hex character");
    };
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        result.push_back((hexChar(hex[i]) << 4) | hexChar(hex[i + 1]));
    }
    
    return result;
}

std::string Helpers::toString(const std::vector<uint8_t>& data) {
    return std::string(data.begin(), data.end());
}

std::vector<uint8_t> Helpers::toBytes(const std::string& str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

std::string Helpers::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Helpers::split(const std::string& str, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string part;
    while (std::getline(ss, part, delimiter)) {
        parts.push_back(part);
    }
    return parts;
}

std::string Helpers::join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) {
        return "";
    }
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += delimiter + parts[i];
    }
    return result;
}

bool Helpers::startsWith(const std::string& str, const std::string& prefix) {
    if (prefix.length() > str.length()) {
        return false;
    }
    return str.compare(0, prefix.length(), prefix) == 0;
}

bool Helpers::endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.length() > str.length()) {
        return false;
    }
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

std::string Helpers::replace(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

std::string Helpers::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string Helpers::toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

// ============================================================================
// Time Utilities
// ============================================================================

std::string Helpers::getTimestamp(const std::string& format) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), format.c_str(), &tm_buf);
    return std::string(buffer);
}

int64_t Helpers::getTimestampMs() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    );
    return ms.count();
}

std::string Helpers::formatDuration(int64_t ms) {
    if (ms < 0) return "0ms";
    
    int64_t seconds = ms / 1000;
    int64_t minutes = seconds / 60;
    int64_t hours = minutes / 60;
    int64_t days = hours / 24;
    
    ms %= 1000;
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    std::stringstream ss;
    if (days > 0) ss << days << "d ";
    if (hours > 0) ss << hours << "h ";
    if (minutes > 0) ss << minutes << "m ";
    if (seconds > 0) ss << seconds << "s ";
    ss << ms << "ms";
    
    return ss.str();
}

void Helpers::sleepMs(int64_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ============================================================================
// Random Utilities
// ============================================================================

double Helpers::randomDouble(double min, double max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(min, max);
    return dist(gen);
}

// ============================================================================
// Platform Detection
// ============================================================================

std::string Helpers::getOSNameInternal() {
#ifdef _WIN32
    return "Windows";
#elif __APPLE__
    return "macOS";
#elif __linux__
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string Helpers::getOSName() {
    static std::string osName = getOSNameInternal();
    return osName;
}

bool Helpers::isWindows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool Helpers::isLinux() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool Helpers::isMacOS() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

unsigned int Helpers::getCpuCores() {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#elif __APPLE__
    return static_cast<unsigned int>(sysconf(_SC_NPROCESSORS_ONLN));
#else
    return static_cast<unsigned int>(sysconf(_SC_NPROCESSORS_ONLN));
#endif
}

size_t Helpers::getSystemMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return static_cast<size_t>(status.ullTotalPhys);
#elif __APPLE__
    mach_port_t host_port = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics_data_t vm_stat;
    if (host_statistics(host_port, HOST_VM_INFO, (host_info_t)&vm_stat, &count) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<size_t>(vm_stat.wire_count + vm_stat.active_count + 
                               vm_stat.inactive_count + vm_stat.free_count) * PAGE_SIZE;
#else
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return 0;
    }
    return static_cast<size_t>(info.totalram) * info.mem_unit;
#endif
}

// ============================================================================
// Serial Port Helpers
// ============================================================================

std::vector<std::string> Helpers::getAvailableSerialPorts() {
    std::vector<std::string> ports;
    
#ifdef _WIN32
    // Windows: Enumerate COM ports using registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                     "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                     0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valueName[256];
        char valueData[256];
        DWORD valueNameSize, valueDataSize;
        DWORD type;
        DWORD index = 0;
        
        while (true) {
            valueNameSize = sizeof(valueName);
            valueDataSize = sizeof(valueData);
            if (RegEnumValueA(hKey, index++, valueName, &valueNameSize, nullptr,
                             &type, (LPBYTE)valueData, &valueDataSize) != ERROR_SUCCESS) {
                break;
            }
            if (type == REG_SZ) {
                ports.push_back(std::string(valueData));
            }
        }
        RegCloseKey(hKey);
    }
#else
    // Unix/Linux: Scan /dev for tty* devices
    DIR* dir = opendir("/dev");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("ttyUSB") == 0 || 
                name.find("ttyACM") == 0 || 
                name.find("ttyS") == 0) {
                ports.push_back("/dev/" + name);
            }
        }
        closedir(dir);
    }
#endif
    
    // Sort ports for consistency
    std::sort(ports.begin(), ports.end());
    return ports;
}

bool Helpers::serialPortExists(const std::string& port) {
    auto ports = getAvailableSerialPorts();
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

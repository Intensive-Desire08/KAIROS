#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
    #include <windows.h>
    #undef ERROR
#endif

// cpp-httplib and json
#include "httplib.h"
#include "json.hpp"
#include "../utilities/Helpers.h"

using json = nlohmann::json;

namespace {

const std::string DEFAULT_HOST = "127.0.0.1";
const int DEFAULT_PORT = 8080;
const std::string B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string readLine(const std::string& prompt = "") {
    if (!prompt.empty()) {
        std::cout << prompt;
    }
    std::string s;
    if (!std::getline(std::cin, s)) {
        return "";
    }
    // Trim trailing carriage return if any
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

int readInt(const std::string& prompt, int defaultVal) {
    std::string s = readLine(prompt);
    if (s.empty()) return defaultVal;
    try {
        return std::stoi(s);
    } catch (...) {
        return defaultVal;
    }
}

std::vector<uint8_t> base64Decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    int val = 0;
    int valb = -8;
    for (char c : encoded) {
        if (c == '=') break;
        auto pos = B64_CHARS.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

std::vector<uint8_t> extractEntropyBytes(const json& j) {
    if (j.contains("base64") && j["base64"].is_string() && !j["base64"].get<std::string>().empty()) {
        return base64Decode(j["base64"].get<std::string>());
    }
    if (j.contains("hex") && j["hex"].is_string() && !j["hex"].get<std::string>().empty()) {
        return Helpers::fromHex(j["hex"].get<std::string>());
    }
    if (j.contains("data") && j["data"].is_string() && !j["data"].get<std::string>().empty()) {
        return Helpers::fromHex(j["data"].get<std::string>());
    }
    if (j.contains("bytes") && j["bytes"].is_string() && !j["bytes"].get<std::string>().empty()) {
        return base64Decode(j["bytes"].get<std::string>());
    }
    return {};
}

void printBanner() {
    std::cout << "\n"
              << "=====================================================================\n"
              << "|               KAIROS ENTROPY MODULE - INTERACTIVE CLI             |\n"
              << "|          Hardware-Backed Randomness & Cryptographic Engine        |\n"
              << "=====================================================================\n"
              << std::endl;
}

void printProgressBar(double percentage, int width = 24) {
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    int filled = static_cast<int>(std::round((percentage / 100.0) * width));
    std::cout << "[";
    for (int i = 0; i < filled; ++i) std::cout << "=";
    for (int i = filled; i < width; ++i) std::cout << " ";
    std::cout << "] " << std::fixed << std::setprecision(1) << percentage << "%";
}

bool checkServerOnline(httplib::Client& cli) {
    auto res = cli.Get("/health");
    return (res && res->status == 200);
}

void ensureServerRunning(const std::string& host, int port) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(1, 0);
    cli.set_read_timeout(1, 0);

    if (checkServerOnline(cli)) {
        return;
    }

    std::cout << "\n[Notice] Entropy Service is not running on http://" << host << ":" << port << std::endl;
    std::cout << "Attempting to start EntropyModule.exe in background..." << std::endl;

#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string cmd = "build\\bin\\EntropyModule.exe config\\backend_config.json";
    char cmdBuf[512];
    strncpy_s(cmdBuf, cmd.c_str(), sizeof(cmdBuf) - 1);

    if (CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "[Notice] Launched EntropyModule.exe (PID: " << pi.dwProcessId << "). Waiting for service...";
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::cout << ".";
            if (checkServerOnline(cli)) {
                std::cout << " READY!\n" << std::endl;
                return;
            }
        }
        std::cout << "\n";
    } else {
        std::cerr << "[Warning] Could not automatically launch EntropyModule.exe.\n"
                  << "Please run 'build\\bin\\EntropyModule.exe' in a separate terminal.\n";
    }
#else
    std::string cmd = "./build/bin/EntropyModule config/backend_config.json > /dev/null 2>&1 &";
    int ret = system(cmd.c_str());
    (void)ret;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
#endif
}

// ============================================================================
// Feature 1: System & Pool Status
// ============================================================================
void viewSystemStatus(httplib::Client& cli) {
    std::cout << "\n--- [1] System & Pool Status ---" << std::endl;
    auto res = cli.Get("/status");
    if (!res || res->status != 200) {
        std::cerr << "Error: Unable to fetch /status (HTTP " << (res ? res->status : -1) << ")" << std::endl;
        return;
    }

    try {
        json j = json::parse(res->body);
        std::string version = j.value("version", "v0.3");
        auto ent = j.value("entropy", json::object());

        size_t avail = ent.value("available_bytes", (size_t)0);
        size_t bufferSize = ent.value("pool_size", ent.value("buffer_size", (size_t)4096));
        size_t totalGen = ent.value("total_bytes_generated", ent.value("total_generated", (size_t)0));
        
        double speedVal = ent.value("speed", 0.0);
        double speedBps = ent.value("current_speed_bps", speedVal * 8.0);
        std::string speedFmt = ent.value("speed_formatted", "");
        if (speedFmt.empty()) {
            std::ostringstream ss;
            if (speedVal >= 1024.0 * 1024.0) {
                ss << std::fixed << std::setprecision(2) << (speedVal / (1024.0 * 1024.0)) << " MB/s";
            } else if (speedVal >= 1024.0) {
                ss << std::fixed << std::setprecision(1) << (speedVal / 1024.0) << " KB/s";
            } else {
                ss << std::fixed << std::setprecision(0) << speedVal << " B/s";
            }
            speedFmt = ss.str();
        }

        std::string srcType = ent.value("source_type", "unknown");
        std::string whitening = ent.value("whitening_algorithm", ent.value("whitening", "unknown"));
        std::string whiteningDisplay = "Unknown";
        if (whitening == "lfsr") {
            whiteningDisplay = "32-bit Galois LFSR (0x80200003)";
        } else if (whitening == "sha256") {
            whiteningDisplay = "NIST SP 800-90B SHA-256";
        } else {
            whiteningDisplay = whitening;
        }

        double fillPct = bufferSize > 0 ? (static_cast<double>(avail) * 100.0 / bufferSize) : 0.0;

        std::cout << "  Version           : " << version << std::endl;
        std::cout << "  Active Source     : " << (srcType == "hardware" ? "ESP32 Continuous DMA Hardware" : "OpenSSL PRNG (Software Fallback)") 
                  << " [" << srcType << "]" << std::endl;
        std::cout << "  Whitening Algo    : " << whiteningDisplay << " [" << whitening << "]" << std::endl;
        std::cout << "  Pool Fill Level   : ";
        printProgressBar(fillPct, 24);
        std::cout << " (" << avail << " / " << bufferSize << " bytes)" << std::endl;
        std::cout << "  Generation Speed  : " << speedFmt << " (" << static_cast<size_t>(speedBps) << " bps)" << std::endl;
        std::cout << "  Total Accumulated : " << totalGen << " bytes (" << (totalGen / 1024) << " KB)" << std::endl;

        if (j.contains("server")) {
            auto srv = j["server"];
            std::cout << "  Server Port       : " << srv.value("port", 8080) << std::endl;
            std::cout << "  Server Uptime     : " << srv.value("uptime_seconds", 0) << " seconds" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    }
}

// ============================================================================
// Feature 2: Generate Test Random Data
// ============================================================================
void generateTestEntropy(httplib::Client& cli) {
    while (true) {
        std::cout << "\n--- [2] Generate Test Entropy ---" << std::endl;
        std::cout << "Select Output Format:" << std::endl;
        std::cout << "  [1] Raw Hex & Base64 bytes" << std::endl;
        std::cout << "  [2] Binary Bitstream & 0/1 Balance Check" << std::endl;
        std::cout << "  [3] Uniform Random Integer in Range [min, max]" << std::endl;
        std::cout << "  [4] Uniform Random Float in Range [0.0, 1.0)" << std::endl;
        std::cout << "  [5] Gaming Simulation: Dice Roll (1-6) & Coin Toss" << std::endl;
        std::cout << "  [6] ASCII Byte Frequency Histogram (256-byte sample)" << std::endl;
        std::cout << "  [0] Return to Main Menu" << std::endl;
        
        int subChoice = readInt("\nChoice [0-6] > ", 0);
        if (subChoice == 0) {
            break;
        }

        if (subChoice == 1) {
            int count = readInt("Number of bytes to generate [1-1024, default 32]: ", 32);
            if (count <= 0 || count > 1024) count = 32;

            auto res = cli.Get(("/entropy?bytes=" + std::to_string(count)).c_str());
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy (HTTP " << (res ? res->status : -1) << ")" << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.empty()) {
                std::cerr << "Failed to decode entropy data from server response." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }

            std::string hexStr = Helpers::toHex(raw);
            std::string b64 = j.value("base64", "");

            std::cout << "\nEntropy Generated (" << raw.size() << " bytes):" << std::endl;
            std::cout << "Hex    : " << hexStr << std::endl;
            if (!b64.empty()) {
                std::cout << "Base64 : " << b64 << std::endl;
            }

        } else if (subChoice == 2) {
            int count = 16; // 128 bits
            auto res = cli.Get(("/entropy?bytes=" + std::to_string(count)).c_str());
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.empty()) {
                std::cerr << "Empty entropy received." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }

            int ones = 0;
            int zeros = 0;
            std::string bits;
            for (uint8_t byte : raw) {
                for (int b = 7; b >= 0; --b) {
                    if ((byte >> b) & 1) {
                        bits += "1";
                        ones++;
                    } else {
                        bits += "0";
                        zeros++;
                    }
                }
            }

            std::cout << "\nBinary Bitstream (" << (raw.size() * 8) << " bits):" << std::endl;
            for (size_t i = 0; i < bits.size(); ++i) {
                std::cout << bits[i];
                if ((i + 1) % 8 == 0) std::cout << " ";
                if ((i + 1) % 64 == 0) std::cout << "\n";
            }
            std::cout << "\nBit Balance: 1s: " << ones << " (" << std::fixed << std::setprecision(2) 
                      << (ones * 100.0 / bits.size()) << "%), 0s: " << zeros << " ("
                      << (zeros * 100.0 / bits.size()) << "%)\n";

        } else if (subChoice == 3) {
            int minVal = readInt("Minimum value [default 1]: ", 1);
            int maxVal = readInt("Maximum value [default 100]: ", 100);
            if (minVal > maxVal) std::swap(minVal, maxVal);

            auto res = cli.Get("/entropy?bytes=4");
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.size() < 4) {
                std::cerr << "Insufficient bytes received." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            uint32_t val = (static_cast<uint32_t>(raw[0]) << 24) |
                           (static_cast<uint32_t>(raw[1]) << 16) |
                           (static_cast<uint32_t>(raw[2]) << 8)  |
                           (static_cast<uint32_t>(raw[3]));
            uint32_t span = static_cast<uint32_t>(maxVal - minVal + 1);
            int result = minVal + static_cast<int>(val % span);
            std::cout << "\nRandom Integer in [" << minVal << ", " << maxVal << "]: " << result << "\n";

        } else if (subChoice == 4) {
            auto res = cli.Get("/entropy?bytes=8");
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.size() < 8) {
                std::cerr << "Insufficient bytes received." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            uint64_t val = 0;
            for (int i = 0; i < 8; ++i) val = (val << 8) | raw[i];
            double f = static_cast<double>(val) / static_cast<double>(UINT64_MAX);
            std::cout << "\nRandom Float in [0.0, 1.0): " << std::setprecision(10) << f << "\n";

        } else if (subChoice == 5) {
            auto res = cli.Get("/entropy?bytes=4");
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.size() < 2) {
                std::cerr << "Insufficient bytes received." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            int dice = 1 + (raw[0] % 6);
            std::string coin = (raw[1] & 1) ? "HEADS" : "TAILS";
            std::cout << "\nDice Roll (1-6) : [" << dice << "]" << std::endl;
            std::cout << "Coin Toss       : [" << coin << "]" << std::endl;

        } else if (subChoice == 6) {
            auto res = cli.Get("/entropy?bytes=256");
            if (!res || res->status != 200) {
                std::cerr << "Failed to fetch entropy." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json j = json::parse(res->body);
            auto raw = extractEntropyBytes(j);
            if (raw.empty()) {
                std::cerr << "No entropy bytes received." << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }

            int buckets[8] = {0};
            for (uint8_t b : raw) {
                buckets[b / 32]++;
            }
            std::cout << "\nByte Distribution across 8 intervals (256 samples, ideal=32 per bucket):" << std::endl;
            for (int i = 0; i < 8; ++i) {
                int low = i * 32;
                int high = low + 31;
                std::cout << "  [" << std::setw(3) << low << "-" << std::setw(3) << high << "]: ";
                int barLen = buckets[i];
                for (int k = 0; k < barLen; ++k) std::cout << "=";
                std::cout << " " << buckets[i] << "\n";
            }
        } else {
            std::cout << "Invalid choice. Please select [0-6]." << std::endl;
        }

        readLine("\nPress Enter to continue...");
    }
}

// ============================================================================
// Feature 3: Cryptographic Key Generator
// ============================================================================
void generateKeys(httplib::Client& cli) {
    while (true) {
        std::cout << "\n--- [3] Cryptographic Key Generator ---" << std::endl;
        std::cout << "Select Key Type:" << std::endl;
        std::cout << "  [1] Random 256-bit Key (Direct from Entropy Pool)" << std::endl;
        std::cout << "  [2] PBKDF2 Password-Derived Key (100,000 iterations)" << std::endl;
        std::cout << "  [0] Return to Main Menu" << std::endl;
        
        int typeChoice = readInt("\nChoice [0-2] > ", 0);
        if (typeChoice == 0) {
            break;
        }

        if (typeChoice == 1) {
            json req = {{"type", "random"}, {"key_size", 32}};
            auto res = cli.Post("/keygen", req.dump(), "application/json");
            if (res && res->status == 200) {
                json resp = json::parse(res->body);
                std::cout << "\nGenerated 256-bit AES Key:" << std::endl;
                std::cout << "Hex Key : " << resp.value("key", "") << std::endl;
                std::cout << "Size    : 32 bytes (256 bits)" << std::endl;
            } else {
                std::cerr << "Key generation failed (HTTP " << (res ? res->status : -1) << ")" << std::endl;
            }
        } else if (typeChoice == 2) {
            std::string password = readLine("Enter password: ");
            if (password.empty()) password = "DefaultSecretPassword123!";

            json req = {
                {"password", password},
                {"salt_size", 32},
                {"iterations", 100000},
                {"key_size", 32}
            };
            auto res = cli.Post("/keygen", req.dump(), "application/json");
            if (res && res->status == 200) {
                json resp = json::parse(res->body);
                std::cout << "\nPBKDF2 Key Derivation Result:" << std::endl;
                std::cout << "Password : " << password << std::endl;
                std::cout << "Salt     : " << resp.value("salt", "") << std::endl;
                std::cout << "Key (Hex): " << resp.value("key", "") << std::endl;
                std::cout << "KDF      : PBKDF2-HMAC-SHA256 (100,000 iterations)" << std::endl;
            } else {
                std::cerr << "PBKDF2 derivation failed (HTTP " << (res ? res->status : -1) << ")" << std::endl;
            }
        } else {
            std::cout << "Invalid choice. Please select [0-2]." << std::endl;
        }

        readLine("\nPress Enter to continue...");
    }
}

// ============================================================================
// Feature 4: AES-256-GCM Encryption & Decryption Round-Trip
// ============================================================================
void encryptDecryptTest(httplib::Client& cli) {
    while (true) {
        std::cout << "\n--- [4] AES-256-GCM Encrypt & Decrypt ---" << std::endl;
        std::cout << "Select Operation:" << std::endl;
        std::cout << "  [1] Interactive Text Encrypt & Decrypt Round-Trip" << std::endl;
        std::cout << "  [2] Encrypt Only" << std::endl;
        std::cout << "  [3] Decrypt Only" << std::endl;
        std::cout << "  [0] Return to Main Menu" << std::endl;
        
        int choice = readInt("\nChoice [0-3] > ", 0);
        if (choice == 0) {
            break;
        }

        if (choice == 1) {
            std::string text = readLine("Enter plaintext to encrypt: ");
            if (text.empty()) text = "Confidential Telemetry Data 2026";
            std::string pass = readLine("Enter password: ");
            if (pass.empty()) pass = "SecretPassword!456";

            std::vector<uint8_t> plainBytes(text.begin(), text.end());
            std::string hexData = Helpers::toHex(plainBytes);

            json encReq = {
                {"data", hexData},
                {"password", pass}
            };
            auto encRes = cli.Post("/encrypt", encReq.dump(), "application/json");
            if (!encRes || encRes->status != 200) {
                std::cerr << "Encryption failed (HTTP " << (encRes ? encRes->status : -1) << ")" << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json encResp = json::parse(encRes->body);
            std::string ct = encResp.value("ciphertext", "");
            std::cout << "\n[Encrypted Successfully]" << std::endl;
            std::cout << "Ciphertext (Hex) : " << ct << std::endl;

            std::cout << "\nProceeding with decryption using the same password..." << std::endl;
            json decReq = {
                {"ciphertext", ct},
                {"password", pass}
            };
            auto decRes = cli.Post("/decrypt", decReq.dump(), "application/json");
            if (!decRes || decRes->status != 200) {
                std::cerr << "Decryption failed (HTTP " << (decRes ? decRes->status : -1) << ")" << std::endl;
                readLine("\nPress Enter to continue...");
                continue;
            }
            json decResp = json::parse(decRes->body);
            std::string decHex = decResp.value("plaintext", "");
            auto decBytes = Helpers::fromHex(decHex);
            std::string recovered(decBytes.begin(), decBytes.end());

            std::cout << "[Decrypted Successfully]" << std::endl;
            std::cout << "Recovered Text   : " << recovered << std::endl;
            if (recovered == text) {
                std::cout << "Verification     : [MATCH - AES-256-GCM ROUND TRIP OK]\n";
            } else {
                std::cout << "Verification     : [MISMATCH!]\n";
            }
        } else if (choice == 2) {
            std::string text = readLine("Enter plaintext: ");
            std::string pass = readLine("Enter password: ");

            std::vector<uint8_t> plainBytes(text.begin(), text.end());
            json encReq = {{"data", Helpers::toHex(plainBytes)}, {"password", pass}};
            auto encRes = cli.Post("/encrypt", encReq.dump(), "application/json");
            if (encRes && encRes->status == 200) {
                json r = json::parse(encRes->body);
                std::cout << "Ciphertext: " << r.value("ciphertext", "") << "\n";
            } else {
                std::cerr << "Encryption failed.\n";
            }
        } else if (choice == 3) {
            std::string ct = readLine("Enter ciphertext (Hex): ");
            std::string pass = readLine("Enter password: ");

            json decReq = {{"ciphertext", ct}, {"password", pass}};
            auto decRes = cli.Post("/decrypt", decReq.dump(), "application/json");
            if (decRes && decRes->status == 200) {
                json r = json::parse(decRes->body);
                auto decBytes = Helpers::fromHex(r.value("plaintext", ""));
                std::string rec(decBytes.begin(), decBytes.end());
                std::cout << "Recovered: " << rec << "\n";
            } else {
                std::cerr << "Decryption failed (bad password or corrupted ciphertext).\n";
            }
        } else {
            std::cout << "Invalid choice. Please select [0-3]." << std::endl;
        }

        readLine("\nPress Enter to continue...");
    }
}

// ============================================================================
// Feature 5: Settings & Control
// ============================================================================
void manageSettings(httplib::Client& cli) {
    while (true) {
        std::cout << "\n--- [5] Entropy Source & Whitening Settings ---" << std::endl;
        auto getRes = cli.Get("/settings");
        if (getRes && getRes->status == 200) {
            json curr = json::parse(getRes->body);
            std::cout << "Current Settings:" << std::endl;
            std::cout << "  Preferred Source : " << curr.value("preferred_source", "auto") << std::endl;
            std::cout << "  Active Source    : " << curr.value("source_type", "auto") << std::endl;
            std::cout << "  Whitening Algo   : " << curr.value("whitening", "lfsr") << std::endl;
            std::cout << "  Pool Size        : " << curr.value("pool_size", 4096) << " bytes" << std::endl;
        }

        std::cout << "\nActions:" << std::endl;
        std::cout << "  [1] Switch Whitening: Galois LFSR (0x80200003, High-Throughput)" << std::endl;
        std::cout << "  [2] Switch Whitening: SHA-256 (NIST SP 800-90B Cryptographic)" << std::endl;
        std::cout << "  [3] Set Preferred Source: Auto-Detect (Hardware preferred)" << std::endl;
        std::cout << "  [4] Set Preferred Source: Force Hardware (ESP32)" << std::endl;
        std::cout << "  [5] Set Preferred Source: Force OpenSSL Fallback" << std::endl;
        std::cout << "  [6] Force Immediate Pool Reseed" << std::endl;
        std::cout << "  [0] Return to Main Menu" << std::endl;
        
        int act = readInt("\nChoice [0-6] > ", 0);
        if (act == 0) {
            break;
        }

        if (act == 1) {
            json req = {{"whitening", "lfsr"}};
            cli.Post("/settings", req.dump(), "application/json");
            std::cout << "Whitening algorithm switched to: lfsr\n";
        } else if (act == 2) {
            json req = {{"whitening", "sha256"}};
            cli.Post("/settings", req.dump(), "application/json");
            std::cout << "Whitening algorithm switched to: sha256\n";
        } else if (act == 3) {
            json req = {{"source_type", "auto"}};
            cli.Post("/settings", req.dump(), "application/json");
            std::cout << "Source preference set to: auto\n";
        } else if (act == 4) {
            json req = {{"source_type", "hardware"}};
            cli.Post("/settings", req.dump(), "application/json");
            std::cout << "Source preference set to: hardware\n";
        } else if (act == 5) {
            json req = {{"source_type", "openssl"}};
            cli.Post("/settings", req.dump(), "application/json");
            std::cout << "Source preference set to: openssl\n";
        } else if (act == 6) {
            cli.Post("/reseed", "{}", "application/json");
            std::cout << "Pool reseed triggered.\n";
        } else {
            std::cout << "Invalid choice. Please select [0-6].\n";
        }

        readLine("\nPress Enter to continue...");
    }
}

// ============================================================================
// Feature 6: Live Stream Monitor
// ============================================================================
void liveStreamMonitor(httplib::Client& cli) {
    std::cout << "\n--- [6] Live Stream Monitor (Press Enter to Stop or runs 15 cycles) ---" << std::endl;
    std::cout << "Streaming real-time metrics from :8080 every 500ms...\n" << std::endl;

    for (int cycle = 0; cycle < 15; ++cycle) {
        auto res = cli.Get("/status");
        auto entRes = cli.Get("/entropy?bytes=8");
        if (res && res->status == 200 && entRes && entRes->status == 200) {
            json j = json::parse(res->body);
            json ej = json::parse(entRes->body);
            auto ent = j.value("entropy", json::object());
            std::string speedFmt = ent.value("speed_formatted", "0 B/s");
            size_t avail = ent.value("available_bytes", (size_t)0);
            size_t bufSize = ent.value("pool_size", ent.value("buffer_size", (size_t)4096));
            std::string src = ent.value("source_type", "");
            auto raw = extractEntropyBytes(ej);
            std::string hex = Helpers::toHex(raw);

            double pct = bufSize > 0 ? (avail * 100.0 / bufSize) : 0.0;
            std::cout << "\r[" << std::setw(2) << cycle + 1 << "] Speed: " << std::setw(10) << speedFmt
                      << " | Pool: " << std::setw(4) << avail << "/" << bufSize << " B (" << std::fixed << std::setprecision(0) << pct << "%)"
                      << " | Source: " << std::setw(8) << src
                      << " | Sample: " << hex.substr(0, 16) << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "\n[Monitor complete]\n";
}

// ============================================================================
// Feature 7: NIST Statistical Analysis & ASCII Uniformity Chart
// ============================================================================
void runStatisticalAnalysis(httplib::Client& cli) {
    while (true) {
        std::cout << "\n--- [7] NIST Statistical Entropy Analysis ---" << std::endl;
        std::cout << "Select Test Mode:" << std::endl;
        std::cout << "  [1] Quick Mode (1,024 bytes, 5 core NIST tests)" << std::endl;
        std::cout << "  [2] Full Mode (4,096 bytes, 8 NIST tests)" << std::endl;
        std::cout << "  [3] Exhaustive Mode (100 multi-sample runs with 10-bin p-value uniformity)" << std::endl;
        std::cout << "  [0] Return to Main Menu" << std::endl;
        
        int modeChoice = readInt("\nChoice [0-3] > ", 0);
        if (modeChoice == 0) {
            break;
        }

        std::string mode = "quick";
        int bytes = 1024;
        int runs = 1;
        if (modeChoice == 2) {
            mode = "full";
            bytes = 4096;
        } else if (modeChoice == 3) {
            mode = "exhaustive";
            bytes = 1024;
            runs = 100;
        }

        std::cout << "\nRequesting " << mode << " statistical analysis (" << bytes << " bytes, " << runs << " runs)..." << std::endl;
        json req = {
            {"mode", mode},
            {"bytes", bytes},
            {"runs", runs}
        };

        auto res = cli.Post("/test", req.dump(), "application/json");
        if (!res || res->status != 200) {
            std::cerr << "Analysis failed or timed out (HTTP " << (res ? res->status : -1) << ")" << std::endl;
            readLine("\nPress Enter to continue...");
            continue;
        }

        try {
            json j = json::parse(res->body);
            std::string verdict = j.value("overall_verdict", "UNKNOWN");
            double passRate = j.value("overall_pass_rate_percent", j.value("pass_rate", 100.0));
            if (passRate <= 1.0) passRate *= 100.0;

            std::cout << "\n========================================================" << std::endl;
            std::cout << "  OVERALL ANALYSIS VERDICT: [" << verdict << "] (Pass Rate: " 
                      << std::fixed << std::setprecision(1) << passRate << "%)" << std::endl;
            std::cout << "========================================================" << std::endl;

            // Try extracting test results from statistics array
            std::vector<json> testList;
            if (j.contains("statistics") && j["statistics"].is_array()) {
                for (const auto& item : j["statistics"]) {
                    testList.push_back(item);
                }
            } else if (j.contains("analysis") && j["analysis"].is_object() && 
                       j["analysis"].contains("test_results") && j["analysis"]["test_results"].is_array()) {
                for (const auto& item : j["analysis"]["test_results"]) {
                    testList.push_back(item);
                }
            }

            if (!testList.empty()) {
                std::cout << std::left << std::setw(34) << "Test Name" 
                          << std::setw(14) << "Statistic" 
                          << std::setw(14) << "p-value" 
                          << "Verdict" << std::endl;
                std::cout << std::string(70, '-') << std::endl;

                for (const auto& testData : testList) {
                    std::string name = testData.value("name", testData.value("key", "Unknown Test"));
                    double pVal = testData.value("p_value", 0.0);
                    double stat = testData.value("statistic", 0.0);
                    bool passed = testData.value("pass", testData.value("passed", false));

                    std::cout << std::left << std::setw(34) << name.substr(0, 33) 
                              << std::setw(14) << std::fixed << std::setprecision(4) << stat 
                              << std::setw(14) << std::fixed << std::setprecision(4) << pVal 
                              << (passed ? "[PASS]" : "[FAIL]") << std::endl;
                }
            } else if (j.contains("statistics") && j["statistics"].is_object()) {
                // Fallback statistical summary
                auto stats = j["statistics"];
                std::cout << "  Byte Count        : " << stats.value("byte_count", 0) << std::endl;
                std::cout << "  Unique Bytes      : " << stats.value("unique_bytes", 0) << " / 256" << std::endl;
                std::cout << "  Chi-Square Stat   : " << stats.value("chi_square", 0.0) << std::endl;
                std::cout << "  Monobit Bit Ratio : " << stats.value("monobit_ratio", 0.0) 
                          << " (" << (stats.value("monobit_pass", false) ? "[PASS]" : "[FAIL]") << ")\n";
            }

            // Exhaustive mode uniformity histograms
            if (j.contains("exhaustive") && j["exhaustive"].is_object()) {
                auto exh = j["exhaustive"];
                if (exh.contains("tests") && exh["tests"].is_object()) {
                    std::cout << "\n--- NIST SP 800-22 Multi-Run Test Uniformity ---" << std::endl;
                    for (auto& [tName, tObj] : exh["tests"].items()) {
                        double unifP = tObj.value("uniformity_p_value", 1.0);
                        bool unifPass = tObj.value("passed", true);
                        double tPassRate = tObj.value("pass_rate", 1.0) * 100.0;
                        std::cout << "  [" << tName << "]: Pass Rate=" << std::fixed << std::setprecision(1) << tPassRate 
                                  << "%, Uniformity p-val=" << std::setprecision(4) << unifP 
                                  << " (" << (unifPass ? "PASS" : "FAIL") << ")\n";
                        if (tObj.contains("bins") && tObj["bins"].is_array()) {
                            std::cout << "    Histogram: ";
                            for (auto& bin : tObj["bins"]) {
                                std::cout << "[" << bin.get<int>() << "] ";
                            }
                            std::cout << "\n";
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON Parse error: " << e.what() << std::endl;
        }

        readLine("\nPress Enter to continue...");
    }
}

} // anonymous namespace

// ============================================================================
// Main Menu Loop
// ============================================================================
int main(int argc, char* argv[]) {
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    std::cout << "Initializing KAIROS Entropy CLI test bench..." << std::endl;
    ensureServerRunning(host, port);

    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(30, 0);

    bool running = true;
    while (running) {
        printBanner();
        std::cout << "Target: http://" << host << ":" << port << " | Status: " 
                  << (checkServerOnline(cli) ? "[ONLINE]" : "[OFFLINE / CONNECTING]") << "\n" << std::endl;
        std::cout << "  [1] System & Pool Status (Fill level, Speed, Source, Whitening)" << std::endl;
        std::cout << "  [2] Generate Test Entropy (Hex, Base64, Bits, Integers, Dice/Coin)" << std::endl;
        std::cout << "  [3] Cryptographic Key Generator (256-bit Random / PBKDF2 Password)" << std::endl;
        std::cout << "  [4] AES-256-GCM Encrypt & Decrypt Interactive Round-Trip" << std::endl;
        std::cout << "  [5] Entropy Source & Whitening Settings" << std::endl;
        std::cout << "  [6] Live Stream Monitor (Speedometer & Hex Stream Preview)" << std::endl;
        std::cout << "  [7] Run NIST Statistical Analysis & Uniformity Chart" << std::endl;
        std::cout << "  [0] Exit" << std::endl;
        
        std::string choiceStr = readLine("\nEnter choice [0-7] > ");
        if (choiceStr.empty()) {
            continue;
        }

        int choice = -1;
        try {
            choice = std::stoi(choiceStr);
        } catch (...) {
            std::cout << "\nInvalid choice. Please enter a number between 0 and 7.\n";
            readLine("\nPress Enter to return to main menu...");
            continue;
        }

        switch (choice) {
            case 1:
                viewSystemStatus(cli);
                readLine("\nPress Enter to return to main menu...");
                break;
            case 2:
                generateTestEntropy(cli);
                break;
            case 3:
                generateKeys(cli);
                break;
            case 4:
                encryptDecryptTest(cli);
                break;
            case 5:
                manageSettings(cli);
                break;
            case 6:
                liveStreamMonitor(cli);
                readLine("\nPress Enter to return to main menu...");
                break;
            case 7:
                runStatisticalAnalysis(cli);
                break;
            case 0:
                std::cout << "\nExiting KAIROS Entropy CLI.\n";
                running = false;
                break;
            default:
                std::cout << "\nInvalid choice. Please select [0-7].\n";
                readLine("\nPress Enter to return to main menu...");
                break;
        }
    }

    return 0;
}

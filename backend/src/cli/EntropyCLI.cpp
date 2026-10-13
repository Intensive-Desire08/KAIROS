#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>

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

void printBanner() {
    std::cout << R"(
╔═════════════════════════════════════════════════════════════════════╗
║             KAIROS ENTROPY MODULE — INTERACTIVE CLI                 ║
║        Hardware-Backed Randomness & Cryptographic Engine            ║
╚═════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printProgressBar(double percentage, int width = 30) {
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    int filled = static_cast<int>(std::round((percentage / 100.0) * width));
    std::cout << "[";
    for (int i = 0; i < filled; ++i) std::cout << "=";
    if (filled < width) std::cout << ">";
    for (int i = filled + 1; i < width; ++i) std::cout << " ";
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
        for (int i = 0; i < 15; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
        auto ent = j["entropy"];
        size_t avail = ent.value("available_bytes", (size_t)0);
        size_t bufferSize = ent.value("buffer_size", (size_t)8192);
        size_t totalGen = ent.value("total_bytes_generated", (size_t)0);
        double speedBps = ent.value("current_speed_bps", 0.0);
        std::string speedFmt = ent.value("speed_formatted", "0 B/s");
        std::string srcType = ent.value("source_type", "unknown");
        std::string whitening = ent.value("whitening_algorithm", "unknown");

        double fillPct = bufferSize > 0 ? (static_cast<double>(avail) * 100.0 / bufferSize) : 0.0;

        std::cout << "  Version           : " << version << std::endl;
        std::cout << "  Active Source     : " << (srcType == "hardware" ? "ESP32 Continuous DMA Hardware" : "OpenSSL PRNG (Software Fallback)") 
                  << " [" << srcType << "]" << std::endl;
        std::cout << "  Whitening Algo    : " << (whitening == "lfsr" ? "32-bit Galois LFSR (0x80200003)" : "NIST SP 800-90B SHA-256") 
                  << " [" << whitening << "]" << std::endl;
        std::cout << "  Pool Fill Level   : ";
        printProgressBar(fillPct, 24);
        std::cout << " (" << avail << " / " << bufferSize << " bytes)" << std::endl;
        std::cout << "  Generation Speed  : " << speedFmt << " (" << static_cast<size_t>(speedBps * 8) << " bps)" << std::endl;
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
    std::cout << "\n--- [2] Generate Test Entropy ---" << std::endl;
    std::cout << "Select Output Format:" << std::endl;
    std::cout << "  [1] Raw Hex & Base64 bytes" << std::endl;
    std::cout << "  [2] Binary Bitstream & 0/1 Balance Check" << std::endl;
    std::cout << "  [3] Uniform Random Integer in Range [min, max]" << std::endl;
    std::cout << "  [4] Uniform Random Float in Range [0.0, 1.0)" << std::endl;
    std::cout << "  [5] Gaming Simulation: Dice Roll (1-6) & Coin Toss" << std::endl;
    std::cout << "  [6] ASCII Byte Frequency Histogram (256-byte sample)" << std::endl;
    std::cout << "Choice > ";
    int subChoice = 1;
    if (!(std::cin >> subChoice)) {
        std::cin.clear();
        std::cin.ignore(1000, '\n');
        return;
    }

    if (subChoice == 1) {
        std::cout << "Number of bytes to generate [1-1024, default 32]: ";
        int count = 32;
        std::cin >> count;
        if (count <= 0 || count > 1024) count = 32;

        auto res = cli.Get(("/entropy?bytes=" + std::to_string(count)).c_str());
        if (!res || res->status != 200) {
            std::cerr << "Failed to fetch entropy." << std::endl;
            return;
        }
        json j = json::parse(res->body);
        std::string b64 = j.value("bytes", "");
        auto raw = base64Decode(b64);
        std::string hexStr = Helpers::toHex(raw);

        std::cout << "\nEntropy Generated (" << raw.size() << " bytes):" << std::endl;
        std::cout << "Hex    : " << hexStr << std::endl;
        std::cout << "Base64 : " << b64 << std::endl;

    } else if (subChoice == 2) {
        int count = 16; // 128 bits
        auto res = cli.Get(("/entropy?bytes=" + std::to_string(count)).c_str());
        if (!res || res->status != 200) return;
        json j = json::parse(res->body);
        auto raw = base64Decode(j.value("bytes", ""));

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

        std::cout << "\nBinary Bitstream (" << (count * 8) << " bits):" << std::endl;
        for (size_t i = 0; i < bits.size(); ++i) {
            std::cout << bits[i];
            if ((i + 1) % 8 == 0) std::cout << " ";
            if ((i + 1) % 64 == 0) std::cout << "\n";
        }
        std::cout << "\nBit Balance: 1s: " << ones << " (" << std::fixed << std::setprecision(2) 
                  << (ones * 100.0 / bits.size()) << "%), 0s: " << zeros << " ("
                  << (zeros * 100.0 / bits.size()) << "%)\n";

    } else if (subChoice == 3) {
        int minVal = 1, maxVal = 100;
        std::cout << "Minimum value: ";
        std::cin >> minVal;
        std::cout << "Maximum value: ";
        std::cin >> maxVal;
        if (minVal > maxVal) std::swap(minVal, maxVal);

        auto res = cli.Get("/entropy?bytes=4");
        if (!res || res->status != 200) return;
        json j = json::parse(res->body);
        auto raw = base64Decode(j.value("bytes", ""));
        uint32_t val = (static_cast<uint32_t>(raw[0]) << 24) |
                       (static_cast<uint32_t>(raw[1]) << 16) |
                       (static_cast<uint32_t>(raw[2]) << 8)  |
                       (static_cast<uint32_t>(raw[3]));
        uint32_t span = static_cast<uint32_t>(maxVal - minVal + 1);
        int result = minVal + static_cast<int>(val % span);
        std::cout << "\nRandom Integer in [" << minVal << ", " << maxVal << "]: " << result << "\n";

    } else if (subChoice == 4) {
        auto res = cli.Get("/entropy?bytes=8");
        if (!res || res->status != 200) return;
        json j = json::parse(res->body);
        auto raw = base64Decode(j.value("bytes", ""));
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) val = (val << 8) | raw[i];
        double f = static_cast<double>(val) / static_cast<double>(UINT64_MAX);
        std::cout << "\nRandom Float in [0.0, 1.0): " << std::setprecision(10) << f << "\n";

    } else if (subChoice == 5) {
        auto res = cli.Get("/entropy?bytes=4");
        if (!res || res->status != 200) return;
        json j = json::parse(res->body);
        auto raw = base64Decode(j.value("bytes", ""));
        int dice = 1 + (raw[0] % 6);
        std::string coin = (raw[1] & 1) ? "HEADS" : "TAILS";
        std::cout << "\nDice Roll (1-6) : [" << dice << "]" << std::endl;
        std::cout << "Coin Toss       : [" << coin << "]" << std::endl;

    } else if (subChoice == 6) {
        auto res = cli.Get("/entropy?bytes=256");
        if (!res || res->status != 200) return;
        json j = json::parse(res->body);
        auto raw = base64Decode(j.value("bytes", ""));

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
    }
}

// ============================================================================
// Feature 3: Cryptographic Key Generator
// ============================================================================
void generateKeys(httplib::Client& cli) {
    std::cout << "\n--- [3] Cryptographic Key Generator ---" << std::endl;
    std::cout << "Select Key Type:" << std::endl;
    std::cout << "  [1] Random 256-bit Key (Direct from Entropy Pool)" << std::endl;
    std::cout << "  [2] PBKDF2 Password-Derived Key (100,000 iterations)" << std::endl;
    std::cout << "Choice > ";
    int typeChoice = 1;
    std::cin >> typeChoice;

    if (typeChoice == 1) {
        json req = {{"type", "random"}};
        auto res = cli.Post("/keygen", req.dump(), "application/json");
        if (res && res->status == 200) {
            json resp = json::parse(res->body);
            std::cout << "\nGenerated 256-bit AES Key:" << std::endl;
            std::cout << "Hex Key : " << resp.value("key", "") << std::endl;
            std::cout << "Size    : 32 bytes (256 bits)" << std::endl;
        } else {
            std::cerr << "Key generation failed." << std::endl;
        }
    } else {
        std::string password;
        std::cout << "Enter password: ";
        std::cin.ignore(1000, '\n');
        std::getline(std::cin, password);
        if (password.empty()) password = "DefaultSecretPassword123!";

        json req = {
            {"type", "password"},
            {"password", password}
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
            std::cerr << "PBKDF2 derivation failed." << std::endl;
        }
    }
}

// ============================================================================
// Feature 4: AES-256-GCM Encryption & Decryption Round-Trip
// ============================================================================
void encryptDecryptTest(httplib::Client& cli) {
    std::cout << "\n--- [4] AES-256-GCM Encrypt & Decrypt ---" << std::endl;
    std::cout << "Select Operation:" << std::endl;
    std::cout << "  [1] Interactive Text Encrypt & Decrypt Round-Trip" << std::endl;
    std::cout << "  [2] Encrypt Only" << std::endl;
    std::cout << "  [3] Decrypt Only" << std::endl;
    std::cout << "Choice > ";
    int choice = 1;
    std::cin >> choice;
    std::cin.ignore(1000, '\n');

    if (choice == 1) {
        std::string text, pass;
        std::cout << "Enter plaintext to encrypt: ";
        std::getline(std::cin, text);
        if (text.empty()) text = "Confidential Telemetry Data 2026";
        std::cout << "Enter password: ";
        std::getline(std::cin, pass);
        if (pass.empty()) pass = "SecretPassword!456";

        std::vector<uint8_t> plainBytes(text.begin(), text.end());
        std::string hexData = Helpers::toHex(plainBytes);

        json encReq = {
            {"data", hexData},
            {"password", pass}
        };
        auto encRes = cli.Post("/encrypt", encReq.dump(), "application/json");
        if (!encRes || encRes->status != 200) {
            std::cerr << "Encryption failed." << std::endl;
            return;
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
            std::cerr << "Decryption failed." << std::endl;
            return;
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
        std::string text, pass;
        std::cout << "Enter plaintext: ";
        std::getline(std::cin, text);
        std::cout << "Enter password: ";
        std::getline(std::cin, pass);

        std::vector<uint8_t> plainBytes(text.begin(), text.end());
        json encReq = {{"data", Helpers::toHex(plainBytes)}, {"password", pass}};
        auto encRes = cli.Post("/encrypt", encReq.dump(), "application/json");
        if (encRes && encRes->status == 200) {
            json r = json::parse(encRes->body);
            std::cout << "Ciphertext: " << r.value("ciphertext", "") << "\n";
        }
    } else {
        std::string ct, pass;
        std::cout << "Enter ciphertext (Hex): ";
        std::getline(std::cin, ct);
        std::cout << "Enter password: ";
        std::getline(std::cin, pass);

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
    }
}

// ============================================================================
// Feature 5: Settings & Control
// ============================================================================
void manageSettings(httplib::Client& cli) {
    std::cout << "\n--- [5] Entropy Source & Whitening Settings ---" << std::endl;
    auto getRes = cli.Get("/settings");
    if (getRes && getRes->status == 200) {
        json curr = json::parse(getRes->body);
        std::cout << "Current Settings:" << std::endl;
        std::cout << "  Preferred Source : " << curr.value("preferred_source", "auto") << std::endl;
        std::cout << "  Active Source    : " << curr.value("source_type", "auto") << std::endl;
        std::cout << "  Whitening Algo   : " << curr.value("whitening", "lfsr") << std::endl;
        std::cout << "  Pool Size        : " << curr.value("pool_size", 8192) << " bytes" << std::endl;
    }

    std::cout << "\nActions:" << std::endl;
    std::cout << "  [1] Switch Whitening: Galois LFSR (0x80200003, High-Throughput)" << std::endl;
    std::cout << "  [2] Switch Whitening: SHA-256 (NIST SP 800-90B Cryptographic)" << std::endl;
    std::cout << "  [3] Set Preferred Source: Auto-Detect (Hardware preferred)" << std::endl;
    std::cout << "  [4] Set Preferred Source: Force Hardware (ESP32)" << std::endl;
    std::cout << "  [5] Set Preferred Source: Force OpenSSL Fallback" << std::endl;
    std::cout << "  [6] Force Immediate Pool Reseed" << std::endl;
    std::cout << "Choice > ";
    int act = 0;
    std::cin >> act;

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
    }
}

// ============================================================================
// Feature 6: Live Stream Monitor
// ============================================================================
void liveStreamMonitor(httplib::Client& cli) {
    std::cout << "\n--- [6] Live Stream Monitor (Press Ctrl+C or Enter to Stop) ---" << std::endl;
    std::cout << "Streaming real-time metrics from :8080 every 500ms...\n" << std::endl;

    for (int cycle = 0; cycle < 30; ++cycle) {
        auto res = cli.Get("/status");
        auto entRes = cli.Get("/entropy?bytes=8");
        if (res && res->status == 200 && entRes && entRes->status == 200) {
            json j = json::parse(res->body);
            json ej = json::parse(entRes->body);
            auto ent = j["entropy"];
            std::string speedFmt = ent.value("speed_formatted", "0 B/s");
            size_t avail = ent.value("available_bytes", (size_t)0);
            size_t bufSize = ent.value("buffer_size", (size_t)8192);
            std::string src = ent.value("source_type", "");
            std::string b64 = ej.value("bytes", "");
            auto raw = base64Decode(b64);
            std::string hex = Helpers::toHex(raw);

            double pct = bufSize > 0 ? (avail * 100.0 / bufSize) : 0.0;
            std::cout << "\r[" << std::setw(2) << cycle + 1 << "] Speed: " << std::setw(10) << speedFmt
                      << " | Pool: " << std::setw(4) << avail << "/" << bufSize << " B (" << std::fixed << std::setprecision(0) << pct << "%)"
                      << " | Source: " << std::setw(8) << src
                      << " | Sample: " << hex.substr(0, 16) << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "\n[Monitor paused]\n";
}

// ============================================================================
// Feature 7: NIST Statistical Analysis & ASCII Uniformity Chart
// ============================================================================
void runStatisticalAnalysis(httplib::Client& cli) {
    std::cout << "\n--- [7] NIST Statistical Entropy Analysis ---" << std::endl;
    std::cout << "Select Test Mode:" << std::endl;
    std::cout << "  [1] Quick Mode (1,024 bytes, 5 core NIST tests)" << std::endl;
    std::cout << "  [2] Full Mode (4,096 bytes, 8 NIST tests)" << std::endl;
    std::cout << "  [3] Exhaustive Mode (100 multi-sample runs with 10-bin p-value uniformity)" << std::endl;
    std::cout << "Choice > ";
    int modeChoice = 1;
    std::cin >> modeChoice;

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
        return;
    }

    try {
        json j = json::parse(res->body);
        std::string verdict = j.value("overall_verdict", "UNKNOWN");
        double passRate = j.value("pass_rate", 1.0);

        std::cout << "\n========================================================" << std::endl;
        std::cout << "  OVERALL ANALYSIS VERDICT: [" << verdict << "] (Pass Rate: " 
                  << std::fixed << std::setprecision(1) << (passRate * 100.0) << "%)" << std::endl;
        std::cout << "========================================================" << std::endl;

        if (j.contains("tests") && j["tests"].is_object()) {
            std::cout << std::left << std::setw(28) << "Test Name" 
                      << std::setw(14) << "Statistic" 
                      << std::setw(14) << "p-value" 
                      << "Verdict" << std::endl;
            std::cout << std::string(64, '-') << std::endl;

            for (auto& [name, testData] : j["tests"].items()) {
                double pVal = testData.value("p_value", 0.0);
                double stat = testData.value("statistic", 0.0);
                bool passed = testData.value("passed", false);

                std::cout << std::left << std::setw(28) << name 
                          << std::setw(14) << std::fixed << std::setprecision(4) << stat 
                          << std::setw(14) << std::fixed << std::setprecision(4) << pVal 
                          << (passed ? "[PASS]" : "[FAIL]") << std::endl;
            }
        }

        // If exhaustive mode, display 10-bin p-value histogram
        if (j.contains("uniformity") && j["uniformity"].is_object()) {
            auto unif = j["uniformity"];
            double unifP = unif.value("p_value", 0.0);
            bool unifPass = unif.value("passed", false);
            std::cout << "\n--- NIST SP 800-22 Uniformity Distribution (10 Bins) ---" << std::endl;
            std::cout << "Uniformity p-value: " << std::fixed << std::setprecision(4) << unifP 
                      << " (" << (unifPass ? "PASSED >= 0.0001" : "FAILED") << ")\n";

            if (unif.contains("bins") && unif["bins"].is_array()) {
                auto bins = unif["bins"];
                for (size_t b = 0; b < bins.size(); ++b) {
                    int count = bins[b].get<int>();
                    double low = b * 0.1;
                    double high = low + 0.1;
                    std::cout << "  Bin [" << std::fixed << std::setprecision(1) << low << " - " << high << "]: ";
                    for (int k = 0; k < count; ++k) std::cout << "=";
                    std::cout << " (" << count << ")\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "JSON Parse error: " << e.what() << std::endl;
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
        std::cout << "\nEnter choice [0-7] > ";

        int choice = -1;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(1000, '\n');
            continue;
        }

        switch (choice) {
            case 1:
                viewSystemStatus(cli);
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
                break;
            case 7:
                runStatisticalAnalysis(cli);
                break;
            case 0:
                std::cout << "\nExiting KAIROS Entropy CLI. Goodbye!\n";
                running = false;
                break;
            default:
                std::cout << "\nInvalid choice. Please select from 0 to 7.\n";
                break;
        }

        if (running) {
            std::cout << "\nPress Enter to return to main menu...";
            std::cin.ignore(1000, '\n');
            std::cin.get();
        }
    }

    return 0;
}

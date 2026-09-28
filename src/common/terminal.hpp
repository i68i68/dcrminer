#pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dcr {
namespace term {

// ANSI Color Codes
constexpr const char* RESET       = "\033[0m";
constexpr const char* BOLD        = "\033[1m";
constexpr const char* DIM         = "\033[2m";

constexpr const char* RED         = "\033[31m";
constexpr const char* GREEN       = "\033[32m";
constexpr const char* YELLOW      = "\033[33m";
constexpr const char* BLUE        = "\033[34m";
constexpr const char* MAGENTA     = "\033[35m";
constexpr const char* CYAN        = "\033[36m";
constexpr const char* WHITE       = "\033[37m";

constexpr const char* BRIGHT_RED     = "\033[91m";
constexpr const char* BRIGHT_GREEN   = "\033[92m";
constexpr const char* BRIGHT_YELLOW  = "\033[93m";
constexpr const char* BRIGHT_BLUE    = "\033[94m";
constexpr const char* BRIGHT_MAGENTA = "\033[95m";
constexpr const char* BRIGHT_CYAN    = "\033[96m";
constexpr const char* BRIGHT_WHITE   = "\033[97m";

inline void init_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

inline std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm buf{};
#ifdef _WIN32
    localtime_s(&buf, &in_time_t);
#else
    localtime_r(&in_time_t, &buf);
#endif
    std::ostringstream ss;
    ss << std::setfill('0')
       << "[" << std::setw(2) << buf.tm_hour << ":"
       << std::setw(2) << buf.tm_min << ":"
       << std::setw(2) << buf.tm_sec << "."
       << std::setw(3) << ms.count() << "]";
    return ss.str();
}

inline void log_info(const std::string& tag, const std::string& msg) {
    std::cout << DIM << get_timestamp() << RESET << " "
              << BRIGHT_CYAN << BOLD << tag << RESET << " "
              << msg << "\n";
}

inline void log_success(const std::string& tag, const std::string& msg) {
    std::cout << DIM << get_timestamp() << RESET << " "
              << BRIGHT_GREEN << BOLD << tag << RESET << " "
              << BRIGHT_GREEN << msg << RESET << "\n";
}

inline void log_warn(const std::string& tag, const std::string& msg) {
    std::cerr << DIM << get_timestamp() << RESET << " "
              << BRIGHT_YELLOW << BOLD << tag << RESET << " "
              << BRIGHT_YELLOW << msg << RESET << "\n";
}

inline void log_error(const std::string& tag, const std::string& msg) {
    std::cerr << DIM << get_timestamp() << RESET << " "
              << BRIGHT_RED << BOLD << tag << RESET << " "
              << BRIGHT_RED << msg << RESET << "\n";
}

inline void print_banner() {
    std::cout << BRIGHT_CYAN << BOLD
              << "========================================================================================\n"
              << "   ____    _____ ____    __  __ _                    \n"
              << "  |  _ \\  / ____|  _ \\  |  \\/  (_)                   \n"
              << "  | | | || |    | |_) | | \\  / |_ _ __   ___ _ __    \n"
              << "  | |_| || |____|  _ <  | |\\/| | | '_ \\ / _ \\ '__|   \n"
              << "  |____/  \\_____|_| \\_\\ |_|  |_|_|_| |_|\\___|_|      \n\n"
              << "  ⚡ ULTRA-OPTIMIZED DECRED (DCR) BLAKE3 MINER (DCP-0011) v0.1.0 ⚡\n"
              << "  💎 Multi-Arch CUDA (Maxwell->Blackwell) + AMD/Intel OpenCL + AVX-512/AVX2 SIMD\n"
              << "========================================================================================\n"
              << RESET << "\n";
}

} // namespace term
} // namespace dcr

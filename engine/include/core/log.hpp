#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace core {

class Log {
public:
// #define LOG_TO_FILE // comment to deactivate.
#define USE_COLOR // comment to deactivate.

    static constexpr char EOL {'\n'};
    static constexpr char const* SUB_LOG_SYMBOL {"╰>"};

    // This class is not meant to be instantiated (aka static class).
    Log() = delete;
    ~Log() = delete;
    Log(const Log&) = delete;

    // Colors.
    static constexpr auto CLEAR {"\033[0m"};
    static constexpr auto BLACK {"\033[30m"};
    static constexpr auto RED {"\033[31m"};
    static constexpr auto GREEN {"\033[32m"};
    static constexpr auto YELLOW {"\033[33m"};
    static constexpr auto BLUE {"\033[34m"};
    static constexpr auto MAGENTA {"\033[35m"};
    static constexpr auto CYAN {"\033[36m"};
    static constexpr auto WHITE {"\033[37m"};
    static constexpr auto GRAY {"\033[90m"};

    static constexpr auto LIGHT_BLACK {"\033[90m"};
    static constexpr auto LIGHT_RED {"\033[91m"};
    static constexpr auto LIGHT_GREEN {"\033[92m"};
    static constexpr auto LIGHT_YELLOW {"\033[93m"};
    static constexpr auto LIGHT_BLUE {"\033[94m"};
    static constexpr auto LIGHT_MAGENTA {"\033[95m"};
    static constexpr auto LIGHT_CYAN {"\033[96m"};
    static constexpr auto LIGHT_WHITE {"\033[97m"};
    static constexpr auto LIGHT_GRAY {"\033[97m"};

    static constexpr auto BOLD {"\033[1m"};
    static constexpr auto UNDERLINE {"\033[4m"};
    static constexpr auto INVERT {"\033[7m"};

    static constexpr auto BG_BLACK {"\033[40m"};
    static constexpr auto BG_RED {"\033[41m"};
    static constexpr auto BG_GREEN {"\033[42m"};
    static constexpr auto BG_YELLOW {"\033[43m"};
    static constexpr auto BG_BLUE {"\033[44m"};
    static constexpr auto BG_MAGENTA {"\033[45m"};
    static constexpr auto BG_CYAN {"\033[46m"};
    static constexpr auto BG_WHITE {"\033[47m"};
    static constexpr auto BG_GRAY {"\033[100m"};

    static constexpr auto LIGHT_BG_BLACK {"\033[100m"};
    static constexpr auto LIGHT_BG_RED {"\033[101m"};
    static constexpr auto LIGHT_BG_GREEN {"\033[102m"};
    static constexpr auto LIGHT_BG_YELLOW {"\033[103m"};
    static constexpr auto LIGHT_BG_BLUE {"\033[104m"};
    static constexpr auto LIGHT_BG_MAGENTA {"\033[105m"};
    static constexpr auto LIGHT_BG_CYAN {"\033[106m"};
    static constexpr auto LIGHT_BG_WHITE {"\033[107m"};
    static constexpr auto LIGHT_BG_GRAY {"\033[107m"};

    // Logging.
    template <typename... Args>
    static void
    info(Args&&... args) noexcept {
        log_message("[INFO]: ", WHITE, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_info(Args&&... args) noexcept {
        sub_info_impl(1, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_info(size_t const level, Args&&... args) noexcept {
        sub_info_impl(level, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    header(Args&&... args) noexcept {
        std::ostringstream oss;
        (oss << ... << args);

        constexpr std::size_t total_width = 50; // Arbitrarily selected.
        std::string const message = oss.str();

        std::size_t const padding = (total_width > message.length())
            ? (total_width - message.length()) / 2
            : 0;

        std::string padded_message =
            std::string(padding, ' ') + message + std::string(padding, ' ');

        if (padded_message.length() < total_width) {
            padded_message += ' ';
        }

        std::cout << "########################################################"
                  << EOL;
        std::cout << "## " << padded_message << " ##" << EOL;
        std::cout << "########################################################"
                  << EOL;
    }

    template <typename... Args>
    static void
    warn(Args&&... args) noexcept {
        log_message("[WARNING]: ", YELLOW, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_warn(Args&&... args) noexcept {
        sub_warn_impl(1, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_warn(size_t const level, Args&&... args) noexcept {
        sub_warn_impl(level, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    error(Args&&... args) noexcept {
        log_message("[ERROR]: ", RED, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_error(Args&&... args) noexcept {
        sub_error_impl(1, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_error(size_t const level, Args&&... args) noexcept {
        sub_error_impl(level, std::forward<Args>(args)...);
    }

    // Cursor.
    static void
    move_cursor(int x, int y) noexcept {
        std::cout << "\033[" << x << ";" << y << "H";
    }

    static void
    move_cursor_up(int n) noexcept {
        std::cout << "\033[" << n << "A";
    }

    static void
    move_cursor_down(int n) noexcept {
        std::cout << "\033[" << n << "B";
    }

    static void
    move_cursor_right(int n) noexcept {
        std::cout << "\033[" << n << "C";
    }

    static void
    move_cursor_left(int n) noexcept {
        std::cout << "\033[" << n << "D";
    }

    static void
    move_cursor_to_column(int n) noexcept {
        std::cout << "\033[" << n << "G";
    }

    static void
    move_cursor_line_start() noexcept {
        std::cout << "\r";
    }

    static void
    save_cursor_position() noexcept {
        std::cout << "\033[s";
    }

    static void
    load_cursor_position() noexcept {
        std::cout << "\033[u";
    }

    static void
    show_cursor() noexcept {
        std::cout << "\033[?25h";
    }

    static void
    hide_cursor() noexcept {
        std::cout << "\033[?25l";
    }

    // Clearing.
    static void
    clear_line() noexcept {
        std::cout << "\033[K";
    }

    static void
    clear_screen() noexcept {
        std::cout << "\033[2J";
    }

    static void
    clear_from_cursor() noexcept {
        std::cout << "\033[0K";
    }

    static void
    clear_from_start_to_cursor() noexcept {
        std::cout << "\033[1K";
    }

    // Convert.
    static std::string
    to_string(bool value) {
        if (value) {
            return std::string(LIGHT_GREEN) + "true";
        }

        return std::string(LIGHT_RED) + "false";
    }

private:
    template <typename... Args>
    static void
    sub_info_impl(size_t const level, Args&&... args) noexcept {
        std::ostringstream oss;
        oss << "[INFO]: ";
        for (std::size_t i {}; i < level; ++i) {
            oss << "   ";
        }
        oss << SUB_LOG_SYMBOL << " ";

        log_message(oss.str().c_str(), WHITE, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_warn_impl(size_t const level, Args&&... args) noexcept {
        std::ostringstream oss;
        oss << "[WARNING]: ";
        for (std::size_t i {}; i < level; ++i) {
            oss << "   ";
        }
        oss << SUB_LOG_SYMBOL << " ";

        log_message(oss.str().c_str(), YELLOW, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    sub_error_impl(size_t const level, Args&&... args) noexcept {
        std::ostringstream oss;
        oss << "[ERROR]: ";
        for (std::size_t i {}; i < level; ++i) {
            oss << "   ";
        }
        oss << SUB_LOG_SYMBOL << " ";

        log_message(oss.str().c_str(), RED, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    log_message(
        char const* prefix,
        char const* color,
        Args&&... args
    ) noexcept {
        std::ostringstream oss;
        (oss << ... << args); // C++17 fold expression to concatenate args.

        std::string const message = oss.str();

#ifdef LOG_TO_FILE
        if (!std::filesystem::exists("../logs/")) {
            std::filesystem::create_directories("../logs/");
        }

        std::ofstream log_file {"../logs/app.log", std::ios_base::app};

        if (!log_file.is_open()) {
            std::cerr << "Failed to open log file." << std::endl;
        } else {
            log_file << prefix << message << EOL;
        }
#else
    #ifdef USE_COLOR
        std::cout << color << prefix << message << CLEAR << EOL;
    #else
        std::cout << prefix << message << EOL;
    #endif
#endif
    }
};

[[maybe_unused]] static void
v_assert(bool statement, char const* message) noexcept {
    if (statement) {
        return;
    }

    Log::error(__FILE__, __LINE__, "Assertion failed: ", message);

#if defined(NDEBUG) // Release mode.
    exit(EXIT_FAILURE);
#else
    assert(0);
#endif
}

} //namespace core
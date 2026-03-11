#include "ui.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

namespace ui {

// Spinner state
static std::atomic<bool> spinner_running{false};
static std::thread spinner_thread;

// Render an FTXUI Element to stdout
static void render(ftxui::Element element) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Full(), ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    std::cout << screen.ToString() << std::flush;
}

void print_banner() {
    using namespace ftxui;
    auto content = vbox({
        text("cc.cpp v0.1.0") | bold | center,
        text("A C++ Claude Code Implementation") | center,
    });
    auto banner = content | border | color(Color::Cyan);
    render(banner);
    std::cout << "\nType your message to start. Press Ctrl+D to exit.\n\n";
}

void print_prompt() {
    using namespace ftxui;
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(2), ftxui::Dimension::Fixed(1));
    ftxui::Render(screen, text("> ") | bold | color(Color::Green));
    std::cout << screen.ToString() << std::flush;
}

void print_tool_call(const std::string& name, const std::string& detail) {
    using namespace ftxui;
    auto label = text(" " + name + " ") | bold | color(Color::White) | bgcolor(Color::Blue);
    auto line = detail.empty()
        ? hbox({label})
        : hbox({label, text(" " + detail)});
    render(line);
    std::cout << "\n";
}

void print_tool_error(const std::string& message) {
    using namespace ftxui;
    auto line = hbox({
        text("  Error: ") | bold | color(Color::Red),
        text(message.length() > 200 ? message.substr(0, 200) : message) | color(Color::Red),
    });
    render(line);
    std::cout << "\n";
}

void print_api_error(const std::string& message) {
    using namespace ftxui;
    auto line = hbox({
        text("API Error: ") | bold | color(Color::Red),
        text(message),
    });
    render(line);
    std::cout << "\n";
}

void print_warning(const std::string& message) {
    using namespace ftxui;
    auto line = hbox({
        text("Warning: ") | bold | color(Color::Yellow),
        text(message) | color(Color::Yellow),
    });
    render(line);
    std::cout << "\n";
}

void print_debug(const std::string& label, const std::string& content) {
    using namespace ftxui;
    auto header = text("[DEBUG " + label + "]") | bold | color(Color::Magenta);
    render(header);
    std::cout << "\n" << content << "\n";
    auto footer = text("[/DEBUG]") | bold | color(Color::Magenta);
    render(footer);
    std::cout << "\n";
}

void print_permission_header(const std::string& tool_name, const std::string& detail) {
    using namespace ftxui;
    auto content = vbox({
        hbox({text("Permission Request") | bold}),
        separator(),
        hbox({text("Tool: ") | bold, text(tool_name)}),
        detail.empty() ? text("") : text(detail),
    });
    auto box = content | border | color(Color::Yellow);
    std::cout << "\n";
    render(box);
    std::cout << "\n";
}

void print_permission_prompt(const std::string& tool_name) {
    using namespace ftxui;
    auto prompt = hbox({
        text("[y]es / [n]o / [a]lways allow " + tool_name + ": ") | bold | color(Color::Yellow),
    });
    render(prompt);
}

// Braille spinner characters
static const char* spinner_frames[] = {
    "\u280B", "\u2819", "\u2839", "\u2838", "\u283C", "\u2834", "\u2826", "\u2827", "\u2807", "\u280F"
};
static const int spinner_frame_count = 10;

void start_spinner(const std::string& message) {
    stop_spinner(); // Stop any existing spinner

    spinner_running = true;
    spinner_thread = std::thread([message]() {
        int f = 0;
        while (spinner_running) {
            std::cout << "\r" << spinner_frames[f % spinner_frame_count] << " " << message << "   " << std::flush;
            f++;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        // Clear the spinner line
        std::cout << "\r" << std::string(message.length() + 10, ' ') << "\r" << std::flush;
    });
}

void stop_spinner() {
    if (spinner_running) {
        spinner_running = false;
        if (spinner_thread.joinable()) {
            spinner_thread.join();
        }
    }
}

} // namespace ui

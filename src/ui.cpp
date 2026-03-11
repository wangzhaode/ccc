#include "ui.hpp"
#include "agent.hpp"
#include <iostream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/color.hpp>
#include <filesystem>
#include <sys/ioctl.h>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

using namespace ftxui;

static std::mutex ui_mutex;

static std::atomic<bool> spinner_running{false};
static std::string spinner_msg = "Thinking...";
static std::thread spinner_thread;

const char* spinner_frames[] = {
    "\u2847", "\u2819", "\u2839", "\u2838",
    "\u283c", "\u2834", "\u2826", "\u2827",
    "\u2807", "\u280f"
};

static int term_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
    return 80;
}

static int term_height() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
        return w.ws_row;
    return 24;
}

static const int FOOTER_LINES = 4;  // top_sep + input + bot_sep + hint

// Build top separator: ─────── ▪▪▪ ─
static std::string build_top_sep(int tw) {
    int n = tw - 6;
    if (n < 1) n = 1;
    std::string s;
    for (int i = 0; i < n; i++) s += "\u2500";
    s += " \u25aa\u25aa\u25aa \u2500";
    return s;
}

// Build bottom separator: ───────────
static std::string build_bot_sep(int tw) {
    std::string s;
    for (int i = 0; i < tw; i++) s += "\u2500";
    return s;
}

// Draw a static footer at terminal bottom. Preserves cursor via DECSC/DECRC.
static void draw_processing_footer() {
    int rows = term_height();
    int tw = term_width();
    int r = rows - 3;  // 4 lines: r, r+1, r+2, r+3

    std::string top_sep = build_top_sep(tw);
    std::string bot_sep = build_bot_sep(tw);

    std::cout << "\0337";  // DECSC — save cursor
    std::cout << "\033[" << r     << ";1H\033[K\033[90m" << top_sep << "\033[0m";
    std::cout << "\033[" << (r+1) << ";1H\033[K\033[1;32m\u276f\033[0m ";
    std::cout << "\033[" << (r+2) << ";1H\033[K\033[90m" << bot_sep << "\033[0m";
    std::cout << "\033[" << (r+3) << ";1H\033[K\033[90m  esc to interrupt\033[0m";
    std::cout << "\0338" << std::flush;  // DECRC — restore cursor
}

// Clear the footer area. Preserves cursor.
static void clear_processing_footer() {
    int rows = term_height();
    int r = rows - 3;
    std::cout << "\0337";
    std::cout << "\033[" << r << ";1H\033[J";
    std::cout << "\0338" << std::flush;
}

void ui::init() {}
void ui::cleanup() {
    stop_spinner();
}

void ui::start_spinner(const std::string& message) {
    if (spinner_running) return;
    spinner_msg = message;
    spinner_running = true;
    spinner_thread = std::thread([]() {
        int i = 0;
        while (spinner_running) {
            {
                std::lock_guard<std::mutex> lock(ui_mutex);
                std::cout << "\r\033[K\033[33m" << spinner_frames[i % 10]
                          << " \033[0m" << spinner_msg << std::flush;
            }
            i++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void ui::stop_spinner() {
    if (spinner_running) {
        spinner_running = false;
        if (spinner_thread.joinable()) {
            spinner_thread.join();
        }
        std::lock_guard<std::mutex> lock(ui_mutex);
        std::cout << "\r\033[K" << std::flush;
    }
}

static std::atomic<bool> assistant_line_started{false};

void ui::append_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    if (!assistant_line_started) {
        std::cout << "\033[36m\u23fa\033[0m ";
        assistant_line_started = true;
    }
    std::cout << text << std::flush;
}

void ui::end_assistant_response() {
    assistant_line_started = false;
}

void ui::print_assistant_stats(const std::string& stats) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[90m" << stats << "\033[0m\n";
}

void ui::print_user_input(const std::string& input) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[1;32m\u276f\033[0m " << input << "\n";
}

void ui::print_tool_call(const std::string& tool_name, const std::string& detail) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\n\033[36m\u23fa\033[0m \033[1m" << tool_name << "\033[0m\n";
    if (!detail.empty()) {
        std::cout << "  \033[90m\u23bf  " << detail << "\033[0m\n";
    }
}

void ui::print_tool_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[31mTool Error: " << error << "\033[0m\n";
}

void ui::print_api_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[1;31mAPI Error: " << error << "\033[0m\n";
}

void ui::print_warning(const std::string& message) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[33m" << message << "\033[0m\n";
}

void ui::print_debug(const std::string& label, const std::string& content) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    std::cout << "\033[90m[" << label << "] " << content << "\033[0m\n";
}

void ui::print_permission_header(const std::string& tool_name, const std::string& detail) {}
void ui::print_permission_prompt(const std::string& tool_name) {}

char ui::prompt_permission(const std::string& tool_name, const std::string& detail) {
    auto screen = ScreenInteractive::FitComponent();
    screen.TrackMouse(false);
    char result = 'n';
    auto render_func = [&] {
        return vbox({
            separator(),
            vbox({
                hbox({text("Permission Request") | bold}),
                separatorLight(),
                hbox({text("Tool: ") | bold, text(tool_name)}),
                text(detail)
            }) | border | color(Color::Yellow),
            text("[y]es / [n]o / [a]lways allow: ") | bold | color(Color::Red)
        });
    };
    auto content = Renderer(render_func);
    content |= CatchEvent([&](Event e) {
        if (e == Event::Character('y') || e == Event::Character('Y')) {
            result = 'y';
            screen.ExitLoopClosure()();
            return true;
        }
        if (e == Event::Character('n') || e == Event::Character('N')) {
            result = 'n';
            screen.ExitLoopClosure()();
            return true;
        }
        if (e == Event::Character('a') || e == Event::Character('A')) {
            result = 'a';
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });
    screen.Loop(content);
    return result;
}

void ui::run_tui(Agent& agent) {
    std::string cwd = std::filesystem::current_path().string();
    const char* home = std::getenv("HOME");
    if (home && cwd.find(home) == 0) {
        cwd = "~" + cwd.substr(std::string(home).length());
    }

    // Banner: "ccc" pixel art logo + info (3 lines)
    std::cout << "\n";
    std::cout << "\033[1;36m\u2597\u2588\u2588\u2596 \u2597\u2588\u2588\u2596 \u2597\u2588\u2588\u2596\033[0m  "
              << "\033[1mcc.cpp\033[0m \033[90mv0.1.0\033[0m\n";
    std::cout << "\033[1;36m\u2588\u2588   \u2588\u2588   \u2588\u2588\033[0m     "
              << "\033[90m" << agent.model() << "\033[0m\n";
    std::cout << "\033[1;36m\u259d\u2588\u2588\u2598 \u259d\u2588\u2588\u2598 \u259d\u2588\u2588\u2598\033[0m  "
              << "\033[90m" << cwd << "\033[0m\n";
    std::cout << "\n";

    while (true) {
        std::string user_input;
        bool exit_requested = false;
        int tw = term_width();

        // === INPUT: FTXUI inline 4-line input box ===
        auto screen = ScreenInteractive::FitComponent();
        screen.TrackMouse(false);

        InputOption input_option;
        input_option.transform = [](InputState state) {
            if (state.is_placeholder) {
                return state.element | color(Color::GrayDark);
            }
            return state.element | color(Color::Default);
        };
        Component input_box = Input(&user_input, "Try \"help me fix this bug...\"", input_option);

        auto render_func = [&] {
            std::string top_sep = build_top_sep(tw);
            std::string bot_sep = build_bot_sep(tw);
            return vbox({
                text(top_sep) | color(Color::GrayDark),
                hbox({text("\u276f ") | bold | color(Color::Green), input_box->Render()}),
                text(bot_sep) | color(Color::GrayDark),
                text("  ? for shortcuts") | color(Color::GrayDark),
            });
        };

        auto main_component = Renderer(input_box, render_func);
        main_component |= CatchEvent([&](Event event) {
            if (event == Event::Character((char)3) || event == Event::Character((char)4)) {
                exit_requested = true;
                screen.ExitLoopClosure()();
                return true;
            }
            if (event == Event::Return) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        });

        screen.Loop(main_component);

        // Clear FTXUI's 4-line output
        std::cout << "\033[4A\033[J" << std::flush;

        if (exit_requested) break;
        if (user_input.empty()) continue;

        print_user_input(user_input);
        std::cout << "\n";
        agent.process(user_input);
        std::cout << "\n";
    }
}

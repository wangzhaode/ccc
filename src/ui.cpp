#include "ui.hpp"
#include "agent.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>

namespace ui {

using namespace ftxui;

enum class AppMode {
    Chat,
    Permission
};

static std::mutex ui_mutex;
static std::vector<Element> chat_history;
static std::string current_stream_text;
static std::string current_stats_text;
static ScreenInteractive* global_screen = nullptr;

static AppMode current_mode = AppMode::Chat;
static std::string perm_tool_name;
static std::string perm_detail;
static char perm_result = 'n';
static std::condition_variable perm_cv;
static bool perm_ready = false;

static std::atomic<bool> spinner_running{false};
static std::string spinner_msg = "Thinking...";
static std::thread spinner_thread;

static void trigger_redraw() {
    if (global_screen) {
        global_screen->PostEvent(Event::Custom);
    }
}

static void flush_stream() {
    if (!current_stats_text.empty() || !current_stream_text.empty()) {
        if (!current_stats_text.empty()) {
            chat_history.push_back(text(current_stats_text) | color(Color::GrayDark));
        }
        if (!current_stream_text.empty()) {
            chat_history.push_back(paragraph(current_stream_text) | color(Color::Cyan));
        }
        current_stats_text.clear();
        current_stream_text.clear();
    }
}

void init() {}
void cleanup() {}

void append_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    current_stream_text += text;
    trigger_redraw();
}

void print_assistant_stats(const std::string& stats) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    current_stats_text = stats;
    trigger_redraw();
}

void print_user_input(const std::string& input) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    chat_history.push_back(hbox({
        text("❯ ") | bold | color(Color::Green),
        paragraph(input)
    }));
    trigger_redraw();
}

void print_tool_call(const std::string& name, const std::string& detail) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    auto label = text(" " + name + " ") | bold | color(Color::White) | bgcolor(Color::Blue);
    auto line = detail.empty() ? hbox({label}) : hbox({label, text(" " + detail)});
    chat_history.push_back(line);
    trigger_redraw();
}

void print_tool_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    auto line = hbox({
        text("  Error: ") | bold | color(Color::Red),
        paragraph(message) | color(Color::Red),
    });
    chat_history.push_back(line);
    trigger_redraw();
}

void print_api_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    auto line = hbox({
        text("API Error: ") | bold | color(Color::Red),
        paragraph(message),
    });
    chat_history.push_back(line);
    trigger_redraw();
}

void print_warning(const std::string& message) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    auto line = hbox({
        text("Warning: ") | bold | color(Color::Yellow),
        paragraph(message) | color(Color::Yellow),
    });
    chat_history.push_back(line);
    trigger_redraw();
}

void print_debug(const std::string& label, const std::string& content) {
    std::lock_guard<std::mutex> lock(ui_mutex);
    flush_stream();
    chat_history.push_back(text("[DEBUG " + label + "]") | bold | color(Color::Magenta));
    chat_history.push_back(paragraph(content));
    chat_history.push_back(text("[/DEBUG]") | bold | color(Color::Magenta));
    trigger_redraw();
}

// Braille spinner characters
static const char* spinner_frames[] = {
    "\u280B", "\u2819", "\u2839", "\u2838", "\u283C", "\u2834", "\u2826", "\u2827", "\u2807", "\u280F"
};

void start_spinner(const std::string& message) {
    // We don't need a separate thread since FTXUI renders on its own.
    // However, to get animation, we can post a custom event continuously.
    // For simplicity, let's just use FTXUI's built in spinner!
    // But since `Agent` calls start_spinner synchronously, we just store state.
    std::lock_guard<std::mutex> lock(ui_mutex);
    spinner_msg = message;
    spinner_running = true;
    trigger_redraw();

    // Start a thread just to trigger redraws so the spinner animates
    spinner_thread = std::thread([]() {
        while (spinner_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            trigger_redraw();
        }
    });
}

void stop_spinner() {
    bool was_running = false;
    {
        std::lock_guard<std::mutex> lock(ui_mutex);
        was_running = spinner_running.exchange(false);
    }
    trigger_redraw();
    if (was_running && spinner_thread.joinable()) {
        spinner_thread.join();
    }
}

char prompt_permission(const std::string& tool_name, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lock(ui_mutex);
        flush_stream();
        perm_tool_name = tool_name;
        perm_detail = detail;
        perm_ready = false;
        current_mode = AppMode::Permission;
    }
    trigger_redraw();

    std::unique_lock<std::mutex> lock(ui_mutex);
    perm_cv.wait(lock, [] { return perm_ready; });

    char result = perm_result;
    current_mode = AppMode::Chat;
    trigger_redraw();
    return result;
}

void print_permission_header(const std::string& tool_name, const std::string& detail) {
    // Handled in TUI render
}

void print_permission_prompt(const std::string& tool_name) {
    // Handled in TUI render
}

void run_tui(Agent& agent) {
    auto screen = ScreenInteractive::Fullscreen();
    global_screen = &screen;

    // Add Banner
    chat_history.push_back(vbox({
        text("cc.cpp v0.1.0") | bold | center,
        text("A C++ Claude Code Implementation") | center,
    }) | border | color(Color::Cyan));
    chat_history.push_back(text("Type your message to start. Press Ctrl+D/Ctrl+C to exit."));

    std::string user_input;
    InputOption input_option;
    input_option.transform = [](InputState state) {
        return state.element | color(Color::Default);
    };
    Component input_box = Input(&user_input, "", input_option);
    int spinner_index = 0;

    auto render_func = [&] {
        std::lock_guard<std::mutex> lock(ui_mutex);

        Elements hist_els;
        for (auto& el : chat_history) {
            hist_els.push_back(el);
        }
        if (!current_stats_text.empty()) {
            hist_els.push_back(text(current_stats_text) | color(Color::GrayDark));
        }
        if (!current_stream_text.empty()) {
            hist_els.push_back(paragraph(current_stream_text) | color(Color::Cyan));
        }

        if (current_mode == AppMode::Chat && spinner_running) {
            hist_els.push_back(hbox({
                text(std::string(spinner_frames[spinner_index % 10]) + " " + spinner_msg) | color(Color::Yellow),
            }));
            spinner_index++;
        }

        auto history_vbox = vbox(std::move(hist_els)) | focusPositionRelative(0, 1.0) | yframe | flex;

        Element bottom_area;
        if (current_mode == AppMode::Chat) {
            bottom_area = vbox({
                separator(),
                hbox({text("❯ ") | bold | color(Color::Green), input_box->Render()}),
                separator(),
                text("  ⏵⏵ send (enter)") | color(Color::GrayDark)
            });
        } else {
            bottom_area = vbox({
                separator(),
                vbox({
                    hbox({text("Permission Request") | bold}),
                    separatorLight(),
                    hbox({text("Tool: ") | bold, text(perm_tool_name)}),
                    text(perm_detail)
                }) | border | color(Color::Yellow),
                text("[y]es / [n]o / [a]lways allow: ") | bold | color(Color::Red)
            });
        }

        return vbox({
            history_vbox,
            bottom_area
        });
    };

    auto main_component = Renderer(input_box, render_func);

    main_component |= CatchEvent([&](Event event) {
        if (event == Event::Character((char)3)) {
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character((char)4)) {
            screen.ExitLoopClosure()();
            return true;
        }

        if (current_mode == AppMode::Chat) {
            if (event == Event::Return) {
                if (user_input.empty()) return true;

                std::string msg = user_input;
                user_input.clear();
                print_user_input(msg);

                // Spawn worker for agent
                std::thread([&agent, msg]() {
                    agent.process(msg);
                    std::lock_guard<std::mutex> lock(ui_mutex);
                    flush_stream();
                    trigger_redraw();
                }).detach();
                return true;
            }
        } else if (current_mode == AppMode::Permission) {
            if (event.is_character()) {
                char ch = std::tolower(event.character()[0]);
                if (ch == 'y' || ch == 'n' || ch == 'a') {
                    std::lock_guard<std::mutex> lock(ui_mutex);
                    perm_result = ch;
                    perm_ready = true;
                    perm_cv.notify_one();
                    return true;
                }
            }
            return true; // absorb all input while awaiting permission
        }

        return false;
    });

    screen.Loop(main_component);

    // Stop and cleanup
    stop_spinner();
    global_screen = nullptr;
}

} // namespace ui

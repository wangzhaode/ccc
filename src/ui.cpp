#include "ui.hpp"
#include "agent.hpp"
#include <iostream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/color.hpp>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

using namespace ftxui;

static std::mutex ui_mutex;

static std::atomic<bool> spinner_running{false};
static std::string spinner_msg = "Thinking...";
static std::thread spinner_thread;

const char* spinner_frames[] = {"\u2847", "\u2819", "\u2839", "\u2838", "\u283c",
                                "\u2834", "\u2826", "\u2827", "\u2807", "\u280f"};

static int term_width() {
#ifdef _WIN32
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      return width > 0 ? width : 80;
    }
  }
  return 80;
#else
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
    return w.ws_col;
  return 80;
#endif
}

static int term_height() {
#ifdef _WIN32
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      return height > 0 ? height : 24;
    }
  }
  return 24;
#else
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
    return w.ws_row;
  return 24;
#endif
}

static const int FOOTER_LINES = 4;  // top_sep + input + bot_sep + hint

// Build top separator: ─────── ▪▪▪ ─
static std::string build_top_sep(int tw) {
  int n = tw - 6;
  if (n < 1)
    n = 1;
  std::string s;
  for (int i = 0; i < n; i++)
    s += "\u2500";
  s += " \u25aa\u25aa\u25aa \u2500";
  return s;
}

// Build bottom separator: ───────────
static std::string build_bot_sep(int tw) {
  std::string s;
  for (int i = 0; i < tw; i++)
    s += "\u2500";
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
  std::cout << "\033[" << r << ";1H\033[K\033[90m" << top_sep << "\033[0m";
  std::cout << "\033[" << (r + 1) << ";1H\033[K\033[1;32m\u276f\033[0m ";
  std::cout << "\033[" << (r + 2) << ";1H\033[K\033[90m" << bot_sep << "\033[0m";
  std::cout << "\033[" << (r + 3) << ";1H\033[K\033[90m  esc to interrupt\033[0m";
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
  if (spinner_running)
    return;
  spinner_msg = message;
  spinner_running = true;
  spinner_thread = std::thread([]() {
    int i = 0;
    while (spinner_running) {
      {
        std::lock_guard<std::mutex> lock(ui_mutex);
        std::cout << "\r\033[K\033[33m" << spinner_frames[i % 10] << " \033[0m" << spinner_msg
                  << std::flush;
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
    return vbox({separator(),
                 vbox({hbox({text("Permission Request") | bold}), separatorLight(),
                       hbox({text("Tool: ") | bold, text(tool_name)}), text(detail)}) |
                     border | color(Color::Yellow),
                 text("[y]es / [n]o / [a]lways allow: ") | bold | color(Color::Red)});
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
#ifdef _WIN32
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  if (home && cwd.find(home) == 0) {
    cwd = "~" + cwd.substr(std::string(home).length());
  }

  // Banner: "ccc" pixel art logo + info (3 lines)
  std::cout << "\n";
  std::cout << "\033[1;36m\u2597\u2588\u2588\u2596 \u2597\u2588\u2588\u2596 "
               "\u2597\u2588\u2588\u2596\033[0m  "
            << "\033[1mccc\033[0m \033[90mv0.1.0\033[0m\n";
  std::cout << "\033[1;36m\u2588\u2588   \u2588\u2588   \u2588\u2588\033[0m     "
            << "\033[90m" << agent.model() << "\033[0m\n";
  std::cout << "\033[1;36m\u259d\u2588\u2588\u2598 \u259d\u2588\u2588\u2598 "
               "\u259d\u2588\u2588\u2598\033[0m  "
            << "\033[90m" << cwd << "\033[0m\n";
  std::cout << "\n";

  // Pre-fetch skill list for autocomplete
  auto skill_list = agent.get_skill_list();

  while (true) {
    std::string user_input;
    bool exit_requested = false;
    int tw = term_width();
    int selected_index = 0;
    int last_render_lines = 4;  // base: top_sep + input + bot_sep + hint

    // === INPUT: FTXUI inline input box with slash-command autocomplete ===
    auto screen = ScreenInteractive::FitComponent();
    screen.TrackMouse(false);

    InputOption input_option;
    input_option.transform = [](InputState state) {
      if (state.is_placeholder) {
        return state.element | color(Color::GrayDark);
      }
      return state.element | color(Color::Default);
    };
    input_option.on_change = [&]() { selected_index = 0; };
    Component input_box = Input(&user_input, "Try \"help me fix this bug...\"", input_option);

    // Helper: get filtered candidates based on current input
    auto get_candidates = [&]() -> std::vector<std::pair<std::string, std::string>> {
      std::vector<std::pair<std::string, std::string>> candidates;
      if (user_input.empty() || user_input[0] != '/')
        return candidates;
      std::string prefix = user_input.substr(1);  // strip leading '/'
      for (const auto& [name, desc] : skill_list) {
        if (prefix.empty() || name.find(prefix) == 0) {
          candidates.emplace_back(name, desc);
        }
      }
      return candidates;
    };

    auto render_func = [&] {
      std::string top_sep = build_top_sep(tw);
      std::string bot_sep = build_bot_sep(tw);

      Elements lines;
      lines.push_back(text(top_sep) | color(Color::GrayDark));
      lines.push_back(hbox({text("\u276f ") | bold | color(Color::Green), input_box->Render()}));

      // Render autocomplete candidates if input starts with '/'
      auto candidates = get_candidates();
      if (!candidates.empty()) {
        for (int i = 0; i < (int)candidates.size(); i++) {
          std::string label = "  /" + candidates[i].first;
          std::string desc = "  - " + candidates[i].second;
          auto entry = hbox({
              text(label) | bold,
              text(desc) | color(Color::GrayDark),
          });
          if (i == selected_index) {
            entry = entry | inverted;
          }
          lines.push_back(entry);
        }
      }

      lines.push_back(text(bot_sep) | color(Color::GrayDark));
      lines.push_back(text("  ? for shortcuts") | color(Color::GrayDark));
      last_render_lines = (int)lines.size();
      return vbox(std::move(lines));
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

      // Autocomplete keyboard handling
      auto candidates = get_candidates();
      bool has_candidates = !candidates.empty();

      if (has_candidates && event == Event::Tab) {
        // Fill input with selected candidate
        user_input = "/" + candidates[selected_index].first + " ";
        return true;
      }
      if (has_candidates && event == Event::ArrowUp) {
        selected_index = (selected_index - 1 + (int)candidates.size()) % (int)candidates.size();
        return true;
      }
      if (has_candidates && event == Event::ArrowDown) {
        selected_index = (selected_index + 1) % (int)candidates.size();
        return true;
      }

      return false;
    });

    screen.Loop(main_component);

    // Clear FTXUI's output (dynamic height due to autocomplete candidates)
    std::cout << "\033[" << last_render_lines << "A\033[J" << std::flush;

    if (exit_requested)
      break;
    if (user_input.empty())
      continue;

    print_user_input(user_input);
    std::cout << "\n";
    agent.process(user_input);
    std::cout << "\n";
  }
}

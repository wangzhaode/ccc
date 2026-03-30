#include <iostream>
#include <string>
#include "agent.hpp"
#include "ui.hpp"

int main(int argc, char* argv[]) {
  Agent agent;

  // Parse flags and collect non-flag arguments
  std::string input;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-y" || arg == "--auto-accept") {
      agent.set_auto_accept(true);
    } else {
      if (!input.empty())
        input += " ";
      input += arg;
    }
  }

  // Support single-shot mode from command line
  if (!input.empty()) {
    agent.process(input);
    return 0;
  }

  // Run Fullscreen TUI mode
  ui::run_tui(agent);

  return 0;
}

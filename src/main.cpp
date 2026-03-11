#include <iostream>
#include <string>
#include "agent.hpp"
#include "ui.hpp"

int main(int argc, char* argv[]) {
    Agent agent;

    // Support single-shot mode from command line
    if (argc > 1) {
        std::string input;
        for (int i = 1; i < argc; i++) {
            if (i > 1) input += " ";
            input += argv[i];
        }
        agent.process(input);
        return 0;
    }

    // Run Fullscreen TUI mode
    ui::run_tui(agent);

    return 0;
}

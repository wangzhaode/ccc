#include <iostream>
#include <string>
#include "agent.hpp"

static void print_banner() {
    std::cout << "\033[1;36m"
              << "╔═══════════════════════════════════════╗\n"
              << "║           cc.cpp v0.1.0               ║\n"
              << "║   A C++ Claude Code Implementation    ║\n"
              << "╚═══════════════════════════════════════╝"
              << "\033[0m\n\n";
    std::cout << "Type your message to start. Press Ctrl+D to exit.\n\n";
}

int main(int argc, char* argv[]) {
    print_banner();

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

    // Interactive REPL mode
    std::string line;
    std::string multiline_input;

    while (true) {
        std::cout << "\033[1;32m> \033[0m";
        if (!std::getline(std::cin, line)) {
            std::cout << "\nGoodbye!\n";
            break;
        }
        if (line.empty()) continue;

        agent.process(line);
    }

    return 0;
}

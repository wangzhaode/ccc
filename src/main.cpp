#include <iostream>
#include <string>
#include "agent.hpp"
#include "ui.hpp"

int main(int argc, char* argv[]) {
    ui::print_banner();

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

    while (true) {
        ui::print_prompt();
        if (!std::getline(std::cin, line)) {
            std::cout << "\nGoodbye!\n";
            break;
        }
        if (line.empty()) continue;

        agent.process(line);
    }

    return 0;
}

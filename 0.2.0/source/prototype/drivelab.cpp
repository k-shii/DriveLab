#include <ncurses.h>

#include <clocale>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "app/core_cli.h"
#include "demo_ui.h"

namespace {

int runNcursesDemo() {
    std::setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_BLUE, -1);
        init_pair(3, COLOR_GREEN, -1);
        init_pair(4, COLOR_RED, -1);
        init_pair(5, COLOR_YELLOW, -1);
    }

    const int result = runDemoUi();
    endwin();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return drivelab::runApplicationCli(
        arguments, argv[0], std::cout, std::cerr, runNcursesDemo);
}

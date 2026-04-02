#ifndef APP_APP_OPTIONS_HPP
#define APP_APP_OPTIONS_HPP

#include "board.hpp"

#include <string>

namespace AppOptions {

enum class ExecutionMode {
    Cli,
    Gui
};

struct Options {
    ExecutionMode mode = ExecutionMode::Cli;
    Color userColor = Color::White;
    Color aiColor = Color::Black;
    int searchDepth = 4;
    bool showBoard = true;
    std::string bookPath = "./openings";
    std::string message;
};

Options parseOptions(int argc, char* argv[]);

} // namespace AppOptions

#endif

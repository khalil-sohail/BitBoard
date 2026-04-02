#include "app/app_cli.hpp"
#include "app/app_options.hpp"
#include "app/app_uci.hpp"
#include "board.hpp"

int main(int argc, char* argv[]) {
    Board board;
    const AppOptions::Options options = AppOptions::parseOptions(argc, argv);

    if (options.mode == AppOptions::ExecutionMode::Gui) {
        AppUci::runUciMode(board, options);
    } else {
        AppCli::runCliMode(board, options);
    }

    return 0;
}

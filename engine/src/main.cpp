#include "app/app_cli.hpp"
#include "app/app_options.hpp"
#include "app/app_uci.hpp"
#include "app/app_eval_features.hpp"
#include "app/app_time_policy.hpp"
#include "board.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    Board board;
    const AppOptions::Options options = AppOptions::parseOptions(argc, argv);

    if (options.showHelp) {
        std::cout << "Usage: " << argv[0] << " [OPTIONS]\n\n"
                  << "Options:\n"
                  << "  -h, --help       Show this help message and exit\n"
                  << "  --mode=MODE      Set mode: cli, gui, eval-features, or time-policy (default: cli)\n"
                  << "  --depth=DEPTH    Set search depth for AI. DEPTH must be a positive integer (default: 4)\n"
                  << "  --color=COLOR    Set user color. COLOR can be 'white' or 'black' (default: white)\n"
                  << "  --no-board       Disable displaying the board in CLI mode\n"
                  << "  --book=PATH      Set path to the opening book (default: ./openings)\n";
        return 0;
    }

    if (options.mode == AppOptions::ExecutionMode::EvalFeatures) {
        return AppEvalFeatures::run(board);
    } else if (options.mode == AppOptions::ExecutionMode::TimePolicy) {
        return AppTimePolicy::run();
    } else if (options.mode == AppOptions::ExecutionMode::Gui) {
        AppUci::runUciMode(board, options);
    } else {
        AppCli::runCliMode(board, options);
    }

    return 0;
}

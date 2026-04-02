#ifndef APP_APP_CLI_HPP
#define APP_APP_CLI_HPP

#include "app/app_options.hpp"
#include "board.hpp"

namespace AppCli {

void runCliMode(Board& board, const AppOptions::Options& options);

} // namespace AppCli

#endif

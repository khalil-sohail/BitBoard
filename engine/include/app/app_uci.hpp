#ifndef APP_APP_UCI_HPP
#define APP_APP_UCI_HPP

#include "app/app_options.hpp"
#include "board.hpp"

namespace AppUci {

void runUciMode(Board& board, const AppOptions::Options& options);

} // namespace AppUci

#endif

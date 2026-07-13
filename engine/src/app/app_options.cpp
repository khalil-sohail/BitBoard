#include "app/app_options.hpp"

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr const char* OPENINGS_ABSOLUTE_PATH = "./openings";

bool hasFlag(int argc, char* argv[], const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> getFlagValue(int argc, char* argv[], const std::string& prefix) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return std::nullopt;
}

int parseDepthValue(const std::string& raw, std::string& message) {
    if (raw.empty()) {
        if (!message.empty()) message += "\n";
        message += "Invalid --depth value. Defaulting to 4.";
        return 4;
    }

    for (char c : raw) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            if (!message.empty()) message += "\n";
            message += "Invalid --depth value. Defaulting to 4.";
            return 4;
        }
    }

    int parsed = 4;
    try {
        parsed = std::stoi(raw);
    } catch (...) {
        if (!message.empty()) message += "\n";
        message += "Invalid --depth value. Defaulting to 4.";
        return 4;
    }

    if (parsed <= 0) {
        if (!message.empty()) message += "\n";
        message += "Depth must be positive. Defaulting to 4.";
        return 4;
    }
    return parsed;
}

int parseSearchDepth(int argc, char* argv[], std::string& message) {
    if (std::optional<std::string> value = getFlagValue(argc, argv, "--depth=")) {
        return parseDepthValue(*value, message);
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--depth") {
            return parseDepthValue(argv[i + 1], message);
        }
    }

    return 4;
}

AppOptions::ExecutionMode parseExecutionMode(int argc, char* argv[], std::string& message) {
    auto parseModeValue = [&](std::string value) -> AppOptions::ExecutionMode {
        for (char& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (value == "cli") {
            return AppOptions::ExecutionMode::Cli;
        }
        if (value == "gui") {
            return AppOptions::ExecutionMode::Gui;
        }
        if (value == "eval-features") {
            return AppOptions::ExecutionMode::EvalFeatures;
        }
        if (!message.empty()) {
            message += "\n";
        }
        message += "Invalid --mode value. Use --mode=cli, --mode=gui, or --mode=eval-features. Defaulting to cli.";
        return AppOptions::ExecutionMode::Cli;
    };

    if (std::optional<std::string> value = getFlagValue(argc, argv, "--mode=")) {
        return parseModeValue(*value);
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode") {
            return parseModeValue(argv[i + 1]);
        }
    }

    return AppOptions::ExecutionMode::Cli;
}

Color parseUserColor(int argc, char* argv[], std::string& message) {
    Color userColor = Color::White;

    if (std::optional<std::string> value = getFlagValue(argc, argv, "--color=")) {
        std::string v = *value;
        for (char& c : v) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (v == "white") {
            return Color::White;
        }
        if (v == "black") {
            return Color::Black;
        }
        if (!message.empty()) message += "\n";
        message += "Invalid --color value. Use --color=white or --color=black. Defaulting to white.";
        return Color::White;
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "--color") {
            std::string v = argv[i + 1];
            for (char& c : v) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (v == "white") {
                return Color::White;
            }
            if (v == "black") {
                return Color::Black;
            }
            if (!message.empty()) message += "\n";
            message += "Invalid --color value. Use --color=white or --color=black. Defaulting to white.";
            return Color::White;
        }
    }

    return userColor;
}

Color oppositeColor(Color color) {
    return (color == Color::White) ? Color::Black : Color::White;
}

std::string parseBookPath(int argc, char* argv[]) {
    std::string bookPath = OPENINGS_ABSOLUTE_PATH;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string value;

        if (arg.rfind("--book=", 0) == 0) {
            value = arg.substr(std::string("--book=").size());
        } else if (arg.rfind("book=", 0) == 0) {
            value = arg.substr(std::string("book=").size());
        } else {
            continue;
        }

        if (value.empty()) {
            continue;
        }

        const bool hasSlash = value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
        if (hasSlash) {
            bookPath = value;
        } else {
            const std::filesystem::path defaultBookPath(OPENINGS_ABSOLUTE_PATH);
            const std::filesystem::path defaultBookDir =
                defaultBookPath.has_extension() ? defaultBookPath.parent_path() : defaultBookPath;
            bookPath = (defaultBookDir / value).string();
        }
    }
    return bookPath;
}

} // namespace

namespace AppOptions {

Options parseOptions(int argc, char* argv[]) {
    Options options;
    std::string message;

    if (hasFlag(argc, argv, "-h") || hasFlag(argc, argv, "--help")) {
        options.showHelp = true;
        return options;
    }

    options.showBoard = !hasFlag(argc, argv, "--no-board");
    options.bookPath = parseBookPath(argc, argv);

    options.userColor = parseUserColor(argc, argv, message);
    options.aiColor = oppositeColor(options.userColor);
    options.searchDepth = parseSearchDepth(argc, argv, message);
    options.mode = parseExecutionMode(argc, argv, message);

    options.message = std::move(message);
    return options;
}

} // namespace AppOptions

// Launcher fixture. Not linked to the game and never loads the supplied library.
#include "cli_arguments.h"
#include "host_file.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    cli::Arguments arguments(argc, argv);
    std::string game, profile;
    for (int i = 1; i < arguments.count; ++i) {
        std::cout << arguments.values[i] << '\n';
        const std::string option = arguments.values[i];
        if (i+1 < arguments.count && option == "--game-dir") game = arguments.values[i+1];
        if (i+1 < arguments.count && option == "--data") profile = arguments.values[i+1];
    }
    if (game.empty() || profile.empty()) return 2;
    std::ofstream(host_file::path(profile + "/synthetic-progress.txt"), std::ios::app) << "fixture-run\n";
    int code = 0, delay = 0;
    std::ifstream(host_file::path(game + "/exit-code.txt")) >> code;
    std::ifstream(host_file::path(game + "/hold-ms.txt")) >> delay;
    if (delay >= 0 && delay <= 6000) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    std::cout << "synthetic-no-game-executed\n";
    return code;
}

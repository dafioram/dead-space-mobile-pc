#include "apk.h"
#include "sha256.h"
#include "cli_arguments.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    cli::Arguments arguments(argc, argv);
    argc = arguments.count;
    argv = arguments.values;
    if (auto result = apk::import_cli(argc, argv)) return *result;
    if (argc == 3 && std::string(argv[1]) == "--hash") {
        std::string p = argv[2];
        std::ifstream f(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(p.data()), p.size())), std::ios::binary);
        if (!f) return 2;
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f), {});
        std::cout << sha256::digest(bytes) << '\n';
        return 0;
    }
    if (argc == 4) {
        apk::ImportFaults faults;
        std::string kind = argv[3];
        if (kind == "write") faults.bytes_before_error = 5;
        else if (kind == "close") faults.close_error = true;
        else if (kind == "publish") faults.publish_error = true;
        else return 2;
        return apk::import(argv[1], argv[2], &faults) ? 0 : 1;
    }
    if (argc != 3) return 2;
    return apk::import(argv[1], argv[2]) ? 0 : 1;
}

// Synthetic process only: no SDL, guest initialization, game assets or profiles.
#include "crash.h"
#include "cli_arguments.h"
#include "host_file.h"
#include "cpu.h"
#include "log.h"
#include "memory.h"
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cpu {
u32 current_pc() { return 0; }
std::string symbolize(u32) { return "synthetic"; }
}
namespace mem { u8* g_base = nullptr; }

int main(int argc, char** argv) {
    cli::Arguments arguments(argc, argv);
    argc = arguments.count;
    argv = arguments.values;
    if (argc != 3) return 2;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    const std::string directory = argv[1];
    logging::open_file((directory + "/probe.log").c_str());
    crash::install(directory);
    if (std::string(argv[2]) == "abort") std::abort();
#ifdef _WIN32
    RaiseException(EXCEPTION_ILLEGAL_INSTRUCTION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    return 2;
}

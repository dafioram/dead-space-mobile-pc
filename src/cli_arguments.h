#pragma once
#include <string>
#include <vector>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace cli {
// The port's path APIs use UTF-8. Do not reinterpret Windows ANSI argv bytes as
// UTF-8 when an APK/destination contains non-ASCII characters.
struct Arguments {
    int count;
    char** values;
    std::vector<std::string> storage;
    std::vector<char*> pointers;
    Arguments(int argc, char** argv) : count(argc), values(argv) {
#ifdef _WIN32
        int n = 0;
        wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &n);
        if (!wide) return;
        for (int i = 0; i < n; i++) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string text(size_t(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), bytes, nullptr, nullptr);
            if (!text.empty()) text.pop_back();
            storage.push_back(std::move(text));
        }
        LocalFree(wide);
        for (auto& s : storage) pointers.push_back(s.data());
        pointers.push_back(nullptr);
        count = n;
        values = pointers.data();
#endif
    }
};
}  // namespace cli

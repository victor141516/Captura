#pragma once

struct StartupOptions {
    bool background = false, managed = false;
};

inline StartupOptions startupOptions(int argc, const wchar_t* const* argv) {
    StartupOptions options;
    for (int n = 1; n < argc; ++n)
        if (wcscmp(argv[n], L"--background") == 0)
            options.background = true;
    wchar_t managed[2]{};
    options.managed =
        GetEnvironmentVariableW(L"WINKIT_MANAGED", managed, 2) == 1 && managed[0] == L'1';
    return options;
}

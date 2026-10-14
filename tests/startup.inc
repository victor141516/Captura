template <typename Check> void testStartupContract(Check check) {
    const wchar_t* normal[] = {L"Captura.exe"};
    const wchar_t* background[] = {L"Captura.exe", L"--background"};
    const wchar_t* argument[] = {L"Captura.exe", L"--background", L"WINKIT_MANAGED=1"};
    DWORD length = GetEnvironmentVariableW(L"WINKIT_MANAGED", nullptr, 0);
    std::vector<wchar_t> previous(length);
    if (length)
        GetEnvironmentVariableW(L"WINKIT_MANAGED", previous.data(), length);
    SetEnvironmentVariableW(L"WINKIT_MANAGED", nullptr);
    check(!startupOptions(1, normal).background && !startupOptions(1, normal).managed,
          L"Standalone launch is interactive and unmanaged by default");
    check(startupOptions(2, background).background && !startupOptions(2, background).managed,
          L"Background launch is independent of managed mode");
    check(!startupOptions(3, argument).managed,
          L"WinKit managed mode is an environment variable, never a command argument");
    SetEnvironmentVariableW(L"WINKIT_MANAGED", L"1");
    check(startupOptions(1, normal).managed && startupOptions(2, background).managed,
          L"WINKIT_MANAGED=1 is honored for normal and background launches");
    for (const wchar_t* value : {L"0", L"true", L"11", L"1x", L" 1"}) {
        SetEnvironmentVariableW(L"WINKIT_MANAGED", value);
        check(!startupOptions(2, background).managed,
              L"Only the exact managed environment value 1 enables WinKit mode");
    }
    SetEnvironmentVariableW(L"WINKIT_MANAGED", length ? previous.data() : nullptr);
}

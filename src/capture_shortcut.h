#pragma once

struct CaptureShortcut {
    UINT modifiers = MOD_CONTROL | MOD_SHIFT;
    UINT key = 'S';
    bool operator==(const CaptureShortcut& other) const {
        return modifiers == other.modifiers && key == other.key;
    }
};

bool validCaptureShortcut(CaptureShortcut shortcut) {
    constexpr UINT allowed = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    if ((shortcut.modifiers & ~allowed) ||
        !(shortcut.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)))
        return false;
    // F12 is reserved by Windows. Modifier keys cannot be the final key.
    UINT key = shortcut.key;
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
           (key >= VK_F1 && key <= VK_F24 && key != VK_F12) ||
           (key >= VK_NUMPAD0 && key <= VK_DIVIDE) || (key >= VK_PRIOR && key <= VK_DOWN) ||
           key == VK_INSERT || key == VK_DELETE || key == VK_SPACE || key == VK_TAB ||
           key == VK_RETURN || key == VK_BACK || key == VK_SNAPSHOT || key == VK_PAUSE ||
           (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_8) ||
           key == VK_OEM_102;
}

std::wstring captureShortcutName(CaptureShortcut shortcut) {
    std::wstring result;
    if (shortcut.modifiers & MOD_CONTROL)
        result += L"Ctrl + ";
    if (shortcut.modifiers & MOD_ALT)
        result += L"Alt + ";
    if (shortcut.modifiers & MOD_SHIFT)
        result += L"Shift + ";
    if (shortcut.modifiers & MOD_WIN)
        result += L"Win + ";
    if ((shortcut.key >= 'A' && shortcut.key <= 'Z') ||
        (shortcut.key >= '0' && shortcut.key <= '9'))
        return result + static_cast<wchar_t>(shortcut.key);
    if (shortcut.key >= VK_F1 && shortcut.key <= VK_F24)
        return result + L"F" + std::to_wstring(shortcut.key - VK_F1 + 1);
    const wchar_t* name = nullptr;
    switch (shortcut.key) {
    case VK_SPACE:
        name = L"Space";
        break;
    case VK_TAB:
        name = L"Tab";
        break;
    case VK_RETURN:
        name = L"Enter";
        break;
    case VK_BACK:
        name = L"Backspace";
        break;
    case VK_PRIOR:
        name = L"Page Up";
        break;
    case VK_NEXT:
        name = L"Page Down";
        break;
    case VK_END:
        name = L"End";
        break;
    case VK_HOME:
        name = L"Home";
        break;
    case VK_LEFT:
        name = L"Left";
        break;
    case VK_UP:
        name = L"Up";
        break;
    case VK_RIGHT:
        name = L"Right";
        break;
    case VK_DOWN:
        name = L"Down";
        break;
    case VK_INSERT:
        name = L"Insert";
        break;
    case VK_DELETE:
        name = L"Delete";
        break;
    case VK_SNAPSHOT:
        name = L"Print Screen";
        break;
    case VK_PAUSE:
        name = L"Pause";
        break;
    }
    if (name)
        return result + name;
    if (shortcut.key >= VK_NUMPAD0 && shortcut.key <= VK_NUMPAD9)
        return result + L"Num " + std::to_wstring(shortcut.key - VK_NUMPAD0);
    wchar_t keyName[64]{};
    UINT scan = MapVirtualKeyW(shortcut.key, MAPVK_VK_TO_VSC);
    GetKeyNameTextW(static_cast<LONG>(scan << 16), keyName, 64);
    return result + (keyName[0] ? keyName : L"?");
}

std::wstring captureSettingsPath() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        return {};
    std::wstring directory = std::wstring(folder) + L"\\Captura";
    CoTaskMemFree(folder);
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    return directory + L"\\settings.ini";
}

CaptureShortcut parseCaptureShortcut(const wchar_t* value) {
    CaptureShortcut shortcut{0, 0};
    auto number = [&value](UINT& result) {
        if (*value < L'0' || *value > L'9')
            return false;
        while (*value >= L'0' && *value <= L'9') {
            result = result * 10 + (*value++ - L'0');
            if (result > 255)
                return false;
        }
        return true;
    };
    if (!number(shortcut.modifiers) || *value++ != L',' || !number(shortcut.key) || *value ||
        !validCaptureShortcut(shortcut))
        return {};
    return shortcut;
}

CaptureShortcut loadCaptureShortcut(const std::wstring& path) {
    if (path.empty())
        return {};
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Captura", L"CaptureShortcut", L"", value, 64, path.c_str());
    return parseCaptureShortcut(value);
}

bool persistCaptureShortcut(const std::wstring& path, CaptureShortcut shortcut) {
    if (path.empty() || !validCaptureShortcut(shortcut))
        return false;
    // Replace a complete settings file atomically; a failed save leaves the old preference intact.
    std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    std::wstring text = L"\xFEFF[Captura]\r\nCaptureShortcut=" +
                        std::to_wstring(shortcut.modifiers) + L"," + std::to_wstring(shortcut.key) +
                        L"\r\n";
    DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t)), written = 0;
    bool saved = WriteFile(file, text.data(), bytes, &written, nullptr) && written == bytes;
    if (saved)
        saved = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (saved)
        saved = MoveFileExW(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!saved)
        DeleteFileW(temporary.c_str());
    return saved;
}

enum class ShortcutSaveResult { Saved, Unavailable, PersistenceError };

// Keep the old registration live until both acquiring and persisting its replacement succeed.
// Callbacks let integration tests cover failures without reserving the user's global shortcuts.
template <typename Register, typename Unregister, typename Persist>
ShortcutSaveResult replaceCaptureShortcut(CaptureShortcut& current, bool& registered, int& activeId,
                                          CaptureShortcut desired, Register acquire,
                                          Unregister release, Persist persist) {
    if (!validCaptureShortcut(desired))
        return ShortcutSaveResult::Unavailable;
    bool alreadyActive = registered && desired == current;
    int candidateId = activeId == 1 ? 2 : 1;
    if (!alreadyActive && !acquire(candidateId, desired))
        return ShortcutSaveResult::Unavailable;
    if (!persist(desired)) {
        if (!alreadyActive)
            release(candidateId);
        return ShortcutSaveResult::PersistenceError;
    }
    if (!alreadyActive) {
        if (registered)
            release(activeId);
        activeId = candidateId;
    }
    current = desired;
    registered = true;
    return ShortcutSaveResult::Saved;
}

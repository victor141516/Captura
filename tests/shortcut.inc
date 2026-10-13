// Settings tests use isolated files and fake registrations; no global hooks or hotkeys.
template <typename Check> void testCaptureShortcuts(const std::wstring& directory, Check check) {
    CaptureShortcut defaultShortcut;
    CaptureShortcut custom{MOD_CONTROL | MOD_ALT, 'K'};
    check(validCaptureShortcut(defaultShortcut) && validCaptureShortcut(custom),
          L"Default and custom capture combinations are accepted");
    check(!validCaptureShortcut({0, 'A'}) && !validCaptureShortcut({MOD_SHIFT, 'A'}) &&
              !validCaptureShortcut({MOD_CONTROL, VK_F12}) &&
              !validCaptureShortcut({MOD_CONTROL, VK_CONTROL}) &&
              !validCaptureShortcut({MOD_CONTROL | MOD_NOREPEAT, 'A'}) &&
              !validCaptureShortcut({MOD_CONTROL, 0x100}),
          L"Bare typing, reserved F12, modifier-only and malformed keys are rejected");
    check(validCaptureShortcut({MOD_ALT, VK_SNAPSHOT}) &&
              validCaptureShortcut({MOD_WIN | MOD_SHIFT, VK_F24}),
          L"Print Screen and modified function keys are supported");
    check(captureShortcutName(defaultShortcut) == L"Ctrl + Shift + S" &&
              captureShortcutName(custom) == L"Ctrl + Alt + K" &&
              captureShortcutName({MOD_ALT, VK_SNAPSHOT}) == L"Alt + Print Screen" &&
              captureShortcutName({MOD_CONTROL, VK_F9}) == L"Ctrl + F9",
          L"Shortcut labels are readable and have consistent modifier ordering");
    check(parseCaptureShortcut(L"3,75") == custom &&
              parseCaptureShortcut(L"999999999999999999,75") == defaultShortcut &&
              parseCaptureShortcut(L"3,75trailing") == defaultShortcut &&
              parseCaptureShortcut(L"3,") == defaultShortcut &&
              parseCaptureShortcut(L"") == defaultShortcut &&
              parseCaptureShortcut(L"4,65") == defaultShortcut,
          L"Invalid or damaged settings fall back safely to the default");

    std::wstring path = directory + L"\\capture-shortcut.ini";
    check(loadCaptureShortcut(directory + L"\\absent-shortcut.ini") == defaultShortcut,
          L"First launch with no preferences uses default capture shortcut");
    check(persistCaptureShortcut(path, custom) && loadCaptureShortcut(path) == custom,
          L"Custom shortcut persists to an isolated settings file and reloads");
    check(persistCaptureShortcut(path, defaultShortcut) &&
              loadCaptureShortcut(path) == defaultShortcut,
          L"Reset to default persists across sessions");
    check(!persistCaptureShortcut(L"", custom) &&
              !persistCaptureShortcut(directory + L"\\missing-settings-parent\\prefs.ini", custom),
          L"Missing or unwritable settings destinations report failure");

    CaptureShortcut current = defaultShortcut;
    bool registered = true, available = false, storageWorks = true;
    int activeId = 1, acquisitionAttempts = 0, persistenceAttempts = 0;
    std::vector<int> released;
    auto acquire = [&](int, CaptureShortcut) {
        acquisitionAttempts++;
        return available;
    };
    auto release = [&](int id) { released.push_back(id); };
    auto persist = [&](CaptureShortcut shortcut) {
        persistenceAttempts++;
        return storageWorks && persistCaptureShortcut(path, shortcut);
    };
    auto replace = [&](CaptureShortcut desired) {
        return replaceCaptureShortcut(current, registered, activeId, desired, acquire, release,
                                      persist);
    };
    check(replace(custom) == ShortcutSaveResult::Unavailable && current == defaultShortcut &&
              registered && activeId == 1 && released.empty() && persistenceAttempts == 0 &&
              loadCaptureShortcut(path) == defaultShortcut,
          L"Occupied combination preserves live shortcut and saved settings");
    available = true;
    storageWorks = false;
    check(replace(custom) == ShortcutSaveResult::PersistenceError && current == defaultShortcut &&
              registered && activeId == 1 && released.size() == 1 && released[0] == 2 &&
              loadCaptureShortcut(path) == defaultShortcut,
          L"Save failure releases only candidate and retains old registration and preference");
    storageWorks = true;
    released.clear();
    check(replace(custom) == ShortcutSaveResult::Saved && current == custom && registered &&
              activeId == 2 && released.size() == 1 && released[0] == 1 &&
              loadCaptureShortcut(path) == custom,
          L"Successful change activates candidate and releases old shortcut after persisting");
    int previousAttempts = acquisitionAttempts;
    released.clear();
    check(replace(custom) == ShortcutSaveResult::Saved && activeId == 2 && released.empty() &&
              acquisitionAttempts == previousAttempts,
          L"Saving unchanged active shortcut avoids duplicate registration");
    check(replace(defaultShortcut) == ShortcutSaveResult::Saved && activeId == 1 &&
              current == defaultShortcut && released.size() == 1 && released[0] == 2,
          L"Restoring default replaces registration without leaving stale shortcuts");
    registered = false;
    available = false;
    previousAttempts = acquisitionAttempts;
    check(replace(defaultShortcut) == ShortcutSaveResult::Unavailable && !registered &&
              acquisitionAttempts == previousAttempts + 1,
          L"Unregistered startup preference is retried even when its value is unchanged");
    available = true;
    check(replace(defaultShortcut) == ShortcutSaveResult::Saved && registered,
          L"Unavailable startup shortcut can recover from the preferences dialog");
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE && !persistCaptureShortcut(path, custom) &&
              loadCaptureShortcut(path) == defaultShortcut,
          L"Atomic settings replacement leaves previous file intact when destination is locked");
    if (locked != INVALID_HANDLE_VALUE)
        CloseHandle(locked);

    app.captureShortcut = custom;
    check(helpText().find(L"Ctrl + Alt + K: new screenshot") != std::wstring::npos &&
              helpText().find(L"{capture-shortcut}") == std::wstring::npos &&
              captureHint() == L"Ctrl + Alt + K to capture",
          L"Help and capture hint follow the selected shortcut");
    HWND dialog = CreateDialogParamW(app.instance, MAKEINTRESOURCEW(IDD_CAPTURE_SHORTCUT), app.main,
                                     ShortcutDialogProc, 0);
    check(dialog && app.shortcutDialog == dialog && shortcutDialogState.recorder == nullptr,
          L"Native shortcut dialog creates without registering hooks in tests");
    if (dialog) {
        auto controlText = [&](int id) {
            wchar_t buffer[256]{};
            GetDlgItemTextW(dialog, id, buffer, 256);
            return std::wstring(buffer);
        };
        check(controlText(IDC_SHORTCUT_INPUT) == L"Ctrl + Alt + K" &&
                  controlText(IDOK) == L"Save" && controlText(IDCANCEL) == L"Cancel",
              L"Dialog initially displays saved combination and English actions");
        SendMessageW(GetDlgItem(dialog, IDC_SHORTCUT_INPUT), WM_SETFOCUS, 0, 0);
        check(controlText(IDC_SHORTCUT_INPUT) == ui::kShortcutPrompt,
              L"Focusing shortcut field shows recording prompt");
        SendMessageW(dialog, RecordShortcutMessage, 'J', MOD_CONTROL | MOD_SHIFT);
        check(controlText(IDC_SHORTCUT_INPUT) == L"Ctrl + Shift + J" &&
                  IsWindowEnabled(GetDlgItem(dialog, IDOK)) && app.captureShortcut == custom,
              L"Recording a combination updates candidate without applying it");
        SendMessageW(dialog, RecordShortcutMessage, 'A', 0);
        check(!IsWindowEnabled(GetDlgItem(dialog, IDOK)) &&
                  controlText(IDC_SHORTCUT_HINT) == ui::kShortcutInvalid,
              L"Invalid combination gives inline feedback and disables Save");
        recordShortcutKey(dialog, VK_SHIFT, MOD_SHIFT);
        check(!shortcutDialogState.valid, L"Modifier press alone does not replace candidate");
        NMHDR reset{GetDlgItem(dialog, IDC_SHORTCUT_RESET), IDC_SHORTCUT_RESET, NM_CLICK};
        SendMessageW(dialog, WM_NOTIFY, IDC_SHORTCUT_RESET, reinterpret_cast<LPARAM>(&reset));
        check(shortcutDialogState.candidate == defaultShortcut &&
                  controlText(IDC_SHORTCUT_INPUT) == L"Ctrl + Shift + S" &&
                  IsWindowEnabled(GetDlgItem(dialog, IDOK)) && app.captureShortcut == custom,
              L"Reset to default remains a candidate until explicitly saved");
        captureHotkey(false);
        check(!app.capturePending && !app.overlay,
              L"Capture shortcut cannot start a screenshot while preferences are open");
        check(shortcutNavigationKey(VK_ESCAPE, MOD_CONTROL) && shortcutNavigationKey(VK_TAB, 0) &&
                  shortcutNavigationKey(VK_TAB, MOD_SHIFT) && shortcutNavigationKey(VK_RETURN, 0) &&
                  !shortcutNavigationKey(VK_TAB, MOD_CONTROL),
              L"Escape and Tab navigation remain accessible while recording modified Tab");
        // Render the native dialog and child controls, without photographing the desktop.
        RECT client{};
        GetWindowRect(dialog, &client);
        client = {0, 0, client.right - client.left, client.bottom - client.top};
        HDC screen = GetDC(nullptr);
        PaintBuffer preview;
        if (preview.ensure(screen, client.right, client.bottom)) {
            FillRect(preview.dc, &client, GetSysColorBrush(COLOR_3DFACE));
            SendMessageW(dialog, WM_PRINT, reinterpret_cast<WPARAM>(preview.dc),
                         PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
            Bitmap image(preview.bitmap, nullptr);
            check(encodePng(image, (directory + L"\\shortcut-settings.png").c_str()),
                  L"Native shortcut dialog preview renders to PNG");
        } else
            check(false, L"Native shortcut dialog preview buffer creation");
        ReleaseDC(nullptr, screen);
        DestroyWindow(dialog);
        check(app.shortcutDialog == nullptr && app.captureShortcut == custom &&
                  shortcutDialogState.recorder == nullptr,
              L"Closing preferences discards candidate and cleans up recorder lifetime");
    }
    app.captureShortcut = defaultShortcut;
}

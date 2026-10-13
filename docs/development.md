# Development

Captura targets Windows 10 / 11 x64 and uses C++17, Win32, and GDI+. The C++ runtime is linked statically. Running the app requires only Windows components.

## Build

Install Visual Studio 2022 Build Tools with **Desktop development with C++** and a Windows SDK. PowerShell 5.1 or later is sufficient; no developer shell is needed.

From the repository root:

```powershell
.\build.ps1
```

The portable executable is written to `dist\Captura.exe`. Choose a different destination with `-OutputDirectory`.

## Verify

```powershell
.\test.ps1
```

The integration suite creates hidden native windows and synthetic screenshots. It covers capture selection, annotation geometry, undo/redo, inline text, color and contrast, reversible crop and expansion, PNG/DIB exports, resource lifetime, and pixel comparisons between cached and complete rendering.

Tests write to `build\test-results`. They do not capture personal content, change the real clipboard, or save files in the user's Pictures folder. The normal executable excludes the test suite.

## Layout

- `src/main.cpp`: application model, geometry, rendering, input, export, and window lifetime.
- `src/ui_text.h`: English interface text and help.
- `src/app.rc`, `src/app.manifest`: version and Windows application resources.
- `tests/integration.inc`: native integration suite included only in test builds.
- `docs/assets`: synthetic screenshots used in the product README.
- `build.ps1`, `test.ps1`: build and verification entry points.

Format C++ changes with clang-format 19 and the checked-in `.clang-format`. Include order is preserved because Windows SDK headers depend on it.

## Release

Update the version in `app.rc` and `app.manifest`, run the tests, and build the normal executable. Package `Captura.exe`, the product README, and a SHA-256 checksum. Release binaries should remain portable and should not include test code or build outputs.

The app keeps editable content in memory during its session. A saved PNG contains the final visible frame, not an editable project.

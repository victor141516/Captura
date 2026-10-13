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

For an interactive preview of the shortcut dialog, run the test executable with `--shortcut-preview` followed by an absolute directory for isolated preferences. This mode creates no tray icon, global shortcut, keyboard hook, or singleton; it can run beside the real app without affecting the current screenshot.

## Layout

- `src/main.cpp`: application model, geometry, rendering, input, export, and window lifetime.
- `src/ui_text.h`: English interface text and help.
- `src/capture_shortcut.h`: shortcut validation, display, atomic preference storage, and registration replacement.
- `src/resource.h`: native dialog control identifiers.
- `src/app.rc`, `src/app.manifest`: version and Windows application resources.
- `tests/integration.inc`: native integration suite included only in test builds.
- `tests/shortcut.inc`: shortcut persistence, failure handling, and dialog tests with fake registrations and isolated settings.
- `tests/retained_desktop.inc`: selected frame, retained desktop, context expansion, undo/redo, and visible export tests.
- `tests/navigation.inc`: middle-button and wheel input, navigation caches, quality restoration, clipping, bounded zoom memory, and GDI resource tests. Writes `navigation-performance.txt` with timings for a retained 4K desktop and selected frame.
- `tests/navigation_bounds.inc`: actual scroll and drag boundary behavior for smaller, larger, cropped, and expanded frames, including reversal, zoom, and window resize.
- `docs/assets`: synthetic screenshots used in the product README.
- `build.ps1`, `test.ps1`: build and verification entry points.

Format C++ changes with clang-format 19 and the checked-in `.clang-format`. Include order is preserved because Windows SDK headers depend on it.

## Release

Update the version in `app.rc` and `app.manifest`, run the tests, and build the normal executable. Package `Captura.exe`, the product README, and a SHA-256 checksum. Release binaries should remain portable and should not include test code or build outputs.

The app keeps editable content in memory during its session. A saved PNG contains the final visible frame, not an editable project.

Area and window selection transfer the complete frozen desktop bitmap into the editor without cloning or cropping it. The initial selection is stored as the canvas frame and trim baseline in desktop-bitmap coordinates. The renderer clips to that frame, and expansion reveals pixels already in the source. Synthetic fill is used only outside the complete bitmap. PNG and clipboard export translate and clip the source and annotations to the current visible frame.

During panning, scaled image pixels are translated independently of the camera's integer origin. A fractional-origin or scale change rebuilds the raster; large zoom levels use a bounded viewport tile with overscan. Navigation also retains static toolbar pixels. Wheel zoom stretches a native top-down RGB copy of the immutable source, then a 140 ms idle timer restores the final-quality view. These display caches never affect PNG or clipboard export. Filtered PARGB cache rendering and direct HDC rendering can differ by up to two RGB levels due to rounding; navigation comparisons permit that only inside the visible image, with exact toolbar and framing pixels.

The capture shortcut is stored per user at `%LOCALAPPDATA%\Captura\settings.ini`, so it survives replacing the portable executable. Tests never access that file, register a real global shortcut, or install the keyboard recorder. The recorder is active only while the real shortcut dialog is open and intercepts combinations only while its input field is focused.

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
- `src/ocr.h`, `src/ocr.cpp`: cancellable background Windows OCR and measurement, with immutable bitmap snapshots and plain text/geometry results.
- `src/resource.h`: native dialog control identifiers.
- `src/app.rc`, `src/app.manifest`: version and Windows application resources.
- `tests/integration.inc`: native integration suite included only in test builds.
- `tests/shortcut.inc`: shortcut persistence, failure handling, and dialog tests with fake registrations and isolated settings.
- `tests/retained_desktop.inc`: selected frame, retained desktop, context expansion, undo/redo, and visible export tests.
- `tests/navigation.inc`: middle-button and wheel input, navigation caches, quality restoration, clipping, bounded zoom memory, and GDI resource tests. Writes `navigation-performance.txt` with timings for a retained 4K desktop and selected frame.
- `tests/navigation_bounds.inc`: actual scroll and drag boundary behavior for smaller, larger, cropped, and expanded frames, including reversal, zoom, and window resize.
- `tests/ocr.inc`: text geometry, Unicode clipboard hooks, cancellation, retained-desktop selection, and real Windows OCR on synthetic screenshots. Writes `ocr-editor.png` and `ocr-benchmark.txt`.
- `docs/assets`: synthetic screenshots used in the product README.
- `build.ps1`, `test.ps1`: build and verification entry points.

Format C++ changes with clang-format 19 and the checked-in `.clang-format`. Include order is preserved because Windows SDK headers depend on it.

## Release

Update the version in `app.rc` and `app.manifest`, run the tests, and build the normal executable. Package `Captura.exe`, the product README, and a SHA-256 checksum. Release binaries should remain portable and should not include test code or build outputs.

The app keeps editable content in memory during its session. A saved PNG contains the final visible frame, not an editable project.

## OCR preview

The local `experiment/ocr-text-selection` branch scans the original retained desktop, without annotations, once when `finishCapture` loads an image. The initial frame does not limit recognition. A dedicated MTA worker owns a private bitmap clone, so it never locks the bitmap used by the UI renderer. A newer capture cancels the previous job and replaces pending work; result IDs prevent stale completion messages from updating a newer capture. Shutdown joins the worker before GDI+ is released.

Windows OCR receives native-resolution blocks up to 2600 pixels per side, or the smaller system limit, with 128 pixels of context around seams. Words belong to the tile core containing their center. Row fragments are merged geometrically. This preview uses rectangular word bounds and top-to-bottom / left-to-right ordering; complex columns, rotated text, and scripts requiring other reading orders need further evaluation.

Select text draws overlays only in the current visible frame. Selection uses word centers to avoid copying neighbors when a rectangle barely touches them. OCR overlays are never exported. Text copy uses `CF_UNICODETEXT` and keeps the editor open so the experiment supports repeated selections; normal image exports retain their existing behavior.

Wall time uses QueryPerformanceCounter and includes snapshot preparation and any worker queue delay. Recognition time sums the async API waits. GetProcessTimes measures CPU for the **whole process**, not an isolated OCR thread; divide CPU time by elapsed time and logical processor count for average Task Manager-style CPU percentage. Working set is sampled during recognition, including the pre-job baseline; reported growth is relative to that baseline. These are diagnostic process metrics and can include concurrent rendering or previous recognition. CPU counters have finite granularity and RAM samples can miss short peaks. Screenshots, recognized text, and metrics are not automatically persisted or sent anywhere.

No OCR model is bundled or installed. The engine uses the user's Windows OCR languages and falls back to the first available language. Missing languages or API failures produce an inline unavailable/error state. The standalone unpackaged probe and integration tests work on the local Windows 11 host with `en-US`; this does not establish compatibility with every supported Windows build. See `examples/windows-ocr` for the minimal probe.

Area and window selection transfer the complete frozen desktop bitmap into the editor without cloning or cropping it. The initial selection is stored as the canvas frame and trim baseline in desktop-bitmap coordinates. The renderer clips to that frame, and expansion reveals pixels already in the source. Synthetic fill is used only outside the complete bitmap. PNG and clipboard export translate and clip the source and annotations to the current visible frame.

During panning, scaled image pixels are translated independently of the camera's integer origin. A fractional-origin or scale change rebuilds the raster; large zoom levels use a bounded viewport tile with overscan. Navigation also retains static toolbar pixels. Wheel zoom stretches a native top-down RGB copy of the immutable source, then a 140 ms idle timer restores the final-quality view. These display caches never affect PNG or clipboard export. Filtered PARGB cache rendering and direct HDC rendering can differ by up to two RGB levels due to rounding; navigation comparisons permit that only inside the visible image, with exact toolbar and framing pixels.

The capture shortcut is stored per user at `%LOCALAPPDATA%\Captura\settings.ini`, so it survives replacing the portable executable. Tests never access that file, register a real global shortcut, or install the keyboard recorder. The recorder is active only while the real shortcut dialog is open and intercepts combinations only while its input field is focused.

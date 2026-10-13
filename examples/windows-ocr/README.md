# Windows OCR API probe

A minimal native C++/WinRT experiment, separate from Captura. It generates `input.bmp` with two known text lines and passes its pixels to `Windows.Media.Ocr.OcrEngine.RecognizeAsync`.

Build and run with Visual Studio C++ Build Tools and a Windows SDK:

```powershell
.\build-and-run.ps1
```

The executable uses Windows APIs and a static C++ runtime. No MSIX package, package registration, extra OCR engine, language installation, or personal screen capture is performed.

Observed on this machine (Windows 11 Pro, build 26300):

- `GetCurrentPackageFullName` returned `APPMODEL_ERROR_NO_PACKAGE` (15700).
- Windows reported one available OCR language: `en-US`.
- Recognition returned both expected lines and the coordinates of all six words.
- The probe exited successfully with code 0. See `result.txt` for the output.

The elapsed time printed by this minimal probe uses `GetTickCount64`; a result of 0 ms means the call finished within that clock's resolution.

This confirms successful execution on this machine. Microsoft's documentation still describes desktop support in terms of package identity; this probe does not establish support on every Windows installation.

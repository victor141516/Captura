# WinKit integration

Captura is a portable native Windows x64 utility. Its C++ runtime is statically linked; users do not need a runtime installer. Windows system APIs provide graphics and OCR.

## Startup

```powershell
$env:WINKIT_MANAGED = '1'
.\Captura.exe --background
```

`--background` starts the tray functionality without a visible editor, console, or focus change. The singleton is session-local and is acquired before graphics initialization, windows, shortcuts, or tray icons. A second background invocation exits successfully and silently. A normal second invocation asks the existing process to open its editor.

`WINKIT_MANAGED=1` is read from the inherited environment, independently of command-line arguments. The tray menu identifies managed sessions. Captura has no updater, startup toggle, scheduled task, Run-key entry, or other independent Windows-startup registration in either mode. WinKit owns downloads, updates, startup, and termination. Captura does not require a `--stop` command. Windows releases its session mutex when the manager terminates its process, allowing a later restart.

Startup errors in background mode stay silent and return a nonzero exit code. A missing tray icon never falls back to a visible editor. A busy capture shortcut leaves the tray running so the user can choose another shortcut there.

## Releases and catalog

- Repository: [victor141516/Captura](https://github.com/victor141516/Captura).
- Resolve the latest stable release using GitHub's `repos/victor141516/Captura/releases/latest` endpoint; do not pin a tag or asset URL.
- Read that release's attached `winkit.json`. Schema 1 declares background support, a single instance, and a self-contained package.
- Select `packages[].architecture = "x64"`. Its `asset` names a ZIP attached to the same release; `sha256` is the actual ZIP hash, not the executable hash.
- Verify the ZIP before extraction. Its root contains `Captura.exe`; preserve that name when launching.
- The release also attaches `SHA256SUMS.txt` and the standalone executable for direct users.

WinKit must add Captura and this repository to its catalog. A compliant release does not change the manager's existing catalog automatically. No ARM64 package is declared until one is built and verified.

## Build and publish

Update the version resources, run `test.ps1`, then run the isolated process tests:

```powershell
.\tests\startup.ps1 -Executable .\build\test-bin\Captura.exe
.\release.ps1 -Version 1.20.0
```

Use the version of the release being built. `release.ps1` checks it against the compiled executable, produces `Captura-v<version>-win-x64-self-contained.zip`, verifies the executable is at the ZIP root, and generates `winkit.json` and `SHA256SUMS.txt` from the resulting bytes. Upload all three files to the same stable, immutable GitHub release, along with `package/Captura.exe` for direct download. Do not publish a test executable or a debug build.

The process tests use isolated mutex and window names, settings files, and fake tray/shortcut services. They run the production startup, duplicate-instance, and shutdown paths without registering personal shortcuts or opening capture windows. They verify both managed and unmanaged launches, foreground preservation, duplicate behavior, termination/restart, and tray failure.

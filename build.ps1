param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'),
    [switch]$WithTests
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio Build Tools with Desktop development with C++.'
}

$visualStudio = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) { throw 'MSVC C++ build tools were not found.' }

$compilerTools = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$windowsKits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $windowsKits 'Lib') -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\x64\kernel32.lib') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'A Windows SDK with x64 libraries is required.' }

$buildKind = if ($WithTests) { 'tests' } else { 'release' }
$buildDirectory = Join-Path $PSScriptRoot "build\$buildKind"
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
New-Item -ItemType Directory -Path $buildDirectory, $output -Force | Out-Null

$previousInclude = $env:INCLUDE
$previousLib = $env:LIB
Push-Location (Join-Path $PSScriptRoot 'src')
try {
    $env:INCLUDE = @(
        (Join-Path $compilerTools.FullName 'include'),
        (Join-Path $windowsKits "Include\$($sdk.Name)\ucrt"),
        (Join-Path $windowsKits "Include\$($sdk.Name)\shared"),
        (Join-Path $windowsKits "Include\$($sdk.Name)\um"),
        (Join-Path $windowsKits "Include\$($sdk.Name)\cppwinrt")
    ) -join ';'
    $env:LIB = @(
        (Join-Path $compilerTools.FullName 'lib\x64'),
        (Join-Path $windowsKits "Lib\$($sdk.Name)\ucrt\x64"),
        (Join-Path $windowsKits "Lib\$($sdk.Name)\um\x64")
    ) -join ';'

    $resourceCompiler = Join-Path $windowsKits "bin\$($sdk.Name)\x64\rc.exe"
    $compiler = Join-Path $compilerTools.FullName 'bin\Hostx64\x64\cl.exe'
    & $resourceCompiler /nologo "/fo$buildDirectory\app.res" app.rc
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }

    # Vendored quirc (C, ISC) keeps its upstream source; it compiles at /W3 so the
    # project's /W4 applies only to Captura's own code.
    $quircSources = @('quirc\quirc.c', 'quirc\decode.c', 'quirc\identify.c', 'quirc\version_db.c')
    $quircObjects = $quircSources | ForEach-Object {
        Join-Path $buildDirectory ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    & $compiler /nologo /c /W3 /O1 /Os /GL /Gy /Gw /MT /DNDEBUG `
        "/Fo$buildDirectory\" @quircSources
    if ($LASTEXITCODE) { throw 'quirc compilation failed.' }

    $options = @(
        '/nologo', '/std:c++20', '/utf-8', '/W4', '/O1', '/Os', '/GL', '/Gy', '/Gw',
        '/MT', '/EHsc', '/DNDEBUG', '/DUNICODE', '/D_UNICODE',
        '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/DGDIPVER=0x0110'
    )
    if ($WithTests) { $options += '/DCAPTURA_TEST' }
    $options += @(
        'main.cpp', 'ocr.cpp', 'redaction.cpp', 'qr.cpp'
    ) + $quircObjects + @(
        "/Fo$buildDirectory\", "/Fe$output\Captura.exe",
        '/link', "$buildDirectory\app.res", '/SUBSYSTEM:WINDOWS', '/MACHINE:X64',
        '/LTCG', '/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO', '/DYNAMICBASE',
        '/NXCOMPAT', '/HIGHENTROPYVA', '/MANIFEST:NO',
        'user32.lib', 'gdi32.lib', 'gdiplus.lib', 'comdlg32.lib', 'shell32.lib',
        'dwmapi.lib', 'ole32.lib', 'comctl32.lib', 'uuid.lib', 'windowsapp.lib'
    )
    & $compiler @options
    if ($LASTEXITCODE) { throw 'Compilation failed.' }
    Get-Item -LiteralPath (Join-Path $output 'Captura.exe') | Select-Object Name, Length
} finally {
    Pop-Location
    $env:INCLUDE = $previousInclude
    $env:LIB = $previousLib
}

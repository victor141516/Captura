$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$toolsDirectory = Get-ChildItem (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem (Join-Path $kits 'Lib') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'um\x64\windowsapp.lib') } |
    Sort-Object Name -Descending | Select-Object -First 1
$savedInclude = $env:INCLUDE
$savedLib = $env:LIB
Push-Location $PSScriptRoot
try {
    $env:INCLUDE = @(
        (Join-Path $toolsDirectory.FullName 'include'),
        (Join-Path $kits "Include\$($sdk.Name)\ucrt"),
        (Join-Path $kits "Include\$($sdk.Name)\shared"),
        (Join-Path $kits "Include\$($sdk.Name)\um"),
        (Join-Path $kits "Include\$($sdk.Name)\cppwinrt")
    ) -join ';'
    $env:LIB = @(
        (Join-Path $toolsDirectory.FullName 'lib\x64'),
        (Join-Path $kits "Lib\$($sdk.Name)\ucrt\x64"),
        (Join-Path $kits "Lib\$($sdk.Name)\um\x64")
    ) -join ';'
    & (Join-Path $toolsDirectory.FullName 'bin\Hostx64\x64\cl.exe') /nologo /std:c++20 /utf-8 /W4 /O1 /Os /GL /MT /EHsc /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX ocr-test.cpp /Fe:ocr-test.exe /link /LTCG /OPT:REF /OPT:ICF windowsapp.lib user32.lib gdi32.lib ole32.lib
    if ($LASTEXITCODE) { throw 'OCR probe compilation failed.' }
    & .\ocr-test.exe | Tee-Object -FilePath result.txt
    $result = $LASTEXITCODE
    Get-Item .\ocr-test.exe | Select-Object Name,Length
    if ($result) { throw "OCR probe failed with exit code $result. See result.txt." }
} finally {
    $env:INCLUDE = $savedInclude
    $env:LIB = $savedLib
    Pop-Location
}

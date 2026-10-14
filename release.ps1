param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist\release')
)
$ErrorActionPreference = 'Stop'
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$package = Join-Path $output 'package'
New-Item -ItemType Directory -Path $output, $package -Force | Out-Null
& (Join-Path $PSScriptRoot 'build.ps1') -OutputDirectory $package
$executable = Join-Path $package 'Captura.exe'
if ((Get-Item -LiteralPath $executable).VersionInfo.FileVersion -ne $Version) {
    throw 'Release version does not match the executable version.'
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $package
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LICENSE') -Destination $package
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'docs') -Destination $package -Recurse -Force
$exeHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Value "$exeHash  Captura.exe" -Encoding ascii
$asset = "Captura-v$Version-win-x64-self-contained.zip"
$archive = Join-Path $output $asset
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -Force
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    if (@($zip.Entries | Where-Object FullName -eq 'Captura.exe').Count -ne 1) {
        throw 'The package must contain Captura.exe at its root.'
    }
} finally { $zip.Dispose() }
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    schema = 1; background = $true; singleInstance = $true; selfContained = $true
    packages = @([ordered]@{
        architecture = 'x64'; asset = $asset; executable = 'Captura.exe'; sha256 = $zipHash
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'winkit.json') -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') `
    -Value @("$zipHash  $asset", "$exeHash  Captura.exe") -Encoding ascii
Get-Item -LiteralPath $archive, (Join-Path $output 'winkit.json'), (Join-Path $output 'SHA256SUMS.txt')

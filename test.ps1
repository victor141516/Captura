param([string]$ResultsDirectory = (Join-Path $PSScriptRoot 'build\test-results'))

$ErrorActionPreference = 'Stop'
$testDirectory = Join-Path $PSScriptRoot 'build\test-bin'
& (Join-Path $PSScriptRoot 'build.ps1') -WithTests -OutputDirectory $testDirectory
$results = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ResultsDirectory)
New-Item -ItemType Directory -Path $results -Force | Out-Null
$process = Start-Process -FilePath (Join-Path $testDirectory 'Captura.exe') `
    -ArgumentList ('--self-test "' + $results + '"') -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath (Join-Path $results 'tests.txt')
if ($process.ExitCode -ne 0) { throw "Integration tests failed with exit code $($process.ExitCode)." }

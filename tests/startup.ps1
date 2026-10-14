param([Parameter(Mandatory)][string]$Executable,
      [string]$ResultsDirectory = (Join-Path $PSScriptRoot '..\build\startup-results'))

$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $Executable).Path
$results = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ResultsDirectory)
New-Item -ItemType Directory -Path $results -Force | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CapturaStartupProbe {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
$checks = [Collections.Generic.List[string]]::new()
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()
function Assert-Contract([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $checks.Add("PASS $Message")
}
function Start-Probe([string]$Directory, [bool]$Managed, [bool]$Background = $true,
                     [bool]$NoTray = $false) {
    $info = [Diagnostics.ProcessStartInfo]::new($binary)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.ArgumentList.Add('--lifecycle-test')
    $info.ArgumentList.Add($Directory)
    if ($Background) { $info.ArgumentList.Add('--background') }
    if ($NoTray) { $info.ArgumentList.Add('--no-tray') }
    $info.Environment['WINKIT_MANAGED'] = $(if ($Managed) { '1' } else { '0' })
    $process = [Diagnostics.Process]::Start($info)
    $processes.Add($process)
    return $process
}
function Read-State([string]$Directory) {
    $file = Join-Path $Directory 'state.txt'
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if (Test-Path -LiteralPath $file) {
            try {
                $state = ConvertFrom-StringData (Get-Content -LiteralPath $file -Raw)
                if ($state.ContainsKey('activations')) { return $state }
            } catch { }
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Startup probe did not become ready: $Directory"
}
try {
    foreach ($managed in @($false, $true)) {
        $directory = Join-Path $results ([Guid]::NewGuid().ToString())
        New-Item -ItemType Directory -Path $directory | Out-Null
        $foreground = [CapturaStartupProbe]::GetForegroundWindow()
        $primary = Start-Probe $directory $managed
        $state = Read-State $directory
        Assert-Contract ($state.background -eq '1' -and $state.visible -eq '0' -and
                         $state.tray -eq '1' -and $state.managed -eq [int]$managed) `
            "Background launch stays hidden with tray functionality (managed=$managed)"
        Assert-Contract ($state.hotkey -eq '0') 'Lifecycle probes never register personal global shortcuts'
        $duplicate = Start-Probe $directory $managed
        Assert-Contract ($duplicate.WaitForExit(10000) -and $duplicate.ExitCode -eq 0) `
            'Second background invocation exits successfully'
        $state = Read-State $directory
        Assert-Contract (!$primary.HasExited -and $state.activations -eq '0') `
            'Duplicate background launch leaves primary unchanged'
        Assert-Contract ([CapturaStartupProbe]::GetForegroundWindow() -eq $foreground) `
            'First and duplicate background launches preserve foreground focus'
        $interactive = Start-Probe $directory $managed $false
        Assert-Contract ($interactive.WaitForExit(10000) -and $interactive.ExitCode -eq 0) `
            'Interactive duplicate exits after notifying existing instance'
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $state = Read-State $directory
            if ($state.activations -eq '1') { break }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        Assert-Contract ($state.activations -eq '1') 'Interactive duplicate activates primary exactly once'
        # Simulate manager termination by PID of the process this test created.
        $primary.Kill()
        $primary.WaitForExit()
        $restarted = Start-Probe $directory $managed
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $state = Read-State $directory
            if ($state.activations -eq '0') { break }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        Assert-Contract (!$restarted.HasExited -and $state.activations -eq '0') `
            'Manager termination releases singleton and allows re-enable'
        $restarted.Kill()
        $restarted.WaitForExit()
    }
    $failure = Join-Path $results ([Guid]::NewGuid().ToString())
    New-Item -ItemType Directory -Path $failure | Out-Null
    $failed = Start-Probe $failure $true $true $true
    Assert-Contract ($failed.WaitForExit(10000) -and $failed.ExitCode -eq 1) `
        'Missing tray makes background startup fail promptly with a nonzero exit code'
    Assert-Contract ((Read-State $failure).visible -eq '0') `
        'Tray failure never falls back to a visible editor'
    $checks.Add("$($checks.Count) startup contract checks, 0 failures")
    $checks | Set-Content -LiteralPath (Join-Path $results 'startup-tests.txt') -Encoding utf8
    $checks
} finally {
    foreach ($process in $processes) {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

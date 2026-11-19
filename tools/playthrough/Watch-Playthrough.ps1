# User-run only. Observes only the process it launches; never kills or suspends it.
[CmdletBinding()]
param([ValidateSet('off','sampled','full')][string]$DetailMode='sampled',[switch]$Geometry)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Resolve-KitPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $Path))
}

# .NET Framework ProcessStartInfo/Start-Process accept a single Windows command
# line. Escape trailing backslashes and embedded quotes rather than using cmd.exe.
function Quote-Argument([string]$Value) {
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

$launcherLock = $null
$csv = $null
$process = $null
$run = $null
$result = 2
$watcherErrors = 0
$census = $null
$censusAttempts = 0
$censusFailures = 0
$nextCensus = 0.0
try {
    $exe = Join-Path $PSScriptRoot 'deadspace.exe'
    $censusExe = Join-Path $PSScriptRoot 'handle-census.exe'
    $config = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Playthrough-Paths.json') -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($config.GameDir)) { throw 'Set GameDir in Playthrough-Paths.json.' }
    $gameDir = Resolve-KitPath $config.GameDir
    if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath $censusExe -PathType Leaf) -or
        !(Test-Path -LiteralPath (Join-Path $gameDir 'assets') -PathType Container) -or
        !(Test-Path -LiteralPath (Join-Path $gameDir 'libDeadSpace.so') -PathType Leaf)) {
        throw 'Missing test EXE or matching game data. Read START-HERE.txt and set GameDir. No game started.'
    }
    $launcherLock = [IO.File]::Open((Join-Path $PSScriptRoot '.playthrough.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $profile = Join-Path $PSScriptRoot 'userdata'
    if ((Test-Path -LiteralPath $profile) -and
        ((Get-Item -LiteralPath $profile).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'userdata must be a real local test folder, not a link to an installed profile.'
    }
    [IO.Directory]::CreateDirectory($profile) | Out-Null
    $name = 'playthrough-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $run = Join-Path (Join-Path $PSScriptRoot 'reports') $name
    [IO.Directory]::CreateDirectory($run) | Out-Null
    $arguments = @('--game-dir', $gameDir, '--data', $profile,
        '--width', '1280', '--height', '720', '--fps', '60',
        '--native-prep', 'optimized', '--perf-interval', '10',
        '--crash-dir', $run, '--log', (Join-Path $run 'deadspace.log'))
    $arguments += @('--perf-detail', $DetailMode)
    if ($Geometry) { $arguments += '--perf-geometry' }
    $commandLine = ($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $metadata = [ordered]@{
        version = 2; start_utc = [DateTime]::UtcNow.ToString('o'); exe = $exe
        exe_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        arguments = $arguments; process_id = $null; exit_code = $null; exit_hex = $null
        os_version = [Environment]::OSVersion.VersionString
        logical_processors = [Environment]::ProcessorCount
        cpu = $env:PROCESSOR_IDENTIFIER; watcher_interval_seconds = 5
        watcher_errors = 0; state = 'prepared'
        handle_census_interval_seconds = 30; handle_census_attempts = 0; handle_census_failures = 0
        handle_helper_sha256 = (Get-FileHash -LiteralPath $censusExe -Algorithm SHA256).Hash
    }
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'session.json') -Encoding UTF8
    Write-Host "LOW-END ATTRIBUTION DIAGNOSTICS: detail=$DetailMode geometry=$Geometry; persistent test profile."
    Write-Host "Profile: $profile"
    Write-Host "Reports: $run"
    Write-Host 'Leave this window open. Choose graphics/renderer in-game. Quit normally yourself.'
    Write-Host 'Press F3 when you notice stutter/audio lag. Show FPS also displays recent worst frame time.'
    $csv = New-Object IO.StreamWriter((Join-Path $run 'process-memory.csv'), $false, (New-Object Text.UTF8Encoding($false)))
    $csv.AutoFlush = $true
    $csv.WriteLine('utc,elapsed_seconds,pid,working_set_bytes,private_bytes,peak_working_set_bytes,virtual_bytes,handles,threads,cpu_seconds')
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -ArgumentList $commandLine -WorkingDirectory $run -PassThru `
        -RedirectStandardOutput (Join-Path $run 'console.log') -RedirectStandardError (Join-Path $run 'bootstrap-stderr.log')
    # Retain the process handle even if the child exits before the first sample.
    $handle = $process.Handle
    $metadata.process_id = $process.Id
    $created = $process.StartTime.ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
    $metadata.state = 'running'
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'session.json') -Encoding UTF8
    do {
        try {
            $process.Refresh()
            if (!$process.HasExited) {
                if ($null -ne $census -and $census.HasExited) {
                    $census.WaitForExit()
                    if ($census.ExitCode -ne 0) { $censusFailures++ }
                    $census.Dispose(); $census = $null
                }
                if ($null -eq $census -and $clock.Elapsed.TotalSeconds -ge $nextCensus) {
                    $nextCensus = $clock.Elapsed.TotalSeconds + 30
                    $censusAttempts++
                    try {
                        $censusArgs = @('--pid', $process.Id.ToString(), '--started', $created,
                            '--out', (Join-Path $run 'handle-types.csv'))
                        $censusLine = ($censusArgs | ForEach-Object { Quote-Argument $_ }) -join ' '
                        $census = Start-Process -FilePath $censusExe -ArgumentList $censusLine -WorkingDirectory $run -NoNewWindow -PassThru
                        $censusHandle = $census.Handle
                    } catch {
                        $censusFailures++
                        Add-Content -LiteralPath (Join-Path $run 'handle-census-errors.log') -Value $_.ToString()
                    }
                }
                $row = @([DateTime]::UtcNow.ToString('o'),
                    $clock.Elapsed.TotalSeconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture),
                    $process.Id, $process.WorkingSet64, $process.PrivateMemorySize64, $process.PeakWorkingSet64,
                    $process.VirtualMemorySize64, $process.HandleCount, $process.Threads.Count,
                    $process.TotalProcessorTime.TotalSeconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture))
                $csv.WriteLine(($row -join ','))
            }
        } catch {
            $watcherErrors++
            Add-Content -LiteralPath (Join-Path $run 'watcher-errors.log') -Value $_.ToString()
            # A failed observation must not terminate gameplay. Keep waiting for
            # this exact child, without enumerating or touching other processes.
        }
    } while (!$process.WaitForExit(5000))
    $process.WaitForExit()
    $result = $process.ExitCode
    $metadata.state = 'exited'
    $metadata.end_utc = [DateTime]::UtcNow.ToString('o')
    $metadata.exit_code = $result
    $metadata.exit_hex = '0x{0:X8}' -f [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$result), 0)
    $metadata.watcher_errors = $watcherErrors
    if ($null -ne $census) {
        if ($census.WaitForExit(2000)) {
            if ($census.ExitCode -ne 0) { $censusFailures++ }
        } else { $metadata.handle_census_unfinished = $true }
    }
    $metadata.handle_census_attempts = $censusAttempts
    $metadata.handle_census_failures = $censusFailures
    $metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $run 'session.json') -Encoding UTF8
    Write-Host "Game exited: $result ($($metadata.exit_hex)). Keep the complete report folder private."
    Write-Host 'If it froze/crashed, note the chapter, location, renderer and last action.'
} catch {
    $result = 2
    Write-Host "Launcher/watcher error: $($_.Exception.Message)"
    if ($null -ne $process) {
        Write-Host 'The game has NOT been stopped. Close it normally yourself; watcher coverage may be incomplete.'
    }
    if ($null -ne $run) {
        try { Add-Content -LiteralPath (Join-Path $run 'watcher-errors.log') -Value $_.ToString() } catch {}
    }
} finally {
    if ($null -ne $csv) { $csv.Dispose() }
    if ($null -ne $process) { $process.Dispose() }
    if ($null -ne $census) { $census.Dispose() }
    if ($null -ne $launcherLock) { $launcherLock.Dispose() }
}
exit $result

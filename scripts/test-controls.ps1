$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path ./dist/sakura-engine.exe).Path
foreach ($stopPaused in @($false, $true)) {
    $file = Join-Path $env:RUNNER_TEMP "sakura-controls-$stopPaused.mp4"
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($arg in @('record', $file, '0', '0', '320', '180', '30', '1500000', 'none', 'false')) {
        $start.ArgumentList.Add($arg)
    }
    $p = [Diagnostics.Process]::new()
    $p.StartInfo = $start
    try {
        [void]$p.Start()
        $stderr = $p.StandardError.ReadToEndAsync()
        function Expect-Line($pattern) {
            $task = $p.StandardOutput.ReadLineAsync()
            if (-not $task.Wait(15000)) { throw "Timed out awaiting $pattern" }
            $line = $task.Result
            Write-Host "engine: $line"
            if ($null -eq $line -or $line -notmatch $pattern) { throw "Expected $pattern; got $line" }
        }
        function Send-Control($command) {
            $p.StandardInput.WriteLine($command)
            $p.StandardInput.Flush()
        }
        Expect-Line '^AUDIO system off$'
        Expect-Line '^AUDIO mic off$'
        Expect-Line '^READY$'
        Start-Sleep -Milliseconds 500
        Send-Control 'pause'
        Expect-Line '^PAUSED$'
        # Absent endpoints must report failure, not terminate the recording.
        foreach ($source in @('mic', 'system')) {
            Send-Control "audio $source on"
            Expect-Line "^(AUDIO $source on|AUDIO_ERROR $source .+)$"
            Send-Control "audio $source off"
            Expect-Line "^AUDIO $source off$"
        }
        if (-not $stopPaused) {
            Send-Control 'resume'
            Expect-Line '^RESUMED$'
            Start-Sleep -Milliseconds 600
        }
        Send-Control 'stop'
        Expect-Line '^SAVED$'
        if (-not $p.WaitForExit(15000)) { throw 'Engine did not exit' }
        if ($p.ExitCode -ne 0) { throw $stderr.Result }
        if ((Get-Item $file).Length -lt 1024) { throw 'Invalid recording output' }
        Write-Host "CONTROL_TEST_OK stopPaused=$stopPaused"
    } finally {
        if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit() }
        $p.Dispose()
    }
}

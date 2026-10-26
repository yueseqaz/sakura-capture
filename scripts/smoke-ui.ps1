$ErrorActionPreference = 'Stop'
$exe = Join-Path (Get-Location) 'dist/SakuraCapture.exe'
$app = Start-Process -FilePath $exe -PassThru
try {
  Start-Sleep -Seconds 5
  $app.Refresh()
  if ($app.HasExited) { throw "Native GUI exited unexpectedly: $($app.ExitCode)" }
  if (!$app.MainWindowHandle) { throw 'Native GUI did not create a visible window' }
  $before = $app.TotalProcessorTime.TotalSeconds
  Start-Sleep -Seconds 2
  $app.Refresh()
  $cpu = ($app.TotalProcessorTime.TotalSeconds - $before) / 2 * 100
  Write-Output ('GUI_SMOKE_OK working_set={0:N2} MB private_memory={1:N2} MB idle_cpu_one_core={2:N2}%' -f ($app.WorkingSet64/1MB), ($app.PrivateMemorySize64/1MB), $cpu)
  $second = Start-Process -FilePath $exe -PassThru
  if (!$second.WaitForExit(8000)) {
    Stop-Process -Id $second.Id -Force -ErrorAction SilentlyContinue
    throw 'Second instance did not hand over to the first'
  }
  if ($second.ExitCode -ne 0) { throw "Second instance failed: $($second.ExitCode)" }
  Write-Output 'SINGLE_INSTANCE_OK'
} finally {
  # No recording is active in this smoke test; the normal close button hides to tray.
  Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
}

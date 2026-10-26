$ErrorActionPreference = 'Stop'
$exe = Join-Path (Get-Location) 'dist/SakuraCapture.exe'
$app = Start-Process -FilePath $exe -PassThru
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SakuraSmokeWindows {
  private delegate bool EnumProc(IntPtr w, IntPtr p);
  [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr w);
  [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr w, out uint p);
  public static bool HasVisibleWindow(int pid) {
    bool found = false;
    EnumWindows((w, _) => { uint p; GetWindowThreadProcessId(w, out p); if(p == pid && IsWindowVisible(w)) found = true; return !found; }, IntPtr.Zero);
    return found;
  }
}
'@
try {
  Start-Sleep -Seconds 5
  $app.Refresh()
  if ($app.HasExited) { throw "Native GUI exited unexpectedly: $($app.ExitCode)" }
  if (![SakuraSmokeWindows]::HasVisibleWindow($app.Id)) { throw 'Native GUI did not create a visible floating toolbar' }
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

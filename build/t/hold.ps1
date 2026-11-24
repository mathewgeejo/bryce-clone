param([string]$proc = "Brycescape", [int]$x, [int]$y, [int]$ms = 1500, [int]$hover = 0)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class H {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
"@
[void][H]::SetProcessDPIAware()
$h = (Get-Process $proc | Select-Object -First 1).MainWindowHandle
$r = New-Object H+RECT; [void][H]::GetWindowRect($h, [ref]$r)
$o = New-Object H+POINT; [void][H]::ClientToScreen($h, [ref]$o)
$cx = $x - ($o.X - $r.L); $cy = $y - ($o.Y - $r.T)
$lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xffff))
[void][H]::PostMessage($h, 0x200, [IntPtr]0, $lp)
if ($hover) { exit }
[void][H]::PostMessage($h, 0x201, [IntPtr]1, $lp)
Start-Sleep -Milliseconds $ms
[void][H]::PostMessage($h, 0x202, [IntPtr]0, $lp)

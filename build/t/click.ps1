param([string]$proc = "ui", [Parameter(ValueFromRemainingArguments = $true)][int[]]$pts)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class C {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
"@
[void][C]::SetProcessDPIAware()
$h = (Get-Process $proc | Select-Object -First 1).MainWindowHandle
$r = New-Object C+RECT; [void][C]::GetWindowRect($h, [ref]$r)
$o = New-Object C+POINT; [void][C]::ClientToScreen($h, [ref]$o)
for ($i = 0; $i -lt $pts.Count; $i += 2) {
    $x = $pts[$i] - ($o.X - $r.L); $y = $pts[$i + 1] - ($o.Y - $r.T)
    $lp = [IntPtr](($y -shl 16) -bor ($x -band 0xffff))
    [void][C]::SendMessage($h, 0x200, [IntPtr]0, $lp)
    [void][C]::SendMessage($h, 0x201, [IntPtr]1, $lp)
    [void][C]::SendMessage($h, 0x202, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 400
}

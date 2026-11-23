param([string]$out, [string]$title = "", [string]$proc = "Canyon3D")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public struct RECT { public int L, T, R, B; }
}
"@
[void][W]::SetProcessDPIAware()
$p = Get-Process $proc | Select-Object -First 1
$hwnds = New-Object System.Collections.ArrayList
[W]::EnumWindows({ param($h, $l) $id = 0; [void][W]::GetWindowThreadProcessId($h, [ref]$id)
    if ($id -eq $p.Id -and [W]::IsWindowVisible($h)) { $sb = New-Object System.Text.StringBuilder 256; [void][W]::GetWindowText($h, $sb, 256); [void]$hwnds.Add(@($h, $sb.ToString())) }; $true }, [IntPtr]::Zero) | Out-Null
foreach ($e in $hwnds) { Write-Output ("window: " + $e[1]) }
$pick = $hwnds | Where-Object { $title -eq "" -or $_[1] -like "*$title*" } | Select-Object -First 1
$h = $pick[0]
$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc(); [void][W]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved $($pick[1])"

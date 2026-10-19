# Capture only the PCSX2 game window to a PNG. Usage: shot_pcsx2.ps1 out.png
# Uses PrintWindow, which asks the window itself to render into the bitmap: no focus change, and nothing else
# on the desktop (other windows, private content) can end up in the picture, even if they cover PCSX2.
# If the result is blank, it is discarded rather than saved.
param([string]$Out, [int]$ProcId = 0)   # ProcId: pick one window when several PCSX2 instances run
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
$p = Get-Process pcsx2-qt -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 -and ($ProcId -eq 0 -or $_.Id -eq $ProcId) } | Select-Object -First 1
$h = $p.MainWindowHandle
if ([W]::IsIconic($h)) { Write-Output "SKIPPED: PCSX2 is minimized"; exit 2 }
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [W]::PrintWindow($h, $hdc, 2)   # 2 = PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
# reject an all-black / empty capture
$small = New-Object System.Drawing.Bitmap $bmp, 32, 32
$sum = 0; foreach ($x in 0..31) { foreach ($y in 0..31) { $c = $small.GetPixel($x, $y); $sum += $c.R + $c.G + $c.B } }
if (-not $ok -or $sum -lt 2000) { Write-Output "SKIPPED: PrintWindow returned no usable image (ok=$ok)"; exit 3 }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"{0}x{1} title=[{2}]" -f $w, $ht, $p.MainWindowTitle

# Looks at kvit-notes' tray icon on the Windows desktop while
# `kvit-notes.exe --tray-check 45s <vault>` runs, which prints what the tray
# reports. It sends nothing to any window but the app's own: the icon's
# messages are posted to the app's hidden tray window (class KvitTrayWindow)
# as Explorer posts them, and keys to the app's own popup menu.
#
#   -Registry      what Windows records about the icon (NotifyIconSettings)
#   -Rect          where the shell has the icon (Shell_NotifyIconGetRect)
#   -OpenMenu      post the icon's right-click message, which opens the menu
#   -Down N        choose the menu's Nth line: Down N times, then Enter
#   -Click         post the icon's click message
#   -Shot FILE     save the bottom-right Width x Height pixels of the screen
param([switch]$Registry, [switch]$Rect, [switch]$OpenMenu, [int]$Down = -1, [switch]$Click,
      [string]$Shot = "", [int]$Width = 1000, [int]$Height = 700)
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [StructLayout(LayoutKind.Sequential)] public struct ID { public uint cbSize; public IntPtr hWnd; public uint uID; public Guid guidItem; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("shell32.dll")] public static extern int Shell_NotifyIconGetRect(ref ID id, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
}
"@
[W]::SetProcessDPIAware() | Out-Null
$sw = [W]::GetSystemMetrics(0); $sh = [W]::GetSystemMetrics(1)
# PowerShell passes $null to a string parameter as "", which FindWindow
# would take as an empty title.
$trayWindow = [W]::FindWindow("KvitTrayWindow", [NullString]::Value)
$wmTrayIcon = 0x8001   # WM_APP + 1, the icon's callback message
$iconID = 1

if ($Registry) {
  Get-ChildItem "HKCU:\Control Panel\NotifyIconSettings" -ErrorAction SilentlyContinue | ForEach-Object {
    $p = Get-ItemProperty $_.PSPath
    if ($p.ExecutablePath -like "*kvit-notes*") {
      "registry: {0} IsPromoted={1} InitialTooltip={2}" -f $p.ExecutablePath, $p.IsPromoted, $p.InitialTooltip
    }
  }
}
if ($Rect) {
  $id = New-Object W+ID
  $id.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][W+ID])
  $id.hWnd = $trayWindow
  $id.uID = $iconID
  $r = New-Object W+RECT
  $hr = [W]::Shell_NotifyIconGetRect([ref]$id, [ref]$r)
  "icon: Shell_NotifyIconGetRect 0x{0:X8}, {1},{2} to {3},{4} on a {5}x{6} screen" -f $hr, $r.L, $r.T, $r.R, $r.B, $sw, $sh
}
if ($OpenMenu) {
  if ($trayWindow -eq [IntPtr]::Zero) { "no KvitTrayWindow"; exit 1 }
  # NOTIFYICON_VERSION_4: the event and the icon's number in lParam, the
  # point in wParam.
  $x = $sw - 260; $y = $sh - 80
  [W]::PostMessage($trayWindow, $wmTrayIcon, [IntPtr](($y -shl 16) -bor $x), [IntPtr](($iconID -shl 16) -bor 0x7B)) | Out-Null
  "menu: opened at $x,$y"
  Start-Sleep -Milliseconds 800
}
if ($Shot -ne "") {
  $bmp = New-Object System.Drawing.Bitmap $Width, $Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($sw - $Width, $sh - $Height, 0, 0, $bmp.Size)
  $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
  "saved $Shot"
}
if ($Down -ge 0) {
  # UI Automation finds the popup menu but not its lines, since it was opened
  # by a thread that is not in front, so its keys are posted to it instead.
  $pid0 = (Get-Process kvit-notes | Select-Object -First 1).Id
  $popup = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, "#32768")
  $menu = @([System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $popup)) |
    Where-Object { $_.Current.ProcessId -eq $pid0 } | Select-Object -First 1
  if ($menu -eq $null) { "no popup menu of kvit-notes"; exit 1 }
  $h = [IntPtr]$menu.Current.NativeWindowHandle
  for ($i = 0; $i -lt $Down; $i++) {
    [W]::PostMessage($h, 0x0100, [IntPtr]0x28, [IntPtr]0) | Out-Null   # WM_KEYDOWN, VK_DOWN
    Start-Sleep -Milliseconds 150
  }
  [W]::PostMessage($h, 0x0100, [IntPtr]0x0D, [IntPtr]0) | Out-Null     # VK_RETURN
  "menu: line $Down chosen"
}
if ($Click) {
  [W]::PostMessage($trayWindow, $wmTrayIcon, [IntPtr]0, [IntPtr](($iconID -shl 16) -bor 0x400)) | Out-Null   # NIN_SELECT
  "icon: clicked"
}

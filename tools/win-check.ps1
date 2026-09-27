# Reads what Windows' own screen-reader interface, UI Automation, reports
# about a running kvit-notes --check window, and saves a picture of that
# window. build.sh --win-check runs it while the check holds its window open.
#
#   powershell.exe -File win-check.ps1 -Title "Kvit Notes check" -Shot out.png
param([string]$Title = "Kvit Notes check", [string]$Shot = "", [string]$Method = "print", [switch]$Prefix)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int size);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$A = [System.Windows.Automation.AutomationElement]
$byName = New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $Title)
$win = $null
for ($i = 0; $i -lt 50 -and $win -eq $null; $i++) {
    if ($Prefix) {
        foreach ($c in $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
            if ($c.Current.Name.StartsWith($Title)) { $win = $c }
        }
    } else {
        $win = $A::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $byName)
    }
    if ($win -eq $null) { Start-Sleep -Milliseconds 200 }
}
if ($win -eq $null) { "no window titled '$Title'"; exit 1 }
$hwnd = [IntPtr]$win.Current.NativeWindowHandle
"window: '$($win.Current.Name)', class $($win.Current.ClassName), framework '$($win.Current.FrameworkId)'"

# The blocks: every editable text in the window, with the text it reports.
$edits = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)))
"editable texts: $($edits.Count)"
foreach ($e in $edits) {
    $value = ""
    try { $value = $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value } catch {}
    "  '$($e.Current.Name)' focus=$($e.Current.HasKeyboardFocus): $value"
}

# The block with the keyboard focus, read through its text interface as a
# screen reader reads it: the whole text, and where the caret is.
$focused = $null
foreach ($e in $edits) { if ($e.Current.HasKeyboardFocus) { $focused = $e } }
if ($focused -eq $null) { "no editable text has the keyboard focus" } else {
    "focused: '$($focused.Current.Name)'"
    $tp = $focused.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern)
    $doc = $tp.DocumentRange
    "  text pattern reads: " + $doc.GetText(-1)
    $sel = $tp.GetSelection()
    if ($sel.Length -gt 0) {
        $before = $doc.Clone()
        $before.MoveEndpointByRange([System.Windows.Automation.Text.TextPatternRangeEndpoint]::End, $sel[0],
            [System.Windows.Automation.Text.TextPatternRangeEndpoint]::Start)
        $b = $before.GetText(-1)
        "  caret after $($b.Length) characters: '$b|'"
    }
    $bold = $doc.Clone()
    $fw = $bold.GetAttributeValue([System.Windows.Automation.TextPattern]::FontWeightAttribute)
    "  font weight over the whole text: $fw (mixed when it differs)"
}

if ($Shot -ne "") {
    # The window's visible frame, without the invisible resize border around
    # it, through which whatever is behind the window would show.
    $r = New-Object Win+RECT
    [void][Win]::DwmGetWindowAttribute($hwnd, 9, [ref]$r, 16)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    if ($Method -eq "screen") {
        # What the screen shows there, after bringing the window to the front;
        # only when it really is in front, so no other window is captured.
        [void][Win]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 400
        if ([Win]::GetForegroundWindow() -ne $hwnd) { "not captured: the window could not be brought to the front"; exit 0 }
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    } else {
        # PrintWindow asks the window for its own pixels, so nothing in front
        # of it is captured; PW_RENDERFULLCONTENT (2) asks for what the GPU
        # drew as well.
        $full = New-Object Win+RECT
        [void][Win]::GetWindowRect($hwnd, [ref]$full)
        $all = New-Object System.Drawing.Bitmap ($full.Right - $full.Left), ($full.Bottom - $full.Top)
        $ga = [System.Drawing.Graphics]::FromImage($all)
        $hdc = $ga.GetHdc()
        [void][Win]::PrintWindow($hwnd, $hdc, 2)
        $ga.ReleaseHdc($hdc)
        $g.DrawImage($all, ($full.Left - $r.Left), ($full.Top - $r.Top))
    }
    $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
    "saved $w x $h to $Shot ($Method)"
}

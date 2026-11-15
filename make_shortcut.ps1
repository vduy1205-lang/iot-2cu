$desktopPath = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::Desktop)
if (-not (Test-Path $desktopPath)) {
    $desktopPath = "C:\Users\VUDUY\Desktop"
}
$wsh = New-Object -ComObject WScript.Shell
$shortcut = $wsh.CreateShortcut("$desktopPath\He Thong IoT.lnk")
$shortcut.TargetPath = "d:\iot final\CHAY_HE_THONG.bat"
$shortcut.WorkingDirectory = "d:\iot final"
$shortcut.IconLocation = "shell32.dll,14"
$shortcut.Save()
Write-Host "SUCCESS: Created shortcut at $desktopPath\He Thong IoT.lnk"


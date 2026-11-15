$desk = [Environment]::GetFolderPath('Desktop')
$wsh = New-Object -ComObject WScript.Shell
$sc = $wsh.CreateShortcut("$desk\He Thong IoT.lnk")
$sc.TargetPath = "d:\iot final\CHAY_HE_THONG.bat"
$sc.WorkingDirectory = "d:\iot final"
$sc.IconLocation = "shell32.dll,14"
$sc.Save()
Write-Output "DONE"


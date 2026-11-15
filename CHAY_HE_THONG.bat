@echo off
title HE THONG IOT VIEN DUONG LAO
chcp 65001 >nul

echo ===================================================
echo     DANG KHOI DONG HE THONG IOT VIEN DUONG LAO
echo ===================================================

cd /d "d:\iot final\backend"

:: Kiem tra neu port 3000 hoac 1883 chua chay thi khoi dong node server.js
netstat -ano | findstr :3000 >nul
if %errorlevel% neq 0 (
    echo [1/2] Dang khoi dong Backend & MQTT Broker...
    start /min cmd /c "node server.js"
    timeout /t 2 /nobreak >nul
) else (
    echo [1/2] Backend & MQTT Broker da dang chay!
)

echo [2/2] Dang mo Web Dashboard tren trinh duyet...
start http://localhost:3000

echo.
echo Da khoi dong xong! Cua so nay se tu dong dong sau 3 giay.
timeout /t 3 >nul
exit


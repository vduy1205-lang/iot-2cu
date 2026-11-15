@echo off
title DONG HE THONG IOT
chcp 65001 >nul

echo Dang tat tien trinh Node.js (Server & MQTT Broker)...
taskkill /F /IM node.exe >nul 2>&1

echo Da tat he thong thanh cong!
timeout /t 2 >nul
exit


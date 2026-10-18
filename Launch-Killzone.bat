@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start-launcher.ps1"
if errorlevel 1 pause

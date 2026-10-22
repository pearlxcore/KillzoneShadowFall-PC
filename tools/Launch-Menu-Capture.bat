@echo off
setlocal
cd /d "%~dp0.."
set "KZSF_CAPTURE_TRIGGER=%CD%\out\menu-render-capture.trigger"
set "KZSF_CAPTURE_DIR=%CD%\out\menu-render-captures"
set "KZSF_FRAME_CAPTURE_KEY=1"
if not exist "out\menu-render-captures" mkdir "out\menu-render-captures"
start "" "%CD%\Killzone Launcher.exe"

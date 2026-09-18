@echo off
setlocal
set "MULTISENDER_ELECTRON_EXE=%~dp0..\build_electron\package\MultiSender Electron.exe"
if exist "%MULTISENDER_ELECTRON_EXE%" (
    start "MultiSender Electron" "%MULTISENDER_ELECTRON_EXE%"
    exit /b 0
)
echo Electron package is not built yet.
echo Run from the repository root:
echo py -3 Installer\build_helper.py --electron --deploy
pause
exit /b 1


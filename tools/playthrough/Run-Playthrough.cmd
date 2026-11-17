@echo off
setlocal DisableDelayedExpansion
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Watch-Playthrough.ps1"
set "RESULT=%errorlevel%"
echo.
echo Playthrough launcher returned %RESULT%.
pause
exit /b %RESULT%

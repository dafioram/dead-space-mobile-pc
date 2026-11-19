@echo off
setlocal DisableDelayedExpansion
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Watch-Playthrough.ps1" -DetailMode off -Geometry
set "RESULT=%errorlevel%"
echo.
echo Geometry capture returned %RESULT%.
pause
exit /b %RESULT%

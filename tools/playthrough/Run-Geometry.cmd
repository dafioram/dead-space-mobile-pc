@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Watch-Playthrough.ps1" -Geometry
exit /b %errorlevel%

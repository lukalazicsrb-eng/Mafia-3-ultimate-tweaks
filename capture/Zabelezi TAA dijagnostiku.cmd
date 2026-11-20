@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0snimi-mafia.ps1" -TraceMod
set "status=%ERRORLEVEL%"
echo.
pause
exit /b %status%

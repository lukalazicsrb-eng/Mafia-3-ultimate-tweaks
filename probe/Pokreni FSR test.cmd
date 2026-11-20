@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-probe.ps1"
set "probeExit=%ERRORLEVEL%"
echo.
pause
exit /b %probeExit%

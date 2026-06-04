@echo off
REM Opens PlatformIO serial monitor (uses monitor_port + monitor_speed from platformio.ini).
cd /d "%~dp0.."
"%USERPROFILE%\.platformio\penv\Scripts\pio.exe" device monitor
echo.
pause

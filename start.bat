@echo off
REM Starts UNISPHERE at http://127.0.0.1:8080/  (close this window or press Ctrl+C to stop)
cd /d "%~dp0backend"
if not exist build\unisphere_server.exe ( echo Run build.bat first. & pause & exit /b 1 )
if not exist ..\data\unisphere.db ( echo Run init-database.bat first. & pause & exit /b 1 )
start "" cmd /c "timeout /t 2 >nul & start http://127.0.0.1:8080/"
build\unisphere_server.exe
pause

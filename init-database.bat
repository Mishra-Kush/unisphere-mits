@echo off
REM Creates data\unisphere.db (schema + all 39 clubs) and the administrator account from backend\config\admin.conf
cd /d "%~dp0backend"
if not exist build\init_admin.exe ( echo Run build.bat first. & pause & exit /b 1 )
build\init_admin.exe %*
echo.
pause

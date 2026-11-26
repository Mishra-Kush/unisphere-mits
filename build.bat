@echo off
REM Builds the UNISPHERE backend (C + SQLite) with gcc. Run once, and again after changing any .c file.
setlocal
cd /d "%~dp0backend"
REM Target Windows 10+ so SQLite and the sockets code see the modern Windows API
REM Always use the MSYS2 compiler we installed, even if another (older) gcc is earlier in PATH
if exist C:\msys64\ucrt64\bin\gcc.exe set PATH=C:\msys64\ucrt64\bin;%PATH%
set WINFLAGS=-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
where gcc >nul 2>nul
if errorlevel 1 (
  echo [ERROR] gcc was not found. Install MSYS2 and add C:\msys64\ucrt64\bin to PATH. See README.md, step 1.
  pause & exit /b 1
)
echo Using compiler:
where gcc
gcc --version | findstr /i gcc
if not exist vendor\sqlite3.c (
  echo [ERROR] backend\vendor\sqlite3.c is missing. Download the SQLite amalgamation and copy sqlite3.c and sqlite3.h
  echo         into the backend\vendor folder. See README.md, step 2.
  pause & exit /b 1
)
if not exist build mkdir build
if not exist build\sqlite3.o (
  echo Compiling SQLite - this happens only once and takes about a minute...
  gcc -O2 %WINFLAGS% -c vendor\sqlite3.c -o build\sqlite3.o || (pause & exit /b 1)
)
set CF=-std=c11 -O2 -Wall -Wextra %WINFLAGS% -Iinclude -Ivendor
echo Building init_admin.exe ...
gcc %CF% -o build\init_admin.exe src\init_admin.c src\password.c src\sha256.c build\sqlite3.o -lbcrypt || (pause & exit /b 1)
echo Building unisphere_server.exe ...
gcc %CF% -o build\unisphere_server.exe src\main.c src\http.c src\json.c src\api.c src\auth.c src\validate.c src\util.c src\fields.c src\clubs.c src\events.c src\registrations.c src\announcements.c src\router.c src\password.c src\sha256.c build\sqlite3.o -lbcrypt -lws2_32 || (pause & exit /b 1)
echo.
echo Build OK. Next: init-database.bat, then start.bat

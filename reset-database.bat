@echo off
REM DEVELOPMENT ONLY: deletes ALL data (accounts, events, registrations, notices) and recreates a fresh database.
cd /d "%~dp0"
set /p OK=This deletes everything in data\unisphere.db. Type YES to continue: 
if /i not "%OK%"=="YES" ( echo Cancelled. & pause & exit /b 0 )
if exist data\unisphere.db del data\unisphere.db
call init-database.bat

@echo off
REM Build Coup (Saturn disc + Linux server .so)
REM Double-click this file or run from Command Prompt.
REM Requires: Git Bash (ships with Git for Windows), Docker Desktop

setlocal

REM Find Git Bash
set "GITBASH="
if exist "C:\Program Files\Git\bin\bash.exe" (
    set "GITBASH=C:\Program Files\Git\bin\bash.exe"
) else if exist "C:\Program Files (x86)\Git\bin\bash.exe" (
    set "GITBASH=C:\Program Files (x86)\Git\bin\bash.exe"
) else (
    where bash >nul 2>&1
    if %errorlevel%==0 (
        set "GITBASH=bash"
    ) else (
        echo ERROR: Git Bash not found. Install Git for Windows.
        pause
        exit /b 1
    )
)

echo Building Coup via Git Bash...
"%GITBASH%" -l -c "cd '%~dp0' && MSYS_NO_PATHCONV=1 make coup-all"

if %errorlevel% neq 0 (
    echo.
    echo BUILD FAILED
) else (
    echo.
    echo BUILD COMPLETE
    echo   Game:   build\coup_game\   (load game.cue in Mednafen)
    echo   Server: build\coup_server\ (copy to Linux server)
)

pause

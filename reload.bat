@echo off
setlocal
cd /d "%~dp0"

REM Rebuild only the game DLL and overwrite the staged copy that a running main.exe watches
REM (see run.bat, which stages temp\game.dll and launches with --so temp\game.dll). The platform
REM notices the mtime change and hot-reloads. Leave the platform exe running while you use this.

echo === Building game DLL ===
call bazel build //dali/game:game.dll || exit /b 1

echo === Staging DLL ===
set "TEMP_DIR=%~dp0temp"
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"
copy /y "bazel-bin\dali\game\game.dll" "%TEMP_DIR%\game.dll" >nul || exit /b 1

echo === Done; running platform will reload ===

@echo off
setlocal
cd /d "%~dp0"

echo === Building game DLL ===
call bazel build //dali/game:game.dll || exit /b 1

echo === Building platform exe ===
call bazel build //dali/platform:main || exit /b 1

REM Bazel outputs are read-only, but GameLibrary::Load stages its hot-reload copies in
REM loaded_game_dlls next to the DLL. Copy game.dll into a writable temp dir so that works.
echo === Staging DLL ===
set "TEMP_DIR=%~dp0temp"
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"
copy /y "bazel-bin\dali\game\game.dll" "%TEMP_DIR%\game.dll" >nul || exit /b 1

echo === Running ===
"bazel-bin\dali\platform\main.exe" --so "%TEMP_DIR%\game.dll"

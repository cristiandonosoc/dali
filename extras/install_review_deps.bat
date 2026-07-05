@echo off
setlocal

REM Installs whatever review.bat (repo root) needs at runtime. Currently that's just Node.js,
REM since `npx diff2html-cli` fetches and runs the reviewer itself on demand - nothing to
REM preinstall for that part.

echo === Checking for Node.js (needed for npx / diff2html-cli) ===
where node >nul 2>&1
if %errorlevel%==0 (
    echo Node.js already installed:
    node --version
    goto :done
)

echo Node.js not found on PATH.

where scoop >nul 2>&1
if %errorlevel%==0 (
    echo Installing Node.js via scoop...
    call scoop install nodejs-lts
    goto :verify
)

echo Scoop not found.

where winget >nul 2>&1
if %errorlevel%==0 (
    echo Falling back to winget...
    call winget install --id OpenJS.NodeJS.LTS -e
    goto :verify
)

echo Neither scoop nor winget is available on this machine.
echo Install scoop from https://scoop.sh, or Node.js directly from https://nodejs.org/, then re-run
echo this script.
goto :eof

:verify
where node >nul 2>&1
if %errorlevel%==0 (
    echo Node.js installed:
    node --version
) else (
    echo Install finished, but node isn't on PATH yet in this shell - open a new terminal and
    echo re-run this script to confirm.
)

:done
echo === review.bat should work now. Run it from the repo root. ===

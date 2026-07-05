@echo off
setlocal
cd /d "%~dp0"

REM Usage:
REM   review.bat                 Review everything changed since the last commit (staged + unstaged
REM                               + untracked).
REM   review.bat <ref>           Review everything changed since <ref> (same, but against <ref>).
REM   review.bat <ref1> <ref2>   Review one link of a stack: what <ref2> adds on top of <ref1>.
REM                              (Pure history diff between two commits, so untracked files don't
REM                              apply here.)

set "REF1=%~1"
if "%REF1%"=="" set "REF1=HEAD"

set "REF2=%~2"

for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD') do set "BRANCH=%%b"

if not "%REF2%"=="" (
    call npx --yes diff2html-cli -s side --su open -t "dali review: %REF1%..%REF2%" -- %REF1%..%REF2%
    goto :eof
)

REM Untracked files never show up in `git diff`. Mark them intent-to-add so they appear as new-file
REM diffs, then un-stage them again right after so the working tree/index is left exactly as it was.
set "UNTRACKED_LIST=%TEMP%\dali_review_untracked.txt"
git ls-files --others --exclude-standard > "%UNTRACKED_LIST%"

for /f "usebackq delims=" %%f in ("%UNTRACKED_LIST%") do git add -N -- "%%f"

npx --yes diff2html-cli -s side --su open -t "dali review: %BRANCH% vs %REF1%" -- %REF1%

for /f "usebackq delims=" %%f in ("%UNTRACKED_LIST%") do git reset -- "%%f" >nul
del "%UNTRACKED_LIST%"

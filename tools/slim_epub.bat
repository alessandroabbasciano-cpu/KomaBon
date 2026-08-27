@echo off
setlocal enabledelayedexpansion

rem Drag .epub files onto this .bat, or run it without arguments
rem to process everything in the "epubs" folder next to the script.
rem
rem No "cd" to the script's folder: doing that would cause a relative argument
rem passed on the command line to resolve against tools\ instead of the folder
rem where the user is. The Python script is always referenced by absolute path.
set SLIM=%~dp0slim_epub.py

rem The Microsoft Store Python is named python.exe; the python.org one also
rem responds to the "py" launcher, which is more reliable when multiple versions exist.
set PYTHON=
where py >nul 2>&1 && set PYTHON=py
if not defined PYTHON (
    where python >nul 2>&1 && set PYTHON=python
)
if not defined PYTHON (
    echo ERROR: Python not found in PATH.
    echo Install it from https://www.python.org/downloads/ and check
    echo "Add Python to PATH" during installation.
    echo.
    pause
    exit /b 1
)

if "%~1"=="" goto :folder

rem Dragging files passes them as arguments. One by one, so a corrupted
rem EPUB does not prevent the remaining ones from being processed.
echo KomaBon - removing embedded images and fonts...
echo.
for %%F in (%*) do (
    %PYTHON% "%SLIM%" "%%~fF"
)
echo.
echo Done. Send the .slim.epub files to KomaBon at
echo http://komabon.local/send
echo.
pause
exit /b 0

:folder
cd /d "%~dp0"
if not exist epubs (
    mkdir epubs
    echo Created "epubs" folder.
    echo Place your .epub files there and run this again,
    echo or drag files directly onto this .bat.
    echo.
    pause
    exit /b 0
)

dir /b "epubs\*.epub" >nul 2>&1
if errorlevel 1 (
    echo The "epubs" folder is empty.
    echo Place your .epub files there and run this again.
    echo.
    pause
    exit /b 0
)

echo KomaBon - removing embedded images and fonts...
echo.
for %%F in ("epubs\*.epub") do (
    echo %%~nxF| findstr /i /c:".slim.epub" >nul || %PYTHON% "%SLIM%" "%%~fF"
)
echo.
echo Done. Send the .slim.epub files to KomaBon at
echo http://komabon.local/send
echo.
pause
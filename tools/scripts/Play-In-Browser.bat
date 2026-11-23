@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "launcher_python=%~dp0python\python.exe"
set "launcher_python_options="
if exist "%launcher_python%" goto serve

set "launcher_python=py.exe"
set "launcher_python_options=-3"
"%launcher_python%" %launcher_python_options% -c "from http.server import ThreadingHTTPServer" >nul 2>&1
if not errorlevel 1 goto serve

set "launcher_python=python.exe"
set "launcher_python_options="
"%launcher_python%" -c "from http.server import ThreadingHTTPServer" >nul 2>&1
if not errorlevel 1 goto serve

echo Install Python 3.7 or later and make py.exe or python.exe available in Command Prompt.>&2
set "launcher_exit=1"
goto finish

:serve
"%launcher_python%" %launcher_python_options% "%~dp0serve-browser-game.py" --open-browser %*
set "launcher_exit=%errorlevel%"

:finish
if not "%launcher_exit%"=="0" if "%~1"=="" pause
exit /b %launcher_exit%

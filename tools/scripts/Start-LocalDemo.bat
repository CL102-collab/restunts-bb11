@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "demo_python=%~dp0python\python.exe"
set "demo_python_options="
if exist "%demo_python%" goto serve

set "demo_python=py.exe"
set "demo_python_options=-3"
"%demo_python%" %demo_python_options% -c "from http.server import ThreadingHTTPServer" >nul 2>&1
if not errorlevel 1 goto serve

set "demo_python=python.exe"
set "demo_python_options="
"%demo_python%" -c "from http.server import ThreadingHTTPServer" >nul 2>&1
if not errorlevel 1 goto serve

echo Install Python 3.7 or later and make py.exe or python.exe available in Command Prompt.>&2
set "demo_exit=1"
goto finish

:serve
"%demo_python%" %demo_python_options% "%~dp0serve-local-demo.py" --open-browser %*
set "demo_exit=%errorlevel%"

:finish
if not "%demo_exit%"=="0" if "%~1"=="" pause
exit /b %demo_exit%

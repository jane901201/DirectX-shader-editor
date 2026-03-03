@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0..
pushd "%ROOT%"

echo [1/2] Checking vcpkg...
if not exist "%ROOT%\vcpkg\bootstrap-vcpkg.bat" (
  echo vcpkg not found. Cloning...
  git clone https://github.com/microsoft/vcpkg "%ROOT%\vcpkg"
  if errorlevel 1 exit /b 1
)

echo [2/2] Bootstrapping vcpkg...
call "%ROOT%\vcpkg\bootstrap-vcpkg.bat"
if errorlevel 1 exit /b 1

echo vcpkg setup complete.
popd
endlocal
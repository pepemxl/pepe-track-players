@echo off
rem Run the app with the OpenCV (vcpkg) and MinGW runtime DLLs on PATH.
setlocal

if "%BUILD_DIR%"=="" set BUILD_DIR=build
set PATH=%~dp0vcpkg_installed\x64-mingw-dynamic\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%

start "" "%~dp0%BUILD_DIR%\bin\pepe_track_players.exe" %*
endlocal

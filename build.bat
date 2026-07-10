@echo off
rem Build helper (MinGW + Qt 6.11 + OpenCV via vcpkg manifest).
rem   set BUILD_DIR=build & set BUILD_TYPE=Release & build.bat [extra cmake args...]
rem La primera configuracion compila OpenCV+FFmpeg desde fuente (tarda).
setlocal enabledelayedexpansion

if "%BUILD_DIR%"=="" set BUILD_DIR=build
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release
if "%QT_DIR%"=="" set QT_DIR=C:/Qt/6.11.0/mingw_64
if "%MINGW_DIR%"=="" set MINGW_DIR=C:/Qt/Tools/mingw1310_64
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:/vcpkg
rem Instalado local al proyecto para que "clean" del build no recompile OpenCV.
if "%VCPKG_INSTALLED%"=="" set VCPKG_INSTALLED=%~dp0vcpkg_installed

set PATH=%MINGW_DIR:/=\%\bin;C:\Qt\Tools\Ninja;%PATH%

cmake -S . -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_CXX_COMPILER="%MINGW_DIR%/bin/g++.exe" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic ^
    -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED%" %*
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b %errorlevel%

echo.
echo Built in %BUILD_DIR%\bin
endlocal

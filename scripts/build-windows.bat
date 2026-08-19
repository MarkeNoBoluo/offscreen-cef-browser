@echo off
setlocal EnableDelayedExpansion

rem ============================================================
rem  Defaults
rem ============================================================
set "ARCHITECTURE=x64"
set "CEF_ROOT=D:\Git\cef_binary_100.0.14+g4e5ba66+chromium-100.0.4896.75_windows64_minimal"
set "BUILD_DIR=D:\Git\CEF-SDK\build\cef100-msvc2022-x64"
set "CONFIGURATION=Release"
set "QT_PREFIX=D:\IDE\QT6.9.3\6.9.3\msvc2022_64"
set "INSTALL_PREFIX="

rem ============================================================
rem  Parse arguments
rem  Usage: build-windows.bat [-Architecture x64] [-CefRoot "D:\..."]
rem         [-BuildDir "build\..."] [-Configuration Release|Debug]
rem         [-QtPrefix "D:\..."] [-InstallPrefix "D:\..."]
rem ============================================================
:parse_args
if "%~1"=="" goto end_parse
if /i "%~1"=="-Architecture"  ( set "ARCHITECTURE=%~2"  & shift & shift & goto parse_args )
if /i "%~1"=="-CefRoot"       ( set "CEF_ROOT=%~2"       & shift & shift & goto parse_args )
if /i "%~1"=="-BuildDir"      ( set "BUILD_DIR=%~2"      & shift & shift & goto parse_args )
if /i "%~1"=="-Configuration" ( set "CONFIGURATION=%~2"  & shift & shift & goto parse_args )
if /i "%~1"=="-QtPrefix"      ( set "QT_PREFIX=%~2"      & shift & shift & goto parse_args )
if /i "%~1"=="-InstallPrefix" ( set "INSTALL_PREFIX=%~2" & shift & shift & goto parse_args )
echo Unknown argument: %~1
exit /b 1
:end_parse

rem ============================================================
rem  Architecture guard (x64 only)
rem ============================================================
if /i not "%ARCHITECTURE%"=="x64" (
    echo CEF 100 configuration supports x64 only.
    exit /b 1
)
set "CEF_DISTRIBUTION=windows64_minimal"
set "QT_PACKAGE=msvc2022_64"
set "CEF_VERSION=100.0.14+g4e5ba66+chromium-100.0.4896.75"

rem ============================================================
rem  Resolve repo root  (scripts\..\)
rem ============================================================
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "REPO_ROOT=%%~fI"

if "%CEF_ROOT%"==""  set "CEF_ROOT=D:\Git\cef_binary_%CEF_VERSION%_%CEF_DISTRIBUTION%"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build\cef100-msvc2022-x64"

rem  Absolute vs relative BUILD_DIR
if "%BUILD_DIR:~1,1%"==":" (
    set "BUILD_PATH=%BUILD_DIR%"
) else (
    set "BUILD_PATH=%REPO_ROOT%\%BUILD_DIR%"
)

set "INSTALL_PATH="
if not "%INSTALL_PREFIX%"=="" (
    if "%INSTALL_PREFIX:~1,1%"==":" (
        set "INSTALL_PATH=%INSTALL_PREFIX%"
    ) else (
        for %%I in ("%REPO_ROOT%\%INSTALL_PREFIX%") do set "INSTALL_PATH=%%~fI"
    )
)

rem ============================================================
rem  Validate prerequisites
rem ============================================================
echo =^> Validating prerequisites

if not exist "%CEF_ROOT%\" (
    echo CEF root not found: %CEF_ROOT%
    exit /b 1
)
if not exist "%CEF_ROOT%\cmake\FindCEF.cmake" (
    echo CEF CMake package not found: %CEF_ROOT%\cmake\FindCEF.cmake
    exit /b 1
)
if not exist "%CEF_ROOT%\include\cef_version.h" (
    echo CEF headers not found: %CEF_ROOT%\include\cef_version.h
    exit /b 1
)

call :get_qt_prefix "%QT_PREFIX%" "%QT_PACKAGE%"
if errorlevel 1 exit /b 1

set "QT6_DIR=%RESOLVED_QT_PREFIX%\lib\cmake\Qt6"
if not exist "%QT6_DIR%\" (
    echo Qt6 CMake package not found: %QT6_DIR%
    exit /b 1
)

call :assert_vs2022_generator
if errorlevel 1 exit /b 1

echo CEF_ROOT: %CEF_ROOT%
echo Architecture: %ARCHITECTURE%
echo Qt prefix: %RESOLVED_QT_PREFIX%
echo Qt6_DIR: %QT6_DIR%
echo Build dir: %BUILD_PATH%
echo Configuration: %CONFIGURATION%
if not "%INSTALL_PATH%"=="" echo Install prefix: %INSTALL_PATH%

rem ============================================================
rem  Configure CMake
rem ============================================================
echo =^> Configuring CMake
echo ^> cmake -S "%REPO_ROOT%" -B "%BUILD_PATH%" -G "Visual Studio 17 2022" -A %ARCHITECTURE% -DCEF_ROOT="%CEF_ROOT%" -DQt6_DIR="%QT6_DIR%" -DCEF_RUNTIME_LIBRARY_FLAG=/MD -DOFFSCREEN_BUILD_APP=ON -DOFFSCREEN_BUILD_TESTS=OFF -DOFFSCREEN_BUILD_EMBEDDING_DEMOS=OFF

cmake -S "%REPO_ROOT%" -B "%BUILD_PATH%" ^
    -G "Visual Studio 17 2022" ^
    -A %ARCHITECTURE% ^
    "-DCEF_ROOT=%CEF_ROOT%" ^
    "-DQt6_DIR=%QT6_DIR%" ^
    -DCEF_RUNTIME_LIBRARY_FLAG=/MD ^
    -DOFFSCREEN_BUILD_APP=ON ^
    -DOFFSCREEN_BUILD_TESTS=OFF ^
    -DOFFSCREEN_BUILD_EMBEDDING_DEMOS=OFF
if errorlevel 1 (
    echo CMake configure failed.
    exit /b 1
)

rem ============================================================
rem  Build targets
rem ============================================================
echo =^> Building targets
echo ^> cmake --build "%BUILD_PATH%" --config %CONFIGURATION% --target offscreen_cef_browser offscreen_cef_subprocess

cmake --build "%BUILD_PATH%" --config %CONFIGURATION% ^
    --target offscreen_cef_browser offscreen_cef_subprocess
if errorlevel 1 (
    echo CMake build failed.
    exit /b 1
)

rem ============================================================
rem  Optional install
rem ============================================================
if not "%INSTALL_PATH%"=="" (
    echo =^> Installing runtime
    cmake --install "%BUILD_PATH%" --config %CONFIGURATION% --prefix "%INSTALL_PATH%"
    if errorlevel 1 (
        echo CMake install failed.
        exit /b 1
    )
)

echo =^> Build completed
exit /b 0

rem ============================================================
rem  :get_qt_prefix  "explicit_prefix"  "package_name"
rem  Sets RESOLVED_QT_PREFIX on success.
rem ============================================================
:get_qt_prefix
set "_EXPLICIT=%~1"
set "_PACKAGE=%~2"

if not "%_EXPLICIT%"=="" (
    if not exist "%_EXPLICIT%\" (
        echo Qt prefix not found: %_EXPLICIT%
        exit /b 1
    )
    for %%I in ("%_EXPLICIT%") do set "_LEAF=%%~nxI"
    if /i not "%_LEAF%"=="%_PACKAGE%" (
        echo Qt prefix must be Qt 6.9.3 %_PACKAGE%: %_EXPLICIT%
        exit /b 1
    )
    set "RESOLVED_QT_PREFIX=%_EXPLICIT%"
    exit /b 0
)

rem  Auto-detect: iterate qmake.exe entries in PATH
rem  Use a helper subroutine to avoid goto inside nested for blocks.
set "RESOLVED_QT_PREFIX="
for /f "delims=" %%Q in ('where qmake.exe 2^>nul') do (
    if "!RESOLVED_QT_PREFIX!"=="" call :_probe_qmake "%%Q" "%_PACKAGE%"
)
if "%RESOLVED_QT_PREFIX%"=="" (
    echo qmake.exe (Qt 6.9.3 %_PACKAGE%) not found in PATH. Add it to PATH or pass -QtPrefix.
    exit /b 1
)
exit /b 0

rem  Helper: probe one qmake.exe candidate; sets RESOLVED_QT_PREFIX if it matches.
:_probe_qmake
set "_Q=%~1"
set "_PKG=%~2"
for /f "delims=" %%V in ('"%_Q%" -query QT_VERSION 2^>nul') do (
    if "%%V"=="6.9.3" (
        for /f "delims=" %%P in ('"%_Q%" -query QT_INSTALL_PREFIX 2^>nul') do (
            for %%L in ("%%P") do (
                if /i "%%~nxL"=="%_PKG%" set "RESOLVED_QT_PREFIX=%%P"
            )
        )
    )
)
exit /b 0

rem ============================================================
rem  :assert_vs2022_generator
rem ============================================================
:assert_vs2022_generator
cmake --help 2>&1 | findstr /c:"Visual Studio 17 2022" >nul
if errorlevel 1 (
    echo CMake generator 'Visual Studio 17 2022' is not available.
    exit /b 1
)
exit /b 0

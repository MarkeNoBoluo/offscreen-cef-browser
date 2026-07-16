param(
  [string]$CefRoot = "D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017",
  [string]$BuildDir = "build\v1-cef96-msvc2017-x64",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$QtPrefix = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
  param([string]$Message)
  Write-Host "==> $Message"
}

function Invoke-Checked {
  param(
    [string]$FilePath,
    [string[]]$Arguments
  )

  Write-Host "> $FilePath $($Arguments -join ' ')"
  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
  }
}

function Require-Path {
  param(
    [string]$Path,
    [string]$Description
  )

  if (-not (Test-Path -LiteralPath $Path)) {
    throw "$Description not found: $Path"
  }
}

function Get-QtPrefix {
  param([string]$ExplicitQtPrefix)

  if ($ExplicitQtPrefix) {
    Require-Path $ExplicitQtPrefix "Qt prefix"
    return (Resolve-Path -LiteralPath $ExplicitQtPrefix).Path
  }

  $qmakePaths = @(where.exe qmake 2>$null)
  if ($qmakePaths.Count -eq 0) {
    throw "qmake.exe was not found in PATH. Add Qt 5.14.2 msvc2017_64 bin to PATH or pass -QtPrefix."
  }

  foreach ($qmakePath in $qmakePaths) {
    $qtVersion = (& $qmakePath -query QT_VERSION).Trim()
    if ($LASTEXITCODE -ne 0 -or $qtVersion -ne "5.14.2") {
      continue
    }

    $prefix = (& $qmakePath -query QT_INSTALL_PREFIX).Trim()
    if ($LASTEXITCODE -ne 0) {
      continue
    }
    if ($prefix -match "msvc2017_64") {
      return $prefix
    }
  }

  throw "Qt 5.14.2 msvc2017_64 qmake was not found in PATH. Add it to PATH or pass -QtPrefix."
}

function Assert-VS2017GeneratorAvailable {
  $generators = (& cmake --help 2>&1) -join "`n"
  if ($generators -notmatch "Visual Studio 15 2017") {
    throw "CMake generator 'Visual Studio 15 2017' is not available."
  }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $scriptDir "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir

Write-Step "Validating prerequisites"
Require-Path $CefRoot "CEF root"
Require-Path (Join-Path $CefRoot "cmake\FindCEF.cmake") "CEF CMake package"
Require-Path (Join-Path $CefRoot "include\cef_version.h") "CEF headers"
$resolvedQtPrefix = Get-QtPrefix $QtPrefix
$qt5Dir = Join-Path $resolvedQtPrefix "lib\cmake\Qt5"
Require-Path $qt5Dir "Qt5 CMake package"
Assert-VS2017GeneratorAvailable

Write-Host "CEF_ROOT: $CefRoot"
Write-Host "Qt prefix: $resolvedQtPrefix"
Write-Host "Qt5_DIR: $qt5Dir"
Write-Host "Build dir: $buildPath"
Write-Host "Configuration: $Configuration"

Write-Step "Configuring CMake"
Invoke-Checked "cmake" @(
  "-S", $repoRoot,
  "-B", $buildPath,
  "-G", "Visual Studio 15 2017",
  "-A", "x64",
  "-DCEF_ROOT=$CefRoot",
  "-DQt5_DIR=$qt5Dir",
  "-DOFFSCREEN_BUILD_APP=ON",
  "-DOFFSCREEN_BUILD_TESTS=ON"
)

Write-Step "Building targets"
Invoke-Checked "cmake" @(
  "--build", $buildPath,
  "--config", $Configuration,
  "--target", "offscreen_core_tests", "offscreen_cef_browser", "offscreen_cef_subprocess"
)

Write-Step "Running tests"
Invoke-Checked "ctest" @(
  "--test-dir", $buildPath,
  "-C", $Configuration,
  "--output-on-failure"
)

Write-Step "Build and tests completed"

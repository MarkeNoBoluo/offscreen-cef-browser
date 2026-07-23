param(
  [ValidateSet("Win32", "x64")]
  [string]$Architecture = "Win32",
  [string]$CefRoot = "",
  [string]$BuildDir = "",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Release",
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
  param(
    [string]$ExplicitQtPrefix,
    [string]$QtPackage
  )

  if ($ExplicitQtPrefix) {
    Require-Path $ExplicitQtPrefix "Qt prefix"
    $resolvedPrefix = (Resolve-Path -LiteralPath $ExplicitQtPrefix).Path
    if ((Split-Path -Leaf $resolvedPrefix) -ne $QtPackage) {
      throw "Qt prefix must be Qt 5.14.2 ${QtPackage}: $resolvedPrefix"
    }
    return $resolvedPrefix
  }

  $qmakePaths = @(where.exe qmake 2>$null)
  if ($qmakePaths.Count -eq 0) {
    throw "qmake.exe was not found in PATH. Add Qt 5.14.2 $QtPackage bin to PATH or pass -QtPrefix."
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
    if ((Split-Path -Leaf $prefix) -eq $QtPackage) {
      return $prefix
    }
  }

  throw "Qt 5.14.2 $QtPackage qmake was not found in PATH. Add it to PATH or pass -QtPrefix."
}

function Assert-VS2017GeneratorAvailable {
  $generators = (& cmake --help 2>&1) -join "`n"
  if ($generators -notmatch "Visual Studio 15 2017") {
    throw "CMake generator 'Visual Studio 15 2017' is not available."
  }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $scriptDir "..")).Path
$cefVersion = "98.0.0+g2f5e1b6+chromium-98.0.4758.0"
if ($Architecture -eq "x64") {
  $cefDistribution = "windows64"
  $qtPackage = "msvc2017_64"
  $outputArchitecture = "x64"
} else {
  $cefDistribution = "windows32_minimal"
  $qtPackage = "msvc2017"
  $outputArchitecture = "x86"
}
if (-not $CefRoot) {
  $CefRoot = "D:\Git\cef_binary_${cefVersion}_${cefDistribution}"
}
if (-not $BuildDir) {
  $BuildDir = "build\cef98-msvc2017-${outputArchitecture}"
}
$buildPath = Join-Path $repoRoot $BuildDir

Write-Step "Validating prerequisites"
Require-Path $CefRoot "CEF root"
Require-Path (Join-Path $CefRoot "cmake\FindCEF.cmake") "CEF CMake package"
Require-Path (Join-Path $CefRoot "include\cef_version.h") "CEF headers"
$resolvedQtPrefix = Get-QtPrefix $QtPrefix $qtPackage
$qt5Dir = Join-Path $resolvedQtPrefix "lib\cmake\Qt5"
Require-Path $qt5Dir "Qt5 CMake package"
Assert-VS2017GeneratorAvailable

Write-Host "CEF_ROOT: $CefRoot"
Write-Host "Architecture: $Architecture"
Write-Host "Qt prefix: $resolvedQtPrefix"
Write-Host "Qt5_DIR: $qt5Dir"
Write-Host "Build dir: $buildPath"
Write-Host "Configuration: $Configuration"

Write-Step "Configuring CMake"
Invoke-Checked "cmake" @(
  "-S", $repoRoot,
  "-B", $buildPath,
  "-G", "Visual Studio 15 2017",
  "-A", $Architecture,
  "-DCEF_ROOT=$CefRoot",
  "-DQt5_DIR=$qt5Dir",
  "-DOFFSCREEN_BUILD_APP=ON",
  "-DOFFSCREEN_BUILD_TESTS=ON"
)

Write-Step "Building targets"
Invoke-Checked "cmake" @(
  "--build", $buildPath,
  "--config", $Configuration,
  "--target", "offscreen_core_tests", "offscreen_cef_browser", "offscreen_cef_subprocess", "embedding_demo_webview", "embedding_demo_tabbed_browser"
)

Write-Step "Running tests"
Invoke-Checked "ctest" @(
  "--test-dir", $buildPath,
  "-C", $Configuration,
  "--output-on-failure"
)

Write-Step "Build and tests completed"

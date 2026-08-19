param(
  [ValidateSet("Win32", "x64")]
  [string]$Architecture = "x64",
  [string]$CefRoot = "",
  [string]$BuildDir = "",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Release",
  [string]$QtPrefix = "",
  [string]$InstallPrefix = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step {
  param([string]$Message)
  Write-Host "==> $Message"
}

function Quote-CommandLineArgument {
  param([string]$Argument)

  if ($Argument -notmatch '[\s"]') {
    return $Argument
  }
  return '"' + ($Argument -replace '"', '\"') + '"'
}

function Get-SanitizedProcessEnvironment {
  $environment = @{}
  $seenKeys = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
  $pathParts = New-Object 'System.Collections.Generic.List[string]'
  $seenPathParts = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)

  $rawEnvironment = & "$env:COMSPEC" /d /c set
  foreach ($line in $rawEnvironment) {
    if ($line -notmatch '^([^=]+)=(.*)$') {
      continue
    }

    $name = $Matches[1]
    $value = $Matches[2]
    if ($name -ieq 'Path') {
      foreach ($part in ($value -split ';')) {
        if ($part -and $seenPathParts.Add($part)) {
          $pathParts.Add($part)
        }
      }
      continue
    }

    if ($seenKeys.Add($name)) {
      $environment[$name] = $value
    }
  }

  $environment['Path'] = ($pathParts -join ';')
  return $environment
}

function Invoke-Checked {
  param(
    [string]$FilePath,
    [string[]]$Arguments
  )

  Write-Host "> $FilePath $($Arguments -join ' ')"
  $processInfo = New-Object System.Diagnostics.ProcessStartInfo
  $processInfo.FileName = $FilePath
  $processInfo.Arguments = ($Arguments | ForEach-Object {
      Quote-CommandLineArgument $_
    }) -join ' '
  $processInfo.UseShellExecute = $false
  $sanitizedEnvironment = Get-SanitizedProcessEnvironment
  if ($null -ne $processInfo.EnvironmentVariables) {
    $processInfo.EnvironmentVariables.Clear()
    foreach ($entry in $sanitizedEnvironment.GetEnumerator()) {
      $processInfo.EnvironmentVariables[$entry.Key] = $entry.Value
    }
  } elseif ($null -ne $processInfo.Environment) {
    $processInfo.Environment.Clear()
    foreach ($entry in $sanitizedEnvironment.GetEnumerator()) {
      $processInfo.Environment[$entry.Key] = $entry.Value
    }
  } else {
    throw "ProcessStartInfo does not expose an environment collection."
  }

  $process = [System.Diagnostics.Process]::Start($processInfo)
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) {
    throw "Command failed with exit code $($process.ExitCode): $FilePath $($Arguments -join ' ')"
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
      throw "Qt prefix must be Qt 6.9.3 ${QtPackage}: $resolvedPrefix"
    }
    return $resolvedPrefix
  }

  $qmakePaths = @(where.exe qmake 2>$null)
  if ($qmakePaths.Count -eq 0) {
    throw "qmake.exe was not found in PATH. Add Qt 6.9.3 $QtPackage bin to PATH or pass -QtPrefix."
  }

  foreach ($qmakePath in $qmakePaths) {
    $qtVersion = (& $qmakePath -query QT_VERSION).Trim()
    if ($LASTEXITCODE -ne 0 -or $qtVersion -ne "6.9.3") {
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

  throw "Qt 6.9.3 $QtPackage qmake was not found in PATH. Add it to PATH or pass -QtPrefix."
}

function Assert-VS2022GeneratorAvailable {
  $generators = (& cmake --help 2>&1) -join "`n"
  if ($generators -notmatch "Visual Studio 17 2022") {
    throw "CMake generator 'Visual Studio 17 2022' is not available."
  }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $scriptDir "..")).Path
$cefVersion = "100.0.14+g4e5ba66+chromium-100.0.4896.75"
if ($Architecture -eq "x64") {
  $cefDistribution = "windows64_minimal"
  $qtPackage = "msvc2022_64"
  $outputArchitecture = "x64"
} else {
  throw "CEF 100 configuration supports x64 only."
}
if (-not $CefRoot) {
  $CefRoot = "D:\Git\cef_binary_${cefVersion}_${cefDistribution}"
}
if (-not $BuildDir) {
  $BuildDir = "build\cef100-msvc2022-${outputArchitecture}"
}
$buildPath = Join-Path $repoRoot $BuildDir
$installPath = ""
if ($InstallPrefix) {
  if ([System.IO.Path]::IsPathRooted($InstallPrefix)) {
    $installPath = [System.IO.Path]::GetFullPath($InstallPrefix)
  } else {
    $installPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $InstallPrefix))
  }
}

Write-Step "Validating prerequisites"
Require-Path $CefRoot "CEF root"
Require-Path (Join-Path $CefRoot "cmake\FindCEF.cmake") "CEF CMake package"
Require-Path (Join-Path $CefRoot "include\cef_version.h") "CEF headers"
$resolvedQtPrefix = Get-QtPrefix $QtPrefix $qtPackage
$qt6Dir = Join-Path $resolvedQtPrefix "lib\cmake\Qt6"
Require-Path $qt6Dir "Qt6 CMake package"
Assert-VS2022GeneratorAvailable

Write-Host "CEF_ROOT: $CefRoot"
Write-Host "Architecture: $Architecture"
Write-Host "Qt prefix: $resolvedQtPrefix"
Write-Host "Qt6_DIR: $qt6Dir"
Write-Host "Build dir: $buildPath"
Write-Host "Configuration: $Configuration"
if ($installPath) {
  Write-Host "Install prefix: $installPath"
}

Write-Step "Configuring CMake"
Invoke-Checked "cmake" @(
  "-S", $repoRoot,
  "-B", $buildPath,
  "-G", "Visual Studio 17 2022",
  "-A", $Architecture,
  "-DCEF_ROOT=$CefRoot",
  "-DQt6_DIR=$qt6Dir",
  "-DCEF_RUNTIME_LIBRARY_FLAG=/MD",
  "-DOFFSCREEN_BUILD_APP=ON",
  "-DOFFSCREEN_BUILD_TESTS=OFF",
  "-DOFFSCREEN_BUILD_EMBEDDING_DEMOS=OFF"
)

Write-Step "Building targets"
Invoke-Checked "cmake" @(
  "--build", $buildPath,
  "--config", $Configuration
)

# Write-Step "Running tests"
# Invoke-Checked "ctest" @(
#   "--test-dir", $buildPath,
#   "-C", $Configuration,
#   "--output-on-failure"
# )

if ($installPath) {
  Write-Step "Installing runtime"
  Invoke-Checked "cmake" @(
    "--install", $buildPath,
    "--config", $Configuration,
    "--prefix", $installPath
  )
}

Write-Step "Build and tests completed"

# Build Script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PowerShell one-command build script that configures, builds, and tests the Windows x64 Qt 5.14.2 + CEF 96 project.

**Architecture:** Keep the script self-contained in `scripts/build.ps1`. It validates local prerequisites, configures CMake with the fixed VS2017 x64 generator and CEF root, builds the required targets, and runs CTest.

**Tech Stack:** PowerShell 5.1, CMake 3.21+, Visual Studio 2017 x64 generator, Qt 5.14.2 MSVC2017 64bit, CEF 96 windows64 VS2017 binary distribution.

## Global Constraints

- Windows x64 only.
- Default CEF root: `D:\Git\cef_binary_96.0.18+gfe551e4+chromium-96.0.4664.110_windows64_vs2017`.
- Default build directory: `build\v1-cef96-msvc2017-x64`.
- Default configuration: `Debug`.
- Do not commit changes unless explicitly requested.

---

## Task 1: PowerShell Build Script

**Files:**
- Create: `scripts/build.ps1`

**Interfaces:**
- Consumes: optional parameters `-CefRoot`, `-BuildDir`, `-Configuration`, `-QtPrefix`.
- Produces: configured build directory, built targets `offscreen_core_tests`, `offscreen_cef_browser`, `offscreen_cef_subprocess`, and CTest results.

- [ ] **Step 1: Add script with prerequisite checks, configure, build, and test**

Create `scripts/build.ps1` with strict mode, parameter defaults, helper functions, and sequential commands.

- [ ] **Step 2: Validate script syntax**

Run:

```powershell
$null = [System.Management.Automation.Language.Parser]::ParseFile("scripts\build.ps1", [ref]$null, [ref]$errors); if ($errors.Count) { $errors; exit 1 }
```

Expected: no parser errors.

- [ ] **Step 3: Run the script**

Run:

```powershell
PowerShell -ExecutionPolicy Bypass -File "scripts\build.ps1"
```

Expected: configure succeeds, build succeeds, and CTest reports all tests passed.

## Self-Review

- Spec coverage: The plan covers the requested one-command compile path.
- Placeholder scan: No placeholders remain.
- Type consistency: Parameter names are consistent across the plan.

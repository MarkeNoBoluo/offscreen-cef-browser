# Repository Guidelines

## Project Structure & Module Organization

This repository is currently in the planning and design phase. The active files are `README.md` and the `docs/` design set: `research.md`, `architecture.md`, `implementation-plan.md`, and compatibility notes.

The planned source layout is:

- `CMakeLists.txt` and `cmake/` for the CMake project and helper modules.
- `src/app/` for application startup, `CefExecuteProcess`, CEF/Qt initialization, and shutdown.
- `src/browser/` for `BrowserService`, `BrowserClient`, CEF handlers, lifecycle, navigation, downloads, and DevTools.
- `src/qt/` for `BrowserWidget`, paint integration, DPI handling, input mapping, and IME support.
- `src/subprocess/` for the CEF subprocess executable.
- `resources/` for runtime assets and local compatibility test pages.

Do not commit large CEF binary distributions. Prefer a local `CEF_ROOT` path or an ignored `third_party/cef/` directory.

## Build, Test, and Development Commands

No buildable source or test suite exists yet. Once the engineering skeleton is added, use CMake as the baseline:

```powershell
cmake -S . -B build -G Ninja -DCEF_ROOT=C:\path\to\cef -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

Use `cmake --install build --prefix dist` after install rules are added to verify deployable runtime layout.

## Coding Style & Naming Conventions

Use C++17 or newer. Follow Qt and CEF ownership and threading conventions closely. Name classes in PascalCase, matching the design docs, for example `BrowserService`, `BrowserWidget`, and `OsrRenderHandler`. Prefer clear module-oriented filenames such as `browser_service.*` and `osr_render_handler.*`. Keep Qt UI work on the GUI thread; when CEF callbacks cross threads, dispatch explicitly with queued Qt calls.

## Testing Guidelines

Place future unit and integration tests under `tests/`. Add focused tests or manual test pages for lifecycle, OSR painting, DPI scaling, popup composition, keyboard/mouse/wheel input, Chinese IME, LocalStorage, Canvas, Video, Cookie, Fetch, WebSocket, Clipboard, Drag & Drop, and File API. Run `ctest --test-dir build --output-on-failure` once tests exist.

## Commit & Pull Request Guidelines

This repository has no commit history yet, so no existing convention can be inferred. Use short imperative commit subjects with an optional scope, for example `docs: add architecture baseline` or `build: add Qt/CEF CMake skeleton`.

Pull requests should include the purpose, changed files or modules, verification performed, and any unresolved CEF/Qt platform risks. UI-visible changes should include screenshots or a short reproduction note.

## Security & Configuration Tips

Keep remote debugging ports disabled by default in production builds. Do not commit local cache paths, logs, downloaded CEF packages, Qt binaries, certificates, or machine-specific deployment paths.

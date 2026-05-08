# KraftonEngine - Agent Context

Behavioral guidelines and project context for AI coding assistants working on this codebase.

---

## Project Overview

**KraftonEngine** is a custom C++ game engine project.

- **Architecture**: The engine is broken down into various modules (e.g., `Core`, `Render`, `Scripting`, `UI`, `Sound`, `Physics/Collision`) located under `KraftonEngine/Source/Engine`. The project also contains an `Editor`, `GameClient`, and `ObjViewer`.
- **Graphics/Renderer**: Uses its own DirectX-based renderer (`directxtk_desktop_win10` NuGet package, HLSL shaders). No external graphics libraries.
- **Dependencies**:
  - **vcpkg**: manages `luajit`, `sol2`, `rmlui` (UI framework)
  - **SFML**: `audio`, `window`, `system` only (no graphics/network)
  - **ImGui**, **SimpleJSON**: included as third-party sources
- **Project Structure**: Visual Studio project files (`.sln`, `.vcxproj`, `.vcxproj.filters`) are **auto-generated** via a custom Python script — do not edit them manually.

---

## Building and Running

All scripts are located at the repository root.

### 1. Setup Dependencies
```cmd
SetupVcpkg.bat
```
Bootstraps a local `vcpkg` installation if needed and installs manifest dependencies from `vcpkg.json`.

### 2. Generate Project Files
```cmd
GenerateProjectFiles.bat
```
Run this whenever source files, headers, or shaders are added, removed, or moved. Executes `Scripts/GenerateProjectFiles.py`, which scans `Source/`, `ThirdParty/`, and `Shaders/` to rebuild MSBuild files.

### 3. Build
Build via MSBuild using one of the provided batch scripts:

| Script | Configuration | Output |
|---|---|---|
| `DemoBuild.bat` | Demo x64 | Copies output to `DemoBuild/` |
| `ReleaseBuild.bat` | Release | Standard release build |
| `ReleaseWithObjViewerBuild.bat` | Release + ObjViewer | Includes ObjViewer |

> Toolset: Visual Studio 2022 (v143), Windows 10 SDK

---

## Development Conventions

- **Asset Pipeline**: Raw assets in `Data/` are parsed/optimized into `.bin` or custom formats stored in `Asset/` at runtime or build time.
- **Scripting**: Actor-specific gameplay logic is implemented in Lua (e.g., `PIE_AStaticMeshActor_*.lua`).
- **UI**: ImGui is used for the Editor interface and debug overlays in the game client.

---

## Coding Guidelines

Guidelines to reduce common AI coding mistakes. Bias toward caution over speed — use judgment for trivial tasks.

### 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

- State assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No unrequested "flexibility" or "configurability".
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

> Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that **your** changes made unused.
- Don't remove pre-existing dead code unless asked.

> Every changed line should trace directly to the request.

### 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- `"Add validation"` → write tests for invalid inputs, then make them pass
- `"Fix the bug"` → write a test that reproduces it, then make it pass
- `"Refactor X"` → ensure tests pass before and after

For multi-step tasks, state a plan upfront:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria allow independent looping. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** clarifying questions come before implementation, diffs have fewer unnecessary changes, and rewrites due to overcomplication decrease.

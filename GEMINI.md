# KraftonEngine - Project Context

## Project Overview

**KraftonEngine** is a custom C++ game engine project. 
- **Architecture**: The engine is broken down into various modules (e.g., `Core`, `Render`, `Scripting`, `UI`, `Sound`, `Physics/Collision`) located under `KraftonEngine/Source/Engine`. The project also contains an `Editor`, `GameClient`, and `ObjViewer`.
- **Graphics/Renderer**: It uses its own DirectX-based renderer (evidenced by the use of `directxtk_desktop_win10` NuGet package and HLSL shaders) rather than relying on external graphics libraries.
- **Dependencies**: 
  - **vcpkg** is used to manage C++ libraries: `luajit`, `sol2`, and `rmlui` (UI framework).
  - **SFML** is used for `audio`, `window`, and `system` management (no graphics/network modules are linked).
  - **ImGui** and **SimpleJSON** are included as third-party sources.
- **Project Structure**: Visual Studio project files (`.sln`, `.vcxproj`, `.vcxproj.filters`) are **auto-generated** from the directory structure using a custom Python script.

## Building and Running

<<<<<<< HEAD
The project relies on a set of batch scripts located at the repository root to streamline setup and builds.

### 1. Setup Dependencies
Before building, ensure dependencies are installed via vcpkg:
```cmd
SetupVcpkg.bat
```
This script will bootstrap a local `vcpkg` installation if necessary and install the manifest dependencies (`vcpkg.json`).

### 2. Generate Project Files
Whenever you add, remove, or move source files, headers, or shaders, you must regenerate the Visual Studio solution and project files:
```cmd
GenerateProjectFiles.bat
```
This runs `Scripts/GenerateProjectFiles.py` which scans the `Source`, `ThirdParty`, and `Shaders` directories to build the MSBuild files.

### 3. Build the Project
You can build the project by opening `KraftonEngine.sln` in Visual Studio 2022 (v143 toolset, Windows 10 SDK) or by using one of the provided batch scripts which use MSBuild:
- `DemoBuild.bat`: Builds the `Demo` x64 configuration and copies the output to a clean `DemoBuild` folder.
- `ReleaseBuild.bat`: Builds the `Release` configuration.
- `ReleaseWithObjViewerBuild.bat`: Builds the Release configuration alongside the ObjViewer.

## Development Conventions
* **Asset Pipeline:** Raw assets placed in `Data/` are likely parsed and optimized into `.bin` or custom formats stored in `Asset/` at runtime or build time.
* **Scripting:** Gameplay logic for specific actors is often implemented in Lua (e.g., `PIE_AStaticMeshActor_*.lua`).
* **UI:** The engine heavily uses ImGui for the Editor interface and potentially debug overlays in the game client.


# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
=======
The project is built using Visual Studio (MSBuild). 
>>>>>>> feature/delegate
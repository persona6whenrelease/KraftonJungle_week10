#pragma once

// RmlUi is an optional external dependency.
// Define WITH_RMLUI=1 in your build once the RmlUi headers/libs are installed.
#ifndef WITH_RMLUI
#define WITH_RMLUI 1
#endif

// Do not put Win32 macro #undefs in this file. This header is protected by
// #pragma once, so it cannot be used as a repeated macro sanitizer. Use
// RmlUiWindowsMacroGuard.h immediately before each RmlUi header include instead.

// Intentionally no #pragma once.
//
// Some Win32 helper headers, especially windowsx.h, define function-like
// macros with names used by RmlUi::Element member functions. This file must be
// included immediately before every RmlUi header include, even if it has already
// been included earlier in the same translation unit. Do not add an include
// guard or #pragma once.

#ifdef GetNextSibling
#undef GetNextSibling
#endif

#ifdef GetPrevSibling
#undef GetPrevSibling
#endif

#ifdef GetFirstChild
#undef GetFirstChild
#endif

#ifdef GetLastChild
#undef GetLastChild
#endif

#ifdef GetWindowFont
#undef GetWindowFont
#endif

// SolInclude.h
#pragma once

#ifndef SOL_ALL_SAFETIES_ON
#define SOL_ALL_SAFETIES_ON 1
#endif

#ifndef SOL_LUAJIT
#define SOL_LUAJIT 1
#endif


#ifdef check
	#pragma push_macro("check")
	#undef check
	#define KRAFTON_RESTORE_CHECK_MACRO
#endif

#ifdef checkf
	#pragma push_macro("checkf")
	#undef checkf
	#define KRAFTON_RESTORE_CHECKF_MACRO
#endif

#include <sol/sol.hpp>

#ifdef KRAFTON_RESTORE_CHECKF_MACRO
	#pragma pop_macro("checkf")
	#undef KRAFTON_RESTORE_CHECKF_MACRO
#endif

#ifdef KRAFTON_RESTORE_CHECK_MACRO
	#pragma pop_macro("check")
	#undef KRAFTON_RESTORE_CHECK_MACRO
#endif



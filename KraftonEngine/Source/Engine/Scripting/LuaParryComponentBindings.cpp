// LuaParryComponentBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"

void RegisterParryComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaParryComponentHandle>(
		"UParryComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaParryComponentHandle),

		"Parry",
		[](const FLuaParryComponentHandle& Self)
		{
			UParryComponent* Comp = Self.Resolve();
			if (!Comp)
			{
				UE_LOG("[Lua] Invalid ParryComponent.Parry Call.");
				return;
			}
			Comp->Parry();
		},

		"IsParrying",
		sol::property(
			[](const FLuaParryComponentHandle& Self) -> bool
			{
				UParryComponent* Comp = Self.Resolve();
				return Comp ? Comp->IsParrying() : false;
			}
		)
	);
}

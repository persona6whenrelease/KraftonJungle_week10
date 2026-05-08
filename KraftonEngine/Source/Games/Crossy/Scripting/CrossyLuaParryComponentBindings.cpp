// LuaParryComponentBindings.cpp

#include "Games/Crossy/Scripting/CrossyLuaBindings.h"
#include "Scripting/SolInclude.h"

#include "Scripting/LuaBindingHelper.h"
#include "Games/Crossy/Scripting/CrossyLuaHandles.h"

void RegisterCrossyParryComponentBinding(sol::state& Lua)
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

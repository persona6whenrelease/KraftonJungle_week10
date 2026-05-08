#include "Games/Crossy/Scripting/CrossyLuaBindings.h"

#include "Scripting/SolInclude.h"
#include "Scripting/LuaWorldLibrary.h"

#include "Games/Crossy/Components/HopMovementComponent.h"
#include "Games/Crossy/Components/ParryComponent.h"
#include "Games/Crossy/Components/ParryableProjectileComponent.h"

namespace
{
	void RegisterCrossyLuaComponentTypes()
	{
		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"hopmovement",
			UHopMovementComponent::StaticClass(),
			true
		);

		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"hopmovementcomponent",
			UHopMovementComponent::StaticClass(),
			true
		);

		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"parry",
			UParryComponent::StaticClass(),
			true
		);

		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"parrycomponent",
			UParryComponent::StaticClass(),
			true
		);

		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"parryableprojectile",
			UParryableProjectileComponent::StaticClass(),
			true
		);

		FLuaWorldLibrary::RegisterAllowedComponentClass(
			"parryableprojectilecomponent",
			UParryableProjectileComponent::StaticClass(),
			true
		);
	}
}

void RegisterCrossyLuaBindings(sol::state& Lua)
{
	RegisterCrossyLuaComponentTypes();

	RegisterCrossyHopMovementComponentBinding(Lua);
	RegisterCrossyParryComponentBinding(Lua);
	RegisterCrossyGameObjectComponentBindings(Lua);
	RegisterCrossyRowManagerBinding(Lua);
	RegisterCrossyUiBinding(Lua);
	RegisterCrossySaveGameBinding(Lua);
}
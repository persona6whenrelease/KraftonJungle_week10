#pragma once

#include "Scripting/LuaHandles.h"
#include "Games/Crossy/Components/HopMovementComponent.h"
#include "Games/Crossy/Components/ParryComponent.h"

struct FLuaHopMovementComponentHandle
{
	uint32 UUID = 0;

	UHopMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UHopMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaParryComponentHandle
{
	uint32 UUID = 0;

	UParryComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UParryComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

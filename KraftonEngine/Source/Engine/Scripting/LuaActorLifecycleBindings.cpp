// LuaActorLifecycleBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaHandles.h"
#include "LuaWorldLibrary.h"

#include "GameFramework/AActor.h"

void RegisterActorLifecycleBinding(sol::state& Lua)
{
	Lua.set_function(
		"SpawnActor",
		[](const FString& ClassName, const FVector& Location, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			AActor* Actor = FLuaWorldLibrary::SpawnActorByClassName(
				ClassName,
				Location
			);

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();

			return sol::make_object(LuaView, Handle);
		}
	);

	Lua.set_function(
		"SpawnStaticMeshActor",
		[](const FString& StaticMeshPath, const FVector& Location, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			AActor* Actor = FLuaWorldLibrary::SpawnStaticMeshActor(
				StaticMeshPath,
				Location
			);

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();

			return sol::make_object(LuaView, Handle);
		}
	);

	Lua.set_function(
		"AcquireActor",
		[](const FString& ClassName, const FVector& Location, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			AActor* Actor = FLuaWorldLibrary::AcquireActorByClassName(
				ClassName,
				Location
			);

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();

			return sol::make_object(LuaView, Handle);
		}
	);


	Lua.set_function(
		"AcquirePrefab",
		[](const FString& PrefabPath, const FVector& Location, sol::optional<FRotator> MaybeRotation, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			AActor* Actor = FLuaWorldLibrary::AcquirePrefab(
				PrefabPath,
				Location,
				MaybeRotation.value_or(FRotator())
			);

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();

			return sol::make_object(LuaView, Handle);
		}
	);

	Lua.set_function(
		"WarmUpActorPool",
		[](const FString& ClassName, int32 Count)
		{
			return FLuaWorldLibrary::WarmUpActorPool(ClassName, Count);
		}
	);

	Lua.set_function(
		"ReleaseActor",
		[](const FLuaGameObjectHandle& Handle)
		{
			AActor* Actor = Handle.Resolve();
			if (Actor)
			{
				FLuaWorldLibrary::ReleaseActorToPool(Actor);
			}
		}
	);

	Lua.set_function(
		"WarmUpPrefabPool",
		[](const FString& PrefabPath, int32 Count)
		{
			return FLuaWorldLibrary::WarmUpPrefabPool(PrefabPath, Count);
		}
	);
}

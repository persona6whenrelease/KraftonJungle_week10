#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaWorldLibrary.h"
#include "LuaScriptSubsystem.h"

#include "Core/Log.h"
#include "GameFramework/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CameraActor.h"
#include "Component/CameraComponent.h"
#include "Runtime/Engine.h"
#include "Serialization/PrefabSaveManager.h"

void RegisterWorldExtendedBinding(sol::state& Lua)
{
	// 기존 World 테이블이 있으면 가져오고, 없으면 생성
	sol::table World = Lua.get_or("World", Lua.create_table());
	Lua["World"] = World;

	World.set_function("SpawnPrefab",
		[](const std::string& PrefabName, sol::optional<FVector> MaybeLocation, sol::optional<FRotator> MaybeRotation, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);
			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.SpawnPrefab: No active world.");
				return sol::nil;
			}

			FVector Location = MaybeLocation.value_or(FVector(0, 0, 0));
			FRotator Rotation = MaybeRotation.value_or(FRotator());
			AActor* Actor = FPrefabSaveManager::SpawnPrefab(W, FString(PrefabName), Location, Rotation);

			if (!Actor)
			{
				UE_LOG("[Lua] World.SpawnPrefab: Failed to spawn prefab %s", PrefabName.c_str());
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();
			return sol::make_object(LuaView, Handle);
		});



	World.set_function("AcquirePrefab",
		[](const std::string& PrefabName, sol::optional<FVector> MaybeLocation, sol::optional<FRotator> MaybeRotation, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);
			FVector Location = MaybeLocation.value_or(FVector(0, 0, 0));
			FRotator Rotation = MaybeRotation.value_or(FRotator());
			AActor* Actor = FLuaWorldLibrary::AcquirePrefab(FString(PrefabName), Location, Rotation);

			if (!Actor)
			{
				UE_LOG("[Lua] World.AcquirePrefab: Failed to acquire prefab %s", PrefabName.c_str());
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	World.set_function("WarmUpPrefabPool",
		[](const std::string& PrefabName, int32 Count)
		{
			return FLuaWorldLibrary::WarmUpPrefabPool(FString(PrefabName), Count);
		});

	// World.GetPlayerController(index)
	World.set_function("GetPlayerController",
		[](int32 Index, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.GetPlayerController: No active world.");
				return sol::nil;
			}

			APlayerController* Controller = W->GetPlayerController(Index);
			if (!Controller)
			{
				return sol::nil;
			}

			FLuaPlayerControllerHandle Handle;
			Handle.UUID = Controller->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	// World.GetOrCreatePlayerController()
	World.set_function("GetOrCreatePlayerController",
		[](sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.GetOrCreatePlayerController: No active world.");
				return sol::nil;
			}

			APlayerController* Controller = W->FindOrCreatePlayerController();
			if (!Controller)
			{
				return sol::nil;
			}

			FLuaPlayerControllerHandle Handle;
			Handle.UUID = Controller->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	// World.AutoWirePlayerController([controller])
	World.set_function("AutoWirePlayerController",
		sol::overload(
			[]()
			{
				UWorld* W = FLuaWorldLibrary::GetActiveWorld();
				if (!W)
				{
					UE_LOG("[Lua] World.AutoWirePlayerController: No active world.");
					return;
				}

				W->AutoWirePlayerController();
			},
			[](const FLuaPlayerControllerHandle& ControllerHandle)
			{
				UWorld* W = FLuaWorldLibrary::GetActiveWorld();
				if (!W)
				{
					UE_LOG("[Lua] World.AutoWirePlayerController: No active world.");
					return;
				}

				W->AutoWirePlayerController(ControllerHandle.Resolve());
			}
		)
	);

	// World.SpawnPawn([location])
	World.set_function("SpawnPawn",
		[](sol::optional<FVector> MaybeLocation, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.SpawnPawn: No active world.");
				return sol::nil;
			}

			APawn* Pawn = W->SpawnActor<APawn>();
			if (!Pawn)
			{
				UE_LOG("[Lua] World.SpawnPawn: SpawnActor failed.");
				return sol::nil;
			}

			Pawn->InitDefaultComponents();

			if (MaybeLocation.has_value())
			{
				Pawn->SetActorLocation(MaybeLocation.value());
			}

			FLuaPawnHandle Handle;
			Handle.UUID = Pawn->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	World.set_function("SpawnCamera",
		[](sol::optional<FVector> MaybeLocation, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);
			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.SpawnCamera: No active world.");
				return sol::nil;
			}
			ACameraActor* CameraActor = W->SpawnActor<ACameraActor>();
			if (!CameraActor)
			{
				UE_LOG("[Lua] World.SpawnCamera: SpawnActor failed.");
				return sol::nil;
			}
			CameraActor->InitDefaultComponents();
			if (MaybeLocation.has_value())
			{
				CameraActor->SetActorLocation(MaybeLocation.value());
			}
			FLuaGameObjectHandle Handle;
			Handle.UUID = CameraActor->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	World.set_function("SpawnPlayerController",
		[](sol::optional<FVector> MaybeLocation, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);
			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.SpawnPlayerController: No active world.");
				return sol::nil;
			}
			APlayerController* Controller = W->CreatePlayerController();
			if (!Controller)
			{
				UE_LOG("[Lua] World.SpawnPlayerController: SpawnActor failed.");
				return sol::nil;
			}
			if (MaybeLocation.has_value())
			{
				Controller->SetActorLocation(MaybeLocation.value());
			}
			FLuaPlayerControllerHandle Handle;
			Handle.UUID = Controller->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	// World.GetActiveCamera()
	World.set_function("GetActiveCamera",
		[](sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W) return sol::nil;

			UCameraComponent* Camera = W->GetActiveCamera();
			if (!Camera) return sol::nil;

			FLuaCameraComponentHandle Handle;
			Handle.UUID = Camera->GetUUID();
			return sol::make_object(LuaView, Handle);
		});

	// World.SetActiveCamera(cameraHandle)
	World.set_function("SetActiveCamera",
		[](const FLuaCameraComponentHandle& CameraHandle)
		{
			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W)
			{
				UE_LOG("[Lua] World.SetActiveCamera: No active world.");
				return;
			}
			UCameraComponent* Camera = CameraHandle.Resolve();
			W->SetActiveCamera(Camera);
		});

	// World.FindActorByName(name)
	World.set_function("FindActorByName",
		[](const FString& Name, sol::this_state TS) -> sol::object
		{
			sol::state_view LuaView(TS);

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W) return sol::nil;

			for (AActor* Actor : W->GetActors())
			{
				if (!Actor) continue;
				if (Actor->GetFName().ToString() == Name)
				{
					FLuaGameObjectHandle Handle;
					Handle.UUID = Actor->GetUUID();
					return sol::make_object(LuaView, Handle);
				}
			}
			return sol::nil;
		});

	// World.FindActorsByTag(tag) — 이름 기반으로 다수 액터 반환
	World.set_function("FindActorsByTag",
		[](const FString& Tag, sol::this_state TS) -> sol::table
		{
			sol::state_view LuaView(TS);
			sol::table Result = LuaView.create_table();

			UWorld* W = FLuaWorldLibrary::GetActiveWorld();
			if (!W) return Result;

			int LuaIndex = 1;
			for (AActor* Actor : W->GetActors())
			{
				if (!Actor) continue;
				if (Actor->GetFName().ToString() == Tag)
				{
					FLuaGameObjectHandle Handle;
					Handle.UUID = Actor->GetUUID();
					Result[LuaIndex++] = Handle;
				}
			}
			return Result;
		});

	// Game 테이블
	sol::table Game = Lua.get_or("Game", Lua.create_table());
	Lua["Game"] = Game;

	Game.set_function("DispatchEvent",
		sol::overload(
			[](const FString& EventName)
			{
				return FLuaScriptSubsystem::Get().DispatchGameEvent(EventName);
			},
			[](const FString& EventName, const FLuaGameObjectHandle& Instigator)
			{
				return FLuaScriptSubsystem::Get().DispatchGameEvent(EventName, Instigator.Resolve());
			}
		));

	Game.set_function("Restart", []()
	{
		if (!GEngine)
		{
			UE_LOG("[Lua] Game.Restart() failed: GEngine is null.");
			return;
		}

		GEngine->RequestRestart();
	});

	Game.set_function("Exit", []()
	{
		if (!GEngine)
		{
			UE_LOG("[Lua] Game.Exit() failed: GEngine is null.");
			return;
		}

		GEngine->RequestExit();
	});

	Game.set_function("Quit", []()
	{
		if (!GEngine)
		{
			UE_LOG("[Lua] Game.Quit() failed: GEngine is null.");
			return;
		}

		GEngine->RequestExit();
	});
}

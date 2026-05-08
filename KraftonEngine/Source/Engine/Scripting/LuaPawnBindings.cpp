#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaBindingHelper.h"
#include "LuaWorldLibrary.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

void RegisterPawnBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaPawnHandle>(
		"Pawn",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaPawnHandle),

		"Location",
		sol::property(
			[](const FLuaPawnHandle& Self) -> FVector
			{
				APawn* P = Self.Resolve();
				return P ? P->GetActorLocation() : FVector::ZeroVector;
			},
			[](const FLuaPawnHandle& Self, const FVector& V)
			{
				APawn* P = Self.Resolve();
				if (!P)
				{
					UE_LOG("[Lua] Invalid Pawn.Location Access.");
					return;
				}
				P->SetActorLocation(V);
			}
		),

		"Rotation",
		sol::property(
			[](const FLuaPawnHandle& Self) -> FRotator
			{
				APawn* P = Self.Resolve();
				return P ? P->GetActorRotation() : FRotator();
			},
			[](const FLuaPawnHandle& Self, const FRotator& R)
			{
				APawn* P = Self.Resolve();
				if (!P)
				{
					UE_LOG("[Lua] Invalid Pawn.Rotation Access.");
					return;
				}
				P->SetActorRotation(R);
			}
		),

		"ForwardVector",
		sol::property(
			[](const FLuaPawnHandle& Self) -> FVector
			{
				APawn* P = Self.Resolve();
				return P ? P->GetActorForward() : FVector(1.0f, 0.0f, 0.0f);
			}
		),

		"RightVector",
		sol::property(
			[](const FLuaPawnHandle& Self) -> FVector
			{
				APawn* P = Self.Resolve();
				if (!P) return FVector(0.0f, 1.0f, 0.0f);
				USceneComponent* Root = P->GetRootComponent();
				return Root ? Root->GetRightVector() : FVector(0.0f, 1.0f, 0.0f);
			}
		),

		"UpVector",
		sol::property(
			[](const FLuaPawnHandle& Self) -> FVector
			{
				APawn* P = Self.Resolve();
				if (!P) return FVector(0.0f, 0.0f, 1.0f);
				USceneComponent* Root = P->GetRootComponent();
				return Root ? Root->GetUpVector() : FVector(0.0f, 0.0f, 1.0f);
			}
		),

		"AddMovementInput",
		[](const FLuaPawnHandle& Self, const FVector& Direction, float Scale)
		{
			APawn* P = Self.Resolve();
			if (!P)
			{
				UE_LOG("[Lua] Invalid Pawn.AddMovementInput Call.");
				return;
			}
			P->AddMovementInput(Direction, Scale);
		},

		"ConsumeMovementInputVector",
		[](const FLuaPawnHandle& Self) -> FVector
		{
			APawn* P = Self.Resolve();
			if (!P)
			{
				UE_LOG("[Lua] Invalid Pawn.ConsumeMovementInputVector Call.");
				return FVector::ZeroVector;
			}
			return P->ConsumeMovementInputVector();
		},

		"GetOrAddCamera",
		[](const FLuaPawnHandle& Self) -> FLuaCameraComponentHandle
		{
			FLuaCameraComponentHandle Handle;
			APawn* P = Self.Resolve();
			if (!P)
			{
				UE_LOG("[Lua] Invalid Pawn.GetOrAddCamera Call.");
				return Handle;
			}

			UCameraComponent* Camera = FLuaWorldLibrary::GetOrAddComponent<UCameraComponent>(P);
			if (Camera)
			{
				Handle.UUID = Camera->GetUUID();
			}
			return Handle;
		},

		"GetController",
		[](const FLuaPawnHandle& Self) -> FLuaPlayerControllerHandle
		{
			FLuaPlayerControllerHandle Handle;
			APawn* P = Self.Resolve();
			if (!P) return Handle;
			APlayerController* Controller = P->GetController();
			if (Controller)
			{
				Handle.UUID = Controller->GetUUID();
			}
			return Handle;
		},

		"IsPossessed",
		[](const FLuaPawnHandle& Self) -> bool
		{
			APawn* P = Self.Resolve();
			return P ? P->IsPossessed() : false;
		},

		"Destroy",
		[](const FLuaPawnHandle& Self) -> bool
		{
			APawn* P = Self.Resolve();
			if (!P)
			{
				UE_LOG("[Lua] Invalid Pawn.Destroy Call.");
				return false;
			}
			return FLuaWorldLibrary::DestroyActor(P);
		}
	);
}

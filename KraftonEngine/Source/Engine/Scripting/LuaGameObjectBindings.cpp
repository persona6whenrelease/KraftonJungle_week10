// LuaGameObjectBindings.cpp

#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"
#include "LuaWorldLibrary.h"

#include "Core/Log.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

#include "Object/Object.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include "Component/StaticMeshComponent.h"
#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/PawnOrientationComponent.h"

#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/SphereComponent.h"

#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/HopMovementComponent.h"
#include "Component/ParryComponent.h"

#ifndef LUA_ENABLE_DEBUG_UUID_LOOKUP
#define LUA_ENABLE_DEBUG_UUID_LOOKUP 0
#endif

namespace
{
	uint32 GetOwnerUUIDFromEnvironment(sol::this_environment ThisEnv)
	{
		sol::environment Env = ThisEnv;
		if (!Env.valid())
		{
			return 0;
		}

		sol::object OwnerUUID = Env["_ownerUUID"];
		if (!OwnerUUID.valid() || OwnerUUID.get_type() != sol::type::number)
		{
			return 0;
		}

		return OwnerUUID.as<uint32>();
	}
}

// Lua에 GameObject를 등록
// Handle을 통해 UUID로만 접근 가능
void RegisterGameObjectBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaGameObjectHandle>(
		"GameObject",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaGameObjectHandle),

		"Name",
		sol::property(
			[](const FLuaGameObjectHandle& Self)
			{
				AActor* Actor = Self.Resolve();
				return Actor ? Actor->GetFName().ToString() : FString();
			},
			[](const FLuaGameObjectHandle& Self, const FString& Name)
			{
				AActor* Actor = Self.Resolve();
				if (!Actor)
				{
					UE_LOG("[Lua] Invalid GameObject.Name Access.");
					return;
				}

				Actor->SetFName(FName(Name));
			}
		),

		"Location",
		sol::property(
			[](const FLuaGameObjectHandle& Self)
			{
				AActor* Actor = Self.Resolve();
				return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
			},
			[](const FLuaGameObjectHandle& Self, const FVector& Location)
			{
				AActor* Actor = Self.Resolve();

				if (!Actor)
				{
					UE_LOG("[Lua] Invalid GameObject.Location Access.");
					return;
				}

				Actor->SetActorLocation(Location);
			}
		),

		"Rotation",
		sol::property(
			[](const FLuaGameObjectHandle& Self)
			{
				AActor* Actor = Self.Resolve();
				return Actor ? Actor->GetActorRotation() : FRotator();
			},
			[](const FLuaGameObjectHandle& Self, const FRotator& Rotation)
			{
				AActor* Actor = Self.Resolve();

				if (!Actor)
				{
					UE_LOG("[Lua] Invalid GameObject.Rotation Access.");
					return;
				}

				Actor->SetActorRotation(Rotation);
			}
		),

		"Scale",
		sol::property(
			[](const FLuaGameObjectHandle& Self)
			{
				AActor* Actor = Self.Resolve();
				return Actor ? Actor->GetActorScale() : FVector::OneVector;
			},
			[](const FLuaGameObjectHandle& Self, const FVector& Scale)
			{
				AActor* Actor = Self.Resolve();

				if (!Actor)
				{
					UE_LOG("[Lua] Invalid GameObject.Scale Access.");
					return;
				}

				Actor->SetActorScale(Scale);
			}
		),

		"Visible",
		sol::property(
			[](const FLuaGameObjectHandle& Self)
			{
				AActor* Actor = Self.Resolve();
				return Actor ? Actor->IsVisible() : false;
			},
			[](const FLuaGameObjectHandle& Self, bool bVisible)
			{
				AActor* Actor = Self.Resolve();
				if (!Actor)
				{
					UE_LOG("[Lua] Invalid GameObject.Visible Access.");
					return;
				}
				Actor->SetVisible(bVisible);
			}
		),

		"Destroy",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.Destroy Call.");
				return false;
			}

			return FLuaWorldLibrary::DestroyActor(Actor);
		},

		"ReleaseToPool",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.ReleaseToPool Call.");
				return false;
			}

			return FLuaWorldLibrary::ReleaseActorToPool(Actor);
		},

		"ReleaseToPool",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.ReleaseToPool Call.");
				return false;
			}

			return FLuaWorldLibrary::ReleaseActorToPool(Actor);
		},

		"AsPawn",
		[](const FLuaGameObjectHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			APawn* Pawn = Cast<APawn>(Self.Resolve());
			if (!Pawn)
			{
				return sol::nil;
			}

			FLuaPawnHandle Handle;
			Handle.UUID = Pawn->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"AsPlayerController",
		[](const FLuaGameObjectHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			APlayerController* Controller = Cast<APlayerController>(Self.Resolve());
			if (!Controller)
			{
				return sol::nil;
			}

			FLuaPlayerControllerHandle Handle;
			Handle.UUID = Controller->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"LuaScript",
			FLuaScriptComponentHandle,
			ULuaScriptComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddLuaScript",
			FLuaScriptComponentHandle,
			ULuaScriptComponent
		),

		"GetLuaScript",
		[](const FLuaGameObjectHandle& Self, const FString& ScriptIdentifier)
		{
			FLuaScriptComponentHandle Handle;

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetLuaScript Call.");
				return Handle;
			}

			ULuaScriptComponent* Component = FLuaWorldLibrary::FindLuaScriptComponent(Actor, ScriptIdentifier);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}

			return Handle;
		},

		"GetLuaScripts",
		[](const FLuaGameObjectHandle& Self, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table Result = LuaView.create_table();

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				return Result;
			}

			int LuaIndex = 1;
			for (ULuaScriptComponent* Component : FLuaWorldLibrary::FindComponents<ULuaScriptComponent>(Actor))
			{
				FLuaScriptComponentHandle Handle;
				Handle.UUID = Component->GetUUID();
				Result[LuaIndex++] = Handle;
			}

			return Result;
		},

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveLuaScript",
			ULuaScriptComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"Camera",
			FLuaCameraComponentHandle,
			UCameraComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddCamera",
			FLuaCameraComponentHandle,
			UCameraComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveCamera",
			UCameraComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"PawnOrientation",
			FLuaPawnOrientationComponentHandle,
			UPawnOrientationComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddPawnOrientation",
			FLuaPawnOrientationComponentHandle,
			UPawnOrientationComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemovePawnOrientation",
			UPawnOrientationComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"Shape",
			FLuaShapeComponentHandle,
			UShapeComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"ProjectileMovement",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"InterpMovement",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"PendulumMovement",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"RotatingMovement",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"HopMovement",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"Parry",
			FLuaParryComponentHandle,
			UParryComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddParry",
			FLuaParryComponentHandle,
			UParryComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddProjectileMovement",
			FLuaProjectileMovementComponentHandle,
			UProjectileMovementComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddInterpMovement",
			FLuaInterpToMoveComponentHandle,
			UInterpToMovementComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddPendulumMovement",
			FLuaPendulumMovementComponentHandle,
			UPendulumMovementComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddRotatingMovement",
			FLuaRotatingMovementComponentHandle,
			URotatingMovementComponent
		),

		LUA_GAMEOBJECT_GET_OR_ADD_COMPONENT_METHOD(
			"GetOrAddHopMovement",
			FLuaHopMovementComponentHandle,
			UHopMovementComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveProjectileMovement",
			UProjectileMovementComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveInterpMovement",
			UInterpToMovementComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemovePendulumMovement",
			UPendulumMovementComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveRotatingMovement",
			URotatingMovementComponent
		),

		LUA_GAMEOBJECT_REMOVE_COMPONENT_METHOD(
			"RemoveHopMovement",
			UHopMovementComponent
		),

		LUA_GAMEOBJECT_SET_SHAPE_METHOD(
			"SetBoxShape",
			FLuaBoxComponentHandle,
			UBoxComponent
		),

		LUA_GAMEOBJECT_SET_SHAPE_METHOD(
			"SetSphereShape",
			FLuaSphereComponentHandle,
			USphereComponent
		),

		LUA_GAMEOBJECT_SET_SHAPE_METHOD(
			"SetCapsuleShape",
			FLuaCapsuleComponentHandle,
			UCapsuleComponent
		),

		LUA_GAMEOBJECT_COMPONENT_PROPERTY(
			"StaticMesh",
			FLuaStaticMeshComponentHandle,
			UStaticMeshComponent
		),

		"GetOrAddStaticMesh",
		[](const FLuaGameObjectHandle& Self)
		{
			FLuaStaticMeshComponentHandle Handle;

			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetOrAddStaticMesh Call.");
				return Handle;
			}

			UStaticMeshComponent* Mesh =
			FLuaWorldLibrary::GetOrAddStaticMeshComponent(Actor);

			if (Mesh)
			{
				Handle.UUID = Mesh->GetUUID();
			}

			return Handle;
		},

		"SetStaticMesh",
		[](const FLuaGameObjectHandle& Self, const FString& ObjPath)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.SetStaticMesh Call.");
				return false;
			}

			UStaticMeshComponent* Mesh =
			FLuaWorldLibrary::GetOrAddStaticMeshComponent(Actor);

			return FLuaWorldLibrary::SetStaticMesh(Mesh, ObjPath);
		},


		"SetMaterial",
		[](const FLuaGameObjectHandle& Self, int32 ElementIndex, const FString& MaterialPath)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.SetMaterial Call.");
				return false;
			}

			UStaticMeshComponent* Mesh =
			FLuaWorldLibrary::GetOrAddStaticMeshComponent(Actor);

			return FLuaWorldLibrary::SetMaterial(Mesh, ElementIndex, MaterialPath);
		},

		"SetMaterialColor",
		[](const FLuaGameObjectHandle& Self, int32 ElementIndex, const FString& ParamName, const FVector4& Color)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.SetMaterialColor Call.");
				return false;
			}

			UStaticMeshComponent* Mesh =
			FLuaWorldLibrary::GetOrAddStaticMeshComponent(Actor);

			return FLuaWorldLibrary::SetMaterialVector4Parameter(
				Mesh,
				ElementIndex,
				ParamName,
				Color
			);
		},

		"GetComponent",
		[](const FLuaGameObjectHandle& Self, const FString& TypeName)
		{
			FLuaActorComponentHandle Handle;

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetComponent Call.");
				return Handle;
			}

			UActorComponent* Component = FLuaWorldLibrary::FindComponentByTypeName(Actor, TypeName);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}

			return Handle;
		},

		"GetComponentByIndex",
		[](const FLuaGameObjectHandle& Self, const FString& TypeName, int32 LuaIndex)
		{
			FLuaActorComponentHandle Handle;

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetComponentByIndex Call.");
				return Handle;
			}

			UActorComponent* Component = FLuaWorldLibrary::FindComponentByTypeName(Actor, TypeName, LuaIndex - 1);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}

			return Handle;
		},

		"GetComponents",
		[](const FLuaGameObjectHandle& Self, const FString& TypeName, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table Result = LuaView.create_table();

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				return Result;
			}

			int LuaIndex = 1;
			for (UActorComponent* Component : FLuaWorldLibrary::FindComponentsByTypeName(Actor, TypeName))
			{
				FLuaActorComponentHandle Handle;
				Handle.UUID = Component->GetUUID();
				Result[LuaIndex++] = Handle;
			}

			return Result;
		},

		"GetOrAddComponent",
		[](const FLuaGameObjectHandle& Self, const FString& TypeName)
		{
			FLuaActorComponentHandle Handle;

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.GetOrAddComponent Call.");
				return Handle;
			}

			UActorComponent* Component = FLuaWorldLibrary::GetOrAddComponentByTypeName(Actor, TypeName);
			if (Component)
			{
				Handle.UUID = Component->GetUUID();
			}

			return Handle;
		},

		"RemoveComponent",
		[](const FLuaGameObjectHandle& Self, const FString& TypeName)
		{
			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.RemoveComponent Call.");
				return false;
			}

			return FLuaWorldLibrary::RemoveComponentByTypeName(Actor, TypeName);
		},

		"ListComponents",
		[](const FLuaGameObjectHandle& Self, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table Result = LuaView.create_table();

			AActor* Actor = Self.Resolve();
			if (!Actor)
			{
				return Result;
			}

			int LuaIndex = 1;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (!Component)
				{
					continue;
				}

				FLuaActorComponentHandle Handle;
				Handle.UUID = Component->GetUUID();
				Result[LuaIndex++] = Handle;
			}

			return Result;
		},

		"RemoveShape",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.RemoveShape Call.");
				return false;
			}

			return FLuaWorldLibrary::RemoveShape(Actor);
		},

		"PrintLocation",
		[](const FLuaGameObjectHandle& Self)
		{
			AActor* Actor = Self.Resolve();

			if (!Actor)
			{
				UE_LOG("[Lua] Invalid GameObject.PrintLocation Access.");
				return;
			}

			const FVector Location = Actor->GetActorLocation();

			UE_LOG(
				"[Lua] GameObject UUID=%u, Location=(%.3f, %.3f, %.3f)",
				Actor->GetUUID(),
				Location.X,
				Location.Y,
				Location.Z
			);
		}
	);

#if LUA_ENABLE_DEBUG_UUID_LOOKUP
	Lua.set_function(
		"FindGameObjectByUUID",
		[](uint32 UUID, sol::this_environment ThisEnv, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			const uint32 OwnerUUID = GetOwnerUUIDFromEnvironment(ThisEnv);
			if (OwnerUUID == 0 || OwnerUUID != UUID)
			{
				UE_LOG(
					"[LuaSecurity] FindGameObjectByUUID blocked. UUID=%u OwnerUUID=%u",
					UUID,
					OwnerUUID
				);
				return sol::nil;
			}

			UObject* Object = UObjectManager::Get().FindByUUID(UUID);
			AActor* Actor = Cast<AActor>(Object);

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();

			return sol::make_object(LuaView, Handle);
		}
	);
#endif
}

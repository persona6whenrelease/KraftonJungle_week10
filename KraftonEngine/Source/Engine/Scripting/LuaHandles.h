#pragma once

#include "Component/StaticMeshComponent.h"
#include "Component/ActorComponent.h"
#include "Component/CameraComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/HopMovementComponent.h"
#include "Component/ParryComponent.h"
#include "Core/CoreTypes.h"
#include "Object/Object.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Component/ControllerInputComponent.h"
#include "Component/PawnOrientationComponent.h"

// Lua가 Object에 직접 접근할 수 없도록 감쌈
// nullptr일 경우를 대비
struct FLuaGameObjectHandle
{
	uint32 UUID = 0;

	AActor* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<AActor>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaActorComponentHandle
{
	uint32 UUID = 0;

	UActorComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UActorComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaScriptComponentHandle
{
	uint32 UUID = 0;

	ULuaScriptComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<ULuaScriptComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaSceneComponentHandle
{
	uint32 UUID = 0;

	USceneComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<USceneComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaPrimitiveComponentHandle
{
	uint32 UUID = 0;

	UPrimitiveComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UPrimitiveComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaMovementComponentHandle
{
	uint32 UUID = 0;

	UMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaProjectileMovementComponentHandle
{
	uint32 UUID = 0;

	UProjectileMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UProjectileMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaInterpToMoveComponentHandle
{
	uint32 UUID = 0;

	UInterpToMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UInterpToMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaPendulumMovementComponentHandle
{
	uint32 UUID = 0;

	UPendulumMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UPendulumMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaRotatingMovementComponentHandle
{
	uint32 UUID = 0;

	URotatingMovementComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<URotatingMovementComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

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

// Lua가 Shape에 직접 접근할 수 없도록 감쌈
// nullptr일 경우를 대비
struct FLuaShapeComponentHandle
{
	uint32 UUID = 0;

	UShapeComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UShapeComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaBoxComponentHandle
{
	uint32 UUID = 0;

	UBoxComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UBoxComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaSphereComponentHandle
{
	uint32 UUID = 0;

	USphereComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<USphereComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaCapsuleComponentHandle
{
	uint32 UUID = 0;

	UCapsuleComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UCapsuleComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaStaticMeshComponentHandle
{
	uint32 UUID = 0;

	UStaticMeshComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UStaticMeshComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaControllerInputComponentHandle
{
	uint32 UUID = 0;

	UControllerInputComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UControllerInputComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaCameraComponentHandle
{
	uint32 UUID = 0;

	UCameraComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UCameraComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaPawnOrientationComponentHandle
{
	uint32 UUID = 0;

	UPawnOrientationComponent* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<UPawnOrientationComponent>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaPawnHandle
{
	uint32 UUID = 0;

	APawn* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<APawn>(Object);
	}

	bool IsValid() const
	{
		return Resolve() != nullptr;
	}
};

struct FLuaPlayerControllerHandle
{
	uint32 UUID = 0;

	APlayerController* Resolve() const
	{
		UObject* Object = UObjectManager::Get().FindByUUID(UUID);
		return Cast<APlayerController>(Object);
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

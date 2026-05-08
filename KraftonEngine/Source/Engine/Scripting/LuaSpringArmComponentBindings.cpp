#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaBindingHelper.h"
#include "LuaPropertyBridge.h"

#include "Component/SpringArmComponent.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

namespace
{
	FLuaActorComponentHandle MakeActorComponentHandle(UActorComponent* Component)
	{
		FLuaActorComponentHandle Handle;
		if (Component)
		{
			Handle.UUID = Component->GetUUID();
		}
		return Handle;
	}

	FLuaSceneComponentHandle MakeSceneComponentHandle(USceneComponent* Component)
	{
		FLuaSceneComponentHandle Handle;
		if (Component)
		{
			Handle.UUID = Component->GetUUID();
		}
		return Handle;
	}
}

void RegisterSpringArmComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaSpringArmComponentHandle>(
		"SpringArmComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaSpringArmComponentHandle),

		"TargetArmLength",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetTargetArmLength() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.TargetArmLength Access.");
					return;
				}
				Component->SetTargetArmLength(Value);
			}
		),

		"TargetOffset",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FVector
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetTargetOffset() : FVector::ZeroVector;
			},
			[](const FLuaSpringArmComponentHandle& Self, const FVector& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.TargetOffset Access.");
					return;
				}
				Component->SetTargetOffset(Value);
			}
		),

		"SocketOffset",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FVector
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetSocketOffset() : FVector::ZeroVector;
			},
			[](const FLuaSpringArmComponentHandle& Self, const FVector& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.SocketOffset Access.");
					return;
				}
				Component->SetSocketOffset(Value);
			}
		),

		"DoCollisionTest",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->IsCollisionTestEnabled() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.DoCollisionTest Access.");
					return;
				}
				Component->SetDoCollisionTest(bValue);
			}
		),

		"CollisionTestEnabled",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->IsCollisionTestEnabled() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetDoCollisionTest(bValue);
				}
			}
		),

		"ProbeSize",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetProbeSize() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.ProbeSize Access.");
					return;
				}
				Component->SetProbeSize(Value);
			}
		),

		"MinArmLength",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetMinArmLength() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.MinArmLength Access.");
					return;
				}
				Component->SetMinArmLength(Value);
			}
		),

		"UsePawnControlRotation",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->UsesPawnControlRotation() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.UsePawnControlRotation Access.");
					return;
				}
				Component->SetUsePawnControlRotation(bValue);
			}
		),

		"InheritPitch",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->InheritsPitch() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetInheritPitch(bValue);
				}
			}
		),

		"InheritYaw",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->InheritsYaw() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetInheritYaw(bValue);
				}
			}
		),

		"InheritRoll",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->InheritsRoll() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetInheritRoll(bValue);
				}
			}
		),

		"EnableCameraLag",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->IsCameraLagEnabled() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.EnableCameraLag Access.");
					return;
				}
				Component->SetEnableCameraLag(bValue);
			}
		),

		"CameraLagEnabled",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->IsCameraLagEnabled() : false;
			},
			[](const FLuaSpringArmComponentHandle& Self, bool bValue)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetEnableCameraLag(bValue);
				}
			}
		),

		"CameraLagSpeed",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetCameraLagSpeed() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.CameraLagSpeed Access.");
					return;
				}
				Component->SetCameraLagSpeed(Value);
			}
		),

		"CollisionPullInSpeed",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetCollisionPullInSpeed() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.CollisionPullInSpeed Access.");
					return;
				}
				Component->SetCollisionPullInSpeed(Value);
			}
		),

		"CollisionRecoverSpeed",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetCollisionRecoverSpeed() : 0.0f;
			},
			[](const FLuaSpringArmComponentHandle& Self, float Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.CollisionRecoverSpeed Access.");
					return;
				}
				Component->SetCollisionRecoverSpeed(Value);
			}
		),

		"CurrentArmLength",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> float
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetCurrentArmLength() : 0.0f;
			}
		),

		"CollisionFixApplied",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> bool
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->IsCollisionFixApplied() : false;
			}
		),

		"RelativeLocation",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FVector
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetRelativeLocation() : FVector::ZeroVector;
			},
			[](const FLuaSpringArmComponentHandle& Self, const FVector& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetRelativeLocation(Value);
				}
			}
		),

		"RelativeRotation",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FRotator
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetRelativeRotation() : FRotator();
			},
			[](const FLuaSpringArmComponentHandle& Self, const FRotator& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetRelativeRotation(Value);
				}
			}
		),

		"WorldLocation",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FVector
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetWorldLocation() : FVector::ZeroVector;
			},
			[](const FLuaSpringArmComponentHandle& Self, const FVector& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetWorldLocation(Value);
				}
			}
		),

		"WorldRotation",
		sol::property(
			[](const FLuaSpringArmComponentHandle& Self) -> FRotator
			{
				USpringArmComponent* Component = Self.Resolve();
				return Component ? Component->GetWorldRotation() : FRotator();
			},
			[](const FLuaSpringArmComponentHandle& Self, const FRotator& Value)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetWorldRotation(Value);
				}
			}
		),

		"RefreshCameraTransform",
		sol::overload(
			[](const FLuaSpringArmComponentHandle& Self)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.RefreshCameraTransform Call.");
					return false;
				}
				Component->RefreshCameraTransform(0.0f);
				return true;
			},
			[](const FLuaSpringArmComponentHandle& Self, float DeltaTime)
			{
				USpringArmComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SpringArmComponent.RefreshCameraTransform Call.");
					return false;
				}
				Component->RefreshCameraTransform(DeltaTime);
				return true;
			}
		),

		"AttachCamera",
		[](const FLuaSpringArmComponentHandle& Self, const FLuaCameraComponentHandle& CameraHandle)
		{
			USpringArmComponent* Component = Self.Resolve();
			UCameraComponent* Camera = CameraHandle.Resolve();
			if (!Component || !Camera)
			{
				UE_LOG("[Lua] Invalid SpringArmComponent.AttachCamera Call.");
				return false;
			}
			Camera->AttachToComponent(Component);
			Component->RefreshCameraTransform(0.0f);
			return true;
		},

		"AsComponent",
		[](const FLuaSpringArmComponentHandle& Self)
		{
			return MakeActorComponentHandle(Self.Resolve());
		},

		"AsScene",
		[](const FLuaSpringArmComponentHandle& Self)
		{
			return MakeSceneComponentHandle(Self.Resolve());
		},

		"ListProperties",
		[](const FLuaSpringArmComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"HasProperty",
		[](const FLuaSpringArmComponentHandle& Self, const FString& Name)
		{
			return FLuaPropertyBridge::HasProperty(Self.Resolve(), Name);
		},

		"GetProperty",
		[](const FLuaSpringArmComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaSpringArmComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);
}

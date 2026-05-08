#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"
#include "LuaPropertyBridge.h"

#include "Core/Log.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

#include "GameFramework/AActor.h"
#include "Component/ActorComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"

namespace
{
	FLuaGameObjectHandle MakeGameObjectHandle(AActor* Actor)
	{
		FLuaGameObjectHandle Handle;
		if (Actor)
		{
			Handle.UUID = Actor->GetUUID();
		}
		return Handle;
	}

	FLuaActorComponentHandle MakeActorComponentHandle(UActorComponent* Component)
	{
		FLuaActorComponentHandle Handle;
		if (Component)
		{
			Handle.UUID = Component->GetUUID();
		}
		return Handle;
	}
}

void RegisterActorComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaActorComponentHandle>(
		"Component",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaActorComponentHandle),

		"ClassName",
		sol::property(
			[](const FLuaActorComponentHandle& Self)
			{
				UActorComponent* Component = Self.Resolve();
				return Component && Component->GetClass()
					? FString(Component->GetClass()->GetName())
					: FString("None");
			}
		),

		"Owner",
		sol::property(
			[](const FLuaActorComponentHandle& Self)
			{
				UActorComponent* Component = Self.Resolve();
				return MakeGameObjectHandle(Component ? Component->GetOwner() : nullptr);
			}
		),

		"Active",
		sol::property(
			[](const FLuaActorComponentHandle& Self)
			{
				UActorComponent* Component = Self.Resolve();
				return Component ? Component->IsActive() : false;
			},
			[](const FLuaActorComponentHandle& Self, bool bActive)
			{
				UActorComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid Component.Active Access.");
					return;
				}
				Component->SetActive(bActive);
			}
		),

		"TickEnabled",
		sol::property(
			[](const FLuaActorComponentHandle& Self)
			{
				UActorComponent* Component = Self.Resolve();
				return Component ? Component->PrimaryComponentTick.bTickEnabled : false;
			},
			[](const FLuaActorComponentHandle& Self, bool bEnabled)
			{
				UActorComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid Component.TickEnabled Access.");
					return;
				}
				Component->SetComponentTickEnabled(bEnabled);
			}
		),

		"ListProperties",
		[](const FLuaActorComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"HasProperty",
		[](const FLuaActorComponentHandle& Self, const FString& Name)
		{
			return FLuaPropertyBridge::HasProperty(Self.Resolve(), Name);
		},

		"GetProperty",
		[](const FLuaActorComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaActorComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		},

		"AsScene",
		[](const FLuaActorComponentHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);
			USceneComponent* Scene = Cast<USceneComponent>(Self.Resolve());
			if (!Scene)
			{
				return sol::nil;
			}
			FLuaSceneComponentHandle Handle;
			Handle.UUID = Scene->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"AsLuaScript",
		[](const FLuaActorComponentHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);
			ULuaScriptComponent* Script = Cast<ULuaScriptComponent>(Self.Resolve());
			if (!Script)
			{
				return sol::nil;
			}
			FLuaScriptComponentHandle Handle;
			Handle.UUID = Script->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"AsPrimitive",
		[](const FLuaActorComponentHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Self.Resolve());
			if (!Primitive)
			{
				return sol::nil;
			}
			FLuaPrimitiveComponentHandle Handle;
			Handle.UUID = Primitive->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"AsStaticMesh",
		[](const FLuaActorComponentHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);
			UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Self.Resolve());
			if (!Mesh)
			{
				return sol::nil;
			}
			FLuaStaticMeshComponentHandle Handle;
			Handle.UUID = Mesh->GetUUID();
			return sol::make_object(LuaView, Handle);
		}
	);
}

void RegisterLuaScriptComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaScriptComponentHandle>(
		"LuaScriptComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaScriptComponentHandle),

		"Owner",
		sol::property(
			[](const FLuaScriptComponentHandle& Self)
			{
				ULuaScriptComponent* Component = Self.Resolve();
				return MakeGameObjectHandle(Component ? Component->GetOwner() : nullptr);
			}
		),

		"ScriptPath",
		sol::property(
			[](const FLuaScriptComponentHandle& Self)
			{
				ULuaScriptComponent* Component = Self.Resolve();
				return Component ? Component->GetScriptPath() : FString();
			},
			[](const FLuaScriptComponentHandle& Self, const FString& Path)
			{
				ULuaScriptComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid LuaScriptComponent.ScriptPath Access.");
					return;
				}
				Component->SetScriptPath(Path);
			}
		),

		"Reload",
		[](const FLuaScriptComponentHandle& Self)
		{
			ULuaScriptComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid LuaScriptComponent.Reload Call.");
				return false;
			}
			return Component->ReloadScript();
		},

		"AsComponent",
		[](const FLuaScriptComponentHandle& Self)
		{
			return MakeActorComponentHandle(Self.Resolve());
		},

		"ListProperties",
		[](const FLuaScriptComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"GetProperty",
		[](const FLuaScriptComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaScriptComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);
}

void RegisterSceneComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaSceneComponentHandle>(
		"SceneComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaSceneComponentHandle),

		"RelativeLocation",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetRelativeLocation() : FVector::ZeroVector;
			},
			[](const FLuaSceneComponentHandle& Self, const FVector& Value)
			{
				USceneComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetRelativeLocation(Value);
				}
			}
		),

		"RelativeRotation",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetRelativeRotation() : FRotator();
			},
			[](const FLuaSceneComponentHandle& Self, const FRotator& Value)
			{
				USceneComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetRelativeRotation(Value);
				}
			}
		),

		"RelativeScale",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetRelativeScale() : FVector::OneVector;
			},
			[](const FLuaSceneComponentHandle& Self, const FVector& Value)
			{
				USceneComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetRelativeScale(Value);
				}
			}
		),

		"WorldLocation",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetWorldLocation() : FVector::ZeroVector;
			},
			[](const FLuaSceneComponentHandle& Self, const FVector& Value)
			{
				USceneComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetWorldLocation(Value);
				}
			}
		),

		"WorldScale",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetWorldScale() : FVector::OneVector;
			}
		),

		"ForwardVector",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetForwardVector() : FVector::ForwardVector;
			}
		),

		"RightVector",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetRightVector() : FVector::RightVector;
			}
		),

		"UpVector",
		sol::property(
			[](const FLuaSceneComponentHandle& Self)
			{
				USceneComponent* Component = Self.Resolve();
				return Component ? Component->GetUpVector() : FVector::UpVector;
			}
		),

		"Move",
		[](const FLuaSceneComponentHandle& Self, const FVector& Delta)
		{
			USceneComponent* Component = Self.Resolve();
			if (!Component)
			{
				return false;
			}
			Component->Move(Delta);
			return true;
		},

		"MoveLocal",
		[](const FLuaSceneComponentHandle& Self, const FVector& Delta)
		{
			USceneComponent* Component = Self.Resolve();
			if (!Component)
			{
				return false;
			}
			Component->MoveLocal(Delta);
			return true;
		},

		"ListProperties",
		[](const FLuaSceneComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"GetProperty",
		[](const FLuaSceneComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaSceneComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);
}

void RegisterPrimitiveComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaPrimitiveComponentHandle>(
		"PrimitiveComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaPrimitiveComponentHandle),

		"Visible",
		sol::property(
			[](const FLuaPrimitiveComponentHandle& Self)
			{
				UPrimitiveComponent* Component = Self.Resolve();
				return Component ? Component->IsVisible() : false;
			},
			[](const FLuaPrimitiveComponentHandle& Self, bool bVisible)
			{
				UPrimitiveComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetVisibility(bVisible);
				}
			}
		),

		"CastShadow",
		sol::property(
			[](const FLuaPrimitiveComponentHandle& Self)
			{
				UPrimitiveComponent* Component = Self.Resolve();
				return Component ? Component->GetCastShadow() : false;
			},
			[](const FLuaPrimitiveComponentHandle& Self, bool bCastShadow)
			{
				UPrimitiveComponent* Component = Self.Resolve();
				if (Component)
				{
					Component->SetCastShadow(bCastShadow);
				}
			}
		),

		"ListProperties",
		[](const FLuaPrimitiveComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"GetProperty",
		[](const FLuaPrimitiveComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaPrimitiveComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);
}

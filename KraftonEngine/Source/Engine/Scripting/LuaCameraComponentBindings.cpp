#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaBindingHelper.h"
#include "LuaWorldLibrary.h"
#include "LuaPropertyBridge.h"

#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cctype>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static constexpr float RAD2DEG = 180.0f / M_PI;
static constexpr float DEG2RAD = M_PI / 180.0f;

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

	FString NormalizeCameraToken(FString Value)
	{
		Value.erase(
			std::remove_if(Value.begin(), Value.end(), [](unsigned char Ch)
			{
				return Ch == ' ' || Ch == '_' || Ch == '-';
			}),
			Value.end());

		for (char& Ch : Value)
		{
			Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
		}

		return Value;
	}

	const char* CameraViewModeToString(ECameraViewMode Mode)
	{
		switch (Mode)
		{
		case ECameraViewMode::FirstPerson:
			return "FirstPerson";
		case ECameraViewMode::ThirdPerson:
			return "ThirdPerson";
		case ECameraViewMode::OrthographicFollow:
			return "OrthographicFollow";
		case ECameraViewMode::Custom:
			return "Custom";
		case ECameraViewMode::Static:
		default:
			return "Static";
		}
	}

	ECameraViewMode CameraViewModeFromName(const FString& Name)
	{
		const FString Token = NormalizeCameraToken(Name);
		if (Token == "FIRSTPERSON" || Token == "FP") return ECameraViewMode::FirstPerson;
		if (Token == "THIRDPERSON" || Token == "TP") return ECameraViewMode::ThirdPerson;
		if (Token == "ORTHOGRAPHIC" || Token == "ORTHOGRAPHICFOLLOW" || Token == "ORTHO") return ECameraViewMode::OrthographicFollow;
		if (Token == "CUSTOM") return ECameraViewMode::Custom;
		return ECameraViewMode::Static;
	}

	ECameraViewMode CameraViewModeFromIndex(int32 Index)
	{
		switch (Index)
		{
		case static_cast<int32>(ECameraViewMode::FirstPerson): return ECameraViewMode::FirstPerson;
		case static_cast<int32>(ECameraViewMode::ThirdPerson): return ECameraViewMode::ThirdPerson;
		case static_cast<int32>(ECameraViewMode::OrthographicFollow): return ECameraViewMode::OrthographicFollow;
		case static_cast<int32>(ECameraViewMode::Custom): return ECameraViewMode::Custom;
		case static_cast<int32>(ECameraViewMode::Static):
		default:
			return ECameraViewMode::Static;
		}
	}

	void SetComponentWorldRotation(USceneComponent* Component, const FRotator& WorldRotation)
	{
		if (!Component)
		{
			return;
		}

		USceneComponent* Parent = Component->GetParent();
		if (!Parent)
		{
			Component->SetRelativeRotation(WorldRotation);
			return;
		}

		FMatrix DesiredWorld = WorldRotation.ToMatrix();
		FMatrix Relative = DesiredWorld * Parent->GetWorldMatrix().GetInverse();
		Component->SetRelativeRotation(Relative.ToRotator());
	}
}

void RegisterCameraComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaCameraComponentHandle>(
		"CameraComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaCameraComponentHandle),

		"FOVDegrees",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetFOV() * RAD2DEG : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Degrees)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.FOVDegrees Access.");
					return;
				}
				C->SetFOV(Degrees * DEG2RAD);
			}
		),

		"FOVRadians",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetFOV() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Radians)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.FOVRadians Access.");
					return;
				}
				C->SetFOV(Radians);
			}
		),

		"AspectRatio",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetAspectRatio() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float AspectRatio)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.AspectRatio Access.");
					return;
				}
				C->SetAspectRatio(AspectRatio);
			}
		),

		"NearZ",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetNearPlane() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float NearZ)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.NearZ Access.");
					return;
				}
				C->SetNearPlane(NearZ);
			}
		),

		"NearClip",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetNearPlane() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float NearZ)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.NearClip Access.");
					return;
				}
				C->SetNearPlane(NearZ);
			}
		),

		"FarZ",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetFarPlane() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float FarZ)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.FarZ Access.");
					return;
				}
				C->SetFarPlane(FarZ);
			}
		),

		"FarClip",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetFarPlane() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float FarZ)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.FarClip Access.");
					return;
				}
				C->SetFarPlane(FarZ);
			}
		),

		"Orthographic",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> bool
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->IsOrthogonal() : false;
			},
			[](const FLuaCameraComponentHandle& Self, bool bOrtho)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.Orthographic Access.");
					return;
				}
				C->SetOrthographic(bOrtho);
			}
		),

		"IsOrthographic",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> bool
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->IsOrthogonal() : false;
			},
			[](const FLuaCameraComponentHandle& Self, bool bOrtho)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetOrthographic(bOrtho);
				}
			}
		),

		"OrthoWidth",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetOrthoWidth() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Width)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.OrthoWidth Access.");
					return;
				}
				C->SetOrthoWidth(Clamp(Width, 0.1f, 100000.0f));
			}
		),

		"ViewMode",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FString
			{
				UCameraComponent* C = Self.Resolve();
				return C ? CameraViewModeToString(C->GetViewMode()) : FString();
			},
			[](const FLuaCameraComponentHandle& Self, const FString& ModeName)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.ViewMode Access.");
					return;
				}
				C->SetViewMode(CameraViewModeFromName(ModeName));
			}
		),

		"ViewModeIndex",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> int32
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetViewModeIndex() : 0;
			},
			[](const FLuaCameraComponentHandle& Self, int32 ModeIndex)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.ViewModeIndex Access.");
					return;
				}
				C->SetViewMode(CameraViewModeFromIndex(ModeIndex));
			}
		),

		"UseOwnerAsTarget",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> bool
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->UsesOwnerAsTarget() : false;
			},
			[](const FLuaCameraComponentHandle& Self, bool bUseOwner)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetUseOwnerAsTarget(bUseOwner);
				}
			}
		),

		"TargetOffset",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FVector
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetTargetOffset() : FVector::ZeroVector;
			},
			[](const FLuaCameraComponentHandle& Self, const FVector& Offset)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetTargetOffset(Offset);
				}
			}
		),

		"BackDistance",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetBackDistance() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Value)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetBackDistance(Value);
				}
			}
		),

		"Height",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetHeight() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Value)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetHeight(Value);
				}
			}
		),

		"SideOffset",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> float
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetSideOffset() : 0.0f;
			},
			[](const FLuaCameraComponentHandle& Self, float Value)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetSideOffset(Value);
				}
			}
		),

		"SetAsActiveCamera",
		sol::overload(
			[](const FLuaCameraComponentHandle& Self)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetActiveCamera();
				}
			},
			[](const FLuaCameraComponentHandle& Self, const FLuaPlayerControllerHandle& ControllerHandle)
			{
				UCameraComponent* C = Self.Resolve();
				APlayerController* Controller = ControllerHandle.Resolve();
				if (C && Controller)
				{
					Controller->SetActiveCamera(C);
				}
			}
		),

		"SetAsActiveCameraWithBlend",
		sol::overload(
			[](const FLuaCameraComponentHandle& Self)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetActiveCameraWithBlend();
				}
			},
			[](const FLuaCameraComponentHandle& Self, const FLuaPlayerControllerHandle& ControllerHandle)
			{
				UCameraComponent* C = Self.Resolve();
				APlayerController* Controller = ControllerHandle.Resolve();
				if (C && Controller)
				{
					Controller->SetActiveCameraWithBlend(C);
				}
			}
		),

		"SetTargetActor",
		sol::overload(
			[](const FLuaCameraComponentHandle& Self, const FLuaGameObjectHandle& ActorHandle)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetTargetActor(ActorHandle.Resolve());
				}
			},
			[](const FLuaCameraComponentHandle& Self, sol::nil_t)
			{
				if (UCameraComponent* C = Self.Resolve())
				{
					C->SetTargetActor(nullptr);
				}
			}
		),

		"ClearTargetActor",
		[](const FLuaCameraComponentHandle& Self)
		{
			if (UCameraComponent* C = Self.Resolve())
			{
				C->ClearTargetActor();
			}
		},

		"WorldLocation",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FVector
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetWorldLocation() : FVector::ZeroVector;
			},
			[](const FLuaCameraComponentHandle& Self, const FVector& V)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.WorldLocation Access.");
					return;
				}
				C->SetWorldLocation(V);
			}
		),

		"RelativeLocation",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FVector
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetRelativeLocation() : FVector::ZeroVector;
			},
			[](const FLuaCameraComponentHandle& Self, const FVector& V)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.RelativeLocation Access.");
					return;
				}
				C->SetRelativeLocation(V);
			}
		),

		"RelativeRotation",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FRotator
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetRelativeRotation() : FRotator();
			},
			[](const FLuaCameraComponentHandle& Self, const FRotator& R)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.RelativeRotation Access.");
					return;
				}
				C->SetRelativeRotation(R);
			}
		),

		"WorldRotation",
		sol::property(
			[](const FLuaCameraComponentHandle& Self) -> FRotator
			{
				UCameraComponent* C = Self.Resolve();
				return C ? C->GetWorldMatrix().ToRotator() : FRotator();
			},
			[](const FLuaCameraComponentHandle& Self, const FRotator& R)
			{
				UCameraComponent* C = Self.Resolve();
				if (!C)
				{
					UE_LOG("[Lua] Invalid CameraComponent.WorldRotation Access.");
					return;
				}
				SetComponentWorldRotation(C, R);
			}
		),

		"AsComponent",
		[](const FLuaCameraComponentHandle& Self)
		{
			return MakeActorComponentHandle(Self.Resolve());
		},

		"AsScene",
		[](const FLuaCameraComponentHandle& Self)
		{
			return MakeSceneComponentHandle(Self.Resolve());
		},

		"ListProperties",
		[](const FLuaCameraComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"HasProperty",
		[](const FLuaCameraComponentHandle& Self, const FString& Name)
		{
			return FLuaPropertyBridge::HasProperty(Self.Resolve(), Name);
		},

		"GetProperty",
		[](const FLuaCameraComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaCameraComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);
}

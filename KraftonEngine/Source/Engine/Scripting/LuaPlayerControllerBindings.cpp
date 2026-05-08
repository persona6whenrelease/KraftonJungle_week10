#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaBindingHelper.h"

#include "Component/ControllerInputComponent.h"
#include "Component/CameraComponent.h"
#include "Component/ActorComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Math/MathUtils.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

#include <algorithm>
#include <cctype>

namespace
{
	FString NormalizeControllerToken(FString Value)
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

	const char* MovementFrameToString(EControllerMovementFrame Frame)
	{
		switch (Frame)
		{
		case EControllerMovementFrame::World:
			return "World";
		case EControllerMovementFrame::ControlRotation:
			return "ControlRotation";
		case EControllerMovementFrame::ViewCamera:
		default:
			return "ViewCamera";
		}
	}

	EControllerMovementFrame MovementFrameFromIndex(int32 Index)
	{
		switch (Index)
		{
		case static_cast<int32>(EControllerMovementFrame::World):
			return EControllerMovementFrame::World;
		case static_cast<int32>(EControllerMovementFrame::ControlRotation):
			return EControllerMovementFrame::ControlRotation;
		case static_cast<int32>(EControllerMovementFrame::ViewCamera):
		default:
			return EControllerMovementFrame::ViewCamera;
		}
	}

	EControllerMovementFrame MovementFrameFromName(const FString& Name)
	{
		const FString Token = NormalizeControllerToken(Name);
		if (Token == "WORLD")
		{
			return EControllerMovementFrame::World;
		}
		if (Token == "CONTROLROTATION" || Token == "CONTROLLER" || Token == "CONTROL")
		{
			return EControllerMovementFrame::ControlRotation;
		}
		return EControllerMovementFrame::ViewCamera;
	}

	FLuaControllerInputComponentHandle MakeControllerInputHandle(UControllerInputComponent* Component)
	{
		FLuaControllerInputComponentHandle Handle;

		if (Component)
		{
			Handle.UUID = Component->GetUUID();
		}

		return Handle;
	}

	FLuaCameraComponentHandle MakeCameraHandle(UCameraComponent* Camera)
	{
		FLuaCameraComponentHandle Handle;
		if (Camera)
		{
			Handle.UUID = Camera->GetUUID();
		}
		return Handle;
	}

	FLuaGameObjectHandle MakeGameObjectHandle(AActor* Actor)
	{
		FLuaGameObjectHandle Handle;

		if (Actor)
		{
			Handle.UUID = Actor->GetUUID();
		}

		return Handle;
	}

	FLuaPawnHandle MakePawnHandle(APawn* Pawn)
	{
		FLuaPawnHandle Handle;

		if (Pawn)
		{
			Handle.UUID = Pawn->GetUUID();
		}

		return Handle;
	}

	void ApplyControllerLookRotation(APlayerController* Controller, FRotator Rotation)
	{
		if (!Controller)
		{
			return;
		}

		Rotation.Roll = 0.0f;
		Controller->SetControlRotation(Rotation);
	}

	void AddControllerYawInput(APlayerController* Controller, float Value)
	{
		if (Controller)
		{
			Controller->AddYawInput(Value);
		}
	}

	void AddControllerPitchInput(APlayerController* Controller, float Value)
	{
		if (!Controller)
		{
			return;
		}

		float MinPitch = -89.0f;
		float MaxPitch = 89.0f;

		if (UControllerInputComponent* Input = Controller->FindControllerInputComponent())
		{
			MinPitch = Input->GetMinPitch();
			MaxPitch = Input->GetMaxPitch();
		}

		FRotator Rotation = Controller->GetControlRotation();
		Rotation.Pitch = Clamp(Rotation.Pitch + Value, MinPitch, MaxPitch);
		Controller->SetControlRotation(Rotation);
	}
}

void RegisterPlayerControllerBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaControllerInputComponentHandle>(
		"ControllerInputComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaControllerInputComponentHandle),

		"MovementFrame",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> FString
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? MovementFrameToString(Input->GetMovementFrame()) : FString();
			},
			[](const FLuaControllerInputComponentHandle& Self, const FString& Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMovementFrame(MovementFrameFromName(Value));
				}
			}
		),

		"MovementFrameIndex",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> int32
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? static_cast<int32>(Input->GetMovementFrame()) : 0;
			},
			[](const FLuaControllerInputComponentHandle& Self, int32 Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMovementFrame(MovementFrameFromIndex(Value));
				}
			}
		),


		"MoveSpeed",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> float
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? Input->GetMoveSpeed() : 0.0f;
			},
			[](const FLuaControllerInputComponentHandle& Self, float Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMoveSpeed(Value);
				}
			}
		),

		"SprintMultiplier",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> float
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? Input->GetSprintMultiplier() : 0.0f;
			},
			[](const FLuaControllerInputComponentHandle& Self, float Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetSprintMultiplier(Value);
				}
			}
		),

		"LookSensitivity",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> float
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? Input->GetLookSensitivity() : 0.0f;
			},
			[](const FLuaControllerInputComponentHandle& Self, float Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetLookSensitivity(Value);
				}
			}
		),

		"MinPitch",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> float
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? Input->GetMinPitch() : -89.0f;
			},
			[](const FLuaControllerInputComponentHandle& Self, float Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMinPitch(Value);
				}
			}
		),

		"MaxPitch",
		sol::property(
			[](const FLuaControllerInputComponentHandle& Self) -> float
			{
				UControllerInputComponent* Input = Self.Resolve();
				return Input ? Input->GetMaxPitch() : 89.0f;
			},
			[](const FLuaControllerInputComponentHandle& Self, float Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMaxPitch(Value);
				}
			}
		),

		"SetMovementFrame",
		sol::overload(
			[](const FLuaControllerInputComponentHandle& Self, const FString& Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMovementFrame(MovementFrameFromName(Value));
				}
			},
			[](const FLuaControllerInputComponentHandle& Self, int32 Value)
			{
				if (UControllerInputComponent* Input = Self.Resolve())
				{
					Input->SetMovementFrame(MovementFrameFromIndex(Value));
				}
			}
		)
	);

	Lua.new_usertype<FLuaPlayerControllerHandle>(
		"PlayerController",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaPlayerControllerHandle),

		"Possess",
		sol::overload(
			[](const FLuaPlayerControllerHandle& Self, const FLuaGameObjectHandle& ActorHandle)
			{
				APlayerController* Controller = Self.Resolve();

				if (!Controller)
				{
					UE_LOG("[Lua] Invalid PlayerController.Possess(GameObject) Call.");
					return;
				}

				Controller->Possess(ActorHandle.Resolve());
			},
			[](const FLuaPlayerControllerHandle& Self, const FLuaPawnHandle& PawnHandle)
			{
				APlayerController* Controller = Self.Resolve();

				if (!Controller)
				{
					UE_LOG("[Lua] Invalid PlayerController.Possess(Pawn) Call.");
					return;
				}

				Controller->Possess(PawnHandle.Resolve());
			}
		),

		"UnPossess",
		[](const FLuaPlayerControllerHandle& Self)
		{
			APlayerController* Controller = Self.Resolve();

			if (!Controller)
			{
				UE_LOG("[Lua] Invalid PlayerController.UnPossess Call.");
				return;
			}

			Controller->UnPossess();
		},

		"GetPossessedActor",
		[](const FLuaPlayerControllerHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			APlayerController* Controller = Self.Resolve();
			AActor* Actor = Controller ? Controller->GetPossessedActor() : nullptr;

			if (!Actor)
			{
				return sol::nil;
			}

			FLuaGameObjectHandle Handle;
			Handle.UUID = Actor->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"GetPossessedPawn",
		[](const FLuaPlayerControllerHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			APlayerController* Controller = Self.Resolve();
			AActor* Actor = Controller ? Controller->GetPossessedActor() : nullptr;
			APawn* Pawn = Cast<APawn>(Actor);

			if (!Pawn)
			{
				return sol::nil;
			}

			FLuaPawnHandle Handle;
			Handle.UUID = Pawn->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"GetControllerInput",
		[](const FLuaPlayerControllerHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);

			APlayerController* Controller = Self.Resolve();
			UControllerInputComponent* Input = Controller ? Controller->FindControllerInputComponent() : nullptr;

			if (!Input)
			{
				return sol::nil;
			}

			FLuaControllerInputComponentHandle Handle;
			Handle.UUID = Input->GetUUID();
			return sol::make_object(LuaView, Handle);
		},

		"Input",
		sol::property(
			[](const FLuaPlayerControllerHandle& Self, sol::this_state State) -> sol::object
			{
				sol::state_view LuaView(State);

				APlayerController* Controller = Self.Resolve();
				UControllerInputComponent* Input = Controller ? Controller->FindControllerInputComponent() : nullptr;

				if (!Input)
				{
					return sol::nil;
				}

				FLuaControllerInputComponentHandle Handle;
				Handle.UUID = Input->GetUUID();
				return sol::make_object(LuaView, Handle);
			}
		),

		"SetActiveCamera",
		sol::overload(
			[](const FLuaPlayerControllerHandle& Self, const FLuaCameraComponentHandle& CameraHandle)
			{
				if (APlayerController* Controller = Self.Resolve())
				{
					Controller->SetActiveCamera(CameraHandle.Resolve());
				}
			},
			[](const FLuaPlayerControllerHandle& Self, sol::nil_t)
			{
				if (APlayerController* Controller = Self.Resolve())
				{
					Controller->ClearActiveCamera();
				}
			}
		),

		"SetActiveCameraWithBlend",
		[](const FLuaPlayerControllerHandle& Self, const FLuaCameraComponentHandle& CameraHandle)
		{
			if (APlayerController* Controller = Self.Resolve())
			{
				Controller->SetActiveCameraWithBlend(CameraHandle.Resolve());
			}
		},

		"GetActiveCamera",
		[](const FLuaPlayerControllerHandle& Self, sol::this_state State) -> sol::object
		{
			sol::state_view LuaView(State);
			APlayerController* Controller = Self.Resolve();
			UCameraComponent* Camera = Controller ? Controller->GetActiveCamera() : nullptr;
			if (!Camera)
			{
				return sol::nil;
			}
			return sol::make_object(LuaView, MakeCameraHandle(Camera));
		},

		"ClearActiveCamera",
		[](const FLuaPlayerControllerHandle& Self)
		{
			if (APlayerController* Controller = Self.Resolve())
			{
				Controller->ClearActiveCamera();
			}
		},

		"GetControlRotation",
		[](const FLuaPlayerControllerHandle& Self) -> FRotator
		{
			APlayerController* Controller = Self.Resolve();
			return Controller ? Controller->GetControlRotation() : FRotator();
		},

		"SetControlRotation",
		[](const FLuaPlayerControllerHandle& Self, const FRotator& Rotation)
		{
			APlayerController* Controller = Self.Resolve();

			if (!Controller)
			{
				UE_LOG("[Lua] Invalid PlayerController.SetControlRotation Call.");
				return;
			}

			ApplyControllerLookRotation(Controller, Rotation);
		},

		"AddYawInput",
		[](const FLuaPlayerControllerHandle& Self, float Value)
		{
			APlayerController* Controller = Self.Resolve();

			if (!Controller)
			{
				UE_LOG("[Lua] Invalid PlayerController.AddYawInput Call.");
				return;
			}

			AddControllerYawInput(Controller, Value);
		},

		"AddPitchInput",
		[](const FLuaPlayerControllerHandle& Self, float Value)
		{
			APlayerController* Controller = Self.Resolve();

			if (!Controller)
			{
				UE_LOG("[Lua] Invalid PlayerController.AddPitchInput Call.");
				return;
			}

			AddControllerPitchInput(Controller, Value);
		},

		"ForwardVector",
		sol::property(
			[](const FLuaPlayerControllerHandle& Self) -> FVector
			{
				APlayerController* Controller = Self.Resolve();
				return Controller ? Controller->GetControlRotation().GetForwardVector() : FVector::ForwardVector;
			}
		),

		"RightVector",
		sol::property(
			[](const FLuaPlayerControllerHandle& Self) -> FVector
			{
				APlayerController* Controller = Self.Resolve();
				return Controller ? Controller->GetControlRotation().GetRightVector() : FVector::RightVector;
			}
		),

		"UpVector",
		sol::property(
			[](const FLuaPlayerControllerHandle& Self) -> FVector
			{
				APlayerController* Controller = Self.Resolve();
				return Controller ? Controller->GetControlRotation().GetUpVector() : FVector::UpVector;
			}
		),

		"AddMovementInput",
		[](const FLuaPlayerControllerHandle& Self, const FVector& Direction, float Scale)
		{
			APlayerController* Controller = Self.Resolve();

			if (!Controller)
			{
				UE_LOG("[Lua] Invalid PlayerController.AddMovementInput Call.");
				return;
			}

			Controller->AddMovementInput(Direction, Scale);
		}
	);
}

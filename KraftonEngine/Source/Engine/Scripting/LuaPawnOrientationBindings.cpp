#include "LuaBindings.h"
#include "SolInclude.h"
#include "LuaHandles.h"
#include "LuaBindingHelper.h"

#include "Component/PawnOrientationComponent.h"
#include "Math/Vector.h"

#include <algorithm>
#include <cctype>

namespace
{
	FString NormalizeFacingModeToken(FString Value)
	{
		Value.erase(
			std::remove_if(Value.begin(), Value.end(), [](unsigned char Ch)
			{
				return Ch == ' ' || Ch == '_' || Ch == '-' || Ch == '+';
			}),
			Value.end());

		for (char& Ch : Value)
		{
			Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
		}

		return Value;
	}

	const char* FacingModeToString(EPawnFacingMode Mode)
	{
		switch (Mode)
		{
		case EPawnFacingMode::ControlRotationYaw:
			return "ControlRotationYaw";

		case EPawnFacingMode::MovementInputDirection:
			return "MovementInputDirection";

		case EPawnFacingMode::MovementVelocityDirection:
			return "MovementVelocityDirection";

		case EPawnFacingMode::MovementDirectionWithControlFallback:
			return "MovementDirectionWithControlFallback";

		case EPawnFacingMode::CustomWorldDirection:
			return "CustomWorldDirection";

		case EPawnFacingMode::None:
		default:
			return "None";
		}
	}

	EPawnFacingMode FacingModeFromIndex(int32 Index)
	{
		switch (Index)
		{
		case static_cast<int32>(EPawnFacingMode::ControlRotationYaw):
			return EPawnFacingMode::ControlRotationYaw;

		case static_cast<int32>(EPawnFacingMode::MovementInputDirection):
			return EPawnFacingMode::MovementInputDirection;

		case static_cast<int32>(EPawnFacingMode::MovementVelocityDirection):
			return EPawnFacingMode::MovementVelocityDirection;

		case static_cast<int32>(EPawnFacingMode::MovementDirectionWithControlFallback):
			return EPawnFacingMode::MovementDirectionWithControlFallback;

		case static_cast<int32>(EPawnFacingMode::CustomWorldDirection):
			return EPawnFacingMode::CustomWorldDirection;

		case static_cast<int32>(EPawnFacingMode::None):
		default:
			return EPawnFacingMode::None;
		}
	}

	EPawnFacingMode FacingModeFromName(const FString& Name)
	{
		const FString Token = NormalizeFacingModeToken(Name);

		if (Token == "CONTROL" || Token == "CONTROLROTATION" || Token == "CONTROLROTATIONYAW")
		{
			return EPawnFacingMode::ControlRotationYaw;
		}

		if (Token == "MOVEMENT" || Token == "MOVEMENTINPUT" || Token == "MOVEMENTINPUTDIRECTION")
		{
			return EPawnFacingMode::MovementInputDirection;
		}

		if (Token == "VELOCITY" || Token == "MOVEMENTVELOCITY" || Token == "MOVEMENTVELOCITYDIRECTION")
		{
			return EPawnFacingMode::MovementVelocityDirection;
		}

		if (Token == "FALLBACK" || Token == "MOVEMENTCONTROLFALLBACK" || Token == "MOVEMENTDIRECTIONWITHCONTROLFALLBACK")
		{
			return EPawnFacingMode::MovementDirectionWithControlFallback;
		}

		if (Token == "CUSTOM" || Token == "CUSTOMWORLD" || Token == "CUSTOMWORLDDIRECTION")
		{
			return EPawnFacingMode::CustomWorldDirection;
		}

		return EPawnFacingMode::None;
	}
}

void RegisterPawnOrientationComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaPawnOrientationComponentHandle>(
		"PawnOrientationComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaPawnOrientationComponentHandle),

		"FacingMode",
		sol::property(
			[](const FLuaPawnOrientationComponentHandle& Self) -> FString
			{
				UPawnOrientationComponent* Component = Self.Resolve();
				return Component ? FacingModeToString(Component->GetFacingMode()) : FString();
			},
			[](const FLuaPawnOrientationComponentHandle& Self, const FString& Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetFacingMode(FacingModeFromName(Value));
				}
			}
		),

		"FacingModeIndex",
		sol::property(
			[](const FLuaPawnOrientationComponentHandle& Self) -> int32
			{
				UPawnOrientationComponent* Component = Self.Resolve();
				return Component ? static_cast<int32>(Component->GetFacingMode()) : 0;
			},
			[](const FLuaPawnOrientationComponentHandle& Self, int32 Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetFacingMode(FacingModeFromIndex(Value));
				}
			}
		),

		"RotationSpeed",
		sol::property(
			[](const FLuaPawnOrientationComponentHandle& Self) -> float
			{
				UPawnOrientationComponent* Component = Self.Resolve();
				return Component ? Component->GetRotationSpeed() : 0.0f;
			},
			[](const FLuaPawnOrientationComponentHandle& Self, float Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetRotationSpeed(Value);
				}
			}
		),

		"YawOnly",
		sol::property(
			[](const FLuaPawnOrientationComponentHandle& Self) -> bool
			{
				UPawnOrientationComponent* Component = Self.Resolve();
				return Component ? Component->IsYawOnly() : true;
			},
			[](const FLuaPawnOrientationComponentHandle& Self, bool bValue)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetYawOnly(bValue);
				}
			}
		),

		"CustomFacingDirection",
		sol::property(
			[](const FLuaPawnOrientationComponentHandle& Self) -> FVector
			{
				UPawnOrientationComponent* Component = Self.Resolve();
				return Component ? Component->GetCustomFacingDirection() : FVector::ForwardVector;
			},
			[](const FLuaPawnOrientationComponentHandle& Self, const FVector& Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetCustomFacingDirection(Value);
				}
			}
		),

		"SetFacingMode",
		sol::overload(
			[](const FLuaPawnOrientationComponentHandle& Self, const FString& Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetFacingMode(FacingModeFromName(Value));
				}
			},
			[](const FLuaPawnOrientationComponentHandle& Self, int32 Value)
			{
				if (UPawnOrientationComponent* Component = Self.Resolve())
				{
					Component->SetFacingMode(FacingModeFromIndex(Value));
				}
			}
		)
	);
}

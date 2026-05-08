#include "Component/PawnOrientationComponent.h"

#include "Component/Movement/MovementComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

IMPLEMENT_CLASS(UPawnOrientationComponent, UActorComponent)

namespace
{
	constexpr int32 PawnFacingModeCount = 6;

	int32 NormalizeFacingModeValue(int32 Value)
	{
		switch (Value)
		{
		case static_cast<int32>(EPawnFacingMode::None):
		case static_cast<int32>(EPawnFacingMode::ControlRotationYaw):
		case static_cast<int32>(EPawnFacingMode::MovementInputDirection):
		case static_cast<int32>(EPawnFacingMode::MovementVelocityDirection):
		case static_cast<int32>(EPawnFacingMode::MovementDirectionWithControlFallback):
		case static_cast<int32>(EPawnFacingMode::CustomWorldDirection):
			return Value;
		default:
			return static_cast<int32>(EPawnFacingMode::MovementDirectionWithControlFallback);
		}
	}

	bool TryGetYawFromDirection(const FVector& Direction, float MinLength, float& OutYaw)
	{
		FVector FlatDirection(Direction.X, Direction.Y, 0.0f);
		if (FlatDirection.Length() <= MinLength)
		{
			return false;
		}

		FlatDirection = FlatDirection.Normalized();
		OutYaw = atan2f(FlatDirection.Y, FlatDirection.X) * RAD_TO_DEG;
		return true;
	}

	float NormalizeAngle180(float Angle)
	{
		Angle = std::fmod(Angle + 180.0f, 360.0f);
		if (Angle < 0.0f)
		{
			Angle += 360.0f;
		}
		return Angle - 180.0f;
	}

	float MoveAngleTowards(float Current, float Target, float MaxDelta)
	{
		const float Delta = NormalizeAngle180(Target - Current);
		if (std::fabs(Delta) <= MaxDelta)
		{
			return Target;
		}
		return Current + (Delta > 0.0f ? MaxDelta : -MaxDelta);
	}
}

void UPawnOrientationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshFacing(DeltaTime);
}

bool UPawnOrientationComponent::RefreshFacing(float DeltaTime)
{
	float DesiredYaw = 0.0f;
	if (!ComputeDesiredFacingYaw(DesiredYaw))
	{
		return false;
	}

	ApplyFacingYaw(DesiredYaw, DeltaTime);
	return true;
}

void UPawnOrientationComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << FacingMode;
	Ar << RotationSpeed;
	Ar << bYawOnly;
	Ar << MinFacingInputSize;
	Ar << MinFacingSpeed;
	Ar << CustomFacingDirection;

	if (Ar.IsLoading())
	{
		NormalizeOptions();
	}
}

void UPawnOrientationComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);

	static const char* FacingModeNames[] =
	{
		"None",
		"Control Rotation Yaw",
		"Movement Input Direction",
		"Movement Velocity Direction",
		"Movement Direction + Control Fallback",
		"Custom World Direction"
	};

	OutProps.push_back({ "Facing Mode", EPropertyType::Enum, &FacingMode, 0.0f, 0.0f, 0.1f, FacingModeNames, PawnFacingModeCount });
	OutProps.push_back({ "Rotation Speed", EPropertyType::Float, &RotationSpeed, 0.0f, 10000.0f, 1.0f });
	OutProps.push_back({ "Yaw Only", EPropertyType::Bool, &bYawOnly });
	OutProps.push_back({ "Min Facing Input Size", EPropertyType::Float, &MinFacingInputSize, 0.0f, 10000.0f, 0.01f });
	OutProps.push_back({ "Min Facing Speed", EPropertyType::Float, &MinFacingSpeed, 0.0f, 10000.0f, 0.1f });
	OutProps.push_back({ "Custom Facing Direction", EPropertyType::Vec3, &CustomFacingDirection, 0.0f, 0.0f, 0.1f });
}

void UPawnOrientationComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	if (std::strcmp(PropertyName, "Facing Mode") == 0)
	{
		NormalizeOptions();
		return;
	}

	RotationSpeed = (std::max)(RotationSpeed, 0.0f);
	MinFacingInputSize = (std::max)(MinFacingInputSize, 0.0f);
	MinFacingSpeed = (std::max)(MinFacingSpeed, 0.0f);
}

void UPawnOrientationComponent::SetFacingMode(EPawnFacingMode InMode)
{
	FacingMode = NormalizeFacingModeValue(static_cast<int32>(InMode));
}

void UPawnOrientationComponent::SetRotationSpeed(float InSpeed)
{
	RotationSpeed = InSpeed < 0.0f ? 0.0f : InSpeed;
}

void UPawnOrientationComponent::SetCustomFacingDirection(const FVector& InDirection)
{
	CustomFacingDirection = InDirection.IsNearlyZero() ? FVector::ForwardVector : InDirection.Normalized();
}

void UPawnOrientationComponent::NormalizeOptions()
{
	FacingMode = NormalizeFacingModeValue(FacingMode);
	RotationSpeed = (std::max)(RotationSpeed, 0.0f);
	MinFacingInputSize = (std::max)(MinFacingInputSize, 0.0f);
	MinFacingSpeed = (std::max)(MinFacingSpeed, 0.0f);
	if (CustomFacingDirection.IsNearlyZero())
	{
		CustomFacingDirection = FVector::ForwardVector;
	}
}

APlayerController* UPawnOrientationComponent::FindOwningPlayerController() const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (APlayerController* Controller : World->GetPlayerControllers())
	{
		if (Controller && Controller->GetPossessedActor() == OwnerActor)
		{
			return Controller;
		}
	}
	return nullptr;
}

UMovementComponent* UPawnOrientationComponent::FindBestMovementComponent() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UMovementComponent* BestMovement = nullptr;
	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UMovementComponent* Movement = Cast<UMovementComponent>(Component);
		if (!Movement)
		{
			continue;
		}

		if (!BestMovement || Movement->GetControllerInputPriority() > BestMovement->GetControllerInputPriority())
		{
			BestMovement = Movement;
		}
	}
	return BestMovement;
}

bool UPawnOrientationComponent::ComputeDesiredFacingYaw(float& OutYaw) const
{
	const EPawnFacingMode Mode = GetFacingMode();
	if (Mode == EPawnFacingMode::None)
	{
		return false;
	}

	UMovementComponent* Movement = FindBestMovementComponent();

	if (Mode == EPawnFacingMode::MovementInputDirection)
	{
		return Movement && TryGetYawFromDirection(
			Movement->GetLastControllerWorldDirection(),
			MinFacingInputSize,
			OutYaw
		);
	}

	if (Mode == EPawnFacingMode::MovementVelocityDirection)
	{
		return Movement && TryGetYawFromDirection(
			Movement->GetMovementVelocity(),
			MinFacingSpeed,
			OutYaw
		);
	}

	if (Mode == EPawnFacingMode::MovementDirectionWithControlFallback)
	{
		if (Movement && TryGetYawFromDirection(
			Movement->GetMovementVelocity(),
			MinFacingSpeed,
			OutYaw
		))
		{
			return true;
		}

		// 속도가 없으면 아래 ControlRotationYaw fallback으로 내려갑니다.
	}

	if (Mode == EPawnFacingMode::CustomWorldDirection)
	{
		return TryGetYawFromDirection(CustomFacingDirection, MinFacingInputSize, OutYaw);
	}

	APlayerController* Controller = FindOwningPlayerController();
	if (!Controller)
	{
		return false;
	}

	OutYaw = Controller->GetControlRotation().Yaw;
	return true;
}

void UPawnOrientationComponent::ApplyFacingYaw(float TargetYaw, float DeltaTime)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	FRotator NewRotation = OwnerActor->GetActorRotation();
	if (bYawOnly)
	{
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
	}

	if (RotationSpeed <= 0.0f || DeltaTime <= 0.0f)
	{
		NewRotation.Yaw = TargetYaw;
	}
	else
	{
		NewRotation.Yaw = MoveAngleTowards(NewRotation.Yaw, TargetYaw, RotationSpeed * DeltaTime);
	}

	OwnerActor->SetActorRotation(NewRotation);
}

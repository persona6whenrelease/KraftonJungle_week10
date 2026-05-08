#include "Component/Movement/PawnMovementComponent.h"

#include "Component/SceneComponent.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <cmath>

IMPLEMENT_CLASS(UPawnMovementComponent, UMovementComponent)

UPawnMovementComponent::UPawnMovementComponent()
{
	bReceiveControllerInput = true;
	ControllerInputPriority = 0;
}

void UPawnMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyPendingMovement(DeltaTime);
}

void UPawnMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
}

void UPawnMovementComponent::AddMovementInput(const FVector& Direction, float Scale)
{
	if (Scale == 0.0f)
	{
		return;
	}
	const float LenSq = Direction.X * Direction.X + Direction.Y * Direction.Y + Direction.Z * Direction.Z;
	if (LenSq < 1e-8f)
	{
		return;
	}
	const float InvLen = 1.0f / sqrtf(LenSq);
	PendingMovementInput.X += Direction.X * InvLen * Scale;
	PendingMovementInput.Y += Direction.Y * InvLen * Scale;
	PendingMovementInput.Z += Direction.Z * InvLen * Scale;
}

FVector UPawnMovementComponent::ConsumeMovementInputVector()
{
	FVector Result = PendingMovementInput;
	PendingMovementInput = FVector::ZeroVector;
	return Result;
}

bool UPawnMovementComponent::ApplyControllerMovementInput(const FControllerMovementInput& Input)
{
	RecordControllerMovementInput(Input);
	if (Input.WorldDelta.IsNearlyZero())
	{
		return false;
	}

	AddMovementInput(Input.WorldDelta, Input.WorldDelta.Length());
	ApplyPendingMovement(Input.DeltaTime);
	return true;
}

void UPawnMovementComponent::ApplyPendingMovement(float DeltaTime)
{
	const FVector Delta = ConsumeMovementInputVector();
	if (Delta.IsNearlyZero())
	{
		return;
	}
	if (!ResolveUpdatedComponent())
	{
		return;
	}

	FVector AppliedDelta;
	SafeMoveUpdatedComponentPreserveInputAxes(
		Delta,
		GetLastControllerMovementForward(),
		GetLastControllerMovementRight(),
		&AppliedDelta,
		nullptr);

	if (DeltaTime > 0.0f)
	{
		MovementVelocity = AppliedDelta / DeltaTime;
	}
	else
	{
		MovementVelocity = AppliedDelta;
	}
}

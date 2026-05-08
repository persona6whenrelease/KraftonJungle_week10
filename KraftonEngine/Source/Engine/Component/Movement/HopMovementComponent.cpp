#include "HopMovementComponent.h"

#include "Component/SceneComponent.h"
#include "Math/MathUtils.h"
#include "Object/ObjectFactory.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"
#include "Sound/SoundManager.h"
#include <cmath>
#include <cstring>

namespace
{
	constexpr float KInputTolerance = 1.0e-4f;
	constexpr float KVelocityStopTolerance = 1.0e-3f;
	constexpr float KMaxReasonableSpeed = 100000.0f;
	constexpr float KMaxReasonableAcceleration = 1000000.0f;
	constexpr float KMaxReasonableHopHeight = 100000.0f;
	constexpr float KMaxReasonableHopFrequency = 240.0f;

	FVector FlattenOnWorldUp(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.0f);
	}

	FVector ClampInputMagnitude(const FVector& Input)
	{
		FVector FlatInput = FlattenOnWorldUp(Input);
		const float Length = FlatInput.Length();
		if (Length <= KInputTolerance)
		{
			return FVector::ZeroVector;
		}

		if (Length > 1.0f)
		{
			FlatInput /= Length;
		}
		return FlatInput;
	}

	FVector MoveTowards(const FVector& Current, const FVector& Target, float MaxDelta)
	{
		if (MaxDelta <= 0.0f)
		{
			return Target;
		}

		const FVector Delta = Target - Current;
		const float DeltaLength = Delta.Length();
		if (DeltaLength <= MaxDelta || DeltaLength <= KVelocityStopTolerance)
		{
			return Target;
		}

		return Current + (Delta / DeltaLength) * MaxDelta;
	}

	float GetHopPhase(float ElapsedTime, float Frequency)
	{
		const float CycleTime = std::fmod(ElapsedTime * Frequency, 1.0f);
		return CycleTime >= 0.0f ? CycleTime : CycleTime + 1.0f;
	}
}

IMPLEMENT_CLASS(UHopMovementComponent, UMovementComponent)

UHopMovementComponent::UHopMovementComponent()
{
	bReceiveControllerInput = true;
	ControllerInputPriority = 10;
	DashDelegate.Add(
		[]()
		{
			FSoundManager::Get().PlayEffect(SoundEffect::Dash);
		}
	);

}

void UHopMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();

	HopElapsedTime = 0.0f;
	AppliedHopOffset = 0.0f;
	DashTimeRemaining = 0.0f;
	PendingMovementInput = FVector::ZeroVector;
	LastMovementInput = FVector::ZeroVector;
	Velocity = FlattenOnWorldUp(Velocity);
	bSimulating = true;
	bHasLockedGameplayPlaneZ = false;
	CaptureGameplayPlane();
	ResolveVisualHopComponent(true);
	LockUpdatedComponentToGameplayPlane();
}

void UHopMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!bSimulating || !UpdatedSceneComponent || DeltaTime <= 0.0f)
	{
		LockUpdatedComponentToGameplayPlane();
		PendingMovementInput = FVector::ZeroVector;
		return;
	}
	CaptureGameplayPlane();
	LockUpdatedComponentToGameplayPlane();

	const FVector RawInput = ConsumeFrameMovementInput();
	const FVector MoveInput = ClampInputMagnitude(RawInput);
	const bool bHasMoveInput = MoveInput.Length() > KInputTolerance;

	if (IsDashing())
	{
		DashTimeRemaining -= DeltaTime;
		Velocity = FlattenOnWorldUp(Velocity);
		LockUpdatedComponentToGameplayPlane();
	}
	else
	{

		const float TargetSpeed = bHasMoveInput ? GetEffectiveMoveSpeed() : 0.0f;
		const FVector TargetVelocity = MoveInput * TargetSpeed;
		const float AccelRate = bHasMoveInput ? Acceleration : BrakingDeceleration;
		const float ClampedAccelRate = Clamp(AccelRate, 0.0f, KMaxReasonableAcceleration);

		Velocity = MoveTowards(FlattenOnWorldUp(Velocity), TargetVelocity, ClampedAccelRate * DeltaTime);
		if (Velocity.Length() <= KVelocityStopTolerance)
		{
			Velocity = FVector::ZeroVector;
		}

		const bool bIsHorizontallyMoving = Velocity.Length() > KVelocityStopTolerance;
		const bool bShouldApplyHop = (HopHeight > 0.0f && HopFrequency > 0.0f)
			&& (!bHopOnlyWhenMoving || bHasMoveInput || bIsHorizontallyMoving);

		float NewHopOffset = 0.0f;
		if (bShouldApplyHop)
		{
			HopElapsedTime += DeltaTime;
			const float Phase = GetHopPhase(HopElapsedTime, Clamp(HopFrequency, 0.0f, KMaxReasonableHopFrequency));
			const float ClampedHopHeight = Clamp(HopHeight, 0.0f, KMaxReasonableHopHeight);
			NewHopOffset = std::sin(Phase * FMath::Pi) * ClampedHopHeight;
		}
		else if (bResetHopWhenIdle)
		{
			HopElapsedTime = 0.0f;
		}

		AppliedHopOffset = NewHopOffset;

		const FVector HorizontalDelta = Velocity * DeltaTime;

		if (!HorizontalDelta.IsNearlyZero())
		{
			FVector SlideForward = GetLastControllerMovementForward();
			FVector SlideRight = GetLastControllerMovementRight();
			if (SlideForward.IsNearlyZero(KInputTolerance))
			{
				SlideForward = MoveInput;
			}
			if (SlideRight.IsNearlyZero(KInputTolerance) && !SlideForward.IsNearlyZero(KInputTolerance))
			{
				SlideRight = FVector(-SlideForward.Y, SlideForward.X, 0.0f);
			}

			FVector AppliedHorizontalDelta;
			if (SafeMoveUpdatedComponentPreserveInputAxes2D(HorizontalDelta, SlideForward, SlideRight, &AppliedHorizontalDelta, nullptr))
			{
				Velocity = DeltaTime > 0.0f ? FlattenOnWorldUp(AppliedHorizontalDelta) / DeltaTime : FVector::ZeroVector;
			}
			else
			{
				Velocity = FVector::ZeroVector;
			}
		}

		LockUpdatedComponentToGameplayPlane();
		ApplyVisualHopOffset(NewHopOffset);
	}

}

void UHopMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity, 0.0f, 0.0f, 1.0f });
	OutProps.push_back({ "Initial Speed", EPropertyType::Float, &InitialSpeed, 0.0f, KMaxReasonableSpeed, 1.0f });
	OutProps.push_back({ "Max Speed", EPropertyType::Float, &MaxSpeed, 0.0f, KMaxReasonableSpeed, 1.0f });
	OutProps.push_back({ "Hop Coefficient", EPropertyType::Float, &HopCoefficient, 0.0f, 100.0f, 0.1f });
	OutProps.push_back({ "Acceleration", EPropertyType::Float, &Acceleration, 0.0f, KMaxReasonableAcceleration, 10.0f });
	OutProps.push_back({ "Braking Deceleration", EPropertyType::Float, &BrakingDeceleration, 0.0f, KMaxReasonableAcceleration, 10.0f });
	OutProps.push_back({ "Hop Height", EPropertyType::Float, &HopHeight, 0.0f, KMaxReasonableHopHeight, 1.0f });
	OutProps.push_back({ "Hop Frequency", EPropertyType::Float, &HopFrequency, 0.0f, KMaxReasonableHopFrequency, 0.1f });
	OutProps.push_back({ "Hop Only When Moving", EPropertyType::Bool, &bHopOnlyWhenMoving });
	OutProps.push_back({ "Reset Hop When Idle", EPropertyType::Bool, &bResetHopWhenIdle });
	OutProps.push_back({ "Visual Hop Component", EPropertyType::SceneComponentRef, &VisualHopComponentPath });
	OutProps.push_back({ "Simulating", EPropertyType::Bool, &bSimulating });	
	OutProps.push_back({ "Dash Speed", EPropertyType::Float, &DashSpeed, 0.0f, KMaxReasonableSpeed, 1.0f });
	OutProps.push_back({ "Dash Duration", EPropertyType::Float, &DashDuration, 0.0f, 5.0f, 0.01f });
}

void UHopMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	// Keep the old field order first for compatibility with existing binary archives.
	Ar << Velocity;
	Ar << InitialSpeed;
	Ar << MaxSpeed;
	Ar << HopCoefficient;
	Ar << HopHeight;
	Ar << bSimulating;

	// New movement/hop configuration. Runtime input vectors are intentionally transient.
	Ar << Acceleration;
	Ar << BrakingDeceleration;
	Ar << HopFrequency;
	Ar << bHopOnlyWhenMoving;
	Ar << bResetHopWhenIdle;
	Ar << DashSpeed;
	Ar << DashDuration;
	Ar << VisualHopComponentPath;
	if (Ar.IsLoading())
	{
		MovementInput = FVector::ZeroVector;
		PendingMovementInput = FVector::ZeroVector;
		LastMovementInput = FVector::ZeroVector;
		HopElapsedTime = 0.0f;
		AppliedHopOffset = 0.0f;
		DashTimeRemaining = 0.0f;          // ★ 추가
		Velocity = FlattenOnWorldUp(Velocity);
		VisualHopComponent = nullptr;
		bHasVisualHopBaseRelativeLocation = false;
		bHasLockedGameplayPlaneZ = false;
		PlaneLockedComponent = nullptr;
	}
}

void UHopMovementComponent::PostEditProperty(const char* PropertyName)
{
	UMovementComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Visual Hop Component") == 0)
	{
		ResolveVisualHopComponent(true);
		return;
	}

	if (std::strcmp(PropertyName, "Updated Component") == 0)
	{
		bHasLockedGameplayPlaneZ = false;
		PlaneLockedComponent = nullptr;
		bHasVisualHopBaseRelativeLocation = false;
		CaptureGameplayPlane();
		ResolveVisualHopComponent(true);
		LockUpdatedComponentToGameplayPlane();
	}
}

void UHopMovementComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	(void)Scene;
}

void UHopMovementComponent::SetMovementInput(const FVector& InWorldInput)
{
	MovementInput = ClampInputMagnitude(InWorldInput);
}

void UHopMovementComponent::ClearMovementInput()
{
	MovementInput = FVector::ZeroVector;
	PendingMovementInput = FVector::ZeroVector;
	LastMovementInput = FVector::ZeroVector;
}

void UHopMovementComponent::AddMovementInput(const FVector& WorldDirection, float Scale)
{
	if (Scale == 0.0f)
	{
		return;
	}

	const FVector Direction = ClampInputMagnitude(WorldDirection);
	if (Direction.Length() <= KInputTolerance)
	{
		return;
	}

	PendingMovementInput += Direction * Scale;
}

void UHopMovementComponent::SetLocalMovementInput(const FVector& InLocalInput)
{
	SetMovementInput(BuildWorldInputFromLocal(InLocalInput));
}

void UHopMovementComponent::AddLocalMovementInput(const FVector& InLocalDirection, float Scale)
{
	AddMovementInput(BuildWorldInputFromLocal(InLocalDirection), Scale);
}

bool UHopMovementComponent::ApplyControllerMovementInput(const FControllerMovementInput& Input)
{
	RecordControllerMovementInput(Input);
	if (Input.WorldDirection.IsNearlyZero())
	{
		return false;
	}

	AddMovementInput(Input.WorldDirection, 1.0f);
	return true;
}

FVector UHopMovementComponent::BuildWorldInputFromLocal(const FVector& InLocalInput) const
{
	USceneComponent* BasisComponent = GetUpdatedComponent();
	if (!BasisComponent)
	{
		return ClampInputMagnitude(InLocalInput);
	}

	const FVector Forward = ClampInputMagnitude(BasisComponent->GetForwardVector());
	const FVector Right = ClampInputMagnitude(BasisComponent->GetRightVector());
	return ClampInputMagnitude((Forward * InLocalInput.X) + (Right * InLocalInput.Y));
}

FVector UHopMovementComponent::ConsumeFrameMovementInput()
{
	const FVector CombinedInput = MovementInput + PendingMovementInput;
	PendingMovementInput = FVector::ZeroVector;
	LastMovementInput = ClampInputMagnitude(CombinedInput);
	return LastMovementInput;
}

float UHopMovementComponent::GetEffectiveMoveSpeed() const
{
	const float BaseSpeed = Clamp(InitialSpeed, 0.0f, KMaxReasonableSpeed);
	const float SpeedScale = Clamp(HopCoefficient, 0.0f, 100.0f);
	const float SpeedCap = Clamp(MaxSpeed, 0.0f, KMaxReasonableSpeed);
	const float ScaledSpeed = BaseSpeed * SpeedScale;
	return SpeedCap > 0.0f ? Clamp(ScaledSpeed, 0.0f, SpeedCap) : ScaledSpeed;
}

void UHopMovementComponent::RemoveAppliedHopOffset()
{
	if (ResolveVisualHopComponent(false) && bHasVisualHopBaseRelativeLocation)
	{
		VisualHopComponent->SetRelativeLocation(VisualHopBaseRelativeLocation);
	}

	if (std::fabs(AppliedHopOffset) <= KVelocityStopTolerance)
	{
		AppliedHopOffset = 0.0f;
		LockUpdatedComponentToGameplayPlane();
		return;
	}

	AppliedHopOffset = 0.0f;
	LockUpdatedComponentToGameplayPlane();
}

void UHopMovementComponent::CaptureGameplayPlane()
{
	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}

	if (bHasLockedGameplayPlaneZ && PlaneLockedComponent == UpdatedSceneComponent)
	{
		return;
	}

	LockedGameplayPlaneZ = UpdatedSceneComponent->GetWorldLocation().Z;
	PlaneLockedComponent = UpdatedSceneComponent;
	bHasLockedGameplayPlaneZ = true;
}

void UHopMovementComponent::LockUpdatedComponentToGameplayPlane()
{
	if (!bHasLockedGameplayPlaneZ)
	{
		CaptureGameplayPlane();
	}

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent || !bHasLockedGameplayPlaneZ)
	{
		return;
	}

	FVector Location = UpdatedSceneComponent->GetWorldLocation();
	if (std::fabs(Location.Z - LockedGameplayPlaneZ) <= KVelocityStopTolerance)
	{
		return;
	}

	Location.Z = LockedGameplayPlaneZ;
	UpdatedSceneComponent->SetWorldLocation(Location);
}

bool UHopMovementComponent::ResolveVisualHopComponent(bool bResetBaseLocation)
{
	if (!VisualHopComponentPath.empty())
	{
		VisualHopComponent = FindUpdatedComponentByPath(VisualHopComponentPath);
	}
	else
	{
		VisualHopComponent = nullptr;
	}

	if (!VisualHopComponent)
	{
		VisualHopComponent = FindFallbackVisualHopComponent();
	}

	if (!VisualHopComponent || VisualHopComponent == GetUpdatedComponent())
	{
		VisualHopComponent = nullptr;
		bHasVisualHopBaseRelativeLocation = false;
		return false;
	}

	if (bResetBaseLocation || !bHasVisualHopBaseRelativeLocation)
	{
		VisualHopBaseRelativeLocation = VisualHopComponent->GetRelativeLocation();
		bHasVisualHopBaseRelativeLocation = true;
	}

	return true;
}

USceneComponent* UHopMovementComponent::FindFallbackVisualHopComponent() const
{
	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return nullptr;
	}

	const TArray<USceneComponent*>& Children = UpdatedSceneComponent->GetChildren();
	return Children.empty() ? nullptr : Children.front();
}

void UHopMovementComponent::ApplyVisualHopOffset(float NewHopOffset)
{
	if (!ResolveVisualHopComponent(false) || !bHasVisualHopBaseRelativeLocation)
	{
		return;
	}

	float ParentScaleZ = 1.0f;
	if (USceneComponent* Parent = VisualHopComponent->GetParent())
	{
		ParentScaleZ = Parent->GetWorldScale().Z;
	}
	if (std::fabs(ParentScaleZ) <= KInputTolerance)
	{
		ParentScaleZ = 1.0f;
	}

	FVector VisualLocation = VisualHopBaseRelativeLocation;
	VisualLocation.Z += NewHopOffset / ParentScaleZ;
	VisualHopComponent->SetRelativeLocation(VisualLocation);
}

void UHopMovementComponent::StopMovementImmediately()
{
	ClearMovementInput();
	Velocity = FVector::ZeroVector;
	HopElapsedTime = 0.0f;
	DashTimeRemaining = 0.0f;
	RemoveAppliedHopOffset();
}

void UHopMovementComponent::StopSimulating()
{
	bSimulating = false;
	StopMovementImmediately();
}

FVector UHopMovementComponent::GetPreviewVelocity() const
{
	const FVector Input = ClampInputMagnitude(MovementInput + PendingMovementInput);
	if (Input.Length() <= KInputTolerance)
	{
		return FVector::ZeroVector;
	}
	return Input * GetEffectiveMoveSpeed();
}

void UHopMovementComponent::Dash()
{
	if (!bSimulating)
	{
		return;
	}

	if (IsDashing())
	{
		return;
	}
	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}
	// 캐릭터 Forward를 수평 평탄화 후 정규화. 평탄화 결과가 거의 0이면 (캐릭터가 거의 똑바로 위/아래를
	// 보고 있는 극단적 상황) Dash를 포기. 일반 게임에서는 발생하지 않는다.
	FVector Forward = FlattenOnWorldUp(UpdatedSceneComponent->GetForwardVector());
	const float ForwardLength = Forward.Length();
	if (ForwardLength <= KInputTolerance)
	{
		return;
	}
	Forward /= ForwardLength;

	// Velocity 주입. Dash 동안 MoveTowards가 우회되므로 그대로 유지된다.
	Velocity = Forward * DashSpeed;
	DashTimeRemaining = DashDuration;

	DashDelegate.BroadCast();
}

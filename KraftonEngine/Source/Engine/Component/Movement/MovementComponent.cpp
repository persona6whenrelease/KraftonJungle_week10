#include "Component/Movement/MovementComponent.h"

#include "Component/SceneComponent.h"
#include "Core/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>

// Base movement logic only; concrete movement types should be added instead.
IMPLEMENT_CLASS(UMovementComponent, UActorComponent)
HIDE_FROM_COMPONENT_LIST(UMovementComponent)

namespace
{
	constexpr const char* RootComponentPathToken = "Root";
	constexpr float MovementAxisTolerance = 1.0e-4f;
	constexpr float MovementClipMinFraction = 1.0e-3f;
	constexpr int32 MovementClipIterations = 12;
	constexpr float MovementUnstuckMinDistance = 0.05f;
	constexpr float MovementUnstuckMaxDistance = 5.0f;

	void GatherSceneComponentsRecursive(USceneComponent* Component, TArray<USceneComponent*>& OutComponents)
	{
		if (!Component)
		{
			return;
		}

		OutComponents.push_back(Component);
		for (USceneComponent* Child : Component->GetChildren())
		{
			GatherSceneComponentsRecursive(Child, OutComponents);
		}
	}

	FVector FlattenOnWorldUp(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.0f);
	}

	FVector SafeNormalized(const FVector& Vector)
	{
		const float Length = Vector.Length();
		return Length > MovementAxisTolerance ? Vector / Length : FVector::ZeroVector;
	}

	FVector SafeHorizontalNormalized(const FVector& Vector)
	{
		return SafeNormalized(FlattenOnWorldUp(Vector));
	}

	bool IsSameDelta(const FVector& A, const FVector& B)
	{
		return (A - B).IsNearlyZero(MovementAxisTolerance);
	}

	void AddUniqueDelta(TArray<FVector>& Deltas, const FVector& Delta)
	{
		if (Delta.IsNearlyZero(MovementAxisTolerance))
		{
			return;
		}

		for (const FVector& Existing : Deltas)
		{
			if (IsSameDelta(Existing, Delta))
			{
				return;
			}
		}

		Deltas.push_back(Delta);
	}

	void AddUniqueAxis(TArray<FVector>& Axes, const FVector& Axis)
	{
		const FVector NormalizedAxis = SafeNormalized(Axis);
		if (NormalizedAxis.IsNearlyZero(MovementAxisTolerance))
		{
			return;
		}

		for (const FVector& Existing : Axes)
		{
			if (std::fabs(Existing.Dot(NormalizedAxis)) >= 0.999f)
			{
				return;
			}
		}

		Axes.push_back(NormalizedAxis);
	}

	void AddAxisProjectedDelta(TArray<FVector>& Deltas, const FVector& Delta, const FVector& Axis)
	{
		const FVector NormalizedAxis = SafeNormalized(Axis);
		if (NormalizedAxis.IsNearlyZero(MovementAxisTolerance))
		{
			return;
		}

		AddUniqueDelta(Deltas, NormalizedAxis * Delta.Dot(NormalizedAxis));
	}

	void BuildWorldAxisDeltas(const FVector& Delta, TArray<FVector>& OutDeltas)
	{
		AddUniqueDelta(OutDeltas, FVector(Delta.X, 0.0f, 0.0f));
		AddUniqueDelta(OutDeltas, FVector(0.0f, Delta.Y, 0.0f));
		AddUniqueDelta(OutDeltas, FVector(0.0f, 0.0f, Delta.Z));
	}

	void BuildWorldAxisDeltas2D(const FVector& Delta, TArray<FVector>& OutDeltas)
	{
		AddUniqueDelta(OutDeltas, FVector(Delta.X, 0.0f, 0.0f));
		AddUniqueDelta(OutDeltas, FVector(0.0f, Delta.Y, 0.0f));
	}

	void SetWorldLocationZ(USceneComponent* Scene, float PlaneZ)
	{
		if (!Scene)
		{
			return;
		}

		FVector Location = Scene->GetWorldLocation();
		if (std::fabs(Location.Z - PlaneZ) <= MovementAxisTolerance)
		{
			return;
		}

		Location.Z = PlaneZ;
		Scene->SetWorldLocation(Location);
	}

	void BuildInputAxisDeltas(const FVector& Delta, const FVector& InputForward, const FVector& InputRight, TArray<FVector>& OutDeltas)
	{
		const FVector HorizontalDelta = FlattenOnWorldUp(Delta);
		const FVector VerticalDelta(0.0f, 0.0f, Delta.Z);

		const FVector Forward = SafeHorizontalNormalized(InputForward);
		const FVector Right = SafeHorizontalNormalized(InputRight);

		TArray<FVector> HorizontalDeltas;
		if (!Forward.IsNearlyZero(MovementAxisTolerance))
		{
			AddAxisProjectedDelta(HorizontalDeltas, HorizontalDelta, Forward);
		}
		if (!Right.IsNearlyZero(MovementAxisTolerance))
		{
			AddAxisProjectedDelta(HorizontalDeltas, HorizontalDelta, Right);
		}

		if (!HorizontalDeltas.empty())
		{
			std::sort(HorizontalDeltas.begin(), HorizontalDeltas.end(), [](const FVector& A, const FVector& B)
			{
				return A.Dot(A) > B.Dot(B);
			});

			for (const FVector& AxisDelta : HorizontalDeltas)
			{
				AddUniqueDelta(OutDeltas, AxisDelta);
			}
			AddUniqueDelta(OutDeltas, VerticalDelta);
		}
	}
}

void UMovementComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	ResolveUpdatedComponent();
}

void UMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	// 기본 이동 컴포넌트는 별도 로직 없이 틱 파이프라인만 유지합니다.
	UActorComponent::TickComponent(DeltaTime,TickType, ThisTickFunction);
}

void UMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Auto Register Updated", EPropertyType::Bool, &bAutoRegisterUpdatedComponent });
	OutProps.push_back({ "Updated Component", EPropertyType::SceneComponentRef, &UpdatedComponentPath });
	OutProps.push_back({ "Receive Controller Input", EPropertyType::Bool, &bReceiveControllerInput });
	OutProps.push_back({ "Controller Input Priority", EPropertyType::Int, &ControllerInputPriority, -100.0f, 100.0f, 1.0f });
	OutProps.push_back({ "Last Controller Direction", EPropertyType::Vec3, &LastControllerWorldDirection, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Last Controller Delta", EPropertyType::Vec3, &LastControllerWorldDelta, 0.0f, 0.0f, 0.1f });
}

void UMovementComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << bAutoRegisterUpdatedComponent;
	Ar << UpdatedComponentPath;
	Ar << bReceiveControllerInput;
	Ar << ControllerInputPriority;
	// UpdatedComponent 포인터는 BeginPlay에서 재해결되므로 직렬화 제외.
}

void UMovementComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Auto Register Updated") == 0)
	{
		if (bAutoRegisterUpdatedComponent && UpdatedComponentPath.empty())
		{
			TryAutoRegisterUpdatedComponent();
		}
		return;
	}

	if (std::strcmp(PropertyName, "Updated Component") == 0)
	{
		ResolveUpdatedComponent();
	}
}

void UMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	UpdatedComponent = NewUpdatedComponent;
	UpdatedComponentPath = BuildUpdatedComponentPath(NewUpdatedComponent);
	if (UpdatedComponent)
	{
		bAutoRegisterUpdatedComponent = false;
	}
}

USceneComponent* UMovementComponent::GetUpdatedComponent() const
{
	return UpdatedComponent;
}

void UMovementComponent::ClearUpdatedComponentIfMatches(const USceneComponent* RemovedComponent)
{
	if (!RemovedComponent || UpdatedComponent != RemovedComponent)
	{
		return;
	}

	UpdatedComponent = nullptr;
	UpdatedComponentPath.clear();
	bAutoRegisterUpdatedComponent = true;
	TryAutoRegisterUpdatedComponent();
}

void UMovementComponent::RecordControllerMovementInput(const FControllerMovementInput& Input)
{
	LastControllerWorldDirection = Input.WorldDirection;
	LastControllerWorldDelta = Input.WorldDelta;
	LastControllerMovementForward = Input.MovementForward.IsNearlyZero() ? FVector::ForwardVector : Input.MovementForward.Normalized();
	LastControllerMovementRight = Input.MovementRight.IsNearlyZero() ? FVector::RightVector : Input.MovementRight.Normalized();
	LastControllerInputTime += Input.DeltaTime;
	if (Input.DeltaTime > 0.0f)
	{
		MovementVelocity = Input.WorldDelta / Input.DeltaTime;
	}
	else
	{
		MovementVelocity = FVector::ZeroVector;
	}
}

bool UMovementComponent::ApplyControllerMovementInput(const FControllerMovementInput& Input)
{
	RecordControllerMovementInput(Input);
	return false;
}

bool UMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, FHitResult* OutHit)
{
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	if (Delta.IsNearlyZero())
	{
		return true;
	}

	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	USceneComponent* Scene = GetUpdatedComponent();
	AActor* OwnerActor = GetOwner();
	if (!Scene || !OwnerActor)
	{
		return false;
	}

	UWorld* World = OwnerActor->GetWorld();
	const FVector OldLocation = Scene->GetWorldLocation();
	Scene->AddWorldOffset(Delta);

	if (World && World->HasBlockingOverlapForActor(OwnerActor, OutHit))
	{
		Scene->SetWorldLocation(OldLocation);
		return false;
	}

	return true;
}

bool UMovementComponent::SafeMoveUpdatedComponent2D(const FVector& Delta, float PlaneZ, FHitResult* OutHit)
{
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	const FVector FlatDelta = FlattenOnWorldUp(Delta);
	if (FlatDelta.IsNearlyZero())
	{
		return true;
	}

	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	USceneComponent* Scene = GetUpdatedComponent();
	AActor* OwnerActor = GetOwner();
	if (!Scene || !OwnerActor)
	{
		return false;
	}

	UWorld* World = OwnerActor->GetWorld();
	FVector OldLocation = Scene->GetWorldLocation();
	OldLocation.Z = PlaneZ;
	Scene->SetWorldLocation(OldLocation);
	Scene->AddWorldOffset(FlatDelta);
	SetWorldLocationZ(Scene, PlaneZ);

	if (World && World->HasBlockingOverlapForActor(OwnerActor, OutHit))
	{
		Scene->SetWorldLocation(OldLocation);
		return false;
	}

	return true;
}

bool UMovementComponent::IsUpdatedComponentBlockingOverlapped(FHitResult* OutHit) const
{
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	USceneComponent* Scene = GetUpdatedComponent();
	AActor* OwnerActor = GetOwner();
	if (!Scene || !OwnerActor)
	{
		return false;
	}

	UWorld* World = OwnerActor->GetWorld();
	return World ? World->HasBlockingOverlapForActor(OwnerActor, OutHit) : false;
}

bool UMovementComponent::SafeMoveUpdatedComponentClipped(const FVector& Delta, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	if (Delta.IsNearlyZero())
	{
		return true;
	}

	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	USceneComponent* Scene = GetUpdatedComponent();
	AActor* OwnerActor = GetOwner();
	if (!Scene || !OwnerActor)
	{
		return false;
	}

	FHitResult InitialHit;
	if (IsUpdatedComponentBlockingOverlapped(&InitialHit))
	{
		if (OutHit)
		{
			*OutHit = InitialHit;
		}
		return false;
	}

	FHitResult FullMoveHit;
	if (SafeMoveUpdatedComponent(Delta, &FullMoveHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = Delta;
		}
		return true;
	}

	if (OutHit)
	{
		*OutHit = FullMoveHit;
	}

	UWorld* World = OwnerActor->GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector OldLocation = Scene->GetWorldLocation();
	float SafeFraction = 0.0f;
	float BlockedFraction = 1.0f;

	for (int32 Iteration = 0; Iteration < MovementClipIterations; ++Iteration)
	{
		const float TestFraction = (SafeFraction + BlockedFraction) * 0.5f;
		Scene->SetWorldLocation(OldLocation);
		Scene->AddWorldOffset(Delta * TestFraction);

		FHitResult TestHit;
		if (World->HasBlockingOverlapForActor(OwnerActor, &TestHit))
		{
			BlockedFraction = TestFraction;
			if (OutHit && TestHit.bHit)
			{
				*OutHit = TestHit;
			}
		}
		else
		{
			SafeFraction = TestFraction;
		}
	}

	Scene->SetWorldLocation(OldLocation);

	if (SafeFraction <= MovementClipMinFraction)
	{
		return false;
	}

	const FVector AppliedDelta = Delta * SafeFraction;
	Scene->AddWorldOffset(AppliedDelta);
	if (World->HasBlockingOverlapForActor(OwnerActor, OutHit))
	{
		Scene->SetWorldLocation(OldLocation);
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = FVector::ZeroVector;
		}
		return false;
	}

	if (OutAppliedDelta)
	{
		*OutAppliedDelta = AppliedDelta;
	}
	return true;
}

bool UMovementComponent::SafeMoveUpdatedComponentClipped2D(const FVector& Delta, float PlaneZ, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	const FVector FlatDelta = FlattenOnWorldUp(Delta);
	if (FlatDelta.IsNearlyZero())
	{
		return true;
	}

	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	USceneComponent* Scene = GetUpdatedComponent();
	AActor* OwnerActor = GetOwner();
	if (!Scene || !OwnerActor)
	{
		return false;
	}

	SetWorldLocationZ(Scene, PlaneZ);

	FHitResult InitialHit;
	if (IsUpdatedComponentBlockingOverlapped(&InitialHit))
	{
		if (OutHit)
		{
			*OutHit = InitialHit;
		}
		return false;
	}

	FHitResult FullMoveHit;
	if (SafeMoveUpdatedComponent2D(FlatDelta, PlaneZ, &FullMoveHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = FlatDelta;
		}
		return true;
	}

	if (OutHit)
	{
		*OutHit = FullMoveHit;
	}

	UWorld* World = OwnerActor->GetWorld();
	if (!World)
	{
		return false;
	}

	FVector OldLocation = Scene->GetWorldLocation();
	OldLocation.Z = PlaneZ;
	float SafeFraction = 0.0f;
	float BlockedFraction = 1.0f;

	for (int32 Iteration = 0; Iteration < MovementClipIterations; ++Iteration)
	{
		const float TestFraction = (SafeFraction + BlockedFraction) * 0.5f;
		Scene->SetWorldLocation(OldLocation);
		Scene->AddWorldOffset(FlatDelta * TestFraction);
		SetWorldLocationZ(Scene, PlaneZ);

		FHitResult TestHit;
		if (World->HasBlockingOverlapForActor(OwnerActor, &TestHit))
		{
			BlockedFraction = TestFraction;
			if (OutHit && TestHit.bHit)
			{
				*OutHit = TestHit;
			}
		}
		else
		{
			SafeFraction = TestFraction;
		}
	}

	Scene->SetWorldLocation(OldLocation);

	if (SafeFraction <= MovementClipMinFraction)
	{
		return false;
	}

	const FVector AppliedDelta = FlatDelta * SafeFraction;
	Scene->AddWorldOffset(AppliedDelta);
	SetWorldLocationZ(Scene, PlaneZ);
	if (World->HasBlockingOverlapForActor(OwnerActor, OutHit))
	{
		Scene->SetWorldLocation(OldLocation);
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = FVector::ZeroVector;
		}
		return false;
	}

	if (OutAppliedDelta)
	{
		*OutAppliedDelta = AppliedDelta;
	}
	return true;
}

bool UMovementComponent::TryResolveBlockingOverlap(const FVector& DesiredDelta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	FHitResult InitialHit;
	if (!IsUpdatedComponentBlockingOverlapped(&InitialHit))
	{
		return false;
	}

	if (OutHit)
	{
		*OutHit = InitialHit;
	}

	TArray<FVector> ProbeAxes;
	AddUniqueAxis(ProbeAxes, InitialHit.WorldNormal);
	AddUniqueAxis(ProbeAxes, SafeNormalized(DesiredDelta) * -1.0f);
	AddUniqueAxis(ProbeAxes, InputForward);
	AddUniqueAxis(ProbeAxes, InputForward * -1.0f);
	AddUniqueAxis(ProbeAxes, InputRight);
	AddUniqueAxis(ProbeAxes, InputRight * -1.0f);
	AddUniqueAxis(ProbeAxes, FVector::ForwardVector);
	AddUniqueAxis(ProbeAxes, FVector::BackwardVector);
	AddUniqueAxis(ProbeAxes, FVector::RightVector);
	AddUniqueAxis(ProbeAxes, FVector::LeftVector);
	AddUniqueAxis(ProbeAxes, FVector::UpVector);
	AddUniqueAxis(ProbeAxes, FVector::DownVector);

	const float DesiredLength = DesiredDelta.Length();
	const float BaseDistance = (std::max)(MovementUnstuckMinDistance, (std::min)(MovementUnstuckMaxDistance, DesiredLength > MovementAxisTolerance ? DesiredLength : 1.0f));
	const float ProbeDistances[] =
	{
		MovementUnstuckMinDistance,
		BaseDistance * 0.25f,
		BaseDistance * 0.5f,
		BaseDistance,
		(std::min)(MovementUnstuckMaxDistance, BaseDistance * 2.0f)
	};

	for (const FVector& Axis : ProbeAxes)
	{
		for (float ProbeDistance : ProbeDistances)
		{
			if (ProbeDistance <= 0.0f)
			{
				continue;
			}

			FVector AppliedDelta;
			FHitResult ProbeHit;
			if (SafeMoveUpdatedComponent(Axis * ProbeDistance, &ProbeHit))
			{
				AppliedDelta = Axis * ProbeDistance;
				if (!IsUpdatedComponentBlockingOverlapped(nullptr))
				{
					if (OutAppliedDelta)
					{
						*OutAppliedDelta = AppliedDelta;
					}
					return true;
				}
			}
			else if (OutHit && ProbeHit.bHit)
			{
				*OutHit = ProbeHit;
			}
		}
	}

	return false;
}

bool UMovementComponent::TryResolveBlockingOverlap2D(const FVector& DesiredDelta, const FVector& InputForward, const FVector& InputRight, float PlaneZ, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	SetWorldLocationZ(GetUpdatedComponent(), PlaneZ);

	FHitResult InitialHit;
	if (!IsUpdatedComponentBlockingOverlapped(&InitialHit))
	{
		return false;
	}

	if (OutHit)
	{
		*OutHit = InitialHit;
	}

	TArray<FVector> ProbeAxes;
	AddUniqueAxis(ProbeAxes, FlattenOnWorldUp(InitialHit.WorldNormal));
	AddUniqueAxis(ProbeAxes, SafeHorizontalNormalized(DesiredDelta) * -1.0f);
	AddUniqueAxis(ProbeAxes, FlattenOnWorldUp(InputForward));
	AddUniqueAxis(ProbeAxes, FlattenOnWorldUp(InputForward) * -1.0f);
	AddUniqueAxis(ProbeAxes, FlattenOnWorldUp(InputRight));
	AddUniqueAxis(ProbeAxes, FlattenOnWorldUp(InputRight) * -1.0f);
	AddUniqueAxis(ProbeAxes, FVector::ForwardVector);
	AddUniqueAxis(ProbeAxes, FVector::BackwardVector);
	AddUniqueAxis(ProbeAxes, FVector::RightVector);
	AddUniqueAxis(ProbeAxes, FVector::LeftVector);

	const float DesiredLength = FlattenOnWorldUp(DesiredDelta).Length();
	const float BaseDistance = (std::max)(MovementUnstuckMinDistance, (std::min)(MovementUnstuckMaxDistance, DesiredLength > MovementAxisTolerance ? DesiredLength : 1.0f));
	const float ProbeDistances[] =
	{
		MovementUnstuckMinDistance,
		BaseDistance * 0.25f,
		BaseDistance * 0.5f,
		BaseDistance,
		(std::min)(MovementUnstuckMaxDistance, BaseDistance * 2.0f)
	};

	for (const FVector& Axis : ProbeAxes)
	{
		const FVector FlatAxis = SafeHorizontalNormalized(Axis);
		if (FlatAxis.IsNearlyZero(MovementAxisTolerance))
		{
			continue;
		}

		for (float ProbeDistance : ProbeDistances)
		{
			if (ProbeDistance <= 0.0f)
			{
				continue;
			}

			const FVector ProbeDelta = FlatAxis * ProbeDistance;
			FHitResult ProbeHit;
			if (SafeMoveUpdatedComponent2D(ProbeDelta, PlaneZ, &ProbeHit))
			{
				if (!IsUpdatedComponentBlockingOverlapped(nullptr))
				{
					if (OutAppliedDelta)
					{
						*OutAppliedDelta = ProbeDelta;
					}
					return true;
				}
			}
			else if (OutHit && ProbeHit.bHit)
			{
				*OutHit = ProbeHit;
			}
		}
	}

	return false;
}

bool UMovementComponent::SafeMoveUpdatedComponentPreserveAxes(const FVector& Delta, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	return SafeMoveUpdatedComponentPreserveInputAxes(Delta, FVector::ZeroVector, FVector::ZeroVector, OutAppliedDelta, OutHit);
}

bool UMovementComponent::SafeMoveUpdatedComponentPreserveInputAxes(const FVector& Delta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	if (Delta.IsNearlyZero())
	{
		return true;
	}
	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	FVector UnstuckDelta;
	if (TryResolveBlockingOverlap(Delta, InputForward, InputRight, &UnstuckDelta, OutHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = UnstuckDelta;
		}
		return true;
	}

	FHitResult FullMoveHit;
	if (SafeMoveUpdatedComponent(Delta, &FullMoveHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = Delta;
		}
		return true;
	}

	if (OutHit)
	{
		*OutHit = FullMoveHit;
	}

	FVector AppliedDelta = FVector::ZeroVector;
	FHitResult LastBlockingHit = FullMoveHit;
	bool bMovedAnyAxis = false;

	TArray<FVector> AxisDeltas;
	BuildInputAxisDeltas(Delta, InputForward, InputRight, AxisDeltas);
	if (AxisDeltas.empty())
	{
		BuildWorldAxisDeltas(Delta, AxisDeltas);
	}

	for (const FVector& AxisDelta : AxisDeltas)
	{
		if (AxisDelta.IsNearlyZero())
		{
			continue;
		}

		FHitResult AxisHit;
		FVector AxisAppliedDelta;
		if (SafeMoveUpdatedComponentClipped(AxisDelta, &AxisAppliedDelta, &AxisHit))
		{
			AppliedDelta += AxisAppliedDelta;
			bMovedAnyAxis = true;
		}
		else
		{
			LastBlockingHit = AxisHit;
		}
	}

	if (OutAppliedDelta)
	{
		*OutAppliedDelta = AppliedDelta;
	}
	if (OutHit && LastBlockingHit.bHit)
	{
		*OutHit = LastBlockingHit;
	}

	if (!bMovedAnyAxis)
	{
		FVector ClippedDelta;
		FHitResult ClippedHit;
		if (SafeMoveUpdatedComponentClipped(Delta, &ClippedDelta, &ClippedHit))
		{
			if (OutAppliedDelta)
			{
				*OutAppliedDelta = ClippedDelta;
			}
			return true;
		}

		if (OutHit && ClippedHit.bHit)
		{
			*OutHit = ClippedHit;
		}
	}

	return bMovedAnyAxis;
}

bool UMovementComponent::SafeMoveUpdatedComponentPreserveInputAxes2D(const FVector& Delta, const FVector& InputForward, const FVector& InputRight, FVector* OutAppliedDelta, FHitResult* OutHit)
{
	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FVector::ZeroVector;
	}
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	const FVector FlatDelta = FlattenOnWorldUp(Delta);
	if (FlatDelta.IsNearlyZero())
	{
		return true;
	}
	if (!ResolveUpdatedComponent())
	{
		return false;
	}

	USceneComponent* Scene = GetUpdatedComponent();
	if (!Scene)
	{
		return false;
	}

	const float PlaneZ = Scene->GetWorldLocation().Z;
	SetWorldLocationZ(Scene, PlaneZ);

	FVector UnstuckDelta;
	if (TryResolveBlockingOverlap2D(FlatDelta, InputForward, InputRight, PlaneZ, &UnstuckDelta, OutHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = UnstuckDelta;
		}
		SetWorldLocationZ(Scene, PlaneZ);
		return true;
	}

	FHitResult FullMoveHit;
	if (SafeMoveUpdatedComponent2D(FlatDelta, PlaneZ, &FullMoveHit))
	{
		if (OutAppliedDelta)
		{
			*OutAppliedDelta = FlatDelta;
		}
		SetWorldLocationZ(Scene, PlaneZ);
		return true;
	}

	if (OutHit)
	{
		*OutHit = FullMoveHit;
	}

	FVector AppliedDelta = FVector::ZeroVector;
	FHitResult LastBlockingHit = FullMoveHit;
	bool bMovedAnyAxis = false;

	TArray<FVector> AxisDeltas;
	BuildInputAxisDeltas(FlatDelta, InputForward, InputRight, AxisDeltas);
	if (AxisDeltas.empty())
	{
		BuildWorldAxisDeltas2D(FlatDelta, AxisDeltas);
	}

	for (const FVector& AxisDelta : AxisDeltas)
	{
		const FVector FlatAxisDelta = FlattenOnWorldUp(AxisDelta);
		if (FlatAxisDelta.IsNearlyZero())
		{
			continue;
		}

		FHitResult AxisHit;
		FVector AxisAppliedDelta;
		if (SafeMoveUpdatedComponentClipped2D(FlatAxisDelta, PlaneZ, &AxisAppliedDelta, &AxisHit))
		{
			AppliedDelta += AxisAppliedDelta;
			bMovedAnyAxis = true;
		}
		else
		{
			LastBlockingHit = AxisHit;
		}
	}

	if (OutAppliedDelta)
	{
		*OutAppliedDelta = FlattenOnWorldUp(AppliedDelta);
	}
	if (OutHit && LastBlockingHit.bHit)
	{
		*OutHit = LastBlockingHit;
	}

	if (!bMovedAnyAxis)
	{
		FVector ClippedDelta;
		FHitResult ClippedHit;
		if (SafeMoveUpdatedComponentClipped2D(FlatDelta, PlaneZ, &ClippedDelta, &ClippedHit))
		{
			if (OutAppliedDelta)
			{
				*OutAppliedDelta = ClippedDelta;
			}
			SetWorldLocationZ(Scene, PlaneZ);
			return true;
		}

		if (OutHit && ClippedHit.bHit)
		{
			*OutHit = ClippedHit;
		}
	}

	SetWorldLocationZ(Scene, PlaneZ);
	return bMovedAnyAxis;
}

void UMovementComponent::TryAutoRegisterUpdatedComponent()
{
	if (!bAutoRegisterUpdatedComponent)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	UpdatedComponent = OwnerActor->GetRootComponent();
	UpdatedComponentPath.clear();
}

TArray<USceneComponent*> UMovementComponent::GetOwnerSceneComponents() const
{
	TArray<USceneComponent*> Components;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return Components;
	}

	GatherSceneComponentsRecursive(OwnerActor->GetRootComponent(), Components);
	return Components;
}

FString UMovementComponent::GetUpdatedComponentDisplayName() const
{
	USceneComponent* CurrentUpdatedComponent = GetUpdatedComponent();
	if (!CurrentUpdatedComponent)
	{
		return bAutoRegisterUpdatedComponent ? FString("Auto (Root)") : FString("None");
	}

	FString DisplayName = CurrentUpdatedComponent->GetFName().ToString();
	if (DisplayName.empty())
	{
		DisplayName = CurrentUpdatedComponent->GetClass()->GetName();
	}

	const FString ComponentPath = BuildUpdatedComponentPath(CurrentUpdatedComponent);
	if (!ComponentPath.empty())
	{
		DisplayName += " (" + ComponentPath + ")";
	}
	return DisplayName;
}

bool UMovementComponent::ResolveUpdatedComponent()
{
	if (!UpdatedComponentPath.empty())
	{
		if (USceneComponent* ResolvedComponent = FindUpdatedComponentByPath(UpdatedComponentPath))
		{
			UpdatedComponent = ResolvedComponent;
			bAutoRegisterUpdatedComponent = false;
			return true;
		}
	}

	UpdatedComponent = nullptr;
	bAutoRegisterUpdatedComponent = true;
	TryAutoRegisterUpdatedComponent();
	return UpdatedComponent != nullptr;
}

FString UMovementComponent::BuildUpdatedComponentPath(const USceneComponent* TargetComponent) const
{
	if (!TargetComponent)
	{
		return FString();
	}

	AActor* OwnerActor = GetOwner();
	USceneComponent* RootComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	if (!RootComponent)
	{
		return FString();
	}

	if (TargetComponent == RootComponent)
	{
		return RootComponentPathToken;
	}

	TArray<int32> PathIndices;
	const USceneComponent* Current = TargetComponent;
	while (Current && Current != RootComponent)
	{
		const USceneComponent* Parent = Current->GetParent();
		if (!Parent)
		{
			return FString();
		}

		const TArray<USceneComponent*>& Siblings = Parent->GetChildren();
		int32 ChildIndex = -1;
		for (int32 Index = 0; Index < static_cast<int32>(Siblings.size()); ++Index)
		{
			if (Siblings[Index] == Current)
			{
				ChildIndex = Index;
				break;
			}
		}
		if (ChildIndex < 0)
		{
			return FString();
		}

		PathIndices.push_back(ChildIndex);
		Current = Parent;
	}

	if (Current != RootComponent)
	{
		return FString();
	}

	FString Path = RootComponentPathToken;
	for (auto It = PathIndices.rbegin(); It != PathIndices.rend(); ++It)
	{
		Path += "/";
		Path += std::to_string(*It);
	}
	return Path;
}

USceneComponent* UMovementComponent::FindUpdatedComponentByPath(const FString& InPath) const
{
	if (InPath.empty())
	{
		return nullptr;
	}

	AActor* OwnerActor = GetOwner();
	USceneComponent* Current = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	if (!Current)
	{
		return nullptr;
	}

	if (InPath == RootComponentPathToken)
	{
		return Current;
	}

	std::stringstream Stream(InPath);
	FString Segment;
	bool bExpectRoot = true;
	while (std::getline(Stream, Segment, '/'))
	{
		if (Segment.empty())
		{
			continue;
		}

		if (bExpectRoot)
		{
			bExpectRoot = false;
			if (Segment != RootComponentPathToken)
			{
				return nullptr;
			}
			continue;
		}

		char* ParseEnd = nullptr;
		const long ParsedIndex = std::strtol(Segment.c_str(), &ParseEnd, 10);
		if (ParseEnd == Segment.c_str() || *ParseEnd != '\0')
		{
			return nullptr;
		}

		const int32 ChildIndex = static_cast<int32>(ParsedIndex);
		const TArray<USceneComponent*>& Children = Current->GetChildren();
		if (ChildIndex < 0 || ChildIndex >= static_cast<int32>(Children.size()))
		{
			return nullptr;
		}

		Current = Children[ChildIndex];
		if (!Current)
		{
			return nullptr;
		}
	}

	return Current;
}

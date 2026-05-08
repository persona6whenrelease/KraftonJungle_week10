#include "ProjectileMovementComponent.h"

#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"
#include "Object/ObjectFactory.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"

#include <cmath>

namespace
{
	void AddProjectileVelocityArrow(FScene& Scene, const FVector& Start, const FVector& Velocity)
	{
		constexpr float ProjectileArrowScale = 0.25f;
		const FVector ScaledVelocity = Velocity * ProjectileArrowScale;
		const float VelocityLength = ScaledVelocity.Length();
		if (VelocityLength <= FMath::Epsilon)
		{
			return;
		}

		const FVector Direction = ScaledVelocity / VelocityLength;
		const FVector End = Start + ScaledVelocity;
		const FColor ArrowColor(135, 206, 235);

		Scene.AddDebugLine(Start, End, ArrowColor);

		const float HeadLength = Clamp(VelocityLength * 0.2f, 0.2f, 1.5f);
		FVector ReferenceUp(0.0f, 0.0f, 1.0f);
		if (std::abs(Direction.Dot(ReferenceUp)) > 0.98f)
		{
			ReferenceUp = FVector(0.0f, 1.0f, 0.0f);
		}

		const FVector Side = Direction.Cross(ReferenceUp).Normalized();
		const FVector Back = Direction * HeadLength;
		const FVector SideOffset = Side * (HeadLength * 0.45f);

		Scene.AddDebugLine(End, End - Back + SideOffset, ArrowColor);
		Scene.AddDebugLine(End, End - Back - SideOffset, ArrowColor);
	}
}

IMPLEMENT_CLASS(UProjectileMovementComponent, UMovementComponent)

void UProjectileMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
}

void UProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}

	FVector EffectiveVelocity = ComputeEffectiveVelocity();
	const float CurrentSpeed = EffectiveVelocity.Length();
	if (MaxSpeed > 0.0f && CurrentSpeed > MaxSpeed)
	{
		EffectiveVelocity = EffectiveVelocity.Normalized() * MaxSpeed;
	}

	const FVector MoveDelta = EffectiveVelocity * DeltaTime;
	if (MoveDelta.Length() <= FMath::Epsilon)
	{
		return;
	}

	UpdatedSceneComponent->AddWorldOffset(MoveDelta);
}

void UProjectileMovementComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UMovementComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity, 0.0f, 0.0f, 1.0f });
	OutProps.push_back({ "Initial Speed", EPropertyType::Float, &InitialSpeed, 0.0f, 0.0f, 10.0f });
	OutProps.push_back({ "Max Speed", EPropertyType::Float, &MaxSpeed, 0.0f, 0.0f, 10.0f });
}

void UProjectileMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	Ar << Velocity;
	Ar << InitialSpeed;
	Ar << MaxSpeed;
}

void UProjectileMovementComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector PreviewVelocity = GetPreviewVelocity();
	if (PreviewVelocity.Length() <= FMath::Epsilon)
	{
		return;
	}

	USceneComponent* SourceComponent = GetUpdatedComponent();
	if (!SourceComponent)
	{
		AActor* OwnerActor = GetOwner();
		SourceComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	}
	if (!SourceComponent)
	{
		return;
	}

	AddProjectileVelocityArrow(Scene, SourceComponent->GetWorldLocation(), PreviewVelocity);
}

void UProjectileMovementComponent::StopSimulating()
{
	Velocity = FVector(0.0f, 0.0f, 0.0f);
}

FVector UProjectileMovementComponent::GetPreviewVelocity() const
{
	return ComputeEffectiveVelocity();
}

EProjectileHitBehavior UProjectileMovementComponent::GetHitBehavior() const
{
	return EProjectileHitBehavior::Stop;
}

FVector UProjectileMovementComponent::ComputeEffectiveVelocity() const
{
	FVector EffectiveVelocity = Velocity;

	if (EffectiveVelocity.Length() <= FMath::Epsilon)
	{
		USceneComponent* SourceComponent = GetUpdatedComponent();
		if (!SourceComponent)
		{
			AActor* OwnerActor = GetOwner();
			SourceComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
		}

		if (SourceComponent)
		{
			EffectiveVelocity = SourceComponent->GetForwardVector().Normalized();
		}
	}

	if (InitialSpeed > 0.0f && EffectiveVelocity.Length() > FMath::Epsilon)
	{
		EffectiveVelocity *= InitialSpeed;
	}

	return EffectiveVelocity;
}

bool UProjectileMovementComponent::HandleBlockingHit(USceneComponent* UpdatedSceneComponent, const FVector& CurrentLocation, const FVector& MoveDelta, const FHitResult& HitResult)
{
	(void)UpdatedSceneComponent;
	(void)CurrentLocation;
	(void)MoveDelta;
	(void)HitResult;

	switch (GetHitBehavior())
	{
	case EProjectileHitBehavior::Stop:
		StopSimulating();
		return true;
	case EProjectileHitBehavior::Bounce:
		// 정확한 반사에는 HitResult.WorldNormal 계산이 필요하다.
		// 현재 충돌 질의는 bool/상대 컴포넌트만 제공하므로 우선 Stop과 동일하게 처리한다.
		StopSimulating();
		return true;
	case EProjectileHitBehavior::Destroy:
		// DestroyActor 연결은 게임플레이 정책이므로 여기서는 이동만 정지한다.
		StopSimulating();
		return true;
	default:
		StopSimulating();
		return true;
	}
}

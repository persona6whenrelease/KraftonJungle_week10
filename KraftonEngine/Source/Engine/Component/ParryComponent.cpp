#include "ParryComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Sound/SoundManager.h"
#include "GameFramework/World.h"
#include "Math/Vector.h"

IMPLEMENT_CLASS(UParryComponent, UActorComponent)

void UParryComponent::BeginPlay()
{

	AActor* Owner = GetOwner();
	if (!Owner) return;

	ParryDelegate.Add(
		[]()
		{
			FSoundManager::Get().PlayEffect(SoundEffect::Parry);
		}
	);

	// Find the best ScaleTarget (prefer BoxComponent)
	for (UActorComponent* Comp : Owner->GetComponents())
	{
		if (UBoxComponent* Box = Cast<UBoxComponent>(Comp))
		{
			ScaleTarget = Box;
			break;
		}
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp))
		{
			if (!ScaleTarget)
			{
				ScaleTarget = Prim;
			}
		}
	}

	if (ScaleTarget)
	{
		if (UBoxComponent* Box = Cast<UBoxComponent>(ScaleTarget))
		{
			ParryDelegate.AddDynamic(Box, &UBoxComponent::OnParry);
		}
		else if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(ScaleTarget))
		{
			ParryDelegate.AddDynamic(Prim, &UPrimitiveComponent::OnParry);
		}
	}
}

void UParryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	if (bIsParrying)
	{
		CurrentParryTime += DeltaTime;
		if (CurrentParryTime >= ParryDuration)
		{
			if (ScaleTarget && IsAliveObject(ScaleTarget))
				ScaleTarget->SetRelativeScale(OriginalScale);
			else
				ScaleTarget = nullptr;
			bIsParrying = false;
			CurrentParryTime = 0.f;
		}
	}

	if (SpinningProjectiles.empty()) return;

	// Y축(Pitch) 기준 초당 회전량: 3바퀴(1080°) / SpinDuration
	constexpr float TotalDegrees = 3.0f * 360.0f;
	const float DeltaDeg = (TotalDegrees / SpinDuration) * DeltaTime;

	TArray<FSpinningProjectile> Remaining;
	for (FSpinningProjectile& Spinning : SpinningProjectiles)
	{
		AActor* Actor = Spinning.Actor;
		if (!Actor || !IsAliveObject(Actor) || !Actor->GetWorld())
		{
			continue;
		}

		Spinning.ElapsedTime += DeltaTime;

		if (USceneComponent* Root = Actor->GetRootComponent())
			Root->AddLocalRotation(FRotator(DeltaDeg, 0.0f, 0.0f));

		if (Spinning.ElapsedTime < SpinDuration)
		{
			Remaining.push_back(Spinning);
		}
		else
		{
			Actor->SetVisible(false);
			Actor->SetActorTickEnabled(false);
			Actor->SetActorEnableCollision(false);
		}
	}
	SpinningProjectiles = std::move(Remaining);
}

void UParryComponent::Parry()
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsAliveObject(Owner) || !Owner->GetWorld()) return;

	if (bIsParrying) return;
	bIsParrying = true;
	CurrentParryTime = 0.f;

	if (ScaleTarget)
	{
		OriginalScale = ScaleTarget->GetRelativeScale();
	}

	ParryDelegate.BroadCast();

	DeflectNearbyProjectiles();
}

void UParryComponent::DeflectNearbyProjectiles()
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsAliveObject(Owner)) return;

	UWorld* World = Owner->GetWorld();
	if (!World) return;

	FVector OwnerPos = Owner->GetActorLocation();

	FBoundingBox QueryBounds;
	QueryBounds.Min = OwnerPos - FVector(ParryRadius, ParryRadius, ParryRadius);
	QueryBounds.Max = OwnerPos + FVector(ParryRadius, ParryRadius, ParryRadius);

	TArray<UPrimitiveComponent*> Candidates;
	World->QueryPrimitivesInAABB(QueryBounds, Candidates);

	TSet<AActor*> Processed;
	for (UPrimitiveComponent* Prim : Candidates)
	{
		if (!Prim || !IsAliveObject(Prim)) continue;

		AActor* OtherActor = Prim->GetOwner();
		if (!OtherActor || !IsAliveObject(OtherActor) || OtherActor == Owner) continue;
		if (OtherActor->GetWorld() != World) continue;
		if (Processed.contains(OtherActor)) continue;
		if (Cast<APawn>(OtherActor)) continue;

		UProjectileMovementComponent* Projectile = nullptr;
		for (UActorComponent* Comp : OtherActor->GetComponents())
		{
			Projectile = Cast<UProjectileMovementComponent>(Comp);
			if (Projectile) break;
		}
		if (!Projectile) continue;
		if (Projectile->IsParried()) continue;
		Projectile->Deactivate();
		Processed.insert(OtherActor);

		Projectile->StopSimulating();
		Projectile->SetParried(true);
		SpinningProjectiles.push_back({ OtherActor, Projectile, 0.0f });
	}
}

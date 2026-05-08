#include "Collision/WorldCollisionSystem.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Collision/PrimitiveCollision.h"
#include "Core/CollisionTypes.h"
#include "Component/Collision/CollisionTypes.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Object/ObjectFactory.h"

FWorldCollisionSystem::FWorldCollisionSystem(UWorld* InWorld)
	: World(InWorld)
{
}

FWorldCollisionSystem::~FWorldCollisionSystem()
{
}

void FWorldCollisionSystem::Reset()
{
	WorldCollisionBVH.Reset();
	PreviousOverlapPairs.clear();
	CurrentOverlapPairs.clear();
}

bool FWorldCollisionSystem::HasBlockingOverlapForActor(AActor* MovingActor, FHitResult* OutHit)
{
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	if (!World || !MovingActor || MovingActor->IsPooledActorInactive() || !MovingActor->IsActorCollisionEnabled())
	{
		return false;
	}

	WorldCollisionBVH.EnsureBuilt(World->GetActors());

	for (UPrimitiveComponent* MovingPrimitive : MovingActor->GetPrimitiveComponents())
	{
		if (!MovingPrimitive ||
			MovingPrimitive->GetCollisionShapeType() == ECollisionShapeType::None ||
			!MovingPrimitive->BlocksMovement())
		{
			continue;
		}

		const FBoundingBox MovingBounds = MovingPrimitive->GetWorldAABB();
		if (!MovingBounds.IsValid())
		{
			continue;
		}

		TArray<UPrimitiveComponent*> Candidates;
		WorldCollisionBVH.QueryAABB(MovingBounds, Candidates);

		for (UPrimitiveComponent* Candidate : Candidates)
		{
			if (!Candidate || Candidate == MovingPrimitive || Candidate->GetOwner() == MovingActor)
			{
				continue;
			}

			AActor* CandidateOwner = Candidate->GetOwner();
			if (!CandidateOwner ||
				CandidateOwner->IsPooledActorInactive() ||
				!CandidateOwner->IsActorCollisionEnabled() ||
				Candidate->GetCollisionShapeType() == ECollisionShapeType::None ||
				!Candidate->BlocksMovement())
			{
				continue;
			}

			if (FPrimitiveCollision::Intersect(MovingPrimitive, Candidate))
			{
				DispatchVehicleDefeatEvent(MovingActor, CandidateOwner);
				if (OutHit)
				{
					const FVector Separation = MovingPrimitive->GetWorldLocation() - Candidate->GetWorldLocation();
					OutHit->bHit = true;
					OutHit->HitComponent = Candidate;
					OutHit->WorldHitLocation = MovingPrimitive->GetWorldLocation();
					OutHit->WorldNormal = Separation.IsNearlyZero() ? FVector::ZeroVector : Separation.Normalized();
					OutHit->Distance = 0.0f;
				}
				return true;
			}
		}
	}

	return false;
}

void FWorldCollisionSystem::UpdateCollision()
{
	if (!World) return;

	WorldCollisionBVH.EnsureBuilt(World->GetActors());

	TArray<FWorldCollisionBVH::FOverlapCandidatePair> PotentialPairs;
	WorldCollisionBVH.GeneratePotentialPairs(PotentialPairs);

	PreviousOverlapPairs = std::move(CurrentOverlapPairs);
	CurrentOverlapPairs.clear();

	TSet<FOverlapPairKey> BeginOverlaps;

	for (const FWorldCollisionBVH::FOverlapCandidatePair& Pair : PotentialPairs)
	{
		if (!Pair.A || !Pair.B ||
			(!Pair.A->ShouldGenerateOverlapEvents() && !Pair.B->ShouldGenerateOverlapEvents()))
		{
			continue;
		}

		if (FPrimitiveCollision::Intersect(Pair.A, Pair.B))
		{
			FOverlapPairKey OverlapKey(Pair.A, Pair.B);
			CurrentOverlapPairs.insert(OverlapKey);

			if (PreviousOverlapPairs.find(OverlapKey) == PreviousOverlapPairs.end())
			{
				BeginOverlaps.insert(OverlapKey);
			}
		}
	}

	// Dispatch events for new overlaps (BeginOverlap / OnOverlap)
	// We dispatch for all CurrentOverlapPairs to maintain the same behavior as before
	// while supporting future expansion to BeginOverlap, OverlapStay, etc.
	DispatchOverlapEvents(CurrentOverlapPairs);
}

void FWorldCollisionSystem::DispatchOverlapEvents(const TSet<FOverlapPairKey>& Overlaps)
{
	TSet<uint64> DispatchedActorPairs;

	for (const FOverlapPairKey& Overlap : Overlaps)
	{
		AActor* ActorA = Overlap.ComponentA ? Overlap.ComponentA->GetOwner() : nullptr;
		AActor* ActorB = Overlap.ComponentB ? Overlap.ComponentB->GetOwner() : nullptr;

		if (ActorA && ActorB && ActorA != ActorB)
		{
			uint32 UUIDA = ActorA->GetUUID();
			uint32 UUIDB = ActorB->GetUUID();
			if (UUIDB < UUIDA)
			{
				std::swap(UUIDA, UUIDB);
			}

			const uint64 PairKey = (static_cast<uint64>(UUIDA) << 32) | UUIDB;
			if (DispatchedActorPairs.insert(PairKey).second)
			{
				DispatchVehicleDefeatEvent(ActorA, ActorB);

				// Dispatch A -> B
				for (UActorComponent* Component : ActorA->GetComponents())
				{
					if (ULuaScriptComponent* LuaScript = Cast<ULuaScriptComponent>(Component))
					{
						FLuaScriptSubsystem::Get().CallComponentOverlap(LuaScript, ActorB);
					}
				}
				// Dispatch B -> A
				for (UActorComponent* Component : ActorB->GetComponents())
				{
					if (ULuaScriptComponent* LuaScript = Cast<ULuaScriptComponent>(Component))
					{
						FLuaScriptSubsystem::Get().CallComponentOverlap(LuaScript, ActorA);
					}
				}
			}
		}
	}
}

void FWorldCollisionSystem::DispatchVehicleDefeatEvent(AActor* ActorA, AActor* ActorB)
{
	if (!ActorA || !ActorB || ActorA == ActorB)
	{
		return;
	}

	AActor* VehicleActor = nullptr;
	AActor* OtherActor = nullptr;
	if (ActorA->HasTag("Vehicle"))
	{
		VehicleActor = ActorA;
		OtherActor = ActorB;
	}
	else if (ActorB->HasTag("Vehicle"))
	{
		VehicleActor = ActorB;
		OtherActor = ActorA;
	}

	if (VehicleActor && Cast<APawn>(OtherActor))
	{
		FLuaScriptSubsystem::Get().DispatchGameEvent("Defeat", VehicleActor);
	}
}

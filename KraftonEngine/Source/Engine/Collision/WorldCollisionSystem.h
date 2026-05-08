#pragma once

#include "Core/CoreTypes.h"
#include "Collision/WorldCollisionBVH.h"

class UWorld;
class AActor;
class UPrimitiveComponent;
struct FHitResult;

// Key type for previous and current overlap pairs.
struct FOverlapPairKey
{
	UPrimitiveComponent* ComponentA = nullptr;
	UPrimitiveComponent* ComponentB = nullptr;

	FOverlapPairKey() = default;
	FOverlapPairKey(UPrimitiveComponent* InA, UPrimitiveComponent* InB)
	{
		if (InA < InB)
		{
			ComponentA = InA;
			ComponentB = InB;
		}
		else
		{
			ComponentA = InB;
			ComponentB = InA;
		}
	}

	bool operator==(const FOverlapPairKey& Other) const
	{
		return ComponentA == Other.ComponentA && ComponentB == Other.ComponentB;
	}
};

namespace std
{
	template <>
	struct hash<FOverlapPairKey>
	{
		size_t operator()(const FOverlapPairKey& Key) const
		{
			size_t HashA = std::hash<UPrimitiveComponent*>()(Key.ComponentA);
			size_t HashB = std::hash<UPrimitiveComponent*>()(Key.ComponentB);
			return HashA ^ (HashB + 0x9e3779b9 + (HashA << 6) + (HashA >> 2));
		}
	};
}

class FWorldCollisionSystem
{
public:
	FWorldCollisionSystem(UWorld* InWorld);
	~FWorldCollisionSystem();

	void Reset();

	void UpdateCollision();
	bool HasBlockingOverlapForActor(AActor* MovingActor, FHitResult* OutHit = nullptr);

	// Forward BVH operations
	void MarkDirty() { WorldCollisionBVH.MarkDirty(); }
	void EnsureBuilt(const TArray<AActor*>& Actors) { WorldCollisionBVH.EnsureBuilt(Actors); }
	void BuildNow(const TArray<AActor*>& Actors) { WorldCollisionBVH.BuildNow(Actors); }
	bool InsertObject(UPrimitiveComponent* Primitive) { return WorldCollisionBVH.InsertObject(Primitive); }
	bool RemoveObject(UPrimitiveComponent* Primitive) { return WorldCollisionBVH.RemoveObject(Primitive); }
	bool UpdateObject(UPrimitiveComponent* Primitive) { return WorldCollisionBVH.UpdateObject(Primitive); }
	void CollectDebugAABBs(TArray<FWorldCollisionBVH::FDebugAABB>& OutAABBs, bool bIncludeFatBounds = false) const
	{
		WorldCollisionBVH.CollectDebugAABBs(OutAABBs, bIncludeFatBounds);
	}
	void QueryAABB(const FBoundingBox& QueryBounds, TArray<UPrimitiveComponent*>& OutCandidates) const
	{
		WorldCollisionBVH.QueryAABB(QueryBounds, OutCandidates);
	}

	const TSet<FOverlapPairKey>& GetCurrentOverlaps() const { return CurrentOverlapPairs; }

private:
	void DispatchOverlapEvents(const TSet<FOverlapPairKey>& NewOverlaps);

	UWorld* World = nullptr;
	FWorldCollisionBVH WorldCollisionBVH;

	TSet<FOverlapPairKey> PreviousOverlapPairs;
	TSet<FOverlapPairKey> CurrentOverlapPairs;
};

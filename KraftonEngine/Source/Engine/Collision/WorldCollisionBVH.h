#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Collision/DynamicAABBTree.h"

class AActor;
class UPrimitiveComponent;

class FWorldCollisionBVH
{
public:
	struct FOverlapCandidatePair
	{
		UPrimitiveComponent* A = nullptr;
		UPrimitiveComponent* B = nullptr;
	};

	struct FDebugAABB
	{
		FVector Min;
		FVector Max;
		FColor Color;
	};

	void MarkDirty();
	void Reset();
	void BuildNow(const TArray<AActor*>& Actors);
	void EnsureBuilt(const TArray<AActor*>& Actors);

	bool InsertObject(UPrimitiveComponent* Primitive);
	bool RemoveObject(UPrimitiveComponent* Primitive);
	bool UpdateObject(UPrimitiveComponent* Primitive);
	bool ContainsObject(UPrimitiveComponent* Primitive) const;

	void QueryAABB(const FBoundingBox& QueryBounds, TArray<UPrimitiveComponent*>& OutCandidates) const;
	void GeneratePotentialPairs(TArray<FOverlapCandidatePair>& OutPairs) const;
	void CollectDebugAABBs(TArray<FDebugAABB>& OutAABBs, bool bIncludeFatBounds = false) const;

private:
	struct FNodePair
	{
		int32 A = TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE;
		int32 B = TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE;
	};

	static bool IsCollisionPrimitive(UPrimitiveComponent* Primitive);
	static FBoundingBox MakeFatBounds(const FBoundingBox& Bounds);

	void ValidateBVH() const;

	bool bDirty = true;
	TDynamicAABBTree<UPrimitiveComponent*> Tree;
	TMap<UPrimitiveComponent*, int32> ObjectToLeafNode;
};

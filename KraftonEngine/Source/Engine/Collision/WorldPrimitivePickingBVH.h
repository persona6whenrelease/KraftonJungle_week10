#pragma once

#include "Engine/Core/CoreTypes.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "Core/EngineTypes.h"
#include "Collision/DynamicAABBTree.h"

class AActor;
class UPrimitiveComponent;
class UStaticMeshComponent;

class FWorldPrimitivePickingBVH
{
public:
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
	void CollectDebugAABBs(TArray<FDebugAABB>& OutAABBs, bool bIncludeFatBounds = false) const;

	bool Raycast(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor, const FRaycastQueryParams& Params = FRaycastQueryParams()) const;

private:
	struct FPickingUserData
	{
		UPrimitiveComponent* Primitive = nullptr;
		UStaticMeshComponent* StaticMeshPrimitive = nullptr;
		AActor* Owner = nullptr;
	};

	static FBoundingBox MakeFatBounds(const FBoundingBox& Bounds);

	void ValidateBVH() const;

	bool bDirty = true;
	TDynamicAABBTree<FPickingUserData> Tree;
	TMap<UPrimitiveComponent*, int32> ObjectToLeafNode;
};

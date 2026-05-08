#include "Collision/WorldPrimitivePickingBVH.h"

#include "Collision/RayUtils.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/AActor.h"

namespace
{
	constexpr float WorldBVHFatBoundsScale = 0.15f;
	constexpr float WorldBVHFatBoundsMinPadding = 1.0f;

	const FColor WorldBVHDepthColors[] = {
		FColor(255,   0,   0),
		FColor(255, 165,   0),
		FColor(255, 255,   0),
		FColor(0,   255,   0),
		FColor(0,   255, 255),
		FColor(0,     0, 255),
	};
	constexpr int32 WorldBVHDepthColorCount = sizeof(WorldBVHDepthColors) / sizeof(WorldBVHDepthColors[0]);
}

void FWorldPrimitivePickingBVH::MarkDirty()
{
	bDirty = true;
}

void FWorldPrimitivePickingBVH::Reset()
{
	Tree.Reset();
	ObjectToLeafNode.clear();
	bDirty = false;
}

void FWorldPrimitivePickingBVH::BuildNow(const TArray<AActor*>& Actors)
{
	Reset();

	for (AActor* Actor : Actors)
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			InsertObject(Primitive);
		}
	}

	bDirty = false;
	ValidateBVH();
}

void FWorldPrimitivePickingBVH::EnsureBuilt(const TArray<AActor*>& Actors)
{
	if (bDirty)
	{
		BuildNow(Actors);
	}
}

bool FWorldPrimitivePickingBVH::InsertObject(UPrimitiveComponent* Primitive)
{
	if (!Primitive || ContainsObject(Primitive))
	{
		return false;
	}

	AActor* Owner = Primitive->GetOwner();
	if (!Owner || !Owner->IsVisible() || !Primitive->IsVisible())
	{
		return false;
	}

	const FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
	if (!Bounds.IsValid())
	{
		return false;
	}

	FPickingUserData UserData;
	UserData.Primitive = Primitive;
	UserData.StaticMeshPrimitive = Cast<UStaticMeshComponent>(Primitive);
	UserData.Owner = Owner;

	const int32 LeafNodeIndex = Tree.Insert(UserData, Bounds, MakeFatBounds(Bounds));
	if (LeafNodeIndex == TDynamicAABBTree<FPickingUserData>::INDEX_NONE)
	{
		return false;
	}

	ObjectToLeafNode[Primitive] = LeafNodeIndex;
	bDirty = false;
	ValidateBVH();
	return true;
}

bool FWorldPrimitivePickingBVH::RemoveObject(UPrimitiveComponent* Primitive)
{
	auto It = ObjectToLeafNode.find(Primitive);
	if (It == ObjectToLeafNode.end())
	{
		return false;
	}

	const int32 LeafNodeIndex = It->second;
	Tree.Remove(LeafNodeIndex);
	ObjectToLeafNode.erase(It);
	bDirty = false;
	ValidateBVH();
	return true;
}

bool FWorldPrimitivePickingBVH::UpdateObject(UPrimitiveComponent* Primitive)
{
	auto It = ObjectToLeafNode.find(Primitive);
	if (It == ObjectToLeafNode.end())
	{
		return InsertObject(Primitive);
	}

	if (!Primitive)
	{
		return false;
	}

	AActor* Owner = Primitive->GetOwner();
	if (!Owner || !Owner->IsVisible() || !Primitive->IsVisible())
	{
		return RemoveObject(Primitive);
	}

	const FBoundingBox NewBounds = Primitive->GetWorldBoundingBox();
	if (!NewBounds.IsValid())
	{
		return RemoveObject(Primitive);
	}

	const int32 LeafNodeIndex = It->second;
	auto& Leaf = Tree.GetNode(LeafNodeIndex);
	if (!Leaf.FatBounds.IsContains(NewBounds))
	{
		RemoveObject(Primitive);
		return InsertObject(Primitive);
	}

	Leaf.UserData.Primitive = Primitive;
	Leaf.UserData.StaticMeshPrimitive = Cast<UStaticMeshComponent>(Primitive);
	Leaf.UserData.Owner = Owner;
	Tree.UpdateBoundsFast(LeafNodeIndex, NewBounds);

	bDirty = false;
	ValidateBVH();
	return true;
}

bool FWorldPrimitivePickingBVH::ContainsObject(UPrimitiveComponent* Primitive) const
{
	return ObjectToLeafNode.find(Primitive) != ObjectToLeafNode.end();
}

void FWorldPrimitivePickingBVH::CollectDebugAABBs(TArray<FDebugAABB>& OutAABBs, bool bIncludeFatBounds) const
{
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<FPickingUserData>::INDEX_NONE || 
		!TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())))
	{
		return;
	}

	TArray<int32> Stack;
	Stack.reserve(Nodes.size());
	Stack.push_back(RootNodeIndex);

	while (!Stack.empty())
	{
		const int32 NodeIndex = Stack.back();
		Stack.pop_back();

		if (!TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size())))
		{
			continue;
		}

		const auto& Node = Nodes[NodeIndex];
		if (!Node.Bounds.IsValid())
		{
			continue;
		}

		const FColor Color = Node.IsLeaf()
			? FColor(220, 255, 220)
			: WorldBVHDepthColors[Node.Depth % WorldBVHDepthColorCount];
		OutAABBs.push_back({ Node.Bounds.Min, Node.Bounds.Max, Color });

		if (bIncludeFatBounds && Node.IsLeaf() && Node.FatBounds.IsValid())
		{
			OutAABBs.push_back({ Node.FatBounds.Min, Node.FatBounds.Max, FColor(80, 180, 255) });
		}

		if (!Node.IsLeaf())
		{
			Stack.push_back(Node.Right);
			Stack.push_back(Node.Left);
		}
	}
}

bool FWorldPrimitivePickingBVH::Raycast(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor) const
{
	struct FTraversalEntry
	{
		int32 NodeIndex = TDynamicAABBTree<FPickingUserData>::INDEX_NONE;
		float TMin = 0.0f;
	};

	OutHitResult = {};
	OutActor = nullptr;

	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<FPickingUserData>::INDEX_NONE || 
		!TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())))
	{
		return false;
	}

	float RootTMin = 0.0f;
	float RootTMax = 0.0f;
	if (!FRayUtils::IntersectRayAABB(Ray, Nodes[RootNodeIndex].Bounds.Min, Nodes[RootNodeIndex].Bounds.Max, RootTMin, RootTMax))
	{
		return false;
	}

	TArray<FTraversalEntry> NodeStack;
	NodeStack.reserve(Nodes.size());
	NodeStack.push_back({ RootNodeIndex, RootTMin });

	while (!NodeStack.empty())
	{
		const FTraversalEntry Entry = NodeStack.back();
		NodeStack.pop_back();
		if (Entry.TMin >= OutHitResult.Distance)
		{
			continue;
		}

		const auto& Node = Nodes[Entry.NodeIndex];
		if (Node.IsLeaf())
		{
			const auto& UserData = Node.UserData;
			if (!UserData.Primitive || !UserData.Owner || !UserData.Owner->IsVisible() || !UserData.Primitive->IsVisible())
			{
				continue;
			}

			FHitResult CandidateHit{};
			bool bHit = false;

			if (UStaticMeshComponent* const StaticMeshComponent = UserData.StaticMeshPrimitive)
			{
				const FMatrix& WorldMatrix = StaticMeshComponent->GetWorldMatrix();
				const FMatrix& WorldInverse = StaticMeshComponent->GetWorldInverseMatrix();
				bHit = StaticMeshComponent->LineTraceStaticMeshFast(Ray, WorldMatrix, WorldInverse, CandidateHit);
			}
			else
			{
				bHit = UserData.Primitive->LineTraceComponent(Ray, CandidateHit);
			}

			if (bHit && CandidateHit.Distance < OutHitResult.Distance)
			{
				OutHitResult = CandidateHit;
				OutActor = UserData.Owner;
			}
			continue;
		}

		float LeftTMin = 0.0f;
		float LeftTMax = 0.0f;
		float RightTMin = 0.0f;
		float RightTMax = 0.0f;

		const bool bHitLeft = TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(Node.Left, static_cast<int32>(Nodes.size())) &&
			FRayUtils::IntersectRayAABB(Ray, Nodes[Node.Left].Bounds.Min, Nodes[Node.Left].Bounds.Max, LeftTMin, LeftTMax) &&
			LeftTMin < OutHitResult.Distance;
		const bool bHitRight = TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(Node.Right, static_cast<int32>(Nodes.size())) &&
			FRayUtils::IntersectRayAABB(Ray, Nodes[Node.Right].Bounds.Min, Nodes[Node.Right].Bounds.Max, RightTMin, RightTMax) &&
			RightTMin < OutHitResult.Distance;

		if (bHitLeft && bHitRight)
		{
			if (LeftTMin <= RightTMin)
			{
				NodeStack.push_back({ Node.Right, RightTMin });
				NodeStack.push_back({ Node.Left, LeftTMin });
			}
			else
			{
				NodeStack.push_back({ Node.Left, LeftTMin });
				NodeStack.push_back({ Node.Right, RightTMin });
			}
		}
		else if (bHitLeft)
		{
			NodeStack.push_back({ Node.Left, LeftTMin });
		}
		else if (bHitRight)
		{
			NodeStack.push_back({ Node.Right, RightTMin });
		}
	}

	return OutActor != nullptr;
}

FBoundingBox FWorldPrimitivePickingBVH::MakeFatBounds(const FBoundingBox& Bounds)
{
	const FVector Extent = Bounds.GetExtent();
	const FVector Padding(
		(std::max)(Extent.X * WorldBVHFatBoundsScale, WorldBVHFatBoundsMinPadding),
		(std::max)(Extent.Y * WorldBVHFatBoundsScale, WorldBVHFatBoundsMinPadding),
		(std::max)(Extent.Z * WorldBVHFatBoundsScale, WorldBVHFatBoundsMinPadding));

	return FBoundingBox(Bounds.Min - Padding, Bounds.Max + Padding);
}

void FWorldPrimitivePickingBVH::ValidateBVH() const
{
#ifdef _DEBUG
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<FPickingUserData>::INDEX_NONE)
	{
		assert(ObjectToLeafNode.empty());
		return;
	}

	assert(TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())));
	assert(Nodes[RootNodeIndex].Parent == TDynamicAABBTree<FPickingUserData>::INDEX_NONE);

	TArray<uint8> bReachable;
	bReachable.resize(Nodes.size(), 0u);

	TArray<int32> Stack;
	Stack.reserve(Nodes.size());
	Stack.push_back(RootNodeIndex);
	bReachable[RootNodeIndex] = 1u;

	while (!Stack.empty())
	{
		const int32 NodeIndex = Stack.back();
		Stack.pop_back();
		const auto& Node = Nodes[NodeIndex];

		if (Node.IsLeaf())
		{
			assert(Node.UserData.Primitive != nullptr);
			const auto It = ObjectToLeafNode.find(Node.UserData.Primitive);
			assert(It != ObjectToLeafNode.end());
			assert(It->second == NodeIndex);
			continue;
		}

		assert(TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(Node.Left, static_cast<int32>(Nodes.size())));
		assert(TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(Node.Right, static_cast<int32>(Nodes.size())));
		assert(Node.Left != Node.Right);
		assert(Nodes[Node.Left].Parent == NodeIndex);
		assert(Nodes[Node.Right].Parent == NodeIndex);

		if (!bReachable[Node.Left])
		{
			bReachable[Node.Left] = 1u;
			Stack.push_back(Node.Left);
		}
		if (!bReachable[Node.Right])
		{
			bReachable[Node.Right] = 1u;
			Stack.push_back(Node.Right);
		}
	}

	for (const auto& Pair : ObjectToLeafNode)
	{
		assert(Pair.first != nullptr);
		assert(TDynamicAABBTree<FPickingUserData>::IsValidNodeIndex(Pair.second, static_cast<int32>(Nodes.size())));
		assert(bReachable[Pair.second]);
		assert(Nodes[Pair.second].IsLeaf());
		assert(Nodes[Pair.second].UserData.Primitive == Pair.first);
	}
#endif
}

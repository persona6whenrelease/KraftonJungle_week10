#include "Collision/WorldCollisionBVH.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Collision/CollisionTypes.h"
#include "GameFramework/AActor.h"

namespace
{
	constexpr float WorldCollisionBVHFatBoundsScale = 0.15f;
	constexpr float WorldCollisionBVHFatBoundsMinPadding = 1.0f;

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

void FWorldCollisionBVH::MarkDirty()
{
	bDirty = true;
}

void FWorldCollisionBVH::Reset()
{
	Tree.Reset();
	ObjectToLeafNode.clear();
	bDirty = false;
}

void FWorldCollisionBVH::BuildNow(const TArray<AActor*>& Actors)
{
	Reset();

	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsPooledActorInactive() || !Actor->IsActorCollisionEnabled())
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

void FWorldCollisionBVH::EnsureBuilt(const TArray<AActor*>& Actors)
{
	if (bDirty)
	{
		BuildNow(Actors);
	}
}

bool FWorldCollisionBVH::InsertObject(UPrimitiveComponent* Primitive)
{
	if (!IsCollisionPrimitive(Primitive) || ContainsObject(Primitive))
	{
		return false;
	}

	const FBoundingBox Bounds = Primitive->GetWorldAABB();
	if (!Bounds.IsValid())
	{
		return false;
	}

	const int32 LeafNodeIndex = Tree.Insert(Primitive, Bounds, MakeFatBounds(Bounds));
	if (LeafNodeIndex == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE)
	{
		return false;
	}

	ObjectToLeafNode[Primitive] = LeafNodeIndex;
	bDirty = false;
	ValidateBVH();
	return true;
}

bool FWorldCollisionBVH::RemoveObject(UPrimitiveComponent* Primitive)
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

bool FWorldCollisionBVH::UpdateObject(UPrimitiveComponent* Primitive)
{
	auto It = ObjectToLeafNode.find(Primitive);
	if (It == ObjectToLeafNode.end())
	{
		return InsertObject(Primitive);
	}

	if (!IsCollisionPrimitive(Primitive))
	{
		return RemoveObject(Primitive);
	}

	const FBoundingBox NewBounds = Primitive->GetWorldAABB();
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

	Leaf.UserData = Primitive;
	Tree.UpdateBoundsFast(LeafNodeIndex, NewBounds);

	bDirty = false;
	ValidateBVH();
	return true;
}

bool FWorldCollisionBVH::ContainsObject(UPrimitiveComponent* Primitive) const
{
	return ObjectToLeafNode.find(Primitive) != ObjectToLeafNode.end();
}

void FWorldCollisionBVH::QueryAABB(const FBoundingBox& QueryBounds, TArray<UPrimitiveComponent*>& OutCandidates) const
{
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (!QueryBounds.IsValid() || RootNodeIndex == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE ||
		!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())))
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
		if (!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size())))
		{
			continue;
		}

		const auto& Node = Nodes[NodeIndex];
		if (!Node.Bounds.IsIntersected(QueryBounds))
		{
			continue;
		}

		if (Node.IsLeaf())
		{
			if (IsCollisionPrimitive(Node.UserData))
			{
				OutCandidates.push_back(Node.UserData);
			}
			continue;
		}

		Stack.push_back(Node.Right);
		Stack.push_back(Node.Left);
	}
}

void FWorldCollisionBVH::GeneratePotentialPairs(TArray<FOverlapCandidatePair>& OutPairs) const
{
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE || !TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())))
	{
		return;
	}

	TArray<FNodePair> Stack;
	Stack.reserve(Nodes.size());
	Stack.push_back({ RootNodeIndex, RootNodeIndex });

	while (!Stack.empty())
	{
		const FNodePair Pair = Stack.back();
		Stack.pop_back();

		if (!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(Pair.A, static_cast<int32>(Nodes.size())) ||
			!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(Pair.B, static_cast<int32>(Nodes.size())))
		{
			continue;
		}

		const auto& A = Nodes[Pair.A];
		const auto& B = Nodes[Pair.B];
		if (Pair.A == Pair.B)
		{
			if (!A.IsLeaf())
			{
				Stack.push_back({ A.Left, A.Left });
				Stack.push_back({ A.Left, A.Right });
				Stack.push_back({ A.Right, A.Right });
			}
			continue;
		}

		if (!A.Bounds.IsIntersected(B.Bounds))
		{
			continue;
		}

		if (A.IsLeaf() && B.IsLeaf())
		{
			if (IsCollisionPrimitive(A.UserData) && IsCollisionPrimitive(B.UserData))
			{
				OutPairs.push_back({ A.UserData, B.UserData });
			}
			continue;
		}

		if (A.IsLeaf())
		{
			Stack.push_back({ Pair.A, B.Left });
			Stack.push_back({ Pair.A, B.Right });
		}
		else if (B.IsLeaf())
		{
			Stack.push_back({ A.Left, Pair.B });
			Stack.push_back({ A.Right, Pair.B });
		}
		else
		{
			Stack.push_back({ A.Left, B.Left });
			Stack.push_back({ A.Left, B.Right });
			Stack.push_back({ A.Right, B.Left });
			Stack.push_back({ A.Right, B.Right });
		}
	}
}

void FWorldCollisionBVH::CollectDebugAABBs(TArray<FDebugAABB>& OutAABBs, bool bIncludeFatBounds) const
{
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE || 
		!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())))
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

		if (!TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size())))
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

bool FWorldCollisionBVH::IsCollisionPrimitive(UPrimitiveComponent* Primitive)
{
	return Primitive && Primitive->GetCollisionShapeType() != ECollisionShapeType::None;
}

FBoundingBox FWorldCollisionBVH::MakeFatBounds(const FBoundingBox& Bounds)
{
	const FVector Extent = Bounds.GetExtent();
	const FVector Padding(
		(std::max)(Extent.X * WorldCollisionBVHFatBoundsScale, WorldCollisionBVHFatBoundsMinPadding),
		(std::max)(Extent.Y * WorldCollisionBVHFatBoundsScale, WorldCollisionBVHFatBoundsMinPadding),
		(std::max)(Extent.Z * WorldCollisionBVHFatBoundsScale, WorldCollisionBVHFatBoundsMinPadding));

	return FBoundingBox(Bounds.Min - Padding, Bounds.Max + Padding);
}

void FWorldCollisionBVH::ValidateBVH() const
{
#ifdef _DEBUG
	const int32 RootNodeIndex = Tree.GetRootNodeIndex();
	const auto& Nodes = Tree.GetNodes();

	if (RootNodeIndex == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE)
	{
		assert(ObjectToLeafNode.empty());
		return;
	}

	assert(TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(RootNodeIndex, static_cast<int32>(Nodes.size())));
	assert(Nodes[RootNodeIndex].Parent == TDynamicAABBTree<UPrimitiveComponent*>::INDEX_NONE);

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
			assert(IsCollisionPrimitive(Node.UserData));
			const auto It = ObjectToLeafNode.find(Node.UserData);
			assert(It != ObjectToLeafNode.end());
			assert(It->second == NodeIndex);
			continue;
		}

		assert(TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(Node.Left, static_cast<int32>(Nodes.size())));
		assert(TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(Node.Right, static_cast<int32>(Nodes.size())));
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
		assert(IsCollisionPrimitive(Pair.first));
		assert(TDynamicAABBTree<UPrimitiveComponent*>::IsValidNodeIndex(Pair.second, static_cast<int32>(Nodes.size())));
		assert(bReachable[Pair.second]);
		assert(Nodes[Pair.second].IsLeaf());
		assert(Nodes[Pair.second].UserData == Pair.first);
	}
#endif
}

#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include <algorithm>
#include <cassert>

template <typename ElementType>
class TDynamicAABBTree
{
public:
	static constexpr int32 INDEX_NONE = -1;

	struct FNode
	{
		FBoundingBox Bounds;
		FBoundingBox FatBounds;
		int32 Parent = INDEX_NONE;
		int32 Left = INDEX_NONE;
		int32 Right = INDEX_NONE;
		int32 Depth = -1;
		ElementType UserData;

		bool IsLeaf() const { return Left == INDEX_NONE && Right == INDEX_NONE; }
	};

	TDynamicAABBTree() = default;

	void Reset()
	{
		Nodes.clear();
		FreeNodeIndices.clear();
		PathToRootScratch.clear();
		RootNodeIndex = INDEX_NONE;
	}

	int32 Insert(const ElementType& UserData, const FBoundingBox& Bounds, const FBoundingBox& FatBounds)
	{
		if (!Bounds.IsValid()) return INDEX_NONE;

		const int32 LeafNodeIndex = AllocateNode();
		FNode& Leaf = Nodes[LeafNodeIndex];
		Leaf.Bounds = Bounds;
		Leaf.FatBounds = FatBounds;
		Leaf.Parent = INDEX_NONE;
		Leaf.Left = INDEX_NONE;
		Leaf.Right = INDEX_NONE;
		Leaf.Depth = 0;
		Leaf.UserData = UserData;

		InsertLeafNode(LeafNodeIndex);
		return LeafNodeIndex;
	}

	void Remove(int32 LeafNodeIndex)
	{
		RemoveLeafNode(LeafNodeIndex);
	}

	void UpdateBounds(int32 LeafNodeIndex, const FBoundingBox& NewBounds, const FBoundingBox& NewFatBounds)
	{
		if (!IsValidNodeIndex(LeafNodeIndex, static_cast<int32>(Nodes.size()))) return;
		Nodes[LeafNodeIndex].Bounds = NewBounds;
		Nodes[LeafNodeIndex].FatBounds = NewFatBounds;
		RefitUpwards(LeafNodeIndex);
		OptimizeAlongPath(LeafNodeIndex);
	}

	void UpdateBoundsFast(int32 LeafNodeIndex, const FBoundingBox& NewBounds)
	{
		if (!IsValidNodeIndex(LeafNodeIndex, static_cast<int32>(Nodes.size()))) return;
		Nodes[LeafNodeIndex].Bounds = NewBounds;
		RefitUpwards(LeafNodeIndex);
		OptimizeAlongPath(LeafNodeIndex);
	}

	FNode& GetNode(int32 NodeIndex) { return Nodes[NodeIndex]; }
	const FNode& GetNode(int32 NodeIndex) const { return Nodes[NodeIndex]; }
	const TArray<FNode>& GetNodes() const { return Nodes; }
	int32 GetRootNodeIndex() const { return RootNodeIndex; }

	static FBoundingBox UnionBounds(const FBoundingBox& A, const FBoundingBox& B)
	{
		FBoundingBox Result;
		if (A.IsValid())
		{
			Result.Expand(A.Min);
			Result.Expand(A.Max);
		}
		if (B.IsValid())
		{
			Result.Expand(B.Min);
			Result.Expand(B.Max);
		}
		return Result;
	}

	static float GetBoundsSurfaceArea(const FBoundingBox& Bounds)
	{
		if (!Bounds.IsValid()) return 0.0f;
		const FVector Extent = Bounds.GetExtent();
		const float Width = (std::max)(Extent.X * 2.0f, 0.0f);
		const float Height = (std::max)(Extent.Y * 2.0f, 0.0f);
		const float Depth = (std::max)(Extent.Z * 2.0f, 0.0f);
		return 2.0f * ((Width * Height) + (Width * Depth) + (Height * Depth));
	}

	static bool IsValidNodeIndex(int32 NodeIndex, int32 NodeCount)
	{
		return NodeIndex >= 0 && NodeIndex < NodeCount;
	}

private:
	struct FRotationCandidate
	{
		bool bValid = false;
		bool bRotateLeftChild = false;
		bool bUseFirstGrandChild = false;
		int32 NodeIndex = INDEX_NONE;
		float OldCost = 0.0f;
		float NewCost = 0.0f;
	};

	int32 AllocateNode()
	{
		if (!FreeNodeIndices.empty())
		{
			const int32 NodeIndex = FreeNodeIndices.back();
			FreeNodeIndices.pop_back();
			Nodes[NodeIndex] = FNode{};
			return NodeIndex;
		}

		const int32 NodeIndex = static_cast<int32>(Nodes.size());
		Nodes.emplace_back();
		return NodeIndex;
	}

	void ReleaseNode(int32 NodeIndex)
	{
		if (!IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size()))) return;
		Nodes[NodeIndex] = FNode{};
		FreeNodeIndices.push_back(NodeIndex);
	}

	int32 FindBestSibling(const FBoundingBox& NewBounds) const
	{
		if (RootNodeIndex == INDEX_NONE) return INDEX_NONE;

		int32 Current = RootNodeIndex;
		while (IsValidNodeIndex(Current, static_cast<int32>(Nodes.size())) && !Nodes[Current].IsLeaf())
		{
			const FNode& Node = Nodes[Current];
			if (!IsValidNodeIndex(Node.Left, static_cast<int32>(Nodes.size())) ||
				!IsValidNodeIndex(Node.Right, static_cast<int32>(Nodes.size())))
			{
				return Current;
			}

			const FNode& Left = Nodes[Node.Left];
			const FNode& Right = Nodes[Node.Right];
			const float NodeArea = GetBoundsSurfaceArea(Node.Bounds);
			const FBoundingBox CombinedBounds = UnionBounds(Node.Bounds, NewBounds);
			const float CombinedArea = GetBoundsSurfaceArea(CombinedBounds);
			const float CostHere = 2.0f * CombinedArea;
			const float InheritanceCost = 2.0f * (CombinedArea - NodeArea);

			auto ComputeDescendCost = [&](const FNode& Child) -> float
			{
				const FBoundingBox CombinedChildBounds = UnionBounds(Child.Bounds, NewBounds);
				const float CombinedChildArea = GetBoundsSurfaceArea(CombinedChildBounds);
				if (Child.IsLeaf()) return CombinedChildArea + InheritanceCost;
				return (CombinedChildArea - GetBoundsSurfaceArea(Child.Bounds)) + InheritanceCost;
			};

			const float CostLeft = ComputeDescendCost(Left);
			const float CostRight = ComputeDescendCost(Right);
			if (CostHere <= CostLeft && CostHere <= CostRight) break;

			Current = (CostLeft <= CostRight) ? Node.Left : Node.Right;
		}
		return Current;
	}

	void InsertLeafNode(int32 LeafNodeIndex)
	{
		if (!IsValidNodeIndex(LeafNodeIndex, static_cast<int32>(Nodes.size()))) return;

		if (RootNodeIndex == INDEX_NONE)
		{
			RootNodeIndex = LeafNodeIndex;
			Nodes[LeafNodeIndex].Parent = INDEX_NONE;
			Nodes[LeafNodeIndex].Depth = 0;
			return;
		}

		const int32 SiblingIndex = FindBestSibling(Nodes[LeafNodeIndex].FatBounds);
		if (SiblingIndex == INDEX_NONE) return;

		const int32 OldParentIndex = Nodes[SiblingIndex].Parent;
		const int32 NewParentIndex = AllocateNode();

		FNode& NewParent = Nodes[NewParentIndex];
		NewParent.Parent = OldParentIndex;
		NewParent.Left = SiblingIndex;
		NewParent.Right = LeafNodeIndex;
		NewParent.Bounds = UnionBounds(Nodes[SiblingIndex].Bounds, Nodes[LeafNodeIndex].Bounds);
		NewParent.FatBounds = UnionBounds(Nodes[SiblingIndex].FatBounds, Nodes[LeafNodeIndex].FatBounds);
		NewParent.Depth = (OldParentIndex == INDEX_NONE) ? 0 : Nodes[OldParentIndex].Depth + 1;

		if (OldParentIndex == INDEX_NONE)
		{
			RootNodeIndex = NewParentIndex;
		}
		else if (Nodes[OldParentIndex].Left == SiblingIndex)
		{
			Nodes[OldParentIndex].Left = NewParentIndex;
		}
		else
		{
			Nodes[OldParentIndex].Right = NewParentIndex;
		}

		Nodes[SiblingIndex].Parent = NewParentIndex;
		Nodes[LeafNodeIndex].Parent = NewParentIndex;
		UpdateDepthsFromNode(NewParentIndex, NewParent.Depth);
		RefitUpwardsAfterStructuralChange(NewParentIndex);
		OptimizeAlongPath(NewParentIndex);
	}

	void RemoveLeafNode(int32 LeafNodeIndex)
	{
		if (!IsValidNodeIndex(LeafNodeIndex, static_cast<int32>(Nodes.size()))) return;

		const int32 ParentIndex = Nodes[LeafNodeIndex].Parent;
		if (ParentIndex == INDEX_NONE)
		{
			RootNodeIndex = INDEX_NONE;
			ReleaseNode(LeafNodeIndex);
			return;
		}

		const int32 GrandParentIndex = Nodes[ParentIndex].Parent;
		const int32 SiblingIndex = (Nodes[ParentIndex].Left == LeafNodeIndex) ? Nodes[ParentIndex].Right : Nodes[ParentIndex].Left;

		if (GrandParentIndex == INDEX_NONE)
		{
			RootNodeIndex = SiblingIndex;
			Nodes[SiblingIndex].Parent = INDEX_NONE;
			UpdateDepthsFromNode(SiblingIndex, 0);
		}
		else
		{
			if (Nodes[GrandParentIndex].Left == ParentIndex)
			{
				Nodes[GrandParentIndex].Left = SiblingIndex;
			}
			else
			{
				Nodes[GrandParentIndex].Right = SiblingIndex;
			}

			Nodes[SiblingIndex].Parent = GrandParentIndex;
			UpdateDepthsFromNode(SiblingIndex, Nodes[GrandParentIndex].Depth + 1);
		}

		const int32 RefitStart = Nodes[SiblingIndex].Parent;
		ReleaseNode(LeafNodeIndex);
		ReleaseNode(ParentIndex);

		if (RefitStart != INDEX_NONE)
		{
			RefitUpwardsAfterStructuralChange(RefitStart);
			OptimizeAlongPath(RefitStart);
		}
	}

	void RefitNode(int32 NodeIndex)
	{
		if (!IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size()))) return;
		FNode& Node = Nodes[NodeIndex];
		if (Node.IsLeaf()) return;

		Node.Bounds = UnionBounds(Nodes[Node.Left].Bounds, Nodes[Node.Right].Bounds);
		Node.FatBounds = UnionBounds(Nodes[Node.Left].FatBounds, Nodes[Node.Right].FatBounds);
	}

	void RefitUpwards(int32 NodeIndex)
	{
		int32 Current = NodeIndex;
		while (Current != INDEX_NONE)
		{
			RefitNode(Current);
			Current = Nodes[Current].Parent;
		}
	}

	void RefitUpwardsAfterStructuralChange(int32 NodeIndex)
	{
		int32 Current = NodeIndex;
		while (Current != INDEX_NONE)
		{
			RefitNode(Current);
			Current = Nodes[Current].Parent;
		}
	}

	void UpdateDepthsFromNode(int32 NodeIndex, int32 Depth)
	{
		if (!IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size()))) return;
		Nodes[NodeIndex].Depth = Depth;
		if (Nodes[NodeIndex].Left != INDEX_NONE) UpdateDepthsFromNode(Nodes[NodeIndex].Left, Depth + 1);
		if (Nodes[NodeIndex].Right != INDEX_NONE) UpdateDepthsFromNode(Nodes[NodeIndex].Right, Depth + 1);
	}

	void OptimizeAlongPath(int32 StartNodeIndex)
	{
		PathToRootScratch.clear();
		PathToRootScratch.reserve(32);

		int32 Current = StartNodeIndex;
		while (Current != INDEX_NONE)
		{
			PathToRootScratch.push_back(Current);
			Current = Nodes[Current].Parent;
		}

		for (int32 NodeIndex : PathToRootScratch)
		{
			while (TryRotateNodeBest(NodeIndex)) {}
		}
	}

	FRotationCandidate EvaluateRotateWithLeftChild(int32 NodeIndex) const
	{
		FRotationCandidate Best{};
		const FNode& Node = Nodes[NodeIndex];
		if (Node.IsLeaf()) return Best;

		const int32 AIndex = Node.Left;
		const int32 BIndex = Node.Right;
		if (!IsValidNodeIndex(AIndex, static_cast<int32>(Nodes.size())) ||
			!IsValidNodeIndex(BIndex, static_cast<int32>(Nodes.size())) ||
			Nodes[AIndex].IsLeaf())
			return Best;

		const FNode& A = Nodes[AIndex];
		const float OldCost = GetBoundsSurfaceArea(Node.Bounds) + GetBoundsSurfaceArea(A.Bounds);

		auto Consider = [&](int32 GrandChildIndex, int32 OtherGrandChildIndex, bool bUseFirstGrandChild)
		{
			const FBoundingBox NewA = UnionBounds(Nodes[BIndex].Bounds, Nodes[OtherGrandChildIndex].Bounds);
			const FBoundingBox NewNode = UnionBounds(NewA, Nodes[GrandChildIndex].Bounds);
			const float NewCost = GetBoundsSurfaceArea(NewNode) + GetBoundsSurfaceArea(NewA);
			if (!Best.bValid || NewCost < Best.NewCost)
			{
				Best.bValid = true;
				Best.bRotateLeftChild = true;
				Best.bUseFirstGrandChild = bUseFirstGrandChild;
				Best.NodeIndex = NodeIndex;
				Best.OldCost = OldCost;
				Best.NewCost = NewCost;
			}
		};

		Consider(A.Left, A.Right, true);
		Consider(A.Right, A.Left, false);

		if (Best.bValid && Best.NewCost >= Best.OldCost) Best.bValid = false;
		return Best;
	}

	FRotationCandidate EvaluateRotateWithRightChild(int32 NodeIndex) const
	{
		FRotationCandidate Best{};
		const FNode& Node = Nodes[NodeIndex];
		if (Node.IsLeaf()) return Best;

		const int32 AIndex = Node.Left;
		const int32 BIndex = Node.Right;
		if (!IsValidNodeIndex(AIndex, static_cast<int32>(Nodes.size())) ||
			!IsValidNodeIndex(BIndex, static_cast<int32>(Nodes.size())) ||
			Nodes[BIndex].IsLeaf())
			return Best;

		const FNode& B = Nodes[BIndex];
		const float OldCost = GetBoundsSurfaceArea(Node.Bounds) + GetBoundsSurfaceArea(B.Bounds);

		auto Consider = [&](int32 GrandChildIndex, int32 OtherGrandChildIndex, bool bUseFirstGrandChild)
		{
			const FBoundingBox NewB = UnionBounds(Nodes[AIndex].Bounds, Nodes[OtherGrandChildIndex].Bounds);
			const FBoundingBox NewNode = UnionBounds(Nodes[GrandChildIndex].Bounds, NewB);
			const float NewCost = GetBoundsSurfaceArea(NewNode) + GetBoundsSurfaceArea(NewB);
			if (!Best.bValid || NewCost < Best.NewCost)
			{
				Best.bValid = true;
				Best.bRotateLeftChild = false;
				Best.bUseFirstGrandChild = bUseFirstGrandChild;
				Best.NodeIndex = NodeIndex;
				Best.OldCost = OldCost;
				Best.NewCost = NewCost;
			}
		};

		Consider(B.Left, B.Right, true);
		Consider(B.Right, B.Left, false);

		if (Best.bValid && Best.NewCost >= Best.OldCost) Best.bValid = false;
		return Best;
	}

	bool TryRotateNodeBest(int32 NodeIndex)
	{
		if (!IsValidNodeIndex(NodeIndex, static_cast<int32>(Nodes.size())) || Nodes[NodeIndex].IsLeaf()) return false;

		const FRotationCandidate LeftCandidate = EvaluateRotateWithLeftChild(NodeIndex);
		const FRotationCandidate RightCandidate = EvaluateRotateWithRightChild(NodeIndex);
		FRotationCandidate Best{};

		if (LeftCandidate.bValid) Best = LeftCandidate;
		if (RightCandidate.bValid && (!Best.bValid || RightCandidate.NewCost < Best.NewCost)) Best = RightCandidate;

		return Best.bValid && ApplyRotation(Best);
	}

	bool ApplyRotation(const FRotationCandidate& Candidate)
	{
		if (!Candidate.bValid || !IsValidNodeIndex(Candidate.NodeIndex, static_cast<int32>(Nodes.size()))) return false;

		FNode& Node = Nodes[Candidate.NodeIndex];
		if (Candidate.bRotateLeftChild)
		{
			const int32 AIndex = Node.Left;
			const int32 BIndex = Node.Right;
			FNode& A = Nodes[AIndex];

			if (Candidate.bUseFirstGrandChild)
			{
				Node.Right = A.Left;
				A.Left = BIndex;
			}
			else
			{
				Node.Right = A.Right;
				A.Right = BIndex;
			}

			Nodes[Node.Right].Parent = Candidate.NodeIndex;
			Nodes[BIndex].Parent = AIndex;
			A.Parent = Candidate.NodeIndex;
			UpdateDepthsFromNode(Candidate.NodeIndex, Nodes[Candidate.NodeIndex].Depth);
			RefitUpwards(AIndex);
		}
		else
		{
			const int32 AIndex = Node.Left;
			const int32 BIndex = Node.Right;
			FNode& B = Nodes[BIndex];

			if (Candidate.bUseFirstGrandChild)
			{
				Node.Left = B.Left;
				B.Left = AIndex;
			}
			else
			{
				Node.Left = B.Right;
				B.Right = AIndex;
			}

			Nodes[Node.Left].Parent = Candidate.NodeIndex;
			Nodes[AIndex].Parent = BIndex;
			B.Parent = Candidate.NodeIndex;
			UpdateDepthsFromNode(Candidate.NodeIndex, Nodes[Candidate.NodeIndex].Depth);
			RefitUpwards(BIndex);
		}
		return true;
	}

	TArray<FNode> Nodes;
	TArray<int32> FreeNodeIndices;
	int32 RootNodeIndex = INDEX_NONE;
	TArray<int32> PathToRootScratch;
};

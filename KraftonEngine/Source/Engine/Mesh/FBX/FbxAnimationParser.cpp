#include "FbxAnimationParser.h"

#include "Core/Log.h"
#include "FBXUtil.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}
}

void FFbxAnimationParser::ParseSkeletonAnimations(
	fbxsdk::FbxScene* Scene,
	const FFbxSkeletonMeta& SkeletonMeta,
	FSkeletalMesh& OutMesh,
	float SampleRate) const
{
	OutMesh.AnimationClips.clear();

	if (!Scene || !SkeletonMeta.bValid || SkeletonMeta.BoneIds.empty())
	{
		return;
	}

	if (SampleRate <= 0.0f)
	{
		SampleRate = 30.0f;
	}

	// Collect the FbxNode* for every bone in skeleton-bone-index order so animation samples line up
	// with FSkeletalMesh::Bones (and therefore LocalBonePoseMatrices).
	const int32 BoneCount = static_cast<int32>(SkeletonMeta.BoneIds.size());
	TArray<FbxNode*> BoneNodes;
	BoneNodes.resize(BoneCount, nullptr);
	for (int32 SkeletonBoneIndex = 0; SkeletonBoneIndex < BoneCount; ++SkeletonBoneIndex)
	{
		const int32 BoneId = SkeletonMeta.BoneIds[SkeletonBoneIndex];
		if (IsValidIndex(ImportMeta.Bones, BoneId))
		{
			BoneNodes[SkeletonBoneIndex] = ImportMeta.Bones[BoneId].Node;
		}
	}

	// We can't use the templated Scene->GetSrcObject<FbxAnimStack>() because that pulls in
	// FbxAnimStack::ClassId (a non-dllimport static) and breaks linking against libfbxsdk.lib.
	// Iterate every scene src object and filter by class name string instead.
	const int32 TotalSrcObjectCount = Scene->GetSrcObjectCount();
	for (int32 ObjIndex = 0; ObjIndex < TotalSrcObjectCount; ++ObjIndex)
	{
		FbxObject* Obj = Scene->GetSrcObject(ObjIndex);
		if (!Obj)
		{
			continue;
		}
		const char* ClassName = Obj->GetClassId().GetName();
		if (!ClassName)
		{
			continue;
		}
		// FBX SDK historically registers this class as "AnimStack"; older builds use "FbxAnimStack".
		if (std::strcmp(ClassName, "AnimStack") != 0 && std::strcmp(ClassName, "FbxAnimStack") != 0)
		{
			continue;
		}
		FbxAnimStack* AnimStack = static_cast<FbxAnimStack*>(Obj);

		Scene->SetCurrentAnimationStack(AnimStack);

		// Prefer the take-info time span; fall back to local time span if the take is missing.
		FbxTimeSpan TimeSpan;
		FbxTakeInfo* TakeInfo = Scene->GetTakeInfo(AnimStack->GetName());
		if (TakeInfo)
		{
			TimeSpan = TakeInfo->mLocalTimeSpan;
		}
		else
		{
			TimeSpan = AnimStack->GetLocalTimeSpan();
		}

		const FbxTime StartTime = TimeSpan.GetStart();
		const FbxTime StopTime = TimeSpan.GetStop();
		const double DurationSeconds = std::max(0.0, (StopTime - StartTime).GetSecondDouble());
		if (DurationSeconds <= 0.0)
		{
			continue;
		}

		const int32 FrameCount = std::max(2, static_cast<int32>(std::round(DurationSeconds * SampleRate)) + 1);

		FAnimationClip Clip;
		Clip.Name = AnimStack->GetName() ? AnimStack->GetName() : "Anim";
		Clip.Duration = static_cast<float>(DurationSeconds);
		Clip.FrameRate = SampleRate;
		Clip.FrameCount = FrameCount;
		Clip.Tracks.resize(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			Clip.Tracks[BoneIndex].BoneIndex = BoneIndex;
			Clip.Tracks[BoneIndex].Samples.resize(FrameCount);
		}

		// Per-frame: evaluate every bone in global FBX space, then re-express relative to the parent bone
		// (or as-is for roots). Matches the convention used to build LocalBindPose.
		TArray<FMatrix> BoneGlobals;
		BoneGlobals.resize(BoneCount, FMatrix::Identity);

		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			const double FrameAlpha = (FrameCount > 1)
				? static_cast<double>(FrameIndex) / static_cast<double>(FrameCount - 1)
				: 0.0;
			const double FrameSeconds = FrameAlpha * DurationSeconds;
			FbxTime SampleTime;
			SampleTime.SetSecondDouble(StartTime.GetSecondDouble() + FrameSeconds);

			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				FbxNode* Node = BoneNodes[BoneIndex];
				if (!Node)
				{
					BoneGlobals[BoneIndex] = FMatrix::Identity;
					continue;
				}
				BoneGlobals[BoneIndex] = FBXUtil::ConvertFbxMatrix(Node->EvaluateGlobalTransform(SampleTime));
			}

			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				const int32 ParentIndex = (BoneIndex < static_cast<int32>(OutMesh.Bones.size()))
					? OutMesh.Bones[BoneIndex].ParentIndex
					: -1;

				FMatrix LocalMatrix;
				if (ParentIndex >= 0 && ParentIndex < BoneCount)
				{
					LocalMatrix = BoneGlobals[BoneIndex] * BoneGlobals[ParentIndex].GetInverse();
				}
				else
				{
					LocalMatrix = BoneGlobals[BoneIndex];
				}

				Clip.Tracks[BoneIndex].Samples[FrameIndex].LocalMatrix = LocalMatrix;
			}
		}

		UE_LOG("[FBXImporter] Baked animation clip. Skeleton=%d Name=%s Duration=%.3fs Frames=%d Bones=%d",
			SkeletonMeta.SkeletonId,
			Clip.Name.c_str(),
			Clip.Duration,
			Clip.FrameCount,
			BoneCount);

		OutMesh.AnimationClips.push_back(std::move(Clip));
	}
}

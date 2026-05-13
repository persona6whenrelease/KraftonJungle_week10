#include "Component/SkeletalMeshComponent.h"

#include <cmath>

#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	USkinnedMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ApplyBakedAnimation(DeltaTime))
	{
		return;
	}

	//ApplyDebugRandomBoneAnimation(DeltaTime);
}

bool USkeletalMeshComponent::ApplyBakedAnimation(float DeltaTime)
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (Asset->AnimationClips.empty() || Asset->Bones.empty())
	{
		return false;
	}

	const FAnimationClip& Clip = Asset->AnimationClips[0];
	if (Clip.FrameCount <= 0 || Clip.Duration <= 0.0f || Clip.Tracks.empty())
	{
		return false;
	}

	BakedAnimTime += DeltaTime;
	float LoopedTime = std::fmod(BakedAnimTime, Clip.Duration);
	if (LoopedTime < 0.0f)
	{
		LoopedTime += Clip.Duration;
	}

	const float Alpha = (Clip.Duration > 0.0f) ? (LoopedTime / Clip.Duration) : 0.0f;
	const float FrameFloat = Alpha * static_cast<float>(Clip.FrameCount - 1);
	int32 FrameA = static_cast<int32>(std::floor(FrameFloat));
	if (FrameA < 0) FrameA = 0;
	if (FrameA > Clip.FrameCount - 1) FrameA = Clip.FrameCount - 1;
	int32 FrameB = FrameA + 1;
	if (FrameB > Clip.FrameCount - 1) FrameB = Clip.FrameCount - 1;
	const float Blend = FrameFloat - static_cast<float>(FrameA);

	const TArray<FBoneInfo>& Bones = Asset->Bones;
	LocalBonePoseMatrices.resize(Bones.size(), FMatrix::Identity);

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		LocalBonePoseMatrices[BoneIndex] = Bones[BoneIndex].LocalBindPose;
		if (BoneIndex >= static_cast<int32>(Clip.Tracks.size()))
		{
			continue;
		}

		const FBoneAnimTrack& Track = Clip.Tracks[BoneIndex];
		if (FrameA >= static_cast<int32>(Track.Samples.size()))
		{
			continue;
		}

		const FMatrix& A = Track.Samples[FrameA].LocalMatrix;
		const FMatrix& B = Track.Samples[FrameB].LocalMatrix;
		// Element-wise lerp keeps things simple; affine drift between adjacent frames is negligible at 30fps.
		FMatrix Interpolated;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				Interpolated.M[Row][Col] = A.M[Row][Col] * (1.0f - Blend) + B.M[Row][Col] * Blend;
			}
		}
		LocalBonePoseMatrices[BoneIndex] = Interpolated;
	}

	RebuildMeshSpaceBoneMatrices();
	SkinVerticesToReferencePose();
	EnsureRuntimeResources();
	MarkWorldBoundsDirty();
	return true;
}

void USkeletalMeshComponent::ApplyDebugRandomBoneAnimation(float DeltaTime)
{
	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	const TArray<FBoneInfo>& Bones = Asset->Bones;
	if (Bones.empty())
	{
		return;
	}

	DebugBoneAnimTime += DeltaTime;
	LocalBonePoseMatrices.resize(Bones.size(), FMatrix::Identity);

	constexpr float MaxAngleRadians = 15.0f * 3.1415926535f / 180.0f;
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		const FBoneInfo& Bone = Bones[BoneIndex];
		FMatrix AnimatedLocal = Bone.LocalBindPose;

		if (Bone.ParentIndex >= 0)
		{
			const float Phase = static_cast<float>((BoneIndex * 37) % 113) * 0.137f;
			const float Frequency = 0.8f + static_cast<float>((BoneIndex * 17) % 5) * 0.21f;
			const float Angle = sinf(DebugBoneAnimTime * Frequency + Phase) * MaxAngleRadians;

			const FVector Axis(
				static_cast<float>((BoneIndex % 3) == 0),
				static_cast<float>((BoneIndex % 3) == 1),
				static_cast<float>((BoneIndex % 3) == 2));
			AnimatedLocal = FMatrix::MakeRotationAxis(Axis, Angle) * Bone.LocalBindPose;
		}

		LocalBonePoseMatrices[BoneIndex] = AnimatedLocal;
	}

	RebuildMeshSpaceBoneMatrices();
	SkinVerticesToReferencePose();
	EnsureRuntimeResources();
}

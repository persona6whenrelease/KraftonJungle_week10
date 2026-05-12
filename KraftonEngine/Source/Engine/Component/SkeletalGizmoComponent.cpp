#include "SkeletalGizmoComponent.h"
#include "Math/Matrix.h"
#include "Component/SkinnedMeshComponent.h"

IMPLEMENT_CLASS(USkeletalGizmoComponent, UGizmoComponent)
HIDE_FROM_COMPONENT_LIST(USkeletalGizmoComponent)


void USkeletalGizmoComponent::SetTargetBone(USkinnedMeshComponent* NewTarget, int32 InBoneIndex)
{
	TargetSkelMeshComp = NewTarget;
	TargetBoneIndex = InBoneIndex;
	SetPreserveWorldLocationOnUpdate(true);
	SetTarget(TargetSkelMeshComp);
}

FMatrix USkeletalGizmoComponent::CalculateParentWorldMatrix(int32 BoneIndex) const
{
	if (!TargetSkelMeshComp || !TargetSkelMeshComp->GetSkeletalMesh())
	{
		return FMatrix::Identity;
	}

	const FSkeletalMesh* Asset = TargetSkelMeshComp->GetSkeletalMesh()->GetSkeletalMeshAsset();
	if (!Asset || BoneIndex < 0 || BoneIndex >= Asset->Bones.size())
	{
		return TargetSkelMeshComp->GetWorldMatrix();
	}

	int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
	FMatrix CompWorld = TargetSkelMeshComp->GetWorldMatrix();

	// 부모 본이 있다면 [부모의 메시 기준 행렬] * [컴포넌트 월드 행렬]
	if (ParentIndex >= 0)
	{
		const TArray<FMatrix>& MeshSpaceBones = TargetSkelMeshComp->GetMeshSpaceBoneMatrices();
		return MeshSpaceBones[ParentIndex] * CompWorld;
	}

	// 최상위 루트 본이라면 컴포넌트의 월드 행렬이 곧 부모 행렬
	return CompWorld;
}

void USkeletalGizmoComponent::TranslateTarget(float DragAmount)
{
	if (!TargetSkelMeshComp || TargetBoneIndex == -1) return;

	// 1. 마우스 드래그로 발생한 월드 기준 이동량 계산
	// UGizmoComponent의 GetVectorForAxis(SelectedAxis) 사용
	FVector WorldDelta = GetVectorForAxis(GetSelectedAxis()) * DragAmount;

	// 2. 부모 본의 월드 행렬 구하기
	FMatrix ParentWorldMatrix = CalculateParentWorldMatrix(TargetBoneIndex);

	// 3. 월드 변화량을 부모 기준의 로컬 공간 벡터로 변환 
	// (이동량이므로 TransformPosition이 아닌 TransformVector 사용)
	FVector LocalDelta = ParentWorldMatrix.GetInverse().TransformVector(WorldDelta);

	// 4. 현재 타겟 본의 기존 로컬 포즈 가져오기
	// 주의: SkinnedMeshComponent.h 에 GetLocalBonePoseMatrices() 게터가 있어야 함
	const TArray<FMatrix>& LocalPoses = TargetSkelMeshComp->GetLocalBonePoseMatrices();
	if (TargetBoneIndex >= LocalPoses.size()) return;

	FMatrix CurrentLocalPose = LocalPoses[TargetBoneIndex];

	// 5. 로컬 포즈(행렬)의 원점에 이동량 더하기
	FVector CurrentOrigin = CurrentLocalPose.GetLocation();
	CurrentLocalPose.SetLocation(CurrentOrigin + LocalDelta);

	// 6. 스켈레탈 메시에 업데이트 명령
	TargetSkelMeshComp->SetBoneLocalPose(TargetBoneIndex, CurrentLocalPose);
}

void USkeletalGizmoComponent::RotateTarget(float DragAmount)
{
}

void USkeletalGizmoComponent::ScaleTarget(float DragAmount)
{
}

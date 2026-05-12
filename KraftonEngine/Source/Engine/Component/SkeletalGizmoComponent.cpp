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
	if (!TargetSkelMeshComp || TargetBoneIndex == -1) return;

	// 1. 월드 기준 회전축 도출 (기즈모에서 선택된 축의 방향)
	FVector WorldRotationAxis = GetVectorForAxis(GetSelectedAxis()).Normalized();

	// 2. 부모 본의 월드 행렬 구하기
	FMatrix ParentWorldMatrix = CalculateParentWorldMatrix(TargetBoneIndex);

	// 3. World 회전 축을 부모 본의 로컬(Parent Space) 공간 축으로 역변환
	FVector ParentSpaceAxis = ParentWorldMatrix.GetInverse().TransformVector(WorldRotationAxis);
	ParentSpaceAxis.Normalize();

	// 4. 부모 공간 기준의 회전 변화량(Delta) 행렬 생성
	// (DragAmount는 이미 각도(Radian)로 계산되어 들어옵니다)
	FMatrix DeltaRotation = FMatrix::MakeRotationAxis(ParentSpaceAxis, DragAmount);

	// 5. 현재 타겟 본의 로컬 포즈 가져오기
	const TArray<FMatrix>& LocalPoses = TargetSkelMeshComp->GetLocalBonePoseMatrices();
	if (TargetBoneIndex >= LocalPoses.size()) return;

	FMatrix CurrentLocalPose = LocalPoses[TargetBoneIndex];

	// 6. 위치값 보존을 위해 백업 후 원점(0,0,0)으로 초기화
	// (회전 행렬을 곱할 때 위치값이 같이 돌아가버리는 것을 방지)
	FVector CurrentLocation = CurrentLocalPose.GetLocation();
	CurrentLocalPose.SetLocation(FVector(0.0f, 0.0f, 0.0f));

	// 7. 현재 로컬 포즈에 회전량 적용 
	// 행렬 곱셈 순서: Row-Major 기준 [로컬 포즈] * [추가 회전량]
	CurrentLocalPose = CurrentLocalPose * DeltaRotation;

	// 8. 백업해두었던 위치값 복구
	CurrentLocalPose.SetLocation(CurrentLocation);

	// 9. 최종 뼈대 포즈 업데이트
	TargetSkelMeshComp->SetBoneLocalPose(TargetBoneIndex, CurrentLocalPose);
}

void USkeletalGizmoComponent::ScaleTarget(float DragAmount)
{
	if (!TargetSkelMeshComp || TargetBoneIndex == -1) return;

	// 1. 현재 타겟 본의 로컬 포즈 가져오기
	const TArray<FMatrix>& LocalPoses = TargetSkelMeshComp->GetLocalBonePoseMatrices();
	if (TargetBoneIndex >= LocalPoses.size()) return;

	FMatrix CurrentLocalPose = LocalPoses[TargetBoneIndex];

	// 2. 현재 스케일값 추출
	FVector CurrentScale = CurrentLocalPose.GetScale();
	FVector TargetScale = CurrentScale;

	// 3. 기즈모 드래그량에 따른 스케일 변화량 더하기 (덧셈 방식)
	float ScaleDelta = DragAmount * 1.0f; // 1.0f는 ScaleSensitivity

	int32 Axis = GetSelectedAxis();
	if (Axis == 0) TargetScale.X += ScaleDelta;
	else if (Axis == 1) TargetScale.Y += ScaleDelta;
	else if (Axis == 2) TargetScale.Z += ScaleDelta;
	else if (Axis == 3) // Center (Uniform Scale)
	{
		TargetScale.X += ScaleDelta;
		TargetScale.Y += ScaleDelta;
		TargetScale.Z += ScaleDelta;
	}

	// 4. 스케일값이 음수가 되거나 0이 되지 않도록 Clamp 적용
	TargetScale.X = std::max(0.001f, TargetScale.X);
	TargetScale.Y = std::max(0.001f, TargetScale.Y);
	TargetScale.Z = std::max(0.001f, TargetScale.Z);

	// 5. 이전 스케일 대비 변화한 비율(Ratio) 계산
	FVector ScaleRatio(
		CurrentScale.X > 1e-4f ? TargetScale.X / CurrentScale.X : 1.0f,
		CurrentScale.Y > 1e-4f ? TargetScale.Y / CurrentScale.Y : 1.0f,
		CurrentScale.Z > 1e-4f ? TargetScale.Z / CurrentScale.Z : 1.0f
	);

	// 6. 스케일 행렬 생성 후 곱셈 적용
	FMatrix ScaleMatrix = FMatrix::MakeScaleMatrix(ScaleRatio);

	// 💡 [핵심] 위치(Translation)를 건드리지 않고 방향 벡터(Row 0,1,2)의 길이만 안전하게 곱해주기 위해 
	// ScaleMatrix를 **왼쪽**에서 곱합니다. ([Scale] * [Local Pose])
	CurrentLocalPose = ScaleMatrix * CurrentLocalPose;

	// 7. 뼈대 포즈 업데이트
	TargetSkelMeshComp->SetBoneLocalPose(TargetBoneIndex, CurrentLocalPose);
}

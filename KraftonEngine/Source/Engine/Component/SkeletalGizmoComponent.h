#pragma once
#include "GizmoComponent.h"

class USkinnedMeshComponent;

class USkeletalGizmoComponent :
    public UGizmoComponent
{
public:
	DECLARE_CLASS(USkeletalGizmoComponent, UGizmoComponent)

	// 본 타겟팅을 위한 전용 함수
	void SetTargetBone(USkinnedMeshComponent* NewTarget, int32 InBoneIndex);

private:
	virtual void TranslateTarget(float DragAmount) override;
	 virtual void RotateTarget(float DragAmount) override; 
	 virtual void ScaleTarget(float DragAmount) override;

private:
	// 부모 본(또는 컴포넌트 자체)의 월드 행렬을 구하는 유틸리티
	FMatrix CalculateParentWorldMatrix(int32 BoneIndex) const;

	USkinnedMeshComponent* TargetSkelMeshComp = nullptr;
	int32 TargetBoneIndex = -1;
};


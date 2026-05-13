#pragma once
#include "Core/CoreTypes.h"

class USkinnedMeshComponent;
class USkeletalGizmoComponent;
class FScene;
class FBoneSelectionManager
{
public:
	void Init();
	void Shutdown();
	void Tick();

	// 본 선택 제어 (Actor 포인터 대신 본 인덱스를 사용)
	void SelectBone(int32 BoneIndex);
	void ToggleSelectBone(int32 BoneIndex);
	void ClearSelection();

	// 현재 조작 중인 타겟 메시 설정
	void SetTargetSkeletalMesh(USkinnedMeshComponent* SkelComp);

	bool IsBoneSelected(int32 BoneIndex) const;
	int32 GetPrimarySelectedBone() const;
	USkinnedMeshComponent* GetTargetSkeletalMesh() const { return TargetSkelMesh; }

	// BoneSelectionManager.h 안의 public 영역에 추가
	class USkeletalGizmoComponent* GetGizmo() const { return BoneGizmo; }

	void SetScene(FScene* InScene);

private:
	void SyncGizmo();

	USkinnedMeshComponent* TargetSkelMesh = nullptr;
	TArray<int32> SelectedBones;
	int32 SelectedBoneIndex = -1;// 단일/메인 타겟 인덱스
	USkeletalGizmoComponent* BoneGizmo = nullptr;
};

#include "BoneSelectionManager.h"
#include "Component/SkeletalGizmoComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/ObjectFactory.h" // 객체 생성용 (기존 SelectionManager 참고)

void FBoneSelectionManager::Init()
{
	BoneGizmo = UObjectManager::Get().CreateObject<USkeletalGizmoComponent>();
	BoneGizmo->SetWorldLocation(FVector(0.0f, 0.0f, 0.0f));
	BoneGizmo->Deactivate();
}

void FBoneSelectionManager::Shutdown()
{
	ClearSelection();

	if (BoneGizmo)
	{
		UObjectManager::Get().DestroyObject(BoneGizmo);
		BoneGizmo = nullptr;
	}
}

void FBoneSelectionManager::Tick()
{
	if (!BoneGizmo || !BoneGizmo->IsActive() || !TargetSkelMesh || SelectedBoneIndex == -1)
	{
		return;
	}

	// 애니메이션 등으로 본이 움직였을 때 기즈모 위치도 계속 따라가게 동기화
	SyncGizmo();
}

void FBoneSelectionManager::SetTargetSkeletalMesh(USkinnedMeshComponent* SkelComp)
{
	if (TargetSkelMesh == SkelComp) return;

	TargetSkelMesh = SkelComp;
	ClearSelection(); // 타겟 메시가 바뀌면 선택 초기화
}

void FBoneSelectionManager::SelectBone(int32 BoneIndex)
{
	if (SelectedBoneIndex == BoneIndex) return;

	SelectedBones.clear();
	SelectedBoneIndex = BoneIndex;

	if (BoneIndex != -1)
	{
		SelectedBones.push_back(BoneIndex);
	}

	SyncGizmo();
}

void FBoneSelectionManager::ToggleSelectBone(int32 BoneIndex)
{
	if (BoneIndex == -1) return;

	auto It = std::find(SelectedBones.begin(), SelectedBones.end(), BoneIndex);
	if (It != SelectedBones.end())
	{
		SelectedBones.erase(It);
		SelectedBoneIndex = SelectedBones.empty() ? -1 : SelectedBones.front();
	}
	else
	{
		SelectedBones.push_back(BoneIndex);
		SelectedBoneIndex = BoneIndex; // 마지막으로 토글한 것을 메인 기즈모 타겟으로
	}

	SyncGizmo();
}

void FBoneSelectionManager::ClearSelection()
{
	SelectedBones.clear();
	SelectedBoneIndex = -1;
	SyncGizmo();
}
bool FBoneSelectionManager::IsBoneSelected(int32 BoneIndex) const
{
	return std::find(SelectedBones.begin(), SelectedBones.end(), BoneIndex) != SelectedBones.end();
}

int32 FBoneSelectionManager::GetPrimarySelectedBone() const
{
	return SelectedBoneIndex;
}

void FBoneSelectionManager::SetScene(FScene* InScene)
{
	if (BoneGizmo)
	{
		BoneGizmo->DestroyRenderState();
		BoneGizmo->SetScene(InScene);
		if (InScene)
		{
			BoneGizmo->CreateRenderState();
		}
	}
}

void FBoneSelectionManager::SyncGizmo()
{
	if (!BoneGizmo || !TargetSkelMesh || SelectedBoneIndex == -1)
	{
		if (BoneGizmo) BoneGizmo->Deactivate();
		return;
	}

	const TArray<FMatrix>& MeshSpaceMatrices = TargetSkelMesh->GetMeshSpaceBoneMatrices();
	if (SelectedBoneIndex < MeshSpaceMatrices.size())
	{
		FMatrix BoneMeshSpaceMatrix = MeshSpaceMatrices[SelectedBoneIndex];
		FMatrix ComponentWorldMatrix = TargetSkelMesh->GetWorldMatrix();
		FMatrix BoneWorldMatrix = BoneMeshSpaceMatrix * ComponentWorldMatrix;

		BoneGizmo->SetWorldLocation(BoneWorldMatrix.GetLocation());
		BoneGizmo->SetTargetBone(TargetSkelMesh, SelectedBoneIndex);

		if (!BoneGizmo->IsActive())
		{
			BoneGizmo->SetVisibility(true);
		}
	}
}

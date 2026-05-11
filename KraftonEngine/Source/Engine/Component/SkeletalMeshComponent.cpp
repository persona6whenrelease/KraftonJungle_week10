#include "Component/SkeletalMeshComponent.h"

#include "Engine/Runtime/Engine.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshManager.h"
#include "Object/ObjectFactory.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"
#include "Core/Log.h"

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}


void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	USkinnedMeshComponent::SetSkeletalMesh(InMesh);

	if (InMesh)
	{
		SkeletalMeshPath = InMesh->GetAssetPathFileName();
	}
	else
	{
		SkeletalMeshPath = "None";
	}
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
	USkinnedMeshComponent::Serialize(Ar);

	Ar << SkeletalMeshPath;
}

void USkeletalMeshComponent::PostDuplicate()
{
	USkinnedMeshComponent::PostDuplicate();

	if (!SkeletalMeshPath.empty() && SkeletalMeshPath != "None")
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

		USkeletalMesh* Loaded =
			FSkeletalMeshManager::LoadFbxSkeletalMesh(SkeletalMeshPath, Device);

		if (Loaded)
		{
			USkinnedMeshComponent::SetSkeletalMesh(Loaded);
		}
	}

	MarkRenderStateDirty();
	MarkWorldBoundsDirty();
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USkinnedMeshComponent::GetEditableProperties(OutProps);

	OutProps.push_back({
		"Skeletal Mesh",
		EPropertyType::SkeletalMeshRef,
		&SkeletalMeshPath
	});
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
	USkinnedMeshComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Skeletal Mesh") == 0)
	{
		if (SkeletalMeshPath.empty() || SkeletalMeshPath == "None")
		{
			USkinnedMeshComponent::SetSkeletalMesh(nullptr);
			return;
		}

		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();

		USkeletalMesh* Loaded =
			FSkeletalMeshManager::LoadFbxSkeletalMesh(SkeletalMeshPath, Device);

		if (!Loaded)
		{
			UE_LOG("[SkeletalMeshComponent] Load failed. Path=%s", SkeletalMeshPath.c_str());
			return;
		}

		FSkeletalMesh* MeshAsset = Loaded->GetSkeletalMeshAsset();

		if (!MeshAsset)
		{
			UE_LOG("[SkeletalMeshComponent] Loaded mesh has no asset. Path=%s", SkeletalMeshPath.c_str());
			return;
		}

		UE_LOG(
			"[SkeletalMeshComponent] Load success. Path=%s, Vertices=%zu, Indices=%zu, Sections=%zu, Bones=%zu",
			SkeletalMeshPath.c_str(),
			MeshAsset->Vertices.size(),
			MeshAsset->Indices.size(),
			MeshAsset->Sections.size(),
			MeshAsset->Bones.size()
		);

		USkinnedMeshComponent::SetSkeletalMesh(Loaded);
	}
}

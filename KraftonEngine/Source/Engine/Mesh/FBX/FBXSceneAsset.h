#pragma once

#include "FBXImporter.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "Object/Object.h"

class UFBXSceneAsset : public UObject
{
public:
	DECLARE_CLASS(UFBXSceneAsset, UObject)

	void SetSourcePath(const FString& InSourcePath) { SourcePath = InSourcePath; }
	const FString& GetSourcePath() const { return SourcePath; }

	void AddStaticMesh(UStaticMesh* Mesh) { StaticMeshes.push_back(Mesh); }
	void AddSkeletalMesh(USkeletalMesh* Mesh) { SkeletalMeshes.push_back(Mesh); }
	void SetMeshIdToStaticMeshAssetIndex(TMap<int32, int32>&& InMeshIdToStaticMeshAssetIndex)
	{
		MeshIdToStaticMeshAssetIndex = std::move(InMeshIdToStaticMeshAssetIndex);
	}
	void SetSkeletonIdToSkeletalMeshAssetIndex(TMap<int32, int32>&& InSkeletonIdToSkeletalMeshAssetIndex)
	{
		SkeletonIdToSkeletalMeshAssetIndex = std::move(InSkeletonIdToSkeletalMeshAssetIndex);
	}
	void SetSceneComponents(TArray<FFBXSceneComponentDesc>&& InSceneComponents)
	{
		SceneComponents = std::move(InSceneComponents);
	}

	const TArray<UStaticMesh*>& GetStaticMeshes() const { return StaticMeshes; }
	const TArray<USkeletalMesh*>& GetSkeletalMeshes() const { return SkeletalMeshes; }
	const TArray<FFBXSceneComponentDesc>& GetSceneComponents() const { return SceneComponents; }
	UStaticMesh* FindStaticMeshBySourceMeshId(int32 SourceMeshId) const
	{
		const auto It = MeshIdToStaticMeshAssetIndex.find(SourceMeshId);
		if (It == MeshIdToStaticMeshAssetIndex.end())
		{
			return nullptr;
		}

		const int32 AssetIndex = It->second;
		if (AssetIndex < 0 || AssetIndex >= static_cast<int32>(StaticMeshes.size()))
		{
			return nullptr;
		}
		return StaticMeshes[AssetIndex];
	}
	USkeletalMesh* FindSkeletalMeshBySourceSkeletonId(int32 SourceSkeletonId) const
	{
		const auto It = SkeletonIdToSkeletalMeshAssetIndex.find(SourceSkeletonId);
		if (It == SkeletonIdToSkeletalMeshAssetIndex.end())
		{
			return nullptr;
		}

		const int32 AssetIndex = It->second;
		if (AssetIndex < 0 || AssetIndex >= static_cast<int32>(SkeletalMeshes.size()))
		{
			return nullptr;
		}
		return SkeletalMeshes[AssetIndex];
	}

private:
	FString SourcePath;
	TArray<UStaticMesh*> StaticMeshes;
	TArray<USkeletalMesh*> SkeletalMeshes;
	TArray<FFBXSceneComponentDesc> SceneComponents;
	TMap<int32, int32> MeshIdToStaticMeshAssetIndex;
	TMap<int32, int32> SkeletonIdToSkeletalMeshAssetIndex;
};

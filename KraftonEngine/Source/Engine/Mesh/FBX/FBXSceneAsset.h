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
	void SetSceneComponents(TArray<FFBXSceneComponentDesc>&& InSceneComponents)
	{
		SceneComponents = std::move(InSceneComponents);
	}

	const TArray<UStaticMesh*>& GetStaticMeshes() const { return StaticMeshes; }
	const TArray<USkeletalMesh*>& GetSkeletalMeshes() const { return SkeletalMeshes; }
	const TArray<FFBXSceneComponentDesc>& GetSceneComponents() const { return SceneComponents; }

private:
	FString SourcePath;
	TArray<UStaticMesh*> StaticMeshes;
	TArray<USkeletalMesh*> SkeletalMeshes;
	TArray<FFBXSceneComponentDesc> SceneComponents;
};

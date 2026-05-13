#include "FBXImporter.h"

#include "Core/Log.h"
#include "FbxAnimationParser.h"
#include "FbxMaterialImportUtils.h"
#include "FbxMetaParser.h"
#include "FbxSkeletalMeshAssembler.h"
#include "FbxSkeletalMeshPartParser.h"
#include "FbxStaticMeshParser.h"

#include <fbxsdk.h>

namespace
{
	template <typename T>
	bool IsValidIndex(const TArray<T>& Items, int32 Index)
	{
		return Index >= 0 && static_cast<size_t>(Index) < Items.size();
	}

	void ClearAsset(FFBXAsset& Asset)
	{
		Asset.PathFileName.clear();
		Asset.StaticMeshes.clear();
		Asset.SkeletalMeshes.clear();
		Asset.StaticMeshMaterials.clear();
		Asset.SkeletalMeshMaterials.clear();
		Asset.SkeletalMaterials.clear();
		Asset.SceneComponents.clear();
		Asset.MeshIdToStaticMeshAssetIndex.clear();
		Asset.SkeletonIdToSkeletalMeshAssetIndex.clear();
		Asset.LightAssets.clear();
		Asset.CameraAssets.clear();
	}

	void BuildSceneComponents(const FFbxImportMeta& ImportMeta, FFBXAsset& Asset)
	{
		TSet<int32> MeshIdsConsumedBySkeletal;
		for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
		{
			for (int32 MeshId : SkeletonMeta.SkinnedMeshIds)
			{
				MeshIdsConsumedBySkeletal.insert(MeshId);
			}
			for (int32 MeshId : SkeletonMeta.RigidAttachedMeshIds)
			{
				MeshIdsConsumedBySkeletal.insert(MeshId);
			}
		}

		for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
		{
			auto AssetIndexIt = Asset.SkeletonIdToSkeletalMeshAssetIndex.find(SkeletonMeta.SkeletonId);
			if (AssetIndexIt == Asset.SkeletonIdToSkeletalMeshAssetIndex.end())
			{
				continue;
			}

			FFBXSceneComponentDesc Desc;
			Desc.Type = EFBXSceneComponentType::SkeletalMesh;
			Desc.Name = SkeletonMeta.Name.empty()
				? "Skeleton_" + std::to_string(SkeletonMeta.SkeletonId)
				: SkeletonMeta.Name;
			Desc.SourceNodeId = SkeletonMeta.RootNodeId;
			Desc.SourceSkeletonId = SkeletonMeta.SkeletonId;
			Desc.SkeletalMeshAssetIndex = AssetIndexIt->second;
			Desc.RelativeTransform = IsValidIndex(ImportMeta.Nodes, SkeletonMeta.RootNodeId)
				? ImportMeta.Nodes[SkeletonMeta.RootNodeId].LocalTransform
				: FMatrix::Identity;
			Asset.SceneComponents.push_back(std::move(Desc));
		}

		for (const FFbxMeshMeta& MeshMeta : ImportMeta.Meshes)
		{
			if (MeshIdsConsumedBySkeletal.find(MeshMeta.MeshId) != MeshIdsConsumedBySkeletal.end())
			{
				continue;
			}

			auto AssetIndexIt = Asset.MeshIdToStaticMeshAssetIndex.find(MeshMeta.MeshId);
			if (AssetIndexIt == Asset.MeshIdToStaticMeshAssetIndex.end())
			{
				continue;
			}

			FFBXSceneComponentDesc Desc;
			Desc.Type = EFBXSceneComponentType::StaticMesh;
			Desc.Name = MeshMeta.Name.empty()
				? "Mesh_" + std::to_string(MeshMeta.MeshId)
				: MeshMeta.Name;
			Desc.SourceNodeId = MeshMeta.NodeId;
			Desc.SourceMeshId = MeshMeta.MeshId;
			Desc.StaticMeshAssetIndex = AssetIndexIt->second;
			Desc.RelativeTransform = IsValidIndex(ImportMeta.Nodes, MeshMeta.NodeId)
				? ImportMeta.Nodes[MeshMeta.NodeId].LocalTransform
				: FMatrix::Identity;
			Asset.SceneComponents.push_back(std::move(Desc));
		}
	}
}

bool FBXImporter::ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset)
{
	ClearAsset(OutFBXAsset);

	if (!InitializeSdk())
	{
		return false;
	}

	if (!LoadScene(InFilePath))
	{
		ShutdownSdk();
		return false;
	}

	ImportMeta.SourceFilePath = InFilePath;

	FFbxMetaParser MetaParser(ImportMeta);
	if (!MetaParser.BuildFbxMeta(Scene))
	{
		ShutdownSdk();
		return false;
	}

	OutFBXAsset.PathFileName = InFilePath;

	FFbxStaticMeshParser StaticMeshParser(ImportMeta);
	if (!StaticMeshParser.Parse(
		OutFBXAsset.StaticMeshes,
		OutFBXAsset.MeshIdToStaticMeshAssetIndex))
	{
		ShutdownSdk();
		return false;
	}

	TArray<FFbxSkinnedMeshPart> SkinnedMeshParts;
	FFbxSkeletalMeshPartParser SkeletalMeshPartParser(ImportMeta);
	if (!SkeletalMeshPartParser.Parse(SkinnedMeshParts))
	{
		ShutdownSdk();
		return false;
	}

	FFbxSkeletalMeshAssembler SkeletalMeshAssembler(ImportMeta);
	if (!SkeletalMeshAssembler.Assemble(
		SkinnedMeshParts,
		OutFBXAsset.SkeletalMeshes,
		OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex))
	{
		ShutdownSdk();
		return false;
	}

	// Bake any FBX AnimStack curves into per-bone tracks attached to the assembled skeletal meshes.
	// We don't have a runtime animation system, so the clips are sampled into a flat array and
	// played back hardcoded by USkeletalMeshComponent.
	{
		FFbxAnimationParser AnimationParser(ImportMeta);
		for (const FFbxSkeletonMeta& SkeletonMeta : ImportMeta.Skeletons)
		{
			auto AssetIndexIt = OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex.find(SkeletonMeta.SkeletonId);
			if (AssetIndexIt == OutFBXAsset.SkeletonIdToSkeletalMeshAssetIndex.end())
			{
				continue;
			}

			const int32 SkeletalMeshIndex = AssetIndexIt->second;
			if (!IsValidIndex(OutFBXAsset.SkeletalMeshes, SkeletalMeshIndex))
			{
				continue;
			}

			AnimationParser.ParseSkeletonAnimations(
				Scene,
				SkeletonMeta,
				OutFBXAsset.SkeletalMeshes[SkeletalMeshIndex]);
		}
	}

	OutFBXAsset.StaticMeshMaterials.resize(OutFBXAsset.StaticMeshes.size());
	for (int32 StaticMeshIndex = 0; StaticMeshIndex < static_cast<int32>(OutFBXAsset.StaticMeshes.size()); ++StaticMeshIndex)
	{
		FbxMaterialImportUtils::BuildStaticMaterials(
			ImportMeta,
			OutFBXAsset.StaticMeshes[StaticMeshIndex],
			OutFBXAsset.StaticMeshMaterials[StaticMeshIndex]);
	}

	OutFBXAsset.SkeletalMeshMaterials.resize(OutFBXAsset.SkeletalMeshes.size());
	for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < static_cast<int32>(OutFBXAsset.SkeletalMeshes.size()); ++SkeletalMeshIndex)
	{
		FbxMaterialImportUtils::BuildSkeletalMaterials(
			ImportMeta,
			OutFBXAsset.SkeletalMeshes[SkeletalMeshIndex],
			OutFBXAsset.SkeletalMeshMaterials[SkeletalMeshIndex]);
	}
	if (!OutFBXAsset.SkeletalMeshMaterials.empty())
	{
		OutFBXAsset.SkeletalMaterials = OutFBXAsset.SkeletalMeshMaterials[0];
	}

	BuildSceneComponents(ImportMeta, OutFBXAsset);

	FinalizeAsset();

	ShutdownSdk();

	return true;
}

bool FBXImporter::InitializeSdk()
{
	ShutdownSdk();

	Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG("[FBXImporter] Failed to create FbxManager.");
		return false;
	}

	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	if (!IOSettings)
	{
		UE_LOG("[FBXImporter] Failed to create FbxIOSettings.");
		ShutdownSdk();
		return false;
	}

	Manager->SetIOSettings(IOSettings);
	return true;
}

void FBXImporter::ShutdownSdk()
{
	DestroyScene();
	ClearState();

	if (Manager)
	{
		Manager->Destroy();
		Manager = nullptr;
	}
}

bool FBXImporter::LoadScene(const FString& InFilePath)
{
	if (!Manager)
	{
		return false;
	}

	DestroyScene();

	FbxImporter* Importer = FbxImporter::Create(Manager, "");
	if (!Importer)
	{
		UE_LOG("[FBXImporter] Failed to create FbxImporter.");
		return false;
	}

	const bool bInitialized = Importer->Initialize(
		InFilePath.c_str(),
		-1,
		Manager->GetIOSettings());

	if (!bInitialized)
	{
		UE_LOG("[FBXImporter] Initialize failed: %s. Error: %s",
			InFilePath.c_str(),
			Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return false;
	}

	Scene = FbxScene::Create(Manager, "ImportScene");
	if (!Scene)
	{
		UE_LOG("[FBXImporter] Failed to create FbxScene.");
		Importer->Destroy();
		return false;
	}

	const bool bImported = Importer->Import(Scene);
	if (!bImported)
	{
		UE_LOG("[FBXImporter] Import failed: %s. Error: %s",
			InFilePath.c_str(),
			Importer->GetStatus().GetErrorString());
	}

	Importer->Destroy();
	if (bImported)
	{
		PreprocessScene();
	}
	else
	{
		DestroyScene();
	}
	return bImported;
}

bool FBXImporter::FinalizeAsset()
{
	return true;
}

void FBXImporter::ClearState()
{
	ImportMeta.Clear();
}

void FBXImporter::PreprocessScene()
{
	if (!Scene || !Manager)
	{
		return;
	}


	FbxAxisSystem EngineAxisSystem;
	if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxisSystem))
	{
		UE_LOG("[FBXImporter] Failed to parse engine axis system.");
		return;
	}


	// Engine convention: +X forward, +Y right, +Z up, left-handed.
	// ConvertScene() only rotates roots and cannot faithfully represent handedness changes.
	// DeepConvertScene() converts transforms, geometry, animation curves, and clusters consistently.
	EngineAxisSystem.DeepConvertScene(Scene);

	FbxSystemUnit::ConversionOptions UnitOptions = {};
	UnitOptions.mConvertRrsNodes = true;
	UnitOptions.mConvertLimits = true;
	UnitOptions.mConvertClusters = true;
	UnitOptions.mConvertLightIntensity = true;
	UnitOptions.mConvertPhotometricLProperties = true;
	UnitOptions.mConvertCameraClipPlanes = true;

	const FbxSystemUnit CentimeterUnit(100.0);
	CentimeterUnit.ConvertScene(Scene, UnitOptions);

	FbxGeometryConverter Converter(Manager);
	Converter.Triangulate(Scene, true);
}

void FBXImporter::DestroyScene()
{
	if (Scene)
	{
		Scene->Destroy();
		Scene = nullptr;
	}
}

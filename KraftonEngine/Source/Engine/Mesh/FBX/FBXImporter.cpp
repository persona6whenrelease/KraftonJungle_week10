#include "FBXImporter.h"

#include <algorithm>
#include <functional>

#include <fbxsdk.h>
#include "FbxMetaParser.h"

#include "Core/Log.h"

bool FBXImporter::ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset)
{
	if (!InitializeSdk())
	{
		return false;
	}

	if (!LoadScene(InFilePath))
	{
		ShutdownSdk();
		return false;
	}

	FFbxMetaParser MetaParser(ImportMeta);
	if (!MetaParser.BuildFbxMeta(Scene))
	{
		ShutdownSdk();
		return false;
	}

	ImportMeta.SourceFilePath = InFilePath;
	OutFBXAsset.PathFileName = InFilePath;

	if (!ParseStaticMeshes(OutFBXAsset.StaticMeshes))
	{
		ShutdownSdk();
		return false;
	}

	TArray<FFbxSkinnedMeshPart> SkinnedMeshParts;
	if (!ParseSkeletalMeshes(SkinnedMeshParts))
	{
		ShutdownSdk();
		return false;
	}

	if (!AttachMeshes(SkinnedMeshParts, OutFBXAsset.SkeletalMeshes))
	{
		ShutdownSdk();
		return false;
	}

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

bool FBXImporter::ParseStaticMeshes(TArray<FStaticMesh>& OutStaticMeshAsset)
{
	//OutStaticMeshAsset.clear();
	//for (int32 MeshId : ImportMeta.StaticMeshIds)
	//{
	//	if (!IsValidIndex(ImportMeta.Meshes, MeshId))
	//	{
	//		continue;
	//	}

	//	// TODO: Build FStaticMesh vertex/index/section data from ImportMeta.Meshes[MeshId].
	//}
	return true;
}

bool FBXImporter::ParseSkeletalMeshes(TArray<FFbxSkinnedMeshPart>& OutSkinnedMeshParts)
{
	/*OutSkinnedMeshParts.clear();
	for (int32 MeshId : ImportMeta.SkeletalMeshIds)
	{
		if (!IsValidIndex(ImportMeta.Meshes, MeshId))
		{
			continue;
		}

		const FFbxMeshMeta& MeshMeta = ImportMeta.Meshes[MeshId];
		FFbxSkinnedMeshPart Part;
		Part.MeshId = MeshId;
		Part.SkinId = MeshMeta.PrimarySkinId;
		Part.SkeletonId = MeshMeta.SkeletonId;
		Part.Node = MeshMeta.Node;
		Part.Mesh = MeshMeta.Mesh;
		OutSkinnedMeshParts.push_back(Part);
	}*/
	return true;
}

bool FBXImporter::AttachMeshes(const TArray<FFbxSkinnedMeshPart>& SkinnedMeshParts, TArray<FSkeletalMesh>& OutSkeletalMeshAssets)
{
	OutSkeletalMeshAssets.clear();
	for (const FFbxSkeletonMeta& Skeleton : ImportMeta.Skeletons)
	{
		if (!Skeleton.bValid)
		{
			continue;
		}

		for (const FFbxSkinnedMeshPart& Part : SkinnedMeshParts)
		{
			if (Part.SkeletonId == Skeleton.SkeletonId)
			{
				// TODO: Merge mesh parts with the same SkeletonId into one FSkeletalMesh.
			}
		}
	}
	return true;
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

	const FbxAxisSystem DirectXAxisSystem(FbxAxisSystem::eDirectX);
	DirectXAxisSystem.ConvertScene(Scene);

	FbxSystemUnit::ConversionOptions UnitOptions = {};
	UnitOptions.mConvertRrsNodes = true;
	UnitOptions.mConvertLimits = true;
	UnitOptions.mConvertClusters = true;
	UnitOptions.mConvertLightIntensity = true;
	UnitOptions.mConvertPhotometricLProperties = true;
	UnitOptions.mConvertCameraClipPlanes = true;

	const FbxSystemUnit CentimeterUnit(1.0);
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

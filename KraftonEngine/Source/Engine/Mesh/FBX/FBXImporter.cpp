#include "FBXImporter.h"

#include "Core/Log.h"
#include "FbxMaterialImportUtils.h"
#include "FbxMetaParser.h"
#include "FbxSkeletalMeshAssembler.h"
#include "FbxSkeletalMeshPartParser.h"
#include "FbxStaticMeshParser.h"

#include <fbxsdk.h>

bool FBXImporter::ImportFbxAsset(const FString& InFilePath, FFBXAsset& OutFBXAsset)
{
	OutFBXAsset.PathFileName.clear();
	OutFBXAsset.SkeletalMeshes.clear();
	OutFBXAsset.SkeletalMaterials.clear();
	OutFBXAsset.StaticMeshes.clear();
	OutFBXAsset.LightAssets.clear();
	OutFBXAsset.CameraAssets.clear();

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
	if (!StaticMeshParser.Parse(OutFBXAsset.StaticMeshes))
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
	if (!SkeletalMeshAssembler.Assemble(SkinnedMeshParts, OutFBXAsset.SkeletalMeshes))
	{
		ShutdownSdk();
		return false;
	}

	if (!OutFBXAsset.SkeletalMeshes.empty())
	{
		FbxMaterialImportUtils::BuildSkeletalMaterials(
			ImportMeta,
			OutFBXAsset.SkeletalMeshes[0],
			OutFBXAsset.SkeletalMaterials);
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
	EngineAxisSystem.ConvertScene(Scene);

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

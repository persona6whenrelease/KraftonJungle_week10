#include "FBXManager.h"
#include "FBXImporter.h"
#include "Mesh/SkeletalMesh.h"
#include "Materials/MaterialManager.h"

// 정적(Static) 멤버 변수 초기화 (cpp 파일에 반드시 필요)
TMap<FString, USkeletalMesh*> FFBXManager::SkeletalMeshCache;

USkeletalMesh* FFBXManager::LoadSkeletalMesh(const FString& FilePath)
{
	// 1. 캐시 창고
	auto it = SkeletalMeshCache.find(FilePath);
	if (it != SkeletalMeshCache.end())
	{
		// 1-1. 이미 로드한 적이 있다면 기존 에셋 반환
		return it->second;
	}

	// 2. 창고에 없다면? 이파싱
	FFBXImporter TransientImporter;

	if (!TransientImporter.Initialize())
	{
		// 초기화 실패 시 (FBX SDK 에러 등)
		return nullptr;
	}

	// 3. 파싱 데이터를 담을 원시 데이터 구조체 생성
	FStkeletalMesh* MeshData = new FStkeletalMesh();
	MeshData->PathFileName = FilePath;

	// 4. 파서(Importer) 실행
	if (TransientImporter.Import(FilePath.c_str(), *MeshData))
	{
		// ========================================================
		// 5. 엔진 전용 에셋(UObject) 생성 및 데이터 조립 
		// ========================================================
		USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();

		// 5-1. Importer가 만든 Section의 MaterialSlotName 기준으로 Material 슬롯 등록
		if (MeshData->MeshAsset.Sections.empty())
		{
			FSkeletalMeshSection Section;
			Section.MaterialIndex = 0;
			Section.MaterialSlotName = "Default";
			Section.FirstIndex = 0;
			Section.NumTriangles = static_cast<uint32>(MeshData->MeshAsset.Indices.size() / 3);
			MeshData->MeshAsset.Sections.push_back(Section);
		}

		TArray<FStaticMaterial> Materials;
		for (FSkeletalMeshSection& Section : MeshData->MeshAsset.Sections)
		{
			if (Section.MaterialSlotName.empty())
			{
				Section.MaterialSlotName = "Default";
			}

			bool bAlreadyRegistered = false;
			for (const FStaticMaterial& Material : Materials)
			{
				if (Material.MaterialSlotName == Section.MaterialSlotName)
				{
					bAlreadyRegistered = true;
					break;
				}
			}

			if (bAlreadyRegistered)
			{
				continue;
			}

			FStaticMaterial NewMaterial;
			NewMaterial.MaterialSlotName = Section.MaterialSlotName;

			const FString MatPath = "Asset/Materials/Auto/" + Section.MaterialSlotName + ".mat";
			NewMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial(MatPath);
			if (!NewMaterial.MaterialInterface)
			{
				NewMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
			}

			Materials.push_back(NewMaterial);
		}

		SkeletalMesh->SetStaticMaterials(std::move(Materials));

		// 5-2. 완성된 원시 데이터를 UObject 에셋에 결합
		// SetSkeletalMeshAsset 내부에서 Section.MaterialSlotName -> MaterialIndex 캐싱이 갱신됩니다.
		SkeletalMesh->SetSkeletalMeshAsset(MeshData);

		// 6. 캐시에 등록하고 반환
		FMaterialManager::Get().ScanMaterialAssets();
		SkeletalMeshCache[FilePath] = SkeletalMesh;
		return SkeletalMesh;
	}

	// Import 실패 시 쓰레기 처리
	delete MeshData;
	return nullptr;
}

void FFBXManager::ReleaseAll()
{

	for (auto& Pair : SkeletalMeshCache)
	{
		// USkeletalMesh는 UObjectManager가 소멸을 관리
	}
	SkeletalMeshCache.clear();
}

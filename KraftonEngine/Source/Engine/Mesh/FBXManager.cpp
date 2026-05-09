#include "FBXManager.h"
#include "FBXImporter.h"
#include "Mesh/SkeletalMesh.h"

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

		// 5-1. 기본 Fallback 머티리얼 세팅
		TArray<FStaticMaterial> Materials;
		FStaticMaterial DefaultMat;
		DefaultMat.MaterialSlotName = "Default";
		DefaultMat.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
		Materials.push_back(DefaultMat);
		SkeletalMesh->SetStaticMaterials(std::move(Materials));

		// 5-2. 섹션(Section) 세팅 
		// (Importer에서 임시로 만들었다면 덮어씌우거나 비워줍니다)
		MeshData->MeshAsset.Sections.clear();

		FSkeletalMeshSection Section;
		Section.MaterialIndex = 0;
		Section.MaterialSlotName = "Default";
		Section.FirstIndex = 0;
		Section.NumTriangles = static_cast<uint32>(MeshData->MeshAsset.Indices.size() / 3);
		MeshData->MeshAsset.Sections.push_back(Section);

		// 5-3. 완성된 원시 데이터를 UObject 에셋에 결합
		SkeletalMesh->SetSkeletalMeshAsset(MeshData);

		// 6. 캐시에 등록하고 반환
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
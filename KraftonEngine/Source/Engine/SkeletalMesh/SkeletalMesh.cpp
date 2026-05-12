#include "SkeletalMesh.h"
#include "Render/Resource/Buffer.h"
#include "Mesh/StaticMesh.h"
#include "Object/ObjectFactory.h"

IMPLEMENT_CLASS(USkeletalMesh, UObject)


USkeletalMesh::~USkeletalMesh()
{
	if (SkeletalMeshAsset)
	{
		delete SkeletalMeshAsset;
		SkeletalMeshAsset = nullptr;
	}
}

static void CacheSectionMaterialIndices(FSkeletalMesh* Asset,
                                        const TArray<FStaticMaterial>& Materials)
{
	if (!Asset) return;
	for (FStaticMeshSection& Section : Asset->Sections)
	{
		Section.MaterialIndex = -1;
		for (int32 i = 0; i < (int32)Materials.size(); ++i)
		{
			if (Materials[i].MaterialSlotName == Section.MaterialSlotName)
			{
				Section.MaterialIndex = i;
				break;
			}
		}
	}
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// 1. 메시 데이터 본체 직렬화 — 버전 미스매치 시 SkeletalMeshAsset을 nullptr로 두고 즉시 반환.
	//    FBXManager가 GetSkeletalMeshAsset() == nullptr 을 재빌드 신호로 사용한다.
	bool bHasAsset = (SkeletalMeshAsset != nullptr);
	Ar << bHasAsset;

	if (Ar.IsLoading())
	{
		if (bHasAsset)
		{
			FSkeletalMesh* Temp = new FSkeletalMesh();
			if (!Temp->Serialize(Ar))
			{
				// 구 포맷(v1) 캐시 — 폐기하고 호출자가 재빌드하도록 한다.
				delete Temp;
				SkeletalMeshAsset = nullptr;
				return;
			}
			SkeletalMeshAsset = Temp;
		}
	}
	else
	{
		if (SkeletalMeshAsset)
		{
			SkeletalMeshAsset->Serialize(Ar);
		}
	}

	// 2. 머티리얼 및 본 이름 정보 직렬화
	Ar << StaticMaterials;
	Ar << BoneNames;

	if (Ar.IsLoading())
	{
		RebuildBoneMap();
		// loading 흐름은 setter 를 거치지 않으므로 직접 호출.
		CacheSectionMaterialIndices(SkeletalMeshAsset, StaticMaterials);
	}

	// 3. EmbeddedStaticMesh (hybrid FBX의 static 파트) 직렬화 — 항상 플래그 1바이트 흐름 유지.
	bool bHasEmbedded = (EmbeddedStaticMesh != nullptr);
	Ar << bHasEmbedded;
	if (bHasEmbedded)
	{
		if (Ar.IsLoading())
		{
			EmbeddedStaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
		}
		if (EmbeddedStaticMesh)
		{
			EmbeddedStaticMesh->Serialize(Ar);
		}
	}
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	if (SkeletalMeshAsset && SkeletalMeshAsset != InMesh)
	{
		delete SkeletalMeshAsset;
	}
	SkeletalMeshAsset = InMesh;

	// Slot 이름 기반으로 Section.MaterialIndex 를 재캐싱한다 (UStaticMesh::SetStaticMeshAsset 와 동일).
	CacheSectionMaterialIndices(SkeletalMeshAsset, StaticMaterials);
}

void USkeletalMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
	StaticMaterials = std::move(InMaterials);
	// 머티리얼 슬롯이 바뀌었으므로 현 asset 의 section 들 MaterialIndex 도 재캐싱.
	CacheSectionMaterialIndices(SkeletalMeshAsset, StaticMaterials);
}

void USkeletalMesh::InitResources(ID3D11Device* InDevice)
{
	if (SkeletalMeshAsset)
	{
		// Bind-pose 렌더링 및 인덱스 버퍼 공유를 위한 RenderBuffer 생성
		SkeletalMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();

		// FMeshBuffer::Create는 TMeshData를 인자로 받으므로 임시 구조체 생성
		TMeshData<FNormalVertex> MeshData;
		MeshData.Vertices = SkeletalMeshAsset->Vertices;
		MeshData.Indices  = SkeletalMeshAsset->Indices;

		SkeletalMeshAsset->RenderBuffer->Create(InDevice, MeshData);

		// 바운드 갱신
		SkeletalMeshAsset->CacheBounds();
	}

	// Hybrid FBX에서 함께 들어온 static 파트의 GPU 리소스도 초기화한다.
	if (EmbeddedStaticMesh)
	{
		EmbeddedStaticMesh->InitResources(InDevice);
	}
}

int32 USkeletalMesh::GetBoneIndex(const FName& Name) const
{
	auto It = BoneNameToIndex.find(Name);
	if (It != BoneNameToIndex.end())
	{
		return It->second;
	}
	return -1;
}

void USkeletalMesh::SetBoneNames(TArray<FName>&& InNames)
{
	BoneNames = std::move(InNames);
	RebuildBoneMap();
}

void USkeletalMesh::RebuildBoneMap()
{
	BoneNameToIndex.clear();
	for (int32 i = 0; i < (int32)BoneNames.size(); ++i)
	{
		BoneNameToIndex[BoneNames[i]] = i;
	}
}

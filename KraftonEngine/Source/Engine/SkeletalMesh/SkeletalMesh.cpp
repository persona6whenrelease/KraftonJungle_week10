#include "SkeletalMesh.h"
#include "Render/Resource/Buffer.h"

USkeletalMesh::~USkeletalMesh()
{
	if (SkeletalMeshAsset)
	{
		delete SkeletalMeshAsset;
		SkeletalMeshAsset = nullptr;
	}
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// 1. 메시 데이터 본체 직렬화
	bool bHasAsset = (SkeletalMeshAsset != nullptr);
	Ar << bHasAsset;

	if (Ar.IsLoading())
	{
		if (bHasAsset)
		{
			SkeletalMeshAsset = new FSkeletalMesh();
			SkeletalMeshAsset->Serialize(Ar);
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
	}
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	if (SkeletalMeshAsset && SkeletalMeshAsset != InMesh)
	{
		delete SkeletalMeshAsset;
	}
	SkeletalMeshAsset = InMesh;
}

void USkeletalMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
	StaticMaterials = std::move(InMaterials);
}

void USkeletalMesh::InitResources(ID3D11Device* InDevice)
{
	if (!SkeletalMeshAsset) return;

	// T-Pose 렌더링 및 인덱스 버퍼 공유를 위한 RenderBuffer 생성
	SkeletalMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
	
	// FMeshBuffer::Create는 TMeshData를 인자로 받으므로 임시 구조체 생성
	TMeshData<FSkeletalMeshVertex> MeshData;
	MeshData.Vertices = SkeletalMeshAsset->Vertices;
	MeshData.Indices = SkeletalMeshAsset->Indices;

	SkeletalMeshAsset->RenderBuffer->Create(InDevice, MeshData);
	
	// 바운드 갱신
	SkeletalMeshAsset->CacheBounds();
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

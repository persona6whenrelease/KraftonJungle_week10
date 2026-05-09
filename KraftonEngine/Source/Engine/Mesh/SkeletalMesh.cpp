#include "SkeletalMesh.h"

IMPLEMENT_CLASS(USkeletalMesh, UObject)

const FString USkeletalMesh::EmptyPath = "";

USkeletalMesh::~USkeletalMesh()
{
	if (SkeletalMeshAsset)
	{
		const uint32 CPUSize =
			static_cast<uint32>(SkeletalMeshAsset->MeshAsset.SourceVertices.size() * sizeof(FNormalVertex)) +
			static_cast<uint32>(SkeletalMeshAsset->MeshAsset.Indices.size() * sizeof(uint32));

		MemoryStats::SubStaticMeshCPUMemory(CPUSize);
	}
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	// 에셋이 비어있으면 로드용으로 생성
	if (Ar.IsLoading() && !SkeletalMeshAsset)
	{
		SkeletalMeshAsset = new FStkeletalMesh();
	}

	// 1. 지오메트리 데이터 직렬화
	SkeletalMeshAsset->Serialize(Ar);

	// 2. 머티리얼 데이터 직렬화 (필수!)
	Ar << StaticMaterials;

	// 3. 로딩 시 Section → MaterialIndex 매핑 캐싱 (매 프레임 문자열 비교 방지)
	if (Ar.IsLoading())
	{
		for (FSkeletalMeshSection& Section : SkeletalMeshAsset->MeshAsset.Sections)
		{
			Section.MaterialIndex = -1;
			for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
			{
				if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
				{
					Section.MaterialIndex = i;
					break;
				}
			}
		}
	}
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
	if (SkeletalMeshAsset)
	{
		return SkeletalMeshAsset->PathFileName;
	}
	return USkeletalMesh::EmptyPath;
}

void USkeletalMesh::SetSkeletalMeshAsset(FStkeletalMesh* InMesh)
{
	SkeletalMeshAsset = InMesh;
	// 현재는 static mesh asset이 로드 후 고정된다고 보고, 메시 변경 dirty 갱신은 비활성화합니다.
	// MarkMeshTrianglePickingBVHDirty();

	// Section → MaterialIndex 캐싱 갱신
	if (SkeletalMeshAsset)
	{
		for (FSkeletalMeshSection& Section : SkeletalMeshAsset->MeshAsset.Sections)
		{
			Section.MaterialIndex = -1;
			for (int32 i = 0; i < (int32)StaticMaterials.size(); ++i)
			{
				if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
				{
					Section.MaterialIndex = i;
					break;
				}
			}
		}
	}
}

FStkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
	return SkeletalMeshAsset;
}

void USkeletalMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
	StaticMaterials = InMaterials;

}

const TArray<FStaticMaterial>& USkeletalMesh::GetStaticMaterials() const
{
	return StaticMaterials;
}
//
//void USkeletalMesh::InitResources(ID3D11Device* InDevice)
//{
//	if (!InDevice || !SkeletalMeshAsset) return;
//
//	// CPU 메모리 추적
//	const uint32 CPUSize =
//		static_cast<uint32>(SkeletalMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
//		static_cast<uint32>(SkeletalMeshAsset->Indices.size() * sizeof(uint32));
//	MemoryStats::AddStaticMeshCPUMemory(CPUSize);
//
//	// CPU → GPU 정점 버퍼 변환
//	TMeshData<FVertexPNCTT> RenderMeshData;
//	RenderMeshData.Vertices.reserve(SkeletalMeshAsset->Vertices.size());
//
//	for (const FNormalVertex& RawVert : SkeletalMeshAsset->Vertices)
//	{
//		FVertexPNCTT RenderVert;
//		RenderVert.Position = RawVert.pos;
//		RenderVert.Normal = RawVert.normal;
//		RenderVert.Color = RawVert.color;
//		RenderVert.UV = RawVert.tex;
//		RenderVert.Tangent = RawVert.tangent;
//		RenderMeshData.Vertices.push_back(RenderVert);
//	}
//	RenderMeshData.Indices = SkeletalMeshAsset->Indices;
//
//	SkeletalMeshAsset->RenderBuffer = std::make_unique<FDynamicVertexBuffer>();
//	SkeletalMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);
//
//
//}


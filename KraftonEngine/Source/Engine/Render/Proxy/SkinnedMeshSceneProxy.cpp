#include "SkinnedMeshSceneProxy.h"
#include "Component/SkinnedMeshComponent.h"
#include "Runtime/Engine.h"
#include "Render/Resource/Buffer.h"

FSkinnedMesScenehProxy::FSkinnedMesScenehProxy(USkinnedMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

void FSkinnedMesScenehProxy::UpdateMaterial()
{
}

void FSkinnedMesScenehProxy::UpdateMesh()
{
	USkinnedMeshComponent* Comp = GetSkinnedMeshComponent();
	if (!Comp) return;

	USkeletalMesh* Mesh = Comp->GetSkeletalMesh();
	if (!Mesh) return;

	FStkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
	if (!Asset) return;

	const uint32 VertexCount =
		static_cast<uint32>(Asset->MeshAsset.SourceVertices.size());
	const uint32 IndexCount =
		static_cast<uint32>(Asset->MeshAsset.Indices.size());

	const bool bNeedCreate =
		!RenderBuffer ||
		CachedVertexCount != VertexCount ||
		CachedIndexCount != IndexCount;
	if (bNeedCreate)
	{
		TMeshData<FVertexPNCTT> InitialData;
		InitialData.Vertices.resize(VertexCount);
		InitialData.Indices = Asset->MeshAsset.Indices;

		// 처음엔 bind pose를 GPU용 정점으로 변환
		for (uint32 i = 0; i < VertexCount; ++i)
		{
			const FSkeletalSourceVertex& Src = Asset->MeshAsset.SourceVertices[i];

			FVertexPNCTT& Dst = InitialData.Vertices[i];
			Dst.Position = Src.Position;
			Dst.Normal = Src.Normal;
			Dst.UV = Src.UV;
			Dst.Color = Src.Color;
			Dst.Tangent = Src.Tangent;
		}

		ID3D11Device* Device =
			GEngine->GetRenderer().GetFD3DDevice().GetDevice();

		RenderBuffer = std::make_unique<FSkinnedMeshBuffer>();
		RenderBuffer->Create(Device, InitialData);

		SkinnedVertices = InitialData.Vertices;
		CachedVertexCount = VertexCount;
		CachedIndexCount = IndexCount;
	}


	SkinnedMeshBuffer = RenderBuffer.get();
}

void FSkinnedMesScenehProxy::UpdateDynamicData()
{
	USkinnedMeshComponent* Comp = GetSkinnedMeshComponent();
	if (!Comp) return;

	const TArray<FVertexPNCTT>& Vertices = Comp->GetSkinnedVertices();

	ID3D11DeviceContext* Context =
		GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();

	RenderBuffer->GetVertexBuffer().Update(
		Context,
		Vertices.data(),
		static_cast<uint32>(Vertices.size())
	);
}

FRenderBufferView FSkinnedMesScenehProxy::GetRenderBufferView() const
{
	if (!SkinnedMeshBuffer) return FRenderBufferView();

	FRenderBufferView BufferView = {};
	BufferView.VB = SkinnedMeshBuffer->GetVertexBuffer().GetBuffer();
	BufferView.VBStride = SkinnedMeshBuffer->GetVertexBuffer().GetStride();
	BufferView.IB = SkinnedMeshBuffer->GetIndexBuffer().GetBuffer();
	BufferView.IndexCount = SkinnedMeshBuffer->GetIndexBuffer().GetIndexCount();

	return BufferView;
}

USkinnedMeshComponent* FSkinnedMesScenehProxy::GetSkinnedMeshComponent() const
{
	return static_cast<USkinnedMeshComponent*>(GetOwner());
}

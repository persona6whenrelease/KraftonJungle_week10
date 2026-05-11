#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Component/SkeletalMeshComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Mesh/SkeletalMesh.h"
#include "Materials/Material.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Device/D3DDevice.h"
#include "Core/Log.h"
#include "Render/Types/RenderConstants.h"

namespace
{
	struct FSkeletalDefaultMaterialConstants
	{
		FVector4 SectionColor = FVector4(0.85f, 0.85f, 0.85f, 1.0f);
		float HasNormalMap = 0.0f;
		float Pad[3] = {};
	};
}

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	DefaultMaterialCB.Release();
}

USkeletalMeshComponent* FSkeletalMeshSceneProxy::GetSkeletalMeshComponent() const
{
	return static_cast<USkeletalMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMesh()
{
	UpdateDynamicGeometry();
	UpdateMaterial();
}

void FSkeletalMeshSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	(void)Frame;

	UpdateDynamicGeometry();
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
	SectionDraws.clear();

	USkeletalMeshComponent* Component = GetSkeletalMeshComponent();

	if (!Component)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Component->GetSkeletalMesh();

	if (!SkeletalMesh)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();

	if (!MeshAsset)
	{
		return;
	}

	if (!DefaultMaterial)
	{
		DefaultMaterial = UMaterial::CreateTransient(
			ERenderPass::Opaque,
			EBlendState::Opaque,
			EDepthStencilState::Default,
			ERasterizerState::SolidNoCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::UberLit)
		);

		FSkeletalDefaultMaterialConstants& Constants =
			DefaultMaterial->BindPerShaderCB<FSkeletalDefaultMaterialConstants>(
				&DefaultMaterialCB,
				ECBSlot::PerShader0
			);

		Constants.SectionColor = FVector4(0.85f, 0.85f, 0.85f, 1.0f);
		Constants.HasNormalMap = 0.0f;
	}

	const TArray<FStaticMaterial>& StaticMaterials = SkeletalMesh->GetStaticMaterials();

	for (const FStaticMeshSection& Section : MeshAsset->Sections)
	{
		FMeshSectionDraw Draw;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumTriangles * 3;

		if (Section.MaterialIndex >= 0 &&
			Section.MaterialIndex < static_cast<int32>(StaticMaterials.size()) &&
			StaticMaterials[Section.MaterialIndex].MaterialInterface)
		{
			Draw.Material = StaticMaterials[Section.MaterialIndex].MaterialInterface;
		}
		else
		{
			Draw.Material = DefaultMaterial;
		}

		SectionDraws.push_back(Draw);
	}

	if (SectionDraws.empty() && !MeshAsset->Indices.empty())
	{
		FMeshSectionDraw Draw;
		Draw.Material = DefaultMaterial;
		Draw.FirstIndex = 0;
		Draw.IndexCount = static_cast<uint32>(MeshAsset->Indices.size());

		SectionDraws.push_back(Draw);
	}

	UE_LOG(
		"[SkeletalProxy] UpdateMaterial. Sections=%zu, SectionDraws=%zu, DefaultMaterial=%p",
		MeshAsset->Sections.size(),
		SectionDraws.size(),
		DefaultMaterial
	);
}

void FSkeletalMeshSceneProxy::UpdateDynamicGeometry()
{
	USkeletalMeshComponent* Component = GetSkeletalMeshComponent();

	if (!Component || !Component->GetSkeletalMesh())
	{
		SetGeometryBuffer({});
		return;
	}

	Component->RefreshSkinning();

	ID3D11Device* Device =
		GEngine->GetRenderer().GetFD3DDevice().GetDevice();

	ID3D11DeviceContext* Context =
		GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();

	Component->UploadSkinnedMeshToGPU(Device, Context);

	const FDynamicVertexBuffer& VertexBuffer =
		Component->GetDynamicVertexBuffer();

	const FDynamicIndexBuffer& IndexBuffer =
		Component->GetDynamicIndexBuffer();

	FGeometryBufferView View;
	View.VertexBuffer = VertexBuffer.GetBuffer();
	View.VertexStride = VertexBuffer.GetStride();
	View.VertexCount =
		static_cast<uint32>(Component->GetSkinnedVertices().size());

	View.IndexBuffer = IndexBuffer.GetBuffer();
	View.IndexCount =
		static_cast<uint32>(Component->GetSkinnedIndices().size());

	SetGeometryBuffer(View);

	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		bLoggedOnce = true;

		const TArray<FNormalVertex>& Vertices = Component->GetSkinnedVertices();
		const TArray<uint32>& Indices = Component->GetSkinnedIndices();

		if (!Vertices.empty())
		{
			const FVector& P = Vertices[0].pos;

			UE_LOG(
				"[SkeletalProxy] DynamicGeometry. Vertices=%zu, Indices=%zu, VB=%p, IB=%p, FirstPos=(%.3f, %.3f, %.3f)",
				Vertices.size(),
				Indices.size(),
				View.VertexBuffer,
				View.IndexBuffer,
				P.X,
				P.Y,
				P.Z
			);
		}
		else
		{
			UE_LOG(
				"[SkeletalProxy] DynamicGeometry has no vertices. Indices=%zu, VB=%p, IB=%p",
				Indices.size(),
				View.VertexBuffer,
				View.IndexBuffer
			);
		}
	}
}

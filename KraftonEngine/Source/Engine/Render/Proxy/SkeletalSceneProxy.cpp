#include "Render/Proxy/SkeletalSceneProxy.h"
#include "Component/SkeletalMeshComponent.h"
#include "SkeletalMesh/SkeletalMesh.h"
#include "SkeletalMesh/SkeletalMeshAsset.h"
#include "Materials/Material.h"

#include <algorithm>

namespace
{
	bool SectionMaterialLess(const FMeshSectionDraw& A, const FMeshSectionDraw& B)
	{
		const uintptr_t AMat = reinterpret_cast<uintptr_t>(A.Material);
		const uintptr_t BMat = reinterpret_cast<uintptr_t>(B.Material);
		if (AMat != BMat)
			return AMat < BMat;
		return A.FirstIndex < B.FirstIndex;
	}

	void SortSectionDrawsByMaterial(TArray<FMeshSectionDraw>& Draws)
	{
		if (Draws.size() > 1)
			std::sort(Draws.begin(), Draws.end(), SectionMaterialLess);
	}
}

// ============================================================
// FSkeletalSceneProxy
// ============================================================

FSkeletalSceneProxy::FSkeletalSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

USkeletalMeshComponent* FSkeletalSceneProxy::GetSkeletalMeshComponent() const
{
	return static_cast<USkeletalMeshComponent*>(GetOwner());
}

// ============================================================
// GetGeometryView — 모드별 버퍼 선택
// ============================================================
FGPUGeometryView FSkeletalSceneProxy::GetGeometryView() const
{
	USkeletalMeshComponent* Comp = GetSkeletalMeshComponent();
	USkeletalMesh* SkeletalMesh = Comp->GetSkeletalMesh();
	if (!SkeletalMesh) return {};

	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset || !Asset->RenderBuffer) return {};

	if (Comp->GetSkinningMode() == ESkinningMode::CPU)
	{
		// 컴포넌트가 매 프레임 갱신하는 동적 버퍼 + 에셋의 정적 IB
		const FDynamicVertexBuffer& DVB = Comp->GetDynamicVB();
		if (!DVB.GetBuffer()) return {};
		return {
			DVB.GetBuffer(),
			DVB.GetStride(),
			Asset->RenderBuffer->GetIndexBuffer().GetBuffer()
		};
	}
	else
	{
		// GPU 스키닝(미구현 stub): 에셋의 정적 bind-pose VB/IB.
		// cluster 모델에서는 vertex에 bone 정보가 없으므로,
		// vertex shader가 cluster SRV(또는 bone-matrix palette)를 별도 슬롯으로 받아야 함.
		if (!Asset->RenderBuffer->IsValid()) return {};
		return {
			Asset->RenderBuffer->GetVertexBuffer().GetBuffer(),
			Asset->RenderBuffer->GetVertexBuffer().GetStride(),
			Asset->RenderBuffer->GetIndexBuffer().GetBuffer()
		};
	}
}

// ============================================================
// UpdateMaterial — 머티리얼 변경 시 SectionDraws 재구축
// ============================================================
void FSkeletalSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

// ============================================================
// UpdateMesh — 메시 버퍼 갱신 + SectionDraws 재구축
// ============================================================
void FSkeletalSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();
}

// ============================================================
// UpdateLOD — LOD 레벨 변경 시 MeshBuffer/SectionDraws 스왑
// ============================================================
void FSkeletalSceneProxy::UpdateLOD(uint32 LODLevel)
{
	if (LODLevel >= LODCount) LODLevel = LODCount - 1;
	if (LODLevel == CurrentLOD) return;

	std::swap(MeshBuffer, LODData[CurrentLOD].MeshBuffer);
	std::swap(SectionDraws, LODData[CurrentLOD].SectionDraws);

	CurrentLOD = LODLevel;
	std::swap(MeshBuffer, LODData[LODLevel].MeshBuffer);
	std::swap(SectionDraws, LODData[LODLevel].SectionDraws);
}

// ============================================================
// RebuildSectionDraws — 에셋 섹션 → SectionDraws 구축
// ============================================================
void FSkeletalSceneProxy::RebuildSectionDraws()
{
	USkeletalMeshComponent* Comp = GetSkeletalMeshComponent();
	USkeletalMesh* SkeletalMesh = Comp->GetSkeletalMesh();

	if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset())
	{
		for (uint32 lod = 0; lod < MAX_LOD; ++lod)
		{
			LODData[lod].MeshBuffer = nullptr;
			LODData[lod].SectionDraws.clear();
		}
		LODCount = 1;
		CurrentLOD = 0;
		MeshBuffer = nullptr;
		SectionDraws.clear();
		return;
	}

	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	const TArray<FStaticMaterial>& Slots = SkeletalMesh->GetStaticMaterials();
	const TArray<UMaterial*>& Overrides = Comp->GetOverrideMaterials();

	// 스켈레탈 메시는 단일 LOD — LOD0만 구축
	LODCount = 1;

	LODData[0].MeshBuffer = Asset->RenderBuffer.get();
	LODData[0].SectionDraws.clear();
	LODData[0].SectionDraws.reserve(Asset->Sections.size());

	for (const FStaticMeshSection& Section : Asset->Sections)
	{
		FMeshSectionDraw Draw;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumTriangles * 3;

		int32 MatIdx = Section.MaterialIndex;
		if (MatIdx >= 0 && MatIdx < (int32)Slots.size())
		{
			if (MatIdx < (int32)Overrides.size() && Overrides[MatIdx])
				Draw.Material = Overrides[MatIdx];
			else if (Slots[MatIdx].MaterialInterface)
				Draw.Material = Slots[MatIdx].MaterialInterface;
		}

		LODData[0].SectionDraws.push_back(Draw);
	}

	SortSectionDrawsByMaterial(LODData[0].SectionDraws);

	// LOD0을 활성 슬롯으로
	CurrentLOD = 0;
	std::swap(MeshBuffer, LODData[0].MeshBuffer);
	std::swap(SectionDraws, LODData[0].SectionDraws);
}

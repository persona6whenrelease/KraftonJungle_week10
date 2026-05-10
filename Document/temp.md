# 구현 계획: USkinnedMeshComponent & USkeletalMeshComponent

## Context

스켈레탈 메시 렌더링 파이프라인 구현의 첫 단계로, Component 레이어를 완성한다.
`FSkeletalSceneProxy`(2단계)가 이 컴포넌트에서 데이터를 가져가는 구조이므로, 컴포넌트의 API와 데이터 구조를 먼저 확정해야 한다.
`StaticMeshComponent` 패턴을 그대로 따르되, CPU/GPU 이중 스키닝 모드 분기를 추가한다.

---

## 1단계: ESkinningMode 추가

**파일:** `KraftonEngine/Source/Engine/Render/Types/RenderTypes.h`

`ERenderPass` 아래에 추가:

```cpp
enum class ESkinningMode
{
    CPU,
    GPU,
};
```

---

## 2단계: USkinnedMeshComponent 생성

**파일:** `KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h/.cpp`

### 헤더 설계

```cpp
#pragma once
#include "Component/MeshComponent.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/RenderTypes.h"
#include "Math/Matrix.h"

struct FSkeletalMesh;

class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

    ESkinningMode GetSkinningMode() const { return SkinningMode; }
    void SetSkinningMode(ESkinningMode InMode) { SkinningMode = InMode; }

    const FDynamicVertexBuffer& GetDynamicVB() const { return DynamicVB; }
    const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }

    // USkeletalMeshComponent에서 mesh가 설정된 뒤 호출
    void UpdateSkinning();

protected:
    // USkeletalMeshComponent가 override — 에셋 접근 인터페이스
    virtual FSkeletalMesh* GetSkeletalMeshData() const { return nullptr; }

    ESkinningMode SkinningMode = ESkinningMode::CPU;

    TArray<FMatrix> BoneMatrices;       // 본 로컬→월드 누적 행렬
    TArray<FMatrix> SkinningMatrices;   // BoneMatrices[i] * InverseBindMatrix[i]

    FDynamicVertexBuffer DynamicVB;     // CPU 모드 전용 스키닝 결과 버퍼

private:
    void UpdateSkinningCPU(FSkeletalMesh* MeshData);
    void UpdateSkinningGPU();
};
```

### cpp 구현 로직

**`UpdateSkinning()`**

```
FSkeletalMesh* MeshData = GetSkeletalMeshData();
if (!MeshData) return;

// SkinningMatrices 계산: BoneMatrices[i] * Bones[i].InverseBindMatrix
SkinningMatrices.resize(BoneMatrices.size());
for (int32 i = 0; i < SkinningMatrices.size(); ++i)
    SkinningMatrices[i] = BoneMatrices[i] * MeshData->Bones[i].InverseBindMatrix;

if (SkinningMode == ESkinningMode::CPU)
    UpdateSkinningCPU(MeshData);
else
    UpdateSkinningGPU();
```

**`UpdateSkinningCPU(FSkeletalMesh*)`**

```
// 원본 정점 순회 → 스키닝 적용 → SkinVertex 배열 생성
TArray<FSkeletalMeshVertex> Skinned = MeshData->Vertices;
for (auto& V : Skinned)
{
    FVector SkinPos = {0,0,0}, SkinNorm = {0,0,0};
    for (int j = 0; j < 4; ++j)
    {
        float W = V.boneWeights[j];
        if (W <= 0.0f) continue;
        int Idx = V.boneIndices[j];
        SkinPos  += SkinningMatrices[Idx].TransformPositionWithW(V.Position) * W;
        SkinNorm += SkinningMatrices[Idx].TransformVector(V.Normal) * W;
    }
    V.Position = SkinPos;
    V.Normal   = SkinNorm;
}
// GPU 업로드
ID3D11DeviceContext* Ctx = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
DynamicVB.Update(Ctx, Skinned.data(), (uint32)Skinned.size());
```

**`UpdateSkinningGPU()`**

```
// SkinningMatrices를 Proxy에 전달 (Proxy 구현 시 완성)
// 현재는 Cast<FSkeletalSceneProxy>(SceneProxy)를 통해 UpdateBoneMatrices() 호출 예정
// — Proxy 단계에서 구현
```

---

## 3단계: USkeletalMeshComponent 생성

**파일:** `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h/.cpp`

### 헤더 설계

```cpp
#pragma once
#include "Component/SkinnedMeshComponent.h"
#include "SkeletalMesh/SkeletalMesh.h"
#include "Mesh/StaticMeshAsset.h"  // FMaterialSlot

class UMaterial;
class FPrimitiveSceneProxy;

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    // 에셋 바인딩
    void SetSkeletalMesh(USkeletalMesh* InMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

    // 머티리얼 오버라이드
    void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
    UMaterial* GetMaterial(int32 ElementIndex) const;
    const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

    // 애니메이션 틱 — 본 행렬 갱신 후 UpdateSkinning() 호출
    void UpdateAnimation(float DeltaTime);

    // UPrimitiveComponent override
    FPrimitiveSceneProxy* CreateSceneProxy() override;
    FMeshBuffer* GetMeshBuffer() const override;
    FMeshDataView GetMeshDataView() const override;
    void UpdateWorldAABB() const override;

protected:
    FSkeletalMesh* GetSkeletalMeshData() const override;

private:
    void CacheLocalBounds();
    void ComputeBoneMatrices();  // 바인드 포즈 FK

    USkeletalMesh* SkeletalMesh = nullptr;
    FString SkeletalMeshPath = "None";
    TArray<UMaterial*> OverrideMaterials;
    TArray<FMaterialSlot> MaterialSlots;

    FVector CachedLocalCenter = {0, 0, 0};
    FVector CachedLocalExtent = {0.5f, 0.5f, 0.5f};
    bool bHasValidBounds = false;
};
```

### cpp 핵심 로직

**`SetSkeletalMesh(USkeletalMesh* InMesh)`** — StaticMeshComponent::SetStaticMesh 패턴 그대로

```
- SkeletalMesh = InMesh
- SkeletalMeshPath 설정, OverrideMaterials/MaterialSlots 초기화
- FSkeletalMesh* Asset = InMesh->GetSkeletalMeshAsset()
- BoneMatrices 크기를 Bones 수에 맞춰 resize + Identity 초기화
- DynamicVB.Create(Device, Asset->Vertices.size(), sizeof(FSkeletalMeshVertex))
- CacheLocalBounds()
- MarkRenderStateDirty() + MarkWorldBoundsDirty()
```

**`UpdateAnimation(float DeltaTime)`**

```
- ComputeBoneMatrices()  // FK 계산 (현재 바인드 포즈 고정)
- UpdateSkinning()
```

**`ComputeBoneMatrices()`**

```
// Forward Kinematics — Bones 배열을 부모→자식 순서로 순회 (정렬 가정)
for (int32 i = 0; i < Bones.size(); ++i)
{
    FMatrix Local = FMatrix::MakeScaleMatrix(Bone.Scale)
                  * FMatrix(Bone.Rotation)          // FQuat→FMatrix
                  * FMatrix::MakeTranslationMatrix(Bone.Translation);
    if (Bone.ParentIndex < 0)
        BoneMatrices[i] = Local;
    else
        BoneMatrices[i] = BoneMatrices[Bone.ParentIndex] * Local;
}
```

**`GetMeshBuffer()`** — CPU/GPU 모드에 따라 분기

```
// Proxy가 DrawCommandBuilder에 제출할 메시 버퍼를 결정
// GetGeometryView()에서 오버라이드되므로 기본은 에셋 RenderBuffer 반환
return SkeletalMesh->GetSkeletalMeshAsset()->RenderBuffer.get();
```

**`CreateSceneProxy()`**

```
return new FSkeletalSceneProxy(this);  // Proxy 단계에서 구현
```

---

## 수정 파일 요약

| 파일 | 작업 |
| --- | --- |
| `Render/Types/RenderTypes.h` | `ESkinningMode` enum 추가 |
| `Component/SkinnedMeshComponent.h` | **신규 생성** |
| `Component/SkinnedMeshComponent.cpp` | **신규 생성** |
| `Component/SkeletalMeshComponent.h` | **신규 생성** |
| `Component/SkeletalMeshComponent.cpp` | **신규 생성** |

`FSkeletalSceneProxy` (h/cpp)는 다음 단계에서 구현.

---

## 보류/전제 사항

1. `FQuat→FMatrix` 변환: `FMatrix::FMatrix(FQuat)` 생성자 또는 유틸 함수 존재 여부 확인 필요.
없으면 FBone 로컬 행렬을 `Scale → Rotate → Translate` 순서로 직접 구성.
2. `GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext()` 경로로 D3D11 context 획득 (StaticMeshComponent.cpp 패턴 참고).
3. `CreateSceneProxy()`는 `FSkeletalSceneProxy`를 반환하나, Proxy 미구현 상태에서는 forward declaration + `nullptr` 반환으로 컴파일 통과.

---

## 검증 방법

1. **컴파일 통과**: 프로젝트 빌드 후 오류 없음 확인.
2. **에셋 바인딩**: `USkeletalMeshComponent`에 `USkeletalMesh` 에셋 설정 시 `BoneMatrices` 크기 = 본 수, `DynamicVB`가 생성됨.
3. **CPU 스키닝 동작**: `UpdateAnimation(0.0f)` 호출 후 `DynamicVB.GetBuffer() != nullptr`.
4. **바운드 계산**: `UpdateWorldAABB()` 호출 후 유효한 AABB 값 확인.
5. **Proxy 연동 준비**: `CreateSceneProxy()`가 컴파일되고 (Proxy 구현 후) 올바른 타입 반환.
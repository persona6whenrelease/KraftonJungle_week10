# FSkeletalSceneProxy 구현 스펙

본 문서는 현재 구현된 `FSkeletalSceneProxy`의 상태를 기록한다.

## 1. 클래스 계층 및 파일 위치

```
FPrimitiveSceneProxy
  └─ FSkeletalSceneProxy
```

| 파일 | 경로 |
|------|------|
| 헤더 | `KraftonEngine/Source/Engine/Render/Proxy/SkeletalSceneProxy.h` |
| 구현 | `KraftonEngine/Source/Engine/Render/Proxy/SkeletalSceneProxy.cpp` |

---

## 2. 멤버 구조

```cpp
// 내부 LOD 슬롯 — 각 LOD별 MeshBuffer + SectionDraws를 보관
struct FLODDrawData {
    FMeshBuffer*             MeshBuffer   = nullptr;
    TArray<FMeshSectionDraw> SectionDraws;
};

FLODDrawData LODData[MAX_LOD];  // MAX_LOD = 4
uint32       LODCount = 1;      // 현재 스켈레탈 메시는 단일 LOD 고정
```

부모 `FPrimitiveSceneProxy`로부터 상속받은 **활성 슬롯**:

| 멤버 | 역할 |
|------|------|
| `FMeshBuffer* MeshBuffer` | 현재 LOD의 활성 정적 버퍼 (에셋 RenderBuffer 포인터) |
| `TArray<FMeshSectionDraw> SectionDraws` | 현재 LOD의 활성 섹션 드로우 정보 |

---

## 3. 주요 함수 상세

### 3.1 `GetGeometryView()` — CPU/GPU 모드 분기

`DrawCommandBuilder`가 매 프레임 호출하여 VB/IB를 획득한다.

| 모드 | VB | IB |
|------|----|----|
| **CPU** | `USkeletalMeshComponent::DynamicVB` (스키닝 완료 정점) | `FSkeletalMesh::RenderBuffer` 정적 IB |
| **GPU** | `FSkeletalMesh::RenderBuffer` 정적 VB (bone indices/weights 포함) | `FSkeletalMesh::RenderBuffer` 정적 IB |

CPU 모드에서는 VB만 동적으로 교체되며, IB는 인덱스가 불변이므로 에셋의 정적 IB를 공유한다.

```cpp
FGPUGeometryView FSkeletalSceneProxy::GetGeometryView() const
{
    // ...
    if (CPU 모드)
        return { DynamicVB.GetBuffer(), DynamicVB.GetStride(), 정적IB };
    else
        return { 정적VB.GetBuffer(), 정적VB.GetStride(), 정적IB };
}
```

### 3.2 `RebuildSectionDraws()`

`FSkeletalMesh::Sections` 배열을 순회하여 `FMeshSectionDraw`를 구축한다.

```
Draw.FirstIndex = Section.FirstIndex
Draw.IndexCount = Section.NumTriangles * 3
Draw.Material   = OverrideMaterials[i] ?? StaticMaterials[i].MaterialInterface
```

구축 후 Material 포인터 기준 `std::sort` 적용 → `DrawCommandBuilder`의 상태 전환 횟수 최소화.

현재 `LODCount = 1` 고정이므로 LOD0만 구축하고 활성 슬롯에 `std::swap`.

### 3.3 `UpdateMesh()` / `UpdateMaterial()`

두 함수 모두 `RebuildSectionDraws()`를 호출한다.  
`UpdateMesh()`는 추가로 부모 멤버 `MeshBuffer`를 에셋의 정적 버퍼로 갱신한다.

### 3.4 `UpdateLOD()`

`StaticMeshSceneProxy`와 동일한 `std::swap` 패턴으로 O(1) 전환한다.  
현재 `LODCount = 1`이므로 실질적으로 동작하지 않으나, 에셋에 다중 LOD 데이터 추가 시 즉시 활성화된다.

---

## 4. 파이프라인 흐름 (CPU 모드 기준)

```
매 틱
  USkeletalMeshComponent::UpdateAnimation()
    → UpdateLocalTransforms()              // FBone SRT → LocalTransforms
    → RecalcComponentSpaceMatrices()       // FK 전파 → ComponentSpaceMatrices
    → UpdateSkinningCPU()                  // CSM * IBP → SkinnedVertices → DynamicVB.Update()

Collect 페이즈
  FDrawCommandBuilder::BuildMeshCommands(Proxy)
    → BuildCommandForProxy(Proxy, PreDepth)
    → BuildCommandForProxy(Proxy, Opaque)
        → Proxy.GetGeometryView()          // DynamicVB + 정적IB 반환
        → Proxy.GetSectionDraws()          // Material / FirstIndex / IndexCount
        → FDrawCommand 생성 → GPU 제출
```

기존 파이프라인 **수정 없음** — `GetGeometryView()` virtual 오버라이드만으로 `DrawCommandBuilder`와 통합된다.

---

## 5. StaticMeshSceneProxy와의 차이점

| 항목 | StaticMeshSceneProxy | SkeletalSceneProxy |
|------|----------------------|--------------------|
| VB 출처 | 에셋 정적 VB (불변) | CPU 모드: DynamicVB / GPU 모드: 에셋 정적 VB |
| IB 출처 | 에셋 정적 IB | 에셋 정적 IB (공통) |
| `GetGeometryView()` | 부모 기본 구현 사용 | 오버라이드 (모드 분기) |
| LOD 수 | 에셋에서 읽음 | 현재 1 고정 |
| 셰이더 | 머티리얼 기본 셰이더 | CPU 모드: 머티리얼 기본 셰이더 재사용 |

---

## 6. 미완성 / 추후 작업

| 항목 | 현황 | 필요 작업 |
|------|------|----------|
| **GPU 스키닝** | `GetGeometryView()` 정적 VB 반환까지만 구현 | `FDrawCommand`에 Bone Matrix SRV 슬롯 추가, `FDrawCommandBuilder` 바인딩 로직, 전용 스켈레탈 셰이더 |
| **UpdateBoneMatrices()** | stub (주석 처리) | GPU 모드 완성 시 Structured/Constant Buffer 업로드 함수로 구현 |
| **LOD** | 인터페이스 구비, `LODCount = 1` 고정 | 에셋에 다중 LOD 데이터 추가 시 `LODCount` 갱신으로 즉시 활성화 |

---

*작성일: 2026-05-10*  
*상태: CPU Skinning 모드 완성 / GPU Skinning 모드 파이프라인 확장 대기*

# RenderPipeLine.md 가이드라인 적용 계획 (안전 변형)

## Context

`Document/RenderPipeLine.md`는 `SceneProxy` ↔ `DrawCommandBuilder` 사이의 `FMeshBuffer` 직참조를 제거하고, `FGPUGeometryView` 값 반환 + 가상 함수 다형성으로 디커플링하자는 설계 가이드다. 가이드 그대로 적용하면 (1) 순수 가상 함수가 8개 자식 클래스를 모두 깨뜨리고, (2) `GetMeshBuffer()` 즉시 삭제는 `ShadowMapPass.cpp`·`StaticMeshSceneProxy::UpdateLOD()` 등 외부/내부 의존을 손상시킨다. 본 계획은 가이드 의도(다형성 디커플링)는 보존하되, 적용 범위를 **DrawCommandBuilder 단일 진입점**으로 한정해 회귀 위험을 최소화한 안전 변형이다.

확정된 정책 (사용자 선택):
- **비순수 가상** + 부모 기본 구현으로 자동 합법화
- `GetMeshBuffer()` getter는 **유지**, Builder에서만 사용 중단
- TextRender/Decal 특수 경로는 **이번 범위 제외** (부모 기본 구현이 빈 view 반환으로 처리)

---

## 변경 대상 파일

| # | 파일 | 변경 종류 |
|---|---|---|
| 1 | `KraftonEngine/Source/Engine/Render/Types/RenderTypes.h` | 신규 구조체 추가 |
| 2 | `KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.h` | 가상 함수 선언 추가 |
| 3 | `KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.cpp` | 기본 구현 추가 |
| 4 | `KraftonEngine/Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.h` | 오버라이드 선언 추가 |
| 5 | `KraftonEngine/Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.cpp` | 오버라이드 구현 추가 |
| 6 | `KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp` | `GetMeshBuffer()` → `GetGeometryView()` 호출 전환 |

`StaticMeshSceneProxy`, `Billboard` 계열, `Gizmo`, `Decal`, `TextRender` 등은 **수정 불필요** — 부모의 기본 구현이 자동 동작.

---

## 구현 단계

### Step 1 — `FGPUGeometryView` 추가
**파일**: `KraftonEngine/Source/Engine/Render/Types/RenderTypes.h`
([RenderTypes.h:13](KraftonEngine/Source/Engine/Render/Types/RenderTypes.h:13)에 `<d3d11.h>` 이미 포함됨)

기존 enum/구조체 정의 영역 근처에 추가:
```cpp
struct FGPUGeometryView
{
    ID3D11Buffer* VB     = nullptr;
    uint32        Stride = 0;
    ID3D11Buffer* IB     = nullptr;

    bool IsValid() const { return VB != nullptr; }
};
```

검증: 컴파일만 통과하면 OK (의존하는 곳 아직 없음).

---

### Step 2 — 부모 클래스에 가상 함수 + 기본 구현
**파일**: `KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.h`

[PrimitiveSceneProxy.h:66](KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.h:66)의 `GetMeshBuffer()`는 **그대로 유지**.
가상 함수 그룹 ([라인 86-91](KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.h:86)) 근처에 선언 추가:
```cpp
virtual FGPUGeometryView GetGeometryView() const;
```
`RenderTypes.h`가 이미 인클루드되는지 확인하고, 없다면 forward include 처리.

**파일**: `KraftonEngine/Source/Engine/Render/Proxy/PrimitiveSceneProxy.cpp`
([Buffer.h:79-103](KraftonEngine/Source/Engine/Render/Resource/Buffer.h:79)의 `FMeshBuffer` 인터페이스 사용)

`UpdateMesh()` 아래에 기본 구현:
```cpp
FGPUGeometryView FPrimitiveSceneProxy::GetGeometryView() const
{
    if (!MeshBuffer || !MeshBuffer->IsValid()) return {};
    return {
        MeshBuffer->GetVertexBuffer().GetBuffer(),
        MeshBuffer->GetVertexBuffer().GetStride(),
        MeshBuffer->GetIndexBuffer().GetBuffer()
    };
}
```

이 기본 구현으로 `StaticMesh`, `Billboard` 계열, `Gizmo`는 자동으로 올바른 view를 반환. `Decal`(MeshBuffer=nullptr)은 빈 view를 반환하여 Builder의 `IsValid()` 가드에 의해 자연스럽게 스킵.

검증: `StaticMesh`만 띄운 씬에서 시각적 동등성 확인.

---

### Step 3 — Skeletal 오버라이드
**파일**: `KraftonEngine/Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.h`
([라인 30-33](KraftonEngine/Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.h:30)의 `InternalVB`/`InternalIB` 사용)

`UpdateMesh()` 선언 부근에 추가:
```cpp
FGPUGeometryView GetGeometryView() const override;
```

**파일**: `KraftonEngine/Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.cpp`
```cpp
FGPUGeometryView FSkeletalMeshSceneProxy::GetGeometryView() const
{
    return {
        InternalVB.GetBuffer(),
        InternalVB.GetStride(),
        InternalIB.GetBuffer()
    };
}
```

`FDynamicVertexBuffer::GetBuffer/GetStride` ([Buffer.h:129-130](KraftonEngine/Source/Engine/Render/Resource/Buffer.h:129)), `FIndexBuffer::GetBuffer` ([Buffer.cpp:212-214](KraftonEngine/Source/Engine/Render/Resource/Buffer.cpp:212))의 기존 시그니처를 그대로 활용.

검증: SkeletalMesh를 띄운 씬에서 정점이 정상 렌더되는지 확인.

---

### Step 4 — Builder의 호출부 교체
**파일**: `KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp`

[BuildCommandForProxy 라인 122, 144-147](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:122) 변경:

기존:
```cpp
if (!Proxy.GetMeshBuffer() || !Proxy.GetMeshBuffer()->IsValid()) return;
// ...
ProxyBuffer.VB       = Proxy.GetMeshBuffer()->GetVertexBuffer().GetBuffer();
ProxyBuffer.VBStride = Proxy.GetMeshBuffer()->GetVertexBuffer().GetStride();
ProxyBuffer.IB       = Proxy.GetMeshBuffer()->GetIndexBuffer().GetBuffer();
```

변경:
```cpp
const FGPUGeometryView Geometry = Proxy.GetGeometryView();
if (!Geometry.IsValid()) return;
// ...
ProxyBuffer.VB       = Geometry.VB;
ProxyBuffer.VBStride = Geometry.Stride;
ProxyBuffer.IB       = Geometry.IB;
```

`GetSectionDraws()`를 비롯한 후속 섹션 순회 로직 ([DrawCommandBuilder.cpp:150-192](KraftonEngine/Source/Engine/Render/Command/DrawCommandBuilder.cpp:150))은 **건드리지 않음**.

검증: 컴파일 성공 + 기존과 동일한 `FDrawCommand` 생성.

---

## 의도적으로 변경하지 않는 것 (회귀 방지)

- `FPrimitiveSceneProxy::MeshBuffer` 멤버 — `UpdateMesh()`/`UpdateLOD()` 의존
- `FPrimitiveSceneProxy::GetMeshBuffer()` public getter — `ShadowMapPass.cpp`, `MeshBufferManager.cpp` 의존
- `FStaticMeshSceneProxy::FLODDrawData`의 `FMeshBuffer*` — LOD 스왑 로직
- `Render/RenderPass/ShadowMapPass.cpp` — 후속 마이그레이션 대상
- `TextRenderSceneProxy`, `DecalSceneProxy` — Builder 일반 경로를 거치지 않음

---

## End-to-End 검증 절차

1. **빌드**: 솔루션 전체 컴파일 (Debug + Release).
2. **정적 메시 회귀**: StaticMesh 컴포넌트가 있는 기본 레벨을 실행해, LOD 0/1 전환 시 정상 렌더 + 머티리얼 섹션 분리 동작 확인.
3. **스켈레탈 메시 회귀**: SkeletalMeshComponent를 가진 액터로 애니메이션 재생 → CPU 스킨 결과가 매 프레임 GPU에 업로드되어 정상 보이는지 확인.
4. **빌보드/Gizmo/SubUV/Decal/TextRender 비회귀**: 에디터 기즈모, 파티클 빌보드, 데칼 액터, 텍스트 렌더 컴포넌트가 변경 전과 동일하게 보이는지 시각 확인.
5. **섀도우맵 비회귀**: `ShadowMapPass`는 변경 안 했으므로 그림자가 변경 전과 동일한지 확인 (회귀 시 즉시 롤백 신호).
6. **PIX/RenderDoc 캡처(선택)**: `DrawIndexed` 호출의 VB/IB/Stride가 동일한 값으로 들어가는지 캡처 비교.
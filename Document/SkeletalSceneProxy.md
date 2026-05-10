# Skeletal Scene Proxy Implementation Specification (Dual Skinning Support)

본 문서는 `FSkeletalSceneProxy`가 **CPU Skinning**과 **GPU Skinning** 모드를 어떻게 전환하고 렌더링 파이프라인(`DrawCommandBuilder`, `RenderPipeline`)에 어떻게 통합되는지 상세히 정의합니다.

## 1. 렌더 파이프라인 통합 전략

### 1.1 CPU Skinning 모드 (초기 학습 및 구현 목표)
- **전략**: 컴포넌트가 CPU에서 정점을 변형하여 전용 동적 버퍼(`DynamicVB`)에 기록합니다. 
- **Geometry**: `GetGeometryView()`를 통해 이 `DynamicVB`를 반환합니다. 이때 버텍스 레이아웃은 일반 `StaticMesh`용 셰이더가 인식할 수 있는 표준 레이아웃(예: `FStaticMeshVertex`)을 따라야 합니다.
- **Shader**: 이미 월드/컴포넌트 공간으로 변형된 정점이므로, `UberLit` 등 기존의 모든 렌더링 패스와 셰이더를 **수정 없이 재사용**할 수 있습니다.
- **장점**: 기존 엔진 구조를 전혀 수정하지 않고 즉시 가동 가능합니다.

### 1.2 GPU Skinning 모드 (추후 확장 계획)
- **전략**: 본 행렬과 원본 메시 데이터를 GPU로 전송하여 Vertex Shader에서 스키닝을 수행합니다.
- **Geometry**: 에셋의 정적 버퍼(`StaticVB/IB`)를 반환합니다. 이 버퍼는 `boneIndices`, `boneWeights`가 포함된 전용 레이아웃을 가집니다.
- **Shader**: 스키닝 연산이 포함된 전용 `SkeletalShader`를 사용해야 합니다.
- **파이프라인 수정 필요 사항**: 
    - `FDrawCommand` 구조체에 프록시 전용 SRV(Bone Matrix Buffer)를 위한 슬롯 추가 필요.
    - `FDrawCommandBuilder`가 프록시로부터 이 SRV를 받아 `FDrawCommand`에 바인딩해 주는 로직 추가 필요.

## 2. 섹션 및 LOD 관리 (StaticMeshSceneProxy 대조)

`FSkeletalSceneProxy`는 `FStaticMeshSceneProxy`의 효율적인 구조를 미러링하여 구현합니다.

### 2.1 다중 섹션 지원
- 스켈레탈 메시도 머티리얼별로 여러 섹션을 가질 수 있습니다. 
- `UpdateMesh()` 시점에 에셋의 섹션 정보를 기반으로 `SectionDraws` 배열을 구축하며, 머티리얼별 정렬(Sorting)을 수행하여 상태 변화를 최소화합니다.

### 2.2 LOD 지원
- 거리 기반 LOD 전환을 지원하기 위해 `LODData[MAX_LOD]` 구조를 유지합니다.
- `UpdateLOD()` 호출 시 현재 활성화된 `MeshBuffer`와 `SectionDraws`를 교체합니다.

## 3. 주요 함수 상세 설계

### 3.1 GetGeometryView() 오버라이드
```cpp
FGPUGeometryView FSkeletalSceneProxy::GetGeometryView() const {
    USkeletalMeshComponent* SkeletalComp = static_cast<USkeletalMeshComponent*>(GetOwner());
    if (SkeletalComp->GetSkinningMode() == ESkinningMode::CPU) {
        // USkeletalMeshComponent가 보유한 동적 버퍼 반환
        return { SkeletalComp->GetDynamicVB().GetBuffer(), 
                 SkeletalComp->GetDynamicVB().GetStride(),
                 SkeletalComp->GetSkeletalMesh()->GetSkeletalMeshAsset()->RenderBuffer->GetIndexBuffer().GetBuffer() };
    } else {
        // USkeletalMesh 에셋의 정적 버퍼 반환
        FSkeletalMesh* Asset = SkeletalComp->GetSkeletalMesh()->GetSkeletalMeshAsset();
        return { Asset->RenderBuffer->GetVertexBuffer().GetBuffer(), 
                 Asset->RenderBuffer->GetVertexBuffer().GetStride(),
                 Asset->RenderBuffer->GetIndexBuffer().GetBuffer() };
    }
}
```

### 3.2 UpdateMesh() 및 머티리얼 정렬
- 에셋의 섹션 데이터를 순회하며 `FMeshSectionDraw`를 생성합니다.
- `std::sort`를 사용하여 머티리얼 포인터 기준으로 정렬함으로써 `DrawCommandBuilder`의 효율성을 높입니다.

## 4. 참조 파일 및 디렉토리 위치 (갱신)
- **FSkeletalSceneProxy**: `KraftonEngine/Source/Engine/Render/Proxy/SkeletalSceneProxy.h / .cpp`
- **필수 참조**: `Render/Command/DrawCommandBuilder.h`, `Render/Proxy/StaticMeshSceneProxy.h` (참고용)

---
*작성일: 2026-05-10*
*상태: Pipeline-Integrated Dual-Mode Specification Ready*

# Component Implementation Specification: Skeletal & Skinned Mesh (Dual Skinning Support)

본 문서는 `USkeletalMeshComponent` 및 `USkinnedMeshComponent`의 구현 상세를 정의하며, 학습 목적의 **CPU Skinning**과 성능 최적화를 위한 **GPU Skinning**을 모두 지원하는 메커니즘을 포함합니다.

## 1. 클래스 계층 구조 및 모드 정의
- `UPrimitiveComponent` -> `UMeshComponent` -> `USkinnedMeshComponent` -> `USkeletalMeshComponent`

### 스키닝 모드 (ESkinningMode)
- **CPU**: CPU에서 모든 정점의 위치를 매 프레임 계산하여 동적 버퍼(Dynamic VB)에 업로드합니다. 단순한 셰이더를 사용할 수 있어 초기 학습에 유리합니다.
- **GPU**: 본 행렬(Bone Matrices)만 GPU로 전달하고, Vertex Shader에서 스키닝을 수행합니다. 정점 데이터 전송량을 최소화합니다.

## 2. USkinnedMeshComponent
스키닝 데이터와 연산 모드를 관리하는 베이스 클래스입니다.

### 주요 데이터
- `ESkinningMode SkinningMode`: 현재 적용된 스키닝 방식 (기본값: CPU).
- `TArray<FMatrix> BoneMatrices`: 현재 프레임의 본 트랜스폼.
- `TArray<FMatrix> SkinningMatrices`: 최종 스키닝 행렬 (Bone * InverseBind).
- `FDynamicVertexBuffer DynamicVB`: **CPU Skinning** 모드에서 변형된 정점을 담기 위한 GPU 버퍼.

### 주요 기능
- `UpdateSkinning()`: `SkinningMode`에 따라 분기 처리합니다.
    - **CPU 모드**: `SkinningMatrices`를 사용하여 에셋의 정점을 순회하며 위치/노멀을 변형하고, `DynamicVB`를 `Update()` 합니다.
    - **GPU 모드**: `SkinningMatrices` 계산 후 `SkeletalSceneProxy`로 전달하여 본 버퍼를 갱신하게 합니다.
- `GetDynamicVB()`: Proxy가 CPU Skinning 시 접근할 수 있도록 버퍼 포인터를 반환합니다.

## 3. USkeletalMeshComponent
에셋 참조 및 애니메이션 시스템과의 인터페이스를 담당합니다.

### 주요 기능
- `SetSkeletalMesh(USkeletalMesh* InMesh)`: 에셋 설정 시 `DynamicVB`의 크기를 메시의 정점 개수에 맞춰 초기화합니다.
- `UpdateAnimation(float DeltaTime)`: 본 트랜스폼을 업데이트하고 `UpdateSkinning()`을 호출합니다.
- `CreateSceneProxy()`: `FSkeletalSceneProxy`를 생성합니다. 이때 현재 `SkinningMode`를 전달합니다.

## 4. StaticMeshComponent와 비교
- `StaticMeshComponent`는 에셋의 정적 버퍼를 직접 Proxy에 전달하지만, `SkeletalMeshComponent`는 모드에 따라 **Asset의 정적 버퍼(GPU 모드)** 또는 **Component의 동적 버퍼(CPU 모드)** 중 하나를 선택적으로 Proxy에 공급합니다.

## 5. 참조 파일 및 디렉토리 위치

### 구현 위치
- **USkinnedMeshComponent**: `KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h / .cpp`
- **USkeletalMeshComponent**: `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h / .cpp`

### 필수 Include 헤더
- `Component/MeshComponent.h`: 부모 클래스 정의.
- `Mesh/SkeletalMesh.h`: `USkeletalMesh` 에셋 클래스 참조.
- `SkeletalMesh/SkeletalMeshAsset.h`: `FSkeletalMesh` 데이터 구조 및 `FSkeletalMeshVertex` 참조.
- `Render/Resource/Buffer.h`: `FDynamicVertexBuffer` 사용을 위함.
- `Render/Types/RenderTypes.h`: `ESkinningMode` 정의 위치 (예정).
- `Math/Matrix.h`: 본 행렬 연산용.

---
*작성일: 2026-05-10*
*상태: Dual-Mode Implementation Specification Ready (with File References)*

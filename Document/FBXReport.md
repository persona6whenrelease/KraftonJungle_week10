# SkeletalMesh 시스템 확장성 분석 보고서

본 문서는 KraftonEngine의 `SkeletalMesh` 관련 클래스들을 분석하여 개별 Bone 조작 및 Texture 삽입(머티리얼 오버라이드)을 위한 확장성 여부와 필요한 추가 로직을 정리한 보고서입니다.

---

## 1. 개별 Bone 조작 확장성 분석

### 1.1 관련 클래스: `SkinnedMeshComponent` / `SkeletalMeshComponent`
*   **현재 구조**:
    *   `USkinnedMeshComponent`가 `LocalTransforms`(SRT 배열)와 `ComponentSpaceMatrices`(FK 결과 행렬)를 관리합니다.
    *   `RecalcComponentSpaceMatrices` 함수가 부모-자식 인덱스를 따라 행렬을 누적 연산(FK)하는 핵심 로직을 담당합니다.
*   **확장성**: **매우 높음**
    *   `USkeletalMeshComponent::UpdateLocalTransforms()`가 현재는 에셋의 Bind Pose를 그대로 복사하고 있습니다.
    *   이곳에 `TMap<int32, FTransform> BoneOverrides`와 같은 멤버를 추가하고, `Recalc` 전 단계에서 특정 본의 트랜스폼을 덮어쓰는 로직을 넣으면 즉시 개별 조작이 가능합니다.
    *   이미 `GetBoneIndex(FName)` 유틸리티가 `USkeletalMesh`에 존재하므로, 이름 기반의 본 제어도 용이합니다.

---

## 2. Texture 삽입(머티리얼 오버라이드) 확장성 분석

### 2.1 관련 클래스: `SkeletalMesh` / `SkeletalMeshComponent`
*   **현재 구조**:
    *   `USkeletalMesh` 에셋이 기본 머티리얼(`StaticMaterials`)을 가지고 있습니다.
    *   `USkeletalMeshComponent`가 인스턴스별로 머티리얼을 교체할 수 있는 `OverrideMaterials` 배열을 이미 소유하고 있습니다.
    *   `SetMaterial(int32 Index, UMaterial* Mat)` 함수가 이미 구현되어 있어 외부에서 즉시 텍스처가 포함된 머티리얼을 주입할 수 있습니다.
*   **확장성**: **이미 구현됨**
    *   추가 로직 없이도 `SetMaterial` 호출만으로 섹션별 텍스처 변경이 가능합니다.
    *   내부적으로 `MarkProxyDirty(EDirtyFlag::Material)`를 호출하여 렌더링 프록시에 변경 사실을 즉시 알리는 구조도 갖춰져 있습니다.

---

## 3. 렌더링 파이프라인 확장성 분석

### 3.1 관련 클래스: `SkeletalSceneProxy`
*   **현재 구조**:
    *   `FGPUGeometryView`라는 추상화된 구조체를 통해 렌더러에 버퍼를 전달합니다.
    *   `GetGeometryView()` 함수에서 `SkinningMode`에 따라 **CPU 스키닝용 동적 VB** 또는 **GPU 스키닝용 정적 VB**를 선택적으로 반환합니다.
*   **확장성**: **보통 (GPU 스키닝 고도화 필요)**
    *   **CPU 모드**: 현재 완벽하게 동작하며, 본 조작 결과가 즉시 반영됩니다.
    *   **GPU 모드**: 프레임별 본 행렬 배열(`SkinningMatrices`)을 GPU Constant Buffer나 Structured Buffer로 전송하는 로직과 이를 처리할 셰이더(VS) 연산 추가가 필요합니다. 하지만 `FSkeletalSceneProxy` 내부에서 이를 처리할 슬롯 설계는 이미 완료된 상태입니다.

---

## 5. 본 계층 구조(Bone Hierarchy) 심층 분석

### 5.1 계층 표현 방식
*   **선형 배열 기반 계층 구조**: 명시적인 트리(Tree) 자료구조 대신, `FSkeletalMesh`의 `TArray<FBone> Bones` 선형 배열을 사용합니다.
*   **인덱스 참조**: 각 `FBone` 객체는 자신의 부모를 가리키는 `int32 ParentIndex`를 소유하며, 이를 통해 역방향(자식->부모) 추적이 가능합니다.
*   **이름 매핑**: `USkeletalMesh`에서 `TArray<FName> BoneNames`와 `TMap<FName, int32> BoneNameToIndex`를 관리하여 이름 기반의 빠른 인덱스 검색을 지원합니다.

### 5.2 연산 및 순회 효율성
*   **부모 우선 순서 보장(Parent-First Ordering)**: `FBXImporter`는 `GatherJoints` 함수에서 **재귀적 전위 순회(Pre-order Traversal)**를 사용하여 본 노드를 수집합니다. 이를 통해 배열 내에서 부모 본의 인덱스가 항상 자식 본의 인덱스보다 작음을 알고리즘적으로 보장합니다.
*   **단일 루프 순회**: 위에서 보장된 순서 덕분에, 복잡한 재귀 호출 없이 `0`번 인덱스부터 순차적으로 루프를 도는 것만으로 전체 본의 월드 트랜스폼(Component Space)을 안전하게 계산할 수 있습니다.
*   **메모리 최적화**: 연속된 메모리 공간에 본 데이터가 위치하여 애니메이션 연산 시 CPU 캐시 효율이 극대화됩니다.

### 5.3 트리 구조의 복원 및 활용
*   **시각화 지원**: 명시적인 트리 자료구조는 없으나, `ParentIndex`를 역추적하거나 특정 인덱스를 부모로 가진 자식들을 검색함으로써 언제든지 트리 형태의 UI(예: 본 계층 구조 트리 뷰)를 출력할 수 있습니다.
*   **개별 조작 용이성**: 특정 인덱스의 트랜스폼만 교체하면 계층 순서가 보장된 루프에 의해 하위 자식들에게 자동으로 변환이 전파되는 구조입니다.
*   **계층 검증**: 임포트 시점에 루트가 아닌 본이 부모를 잃는 경우(Orphan Bone)를 감지하는 진단 로직을 통해 데이터 무결성을 보장합니다.

---

## 6. 개별 Bone 선택 및 기즈모(Gizmo) 조작 설계

### 6.1 Bone 선택 메커니즘 (Bone Picking)
현재 엔진의 Picking 시스템은 액터/컴포넌트 단위(`WorldPrimitivePickingBVH`)로 작동합니다. 개별 본 선택을 위해 다음과 같은 **2단계 피킹(Two-Stage Picking)** 전략을 제안합니다.

1.  **1단계: 컴포넌트 식별**: 기존 레이캐스트 로직을 사용하여 마우스 클릭 지점에 위치한 `USkeletalMeshComponent`를 찾습니다.
2.  **2단계: 세부 본 판정**: 
    *   식별된 컴포넌트의 `ComponentSpaceMatrices`를 기반으로 각 본의 월드 위치를 계산합니다.
    *   각 본 위치에 가상의 충돌 구체(Collision Sphere)를 설정하거나, 본 사이의 캡슐(Capsule)을 생성하여 마우스 레이와의 충돌을 검사합니다.
    *   가장 가까운 충돌 결과의 본 인덱스를 `SelectedBoneIndex`로 설정합니다.

### 6.2 기즈모(Gizmo) 통합 및 조작
*   **Target 확장**: `GizmoComponent`가 일반 `SceneComponent`뿐만 아니라 **(Component, BoneIndex)** 쌍을 타겟으로 가질 수 있도록 확장합니다.
*   **Transform 전파**: 
    1.  사용자가 기즈모를 조작하면, 기즈모의 변위(Delta Transform)를 계산합니다.
    2.  이 변위를 `USkeletalMeshComponent::BoneOverrides` 맵에 해당 본 인덱스에 대한 `FTransform`으로 기록합니다.
    3.  `UpdateAnimation()` 시점에 `BoneOverrides`를 참조하여 애니메이션 포즈를 갱신합니다.

### 6.3 확장성 및 UI 통합
*   **Editor UI**: 본이 선택되면 디테일 패널에 해당 본의 로컬 트랜스폼 정보를 노출하고 수치 입력을 통한 정밀 조작을 지원합니다.
*   **Hierarchy Sync**: 뷰포트에서 본 클릭 시, 에디터의 Bone Tree UI에서도 해당 항목이 하이라이트되도록 `SelectionManager`와 동기화합니다.

---

## 7. SkeletalMesh 시스템 구현 상세 (Current Implementation)

### 7.1 USkeletalMeshComponent (Component)
- **위치**: `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h/cpp`
- **주요 로직**:
    - **에셋 바인딩**: `SetSkeletalMesh()`를 통해 `USkeletalMesh` 에셋을 연결하고 머티리얼 슬롯 및 본 계층 구조를 캐싱합니다.
    - **애니메이션 업데이트**: 매 프레임 `UpdateAnimation()`을 호출하여 본 트랜스폼(`LocalTransforms`)을 갱신하고, 부모-자식 관계에 따른 순차적 행렬 누적 연산(FK)을 수행하여 `ComponentSpaceMatrices`를 계산합니다.
    - **스키닝(Skinning)**:
        - `SkinningMatrices` 계산: `InverseBindMatrix`와 `ComponentSpaceMatrix`를 결합하여 최종 스키닝 행렬을 산출합니다.
        - **CPU 스키닝**: `UpdateSkinningCPU()`에서 `FDynamicVertexBuffer`를 사용하여 각 정점을 본 가중치에 따라 실시간 변형합니다.
        - **GPU 스키닝**: 렌더링 프록시(`SkeletalSceneProxy`)로 행렬 데이터를 전달하여 셰이더에서 처리하도록 설계되어 있습니다.
    - **편집기 지원**: `GetEditableProperties`를 통해 에셋 경로 및 스키닝 모드(CPU/GPU)를 실시간으로 변경할 수 있습니다.

### 7.2 USkeletalMesh & FSkeletalMesh (Asset)
- **위치**: `KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMeshAsset.h`, `SkeletalMesh.h/cpp`
- **주요 로직**:
    - **FSkeletalMesh (Data Structure)**: 정점(`FSkeletalMeshVertex`), 인덱스, 본(`FBone`), 섹션 정보(`FStaticMeshSection`)를 담는 컨테이너입니다. 바이너리 시리얼라이제이션(`Serialize`)과 바운드 박스 계산(`CacheBounds`)을 담당합니다.
    - **USkeletalMesh (UObject)**: `FSkeletalMesh`를 관리하는 엔진 리소스 클래스입니다. 머티리얼 슬롯(`StaticMaterials`)과 본 이름 검색을 위한 맵(`BoneNameToIndex`)을 유지하며, GPU 리소스(`InitResources`) 초기화를 제어합니다.

### 7.3 FSkeletalSceneProxy (SceneProxy)
- **위치**: `KraftonEngine/Source/Engine/Render/Proxy/SkeletalSceneProxy.h/cpp`
- **주요 로직**:
    - **데이터 전달**: 렌더링 스레드에서 `FGPUGeometryView`를 통해 렌더러에 버퍼 정보를 전달합니다.
    - **모드 분기**: CPU 스키닝 모드일 경우 컴포넌트가 업데이트하는 `DynamicVB`를 사용하고, GPU 모드일 경우 에셋의 정적 버퍼를 사용하도록 설계되었습니다.
    - **드로우 콜 생성**: `RebuildSectionDraws()`를 통해 섹션별 머티리얼과 인덱스 범위를 기반으로 렌더링 명령을 구축합니다.

### 7.4 FSkeletalMeshVertex & FBone (VertexType & Types)
- **위치**: `KraftonEngine/Source/Engine/Render/Types/VertexTypes.h`
- **정의**:
    - **FSkeletalMeshVertex**: 표준 정점 속성(Pos, Normal, Tangent, Color, UV) 외에 최대 4개의 본 인덱스와 가중치를 저장합니다.
    - **FBone**: 본의 부모 인덱스, 로컬 트랜스폼(S, R, T), 그리고 정점을 본 공간으로 변환하는 `InverseBindMatrix`를 포함합니다.

### 7.5 FFBXManager (Manager)
- **위치**: `KraftonEngine/Source/Engine/SkeletalMesh/FBXManager.h/cpp`
- **주요 로직**:
    - **에셋 생명주기 관리**: `LoadSkeletalMesh()`를 통해 에셋의 로딩 및 캐싱을 통합 관리합니다.
    - **바이너리 캐싱**: FBX 임포트 비용을 줄이기 위해 파싱된 데이터를 `.bin` 파일로 `Asset/MeshCache` 하위에 저장하고, 원본 FBX의 수정 시간을 체크하여 자동 재빌드를 수행합니다.
    - **파일 스캐닝**: `Data/` 디렉토리와 `Asset/MeshCache/`를 탐색하여 사용 가능한 에셋 리스트를 관리합니다.

### 7.6 FFbxImporter (Importer)
- **위치**: `KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.h/cpp`
- **주요 로직**:
    - **전처리**: FBX SDK를 사용하여 파일을 로드하고, 엔진 좌표계(Z-Up, Left-Handed)로 씬을 변환합니다.
    - **스켈레톤 추출**: `ExtractSkeleton()`에서 조인트 노드를 재귀적으로 수집하여 계층 구조를 복원하고, `TransformLinkMatrix`를 역산하여 로컬 트랜스폼과 `InverseBindMatrix`를 추출합니다.
    - **메시 데이터 추출**: `ExtractMesh()`에서 제어점(Control Points)의 스킨 가중치를 정점 데이터로 변환하고, 삼각형화된 폴리곤의 UV 및 법선 데이터를 수집합니다.
    - **IBP(Inverse Bind Pose) 정밀 계산**: 메시 노드와 본 글로벌 행렬을 결합하여 엔진의 행렬 컨벤션에 맞는 IBP를 산출합니다.

---
*작성일: 2026-05-11*
*업데이트: SkeletalMesh 시스템 세부 로직 정리 추가*

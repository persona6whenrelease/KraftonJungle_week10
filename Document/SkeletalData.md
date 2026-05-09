# SkeletalMesh System Specification

본 문서는 KraftonEngine 내에서 FBX Skeletal Mesh를 처리하기 위한 에셋 및 데이터 구조 명세서입니다. 기존 `StaticMesh` 시스템과의 일관성을 유지하며, `FGPUGeometryView`를 통한 동적 렌더링 구조를 설계에 반영했습니다.

## 1. 개요
Skeletal Mesh 시스템은 크게 **기하학적/본 데이터를 담는 실체(Mesh)**와 **엔진 에셋 시스템에서 관리되는 인터페이스(Asset)**로 나뉩니다. 학습을 위해 초기 단계에서는 **CPU Skinning**을 우선 구현합니다.

- **FSkeletalMesh**: 정점, 인덱스, 본(Bone) 정보 및 GPU 리소스와 이를 사용할 수 있는 CookedData. (원본 T-Pose 및 정적 인덱스 버퍼 보관)
- **USkeletalMesh**: `UObject`를 상속받아 머티리얼 매핑, 에셋 관리, 애니메이션 시스템과의 연결을 담당하는 클래스.
- **USkeletalMeshComponent**: 매 프레임 애니메이션 포즈에 따라 CPU Skinning 연산을 수행하고 결과를 캐싱.
- **FSkeletalMeshSceneProxy**: `GetGeometryView()`를 오버라이드하여, Component로부터 전달받은 변환된 정점(동적 VB)과 에셋의 인덱스(정적 IB)를 파이프라인에 전달.

---

## 2. 데이터 구조 명세

### 2.1 FSkeletalMesh (in SkeletalMeshAsset.h)
`FStaticMesh`와 대응되며, 스키닝(Skinning)을 위한 추가 데이터를 포함합니다.

| 멤버 변수 | 타입 | 설명 |
| :--- | :--- | :--- |
| `PathFileName` | `FString` | 원본 에셋 경로 |
| `Vertices` | `TArray<FSkeletalMeshVertex>` | 원본 정점 배열 (CPU Skinning의 Read Source) |
| `Indices` | `TArray<uint32>` | 인덱스 버퍼 데이터 |
| `Bones` | `TArray<FBone>` | 계층 구조 및 Inverse Bind Matrix를 포함한 본 배열 |
| `Sections` | `TArray<FStaticMeshSection>` | 머티리얼 슬롯별 메시 분할 정보 |
| `RenderBuffer` | `unique_ptr<FMeshBuffer>` | **정적 소스 버퍼**. (IB는 여기서 공유, VB는 T-Pose 프리뷰용) |
| `BoundsCenter/Extent` | `FVector` | 메시의 로컬 바운딩 박스 |

- **주요 기능**:
    - `Serialize(FArchive& Ar)`: 바이너리 직렬화/역직렬화.
    - `CacheBounds()`: 전체 정점을 순회하여 바운드 계산.

### 2.2 USkeletalMesh (in SkeletalMesh.h)
`UStaticMesh`와 대응되며, 렌더링 및 애니메이션 로직의 진입점입니다.

| 멤버 변수 | 타입 | 설명 |
| :--- | :--- | :--- |
| `SkeletalMeshAsset` | `FSkeletalMesh*` | 실제 기하 데이터에 대한 포인터 |
| `StaticMaterials` | `TArray<FStaticMaterial>` | 메시 섹션과 매핑되는 머티리얼 리스트 |
| `BoneNames` | `TArray<FName>` | 각 본 인덱스에 대응하는 이름 리스트 |
| `BoneNameToIndex` | `TMap<FName, int32>` | 이름으로 본 인덱스를 빠르게 찾기 위한 맵 |

---

## 3. CPU Skinning 및 렌더링 파이프라인

### 3.1 연산 주체: USkeletalMeshComponent
1. **Pose Update**: 애니메이션에 따라 본의 `GlobalTransform` 배열 계산.
2. **Matrix Generation**: `SkinningMatrix = Bone.GlobalTransform * Bone.InverseBindMatrix`.
3. **Vertex Transform**: `FSkeletalMesh`의 원본 정점을 순회하며 가중치 합산 변환을 수행, 결과를 `TArray<FVertexPNCTT>`에 저장.

### 3.2 렌더링 전달: FSkeletalMeshSceneProxy
1. **Dynamic VB 소유**: 프록시 내부에서 `FDynamicVertexBuffer`를 관리.
2. **Buffer Upload**: 컴포넌트의 변환된 정점 데이터를 `FDynamicVertexBuffer::Update()`로 GPU 업로드.
3. **FGPUGeometryView 제공**:
    - `GetGeometryView()`를 오버라이드하여 다음을 반환:
        - `VB`: 자신의 `FDynamicVertexBuffer` 핸들.
        - `IB`: 에셋(`FSkeletalMesh->RenderBuffer`)의 정적 인덱스 버퍼 핸들.
        - `Stride`: `sizeof(FVertexPNCTT)`.

---

## 4. 기존 시스템과의 정렬 (Alignment)

1. **Vertex Structure**: `FSkeletalMeshVertex`는 원본 데이터 보관용. 렌더링 시에는 `FVertexPNCTT` 포맷으로 변환하여 `FGPUGeometryView`에 전달.
2. **Pipeline Integration**: `FPrimitiveSceneProxy::GetGeometryView()` 가상 함수를 통해 `DrawCommandBuilder`와 소통함으로써 기존 렌더러와 완벽 호환.
3. **Buffer usage**: `FDynamicVertexBuffer`를 활용하여 매 프레임 업데이트되는 동적 기하 구조를 지원.

---
*작성일: 2026-05-09*
*상태: Draft (GeometryView Alignment Completed)*

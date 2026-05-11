# Skeletal Mesh 정리 및 View 확장 가이드

이 문서는 현재 KraftonEngine의 SkeletalMesh 구현 상태를 기준으로, 이후 SkeletalMesh View/Preview 툴을 만들 때 알아야 할 데이터 흐름, 확장 지점, 주의점을 정리한다.

## 현재 구조 요약

SkeletalMesh는 다음 경로로 구성된다.

```text
FBX source
  -> FBXImporter
  -> FFBXAsset
  -> FSkeletalMesh
  -> USkeletalMesh
  -> USkinnedMeshComponent / USkeletalMeshComponent
  -> FSkeletalMeshSceneProxy
  -> FMeshBuffer draw
```

핵심 파일:

| 영역 | 파일 | 역할 |
| --- | --- | --- |
| 에셋 데이터 | `Source/Engine/Mesh/SkeletalMeshAsset.h` | 정점, 인덱스, 섹션, 본 계층, 바운드 저장 |
| UObject 래퍼 | `Source/Engine/Mesh/SkeletalMesh.h/.cpp` | `FSkeletalMesh` 소유, 머티리얼 슬롯 관리, 직렬화 |
| 컴포넌트 | `Source/Engine/Component/SkinnedMeshComponent.h/.cpp` | CPU 스키닝, 동적 버퍼 업로드, 에디터 프로퍼티 |
| 디버그 컴포넌트 | `Source/Engine/Component/SkeletalMeshComponent.h/.cpp` | 현재는 임시 본 흔들림 애니메이션 적용 |
| 렌더 프록시 | `Source/Engine/Render/Proxy/SkeletalMeshSceneProxy.h/.cpp` | 섹션별 머티리얼 draw 구성 |
| FBX 로더 | `Source/Engine/Mesh/FBX/FBXManager.h/.cpp` | FBX import/cache/load, `.bin` 캐시 관리 |
| FBX import | `Source/Engine/Mesh/FBX/*` | 메타 분석, static/skeletal 분리, 본/스킨/섹션 조립 |

## 데이터 모델

### `FSkeletalVertex`

`FSkeletalVertex`는 렌더링 기본 속성에 스키닝 정보를 추가한 정점이다.

```cpp
FVector pos;
FVector normal;
FVector2 tex;
FVector4 tangent;
uint32 BoneIDs[4];
float BoneWeights[4];
```

현재 영향 본은 정점당 최대 4개다. View에서 weight 시각화나 bone influence 디버깅을 넣을 경우 이 제한을 기준으로 UI를 만들면 된다.

### `FBoneInfo`

`FBoneInfo`는 본 이름, 부모 인덱스, bind pose 변환을 가진다.

```cpp
FString Name;
int32 ParentIndex;
FMatrix BoneSpaceToMeshSpace;
FMatrix MeshSpaceToBoneSpace;
```

주의할 점:

- `ParentIndex`는 부모가 자식보다 먼저 나오도록 importer/assembler에서 검증한다.
- 현재 `BoneSpaceToMeshSpace` 이름은 다소 혼동될 수 있다. `FbxSkeletalMeshAssembler.cpp` 기준으로 부모가 있는 본에는 parent-local bind transform이 저장되고, 런타임에서 부모 행렬과 누적해 mesh space 행렬을 만든다.
- `MeshSpaceToBoneSpace`는 inverse bind 행렬로 CPU 스키닝 시 사용된다.

### `FSkeletalMesh`

`FSkeletalMesh`는 실제 에셋 페이로드다.

- `PathFileName`: 에셋 경로 또는 캐시 경로
- `Vertices`, `Indices`: 스키닝 가능한 지오메트리
- `Sections`: 머티리얼 슬롯 단위 draw range
- `Bones`: 본 계층과 bind pose
- `BoundsCenter`, `BoundsExtent`, `bBoundsValid`: 로컬 AABB 캐시

`Serialize()`는 bounds를 저장하지 않는다. 로드 후 `USkeletalMesh::SetSkeletalMeshAsset()` 또는 `FFBXManager::LoadSkeletalMesh()` 경로에서 `CacheBounds()`가 호출되어야 한다.

## Import/Cache 흐름

### 1. FBX 전처리

`FBXImporter::PreprocessScene()`에서 다음 처리를 한다.

- 엔진 축 기준으로 변환: `+X forward`, `+Y right`, `+Z up`
- centimeter 단위 변환
- triangulate

SkeletalMesh View에서 원본 FBX와 캐시를 동시에 비교할 때, 뷰에 표시되는 것은 전처리 후 엔진 좌표계 기준 데이터라는 점을 전제로 해야 한다.

### 2. 메타 분석

`FFbxMetaParser`가 노드, 메시, 스킨, 클러스터, 본, 스켈레톤, 머티리얼 정보를 `FFbxImportMeta`에 채운다.

중요한 메타:

- `FFbxMeshMeta`: 메시가 static/skinned/rigid attached 후보인지 판단
- `FFbxSkinMeta`: skin과 cluster 목록
- `FFbxClusterMeta`: mesh bind, bone bind, link bone 정보
- `FFbxBoneMeta`: 본 계층, bind global, inverse bind
- `FFbxSkeletonMeta`: 하나의 skeleton과 연결된 skinned/rigid mesh 목록

### 3. Skeletal mesh part 생성

`FFbxSkeletalMeshPartParser`가 두 종류의 part를 만든다.

- skinned mesh part: FBX skin cluster weight를 읽어 vertex bone weight 구성
- rigid attached mesh part: 특정 본에 붙은 rigid mesh를 단일 본 100% weight로 변환

View에서 "part", "source mesh", "rigid attached" 표시를 하려면 이 단계의 정보가 필요하지만, 현재 최종 `FSkeletalMesh`에는 source mesh id/part id가 보존되지 않는다. 디버그 UI를 강화하려면 `FSkeletalMesh` 또는 별도 debug metadata에 source mapping을 남기는 확장이 필요하다.

### 4. Skeletal mesh 조립

`FFbxSkeletalMeshAssembler`가 skeleton별로 part를 합쳐 하나의 `FSkeletalMesh`를 만든다.

- skeleton의 본 목록을 `FBoneInfo`로 변환
- part vertices/indices를 하나의 배열로 병합
- part section을 `FMeshSection`으로 변환
- material slot name을 normalize하고 section material index를 구성

현재 `FFBXManager::LoadSkeletalMesh()`는 FBX 하나에서 skeletal mesh가 여러 개 나오면 첫 번째만 drag-drop preview/cache 대상으로 사용한다. SkeletalMesh View에서 FBX 전체 구조를 보여줄 계획이면 `LoadFbxScene()` 또는 `FFBXAsset::SkeletalMeshes` 전체를 보는 경로가 필요하다.

### 5. `.bin` 캐시

`FFBXManager::LoadSkeletalMesh()`는 FBX 경로를 `Asset/SkeletalMeshCache/<stem>.bin`으로 변환한다.

캐시 헤더:

- magic: `"SFBX"`
- version: 현재 `4`
- source path
- source timestamp

FBX 소스가 최신이면 캐시를 재사용하고, 아니면 FBX를 다시 import해서 캐시를 갱신한다. 캐시 포맷을 바꾸면 `FBXCacheVersion`을 올려야 한다.

## Runtime/Render 흐름

### 컴포넌트 설정

`USkinnedMeshComponent::SetSkeletalMesh()`는 다음 순서로 상태를 갱신한다.

1. `SkeletalMesh` 포인터 및 `SkeletalMeshPath` 갱신
2. material slot 초기화
3. local bounds 캐시
4. reference pose matrices 생성
5. bind pose render vertices 생성
6. runtime mesh buffer release
7. runtime resource ensure
8. render state/world bounds dirty 표시

SkeletalMesh View에서 선택 메시를 바꾸는 API도 이 경로를 타는 것이 좋다. 직접 `SkeletalMesh` 포인터만 바꾸면 material, bounds, buffer가 어긋날 수 있다.

### CPU 스키닝

현재 스키닝은 CPU에서 수행된다.

```text
FSkeletalVertex source
  -> ReferenceBoneMatrices + inverse bind
  -> FVertexPNCTT SkinnedVertices
  -> RuntimeMeshBuffer.UpdateVertices()
```

`USkinnedMeshComponent::SkinVerticesToReferencePose()`는 각 vertex의 4개 influence를 순회한다.

스킨 행렬 계산:

```cpp
SkinMatrix = Asset->Bones[BoneIndex].MeshSpaceToBoneSpace * ReferenceBoneMatrices[BoneIndex];
```

이 구조의 의미:

- `FSkeletalMesh::Vertices`는 원본/bind 기준 데이터로 유지된다.
- 매 tick마다 `SkinnedVertices`가 갱신된다.
- GPU에는 `FVertexPNCTT`만 올라간다.
- 현재 shader는 bone index/weight를 직접 쓰지 않는다.

### 임시 애니메이션

`USkeletalMeshComponent::ApplyDebugRandomBoneAnimation()`은 실제 animation system이 아니라 preview/debug용 임시 동작이다.

- 본마다 작은 회전을 적용한다.
- parent-local bind transform을 기준으로 누적한다.
- 이후 `SkinVerticesToReferencePose()`가 CPU 스키닝한다.

정식 Animation Clip/Sequence가 들어오면 이 함수는 제거하거나 preview-only 모드로 격리하는 것이 좋다.

### 렌더 프록시

`FSkeletalMeshSceneProxy`는 `USkinnedMeshComponent`의 `RuntimeMeshBuffer`를 사용하고, `USkeletalMesh`의 section/material 정보를 draw range로 바꾼다.

중요한 점:

- section draw는 material pointer와 first index 기준으로 정렬된다.
- material override는 `UMeshComponent`의 override materials를 우선한다.
- fallback material은 `"None"`이다.

View에서 wireframe, normal, weight heatmap, skeleton overlay 같은 모드를 추가하려면 기존 proxy를 오염시키기보다 preview 전용 render pass 또는 debug draw overlay를 분리하는 편이 안전하다.

## SkeletalMesh View 설계 제안

목표는 "에셋을 고르고, 메시/본/섹션/머티리얼/스킨 상태를 확인하며, 나중에 애니메이션까지 확장할 수 있는 뷰"다.

### 권장 모듈 구조

ObjViewer 패턴을 참고해 다음처럼 분리한다.

```text
Source/SkeletalMeshViewer/
  SkeletalMeshViewerEngine.h/.cpp
  SkeletalMeshViewerPanel.h/.cpp
  SkeletalMeshViewerViewportClient.h/.cpp
  SkeletalMeshViewerRenderPipeline.h/.cpp
```

또는 Editor 안에 dockable widget으로 넣는다면:

```text
Source/Editor/UI/
  EditorSkeletalMeshViewerWidget.h/.cpp
```

선택 기준:

- 별도 실행 툴이면 `ObjViewer`와 같은 독립 엔진 구조가 좋다.
- 기존 에디터 ContentBrowser에서 `.fbx`/`.bin` 더블클릭으로 열 목적이면 Editor widget 구조가 좋다.

### View 상태는 에셋과 분리

다음 상태는 `USkeletalMesh`나 `FSkeletalMesh`에 넣지 말고 viewer state에 둔다.

- 선택된 본 index
- 선택된 section/material slot
- 카메라 orbit target/distance
- skeleton overlay on/off
- bounds, normals, tangents, weight heatmap 표시 여부
- preview animation 재생 시간/속도
- preview pose override

에셋 객체는 여러 컴포넌트와 캐시에서 공유될 수 있으므로, View 전용 상태가 들어가면 런타임과 에디터 상태가 섞인다.

### 기본 UI 구성

권장 패널:

- Asset List: `FFBXManager::GetAvailableSkeletalMeshFiles()` 기반 `.bin` 캐시 목록
- Source FBX List: `FFBXManager::GetAvailableFbxSourceFiles()` 기반 FBX 목록
- Viewport: orbit camera, grid, bounds
- Details:
  - vertex/index/triangle/section/bone count
  - bounds center/extent
  - source/cache path
  - cache rebuild 상태
- Skeleton Tree:
  - parent/child 계층
  - 선택 본 highlight
  - bind/global transform 표시
- Sections:
  - material slot name
  - first index
  - triangle count
  - override material
- Skin Debug:
  - selected vertex 또는 selected bone 기준 influence 표시
  - weight sum 경고
  - zero weight vertex count

### Viewport 기능 우선순위

1. 메시 로드 및 orbit camera
2. bounds/grid 표시
3. section/material 확인
4. skeleton line overlay
5. 본 선택/highlight
6. vertex normal/tangent 표시
7. bone weight heatmap
8. animation preview
9. retarget/pose 편집

처음부터 animation까지 넣기보다, mesh와 skeleton 진단 기능을 먼저 만드는 것이 좋다. 현재 importer와 runtime의 핵심 리스크가 bind pose, parent index, weight, section/material mapping에 있기 때문이다.

## 확장 포인트

### 1. Skeleton 에셋 분리

현재 skeleton은 `FSkeletalMesh::Bones` 내부에 포함되어 있다. 여러 mesh가 같은 skeleton을 공유하거나 animation clip을 재사용하려면 별도 에셋이 필요하다.

가능한 구조:

```cpp
struct FSkeleton
{
    FString PathFileName;
    TArray<FBoneInfo> Bones;
    TMap<FString, int32> BoneNameToIndex;
};

class USkeleton : public UObject
{
    FSkeleton* SkeletonAsset = nullptr;
};
```

분리 시 고려할 점:

- `USkeletalMesh`는 `USkeleton*` 또는 skeleton asset path를 참조
- `FSkeletalMesh`에는 skeleton index와 mesh section/vertex만 남김
- cache format version 증가 필요
- 기존 scene/prefab serialization migration 필요

### 2. AnimationSequence 추가

정식 animation을 넣을 때 필요한 최소 데이터:

```cpp
struct FBoneTrack
{
    int32 BoneIndex;
    TArray<float> PositionTimes;
    TArray<FVector> Positions;
    TArray<float> RotationTimes;
    TArray<FQuat> Rotations;
    TArray<float> ScaleTimes;
    TArray<FVector> Scales;
};

struct FAnimSequence
{
    float Duration;
    float SampleRate;
    TArray<FBoneTrack> Tracks;
};
```

런타임에는 다음 레이어가 필요하다.

- animation sampling: time -> local bone pose
- pose blending: 여러 pose 혼합
- global pose build: local pose를 parent 계층으로 누적
- skinning matrices build: inverse bind * animated global
- component update: CPU 또는 GPU skinning으로 전달

현재 `ReferenceBoneMatrices` 이름은 정식 animation이 들어오면 `CurrentBoneMatrices` 또는 `BoneComponentSpaceMatrices`처럼 의미가 분명한 이름으로 바꾸는 것이 좋다.

### 3. GPU 스키닝

현재는 CPU 스키닝이라 구현은 단순하지만 vertex 수가 늘면 매 frame CPU 비용과 dynamic vertex upload 비용이 커진다.

GPU 스키닝으로 가려면:

- skeletal vertex buffer에 bone id/weight를 보존
- shader input layout에 bone id/weight 추가
- bone matrix constant/structured buffer 추가
- vertex shader에서 skinning 수행
- shadow/depth/selection pass도 같은 skinning 경로 사용

주의:

- 현재 render path는 `FVertexPNCTT`를 기준으로 많은 pass가 공유될 가능성이 높다.
- GPU 스키닝을 넣을 때 기존 static mesh path와 input layout을 분리해야 한다.
- max bone count가 constant buffer 제한을 넘으면 structured buffer가 필요하다.

### 4. LOD와 Section 확장

현재 `FSkeletalMesh`는 단일 LOD다. LOD를 넣으려면 다음처럼 확장하는 편이 자연스럽다.

```cpp
struct FSkeletalMeshLOD
{
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FMeshSection> Sections;
    FVector BoundsCenter;
    FVector BoundsExtent;
};

struct FSkeletalMesh
{
    TArray<FSkeletalMeshLOD> LODs;
    TArray<FBoneInfo> Bones;
};
```

SkeletalMesh View는 처음부터 LOD selector 자리를 잡아두면 나중에 UI를 갈아엎지 않아도 된다.

### 5. Debug metadata 보존

View를 제대로 만들려면 현재 최종 `FSkeletalMesh`에 없는 정보가 필요할 수 있다.

추천 metadata:

- source FBX path
- source node path
- source mesh id
- source skeleton id
- source material id
- part type: skinned/rigid attached
- original material slot index
- import warnings
- vertex weight statistics

이 정보는 런타임 렌더에는 불필요하므로 `FSkeletalMeshDebugInfo` 같은 별도 구조로 분리하거나 editor-only serialize 정책을 정하는 것이 좋다.

## 구현 시 주의점

### 경로와 캐시

- `.fbx`를 로드하면 `.bin` 캐시로 변환될 수 있다.
- `.bin`만 있으면 FBX source 없이 캐시 로드는 가능하지만, rebuild는 불가능하다.
- source FBX timestamp가 바뀌면 캐시가 rebuild된다.
- 캐시 포맷 변경 시 `FBXCacheVersion`을 올린다.

### 본 행렬

- importer와 runtime의 행렬 곱 순서가 맞아야 한다.
- `ParentIndex`가 자식보다 작은 순서를 유지해야 한다.
- bind pose와 animation pose의 space 이름을 명확히 유지해야 한다.
- View에서 본 축/라인을 그릴 때 local/global을 혼동하면 디버깅이 더 어려워진다.

### Weight

- weight sum이 1에서 크게 벗어나면 warning 대상이다.
- zero weight vertex는 현재 importer에서 fallback bone을 배정하는 경로가 있다.
- rigid attached mesh는 단일 본 100% weight로 들어온다.
- heatmap은 selected bone weight 기준으로 표시하는 것이 가장 직관적이다.

### Bounds

- 현재 bounds는 bind/source vertex 기준 local AABB다.
- 애니메이션으로 포즈가 크게 바뀌면 실제 skinned bounds와 다를 수 있다.
- View에서는 bind bounds와 current pose bounds를 둘 다 표시할 수 있게 설계하면 좋다.

### Material/Section

- section은 `MaterialSlotName`으로 material slot과 다시 매칭된다.
- `USkeletalMesh::RebuildSectionMaterialIndices()`가 slot name 기준으로 `MaterialIndex`를 재구성한다.
- View에서 slot 이름 수정 기능을 넣으면 section mapping이 같이 바뀌는지 확인해야 한다.

## SkeletalMesh View 최소 개발 체크리스트

### 1단계: 읽기 전용 Preview

- `FFBXManager::ScanSkeletalMeshAssets()` 호출
- `.bin` 목록 표시
- 선택 시 `FFBXManager::LoadSkeletalMesh()`
- preview actor/component 생성
- orbit viewport에 렌더
- vertex/index/section/bone count 표시

### 2단계: Skeleton 진단

- 본 트리 표시
- 선택 본 highlight
- skeleton line overlay
- bind pose transform 표시
- root/parent index 검증 결과 표시

### 3단계: Section/Material 진단

- section table 표시
- material slot 표시
- section isolate 보기
- material override preview

### 4단계: Skin Weight 진단

- selected bone weight heatmap
- zero weight vertex count
- bad weight sum count
- rigid attached part 표시

### 5단계: Animation 준비

- preview pose interface 분리
- `ApplyDebugRandomBoneAnimation()`을 viewer-only debug pose로 이동
- animation sampling API 자리 확보
- play/pause/time scrub UI 추가

## 권장 API 방향

View와 runtime이 같이 쓸 수 있는 읽기 API를 먼저 추가하면 이후 기능이 안정적이다.

```cpp
const TArray<FBoneInfo>& USkeletalMesh::GetBones() const;
const FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const;
FVector USkeletalMesh::GetBoundsCenter() const;
FVector USkeletalMesh::GetBoundsExtent() const;
```

컴포넌트에는 pose 접근 API가 필요하다.

```cpp
const TArray<FMatrix>& USkinnedMeshComponent::GetCurrentBoneMatrices() const;
void USkinnedMeshComponent::SetPreviewBoneLocalPose(int32 BoneIndex, const FMatrix& LocalPose);
void USkinnedMeshComponent::ClearPreviewPose();
```

단, preview pose API는 정식 animation system과 충돌하지 않도록 `Preview` 또는 `EditorOnly` 성격을 명확히 해야 한다.

## 현재 한계

- `USkeleton`, `UAnimSequence`가 아직 분리되어 있지 않다.
- 실제 FBX animation curve import가 없다.
- CPU 스키닝만 지원한다.
- 최종 `FSkeletalMesh`에 source mesh/part debug metadata가 보존되지 않는다.
- bounds는 bind/source pose 기준이다.
- multiple skeletal mesh import 시 `LoadSkeletalMesh()` preview/cache 경로는 첫 번째 skeletal mesh만 사용한다.
- `ApplyDebugRandomBoneAnimation()`은 임시 동작이며 정식 animation system이 아니다.

## 결론

SkeletalMesh View는 단순 모델 뷰어가 아니라 importer와 runtime 사이의 검증 도구가 되어야 한다. 첫 구현은 메시 로드, 본 트리, 섹션/머티리얼, bounds, skeleton overlay까지를 목표로 잡고, animation은 pose interface를 분리한 뒤 붙이는 것이 안전하다.

가장 먼저 지켜야 할 경계는 다음 두 가지다.

- `FSkeletalMesh`/`USkeletalMesh`는 공유 에셋 데이터만 가진다.
- 선택 상태, debug draw, preview pose, 카메라 상태는 View 또는 preview component가 가진다.

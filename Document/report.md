# FBX Importer Refactoring — Cluster 기반 Skeletal Mesh + Static 분리

작성일: 2026-05-12
대상 브랜치: `feature/SkeletonMesh`
스코프: Step A + Step B + Step C + Step D + Step F + Step G (Step E는 미진행)

---

## 0. 결과 요약

| 항목 | Before | After |
|---|---|---|
| Skinning 영향 표현 | per-vertex `boneIndices[4] / boneWeights[4]` flatten | per-cluster `FBoneCluster { BoneIndex, VertexIndices[], Weights[], InverseBindMatrix }` |
| Skeletal vertex 레이아웃 | `FSkeletalMeshVertex` 96B | `FNormalVertex` 48B (StaticMesh와 동일) |
| `FBone::InverseBindMatrix` | 본마다 1개 | 제거 — cluster로 이동 |
| 0-weight vertex 동작 | (0,0,0)으로 collapse 버그 | bind-pose passthrough (자연 해결) |
| Skin 없는 FBX mesh node | 처리 경로 없음 | `FStaticMesh`로 분리 추출 |
| Bone influence 개수 한계 | 4개 / vertex | 사실상 무제한 (cluster가 별도 추적) |
| 1 actor당 component | SkeletalMeshComponent 1개 | + hybrid 시 sibling `UStaticMeshComponent` lazy-create |
| Importer 진입점 | `ImportSkeletalMesh(path, OutSkel)` | `ImportFbx(path, OutSkel, OutStatic)` (구 API는 wrapper 유지) |
| 캐시 `.bin` 호환성 | v1 | v2 — 버전 mismatch 시 `Serialize` false 반환 (Step D에서 강제 재import) |

남은 컴파일 영향: **없음**. `FSkeletalMeshVertex` / `FBone::InverseBindMatrix` / 멤버 `SkinningMatrices`에 대한 코드 참조는 모두 정리.

---

## 1. 변경 동기

기존 import 파이프라인의 두 근본 문제:

1. **Cluster 정보 손실**
   - FBX의 `FbxCluster`(한 bone이 어떤 vertex들에 어떤 weight로 영향) 가 import 시점에 per-vertex `boneIndices[4] / boneWeights[4]`로 flatten되어 들어옴 (`FBXImporter.cpp:292-306`, `VertexTypes.h:47-56` — old).
   - 4-bone 초과 영향은 무조건 truncate.
   - Asset/structure는 cluster를 1차 시민으로 보존하지 않음 → editor/툴에서 cluster 단위 처리가 불가능.

2. **Static vertex / sub-mesh 미지원**
   - 모든 vertex가 skinned라고 가정 → `TotalWeight == 0`인 vertex는 CPU 스키닝(`SkeletalMeshComponent.cpp:215-226` — old)에서 원점(0,0,0)으로 collapse.
   - Skin이 없는 FBX mesh node는 처리 경로 자체가 없어 일부 prop이 누락 또는 origin collapse.

설계 결정 (사용자 합의):
- Cluster를 asset의 1차 시민으로 도입 — flatten 모델 폐기.
- StaticMesh 측 타입(`FStaticMesh`, `FNormalVertex`, `FStaticMeshSection`, `FStaticMaterial`, `FMeshBuffer`)을 그대로 재사용.
- Hybrid FBX는 `USkeletalMesh`가 옵셔널 `UStaticMesh*`를 composition으로 보유.

---

## 2. Step A — Data Type Refactor

### 2.1 [`Render/Types/VertexTypes.h`](KraftonEngine/Source/Engine/Render/Types/VertexTypes.h)
- **삭제**: `FSkeletalMeshVertex` struct (96B, pos/normal/tangent/color/uv + boneIndices[4]/boneWeights[4]).
- **삭제**: `TSkeletalData`, `FSkeletalData` alias (현재 코드 사용처 없음).
- **`FBone`**: `InverseBindMatrix` 멤버 제거. 본은 이제 ParentIndex + S·R·T만 보유 (계층/포즈 관리 책임만).

### 2.2 [`Engine/SkeletalMesh/SkeletalMeshAsset.h`](KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMeshAsset.h)
- **신규 struct `FBoneCluster`**:
  ```cpp
  struct FBoneCluster {
      int32          BoneIndex      = -1;
      TArray<uint32> VertexIndices;     // 영향받는 vertex (mesh-local index)
      TArray<float>  Weights;           // 위와 1:1, 동일 길이
      FMatrix        InverseBindMatrix = FMatrix::Identity;
                                         // = TransformLinkMatrix.Inverse() * TransformMatrix
  };
  ```
  `friend operator<<`로 serialize.

- **`FSkeletalMesh` 멤버 재구성**:
  | 멤버 | Before | After |
  |---|---|---|
  | `Vertices` | `TArray<FSkeletalMeshVertex>` | `TArray<FNormalVertex>` |
  | `Indices`  | `TArray<uint32>` | (그대로) |
  | `Bones`    | `TArray<FBone>` (IBP 포함) | `TArray<FBone>` (IBP 없음) |
  | `Clusters` | (없음) | **`TArray<FBoneCluster>` 신규** |
  | `Sections` | (그대로) | (그대로) |
  | `RenderBuffer` | `FMeshBuffer` (96B 정점) | `FMeshBuffer` (48B 정점) |

- **`Serialize(FArchive&) → bool`**: 버전 필드(`SerializeVersion = 2`) 추가. 로드 시 버전 mismatch면 즉시 `false` 반환 — 호출자(FBXManager, Step D)가 재import 트리거.

- **`CacheBounds`**: `Vertices[i].Position` → `Vertices[i].pos` (FNormalVertex 필드명).

### 2.3 [`Engine/SkeletalMesh/SkeletalMesh.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMesh.cpp)
- **`USkeletalMesh::InitResources`**: `TMeshData<FSkeletalMeshVertex>` → `TMeshData<FNormalVertex>`. GPU 정적 VB(`RenderBuffer`)가 48B 스트라이드로 생성됨.

### 2.4 [`Engine/SkeletalMesh/SkeletalMesh.h`](KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMesh.h)
- Forward decl: `class UStaticMesh;`.
- 신규 멤버 `UStaticMesh* EmbeddedStaticMesh = nullptr;` + `Get/SetEmbeddedStaticMesh()` (hybrid FBX의 static 파트 보유).
- 라이프사이클은 UObjectManager가 관리 — destructor에서 명시적 delete 하지 않음.

---

## 3. Step B — FBX Importer Refactor

### 3.1 [`Engine/SkeletalMesh/FBXImporter.h`](KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.h)
- **새 진입점**:
  ```cpp
  static bool ImportFbx(const FString& FilePath,
                        USkeletalMesh* OutSkeletal,   // optional
                        UStaticMesh*   OutStatic);    // optional
  ```
  적어도 한쪽에 유효 데이터가 들어가면 `true`.

- **Backward-compat wrapper**: `ImportSkeletalMesh(path, OutMesh)` → `ImportFbx(path, OutMesh, nullptr)`. 기존 호출자는 변경 불필요.

- **신규 private 헬퍼**:
  - `ExtractSkeletalMesh(FbxMesh*, FSkeletalMesh*, USkeletalMesh*)` — cluster 보존 추출.
  - `ExtractStaticMesh(FbxMesh*, FStaticMesh*)` — bone-less mesh node를 `FNormalVertex`로 추출.
  - `HasValidSkinDeformer(FbxMesh*, USkeletalMesh*)` — skin 분기 판단.
  - `NormalizeClusterWeights(FSkeletalMesh*)` — 모든 cluster의 weight를 per-vertex sum=1.0으로 정규화.

### 3.2 [`Engine/SkeletalMesh/FBXImporter.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.cpp)
- **`ProcessNode`**: 각 mesh node를 `HasValidSkinDeformer` 결과로 분기 →
  - skin 있음 + `OutSkeletal` 바인딩 → `ExtractSkeletalMesh`.
  - skin 없음 + `OutStatic` 바인딩 → `ExtractStaticMesh`.
  - 바인딩 없으면 skip + warning 로그.

- **`ExtractSkeletalMesh`** (현 `ExtractMesh`의 cluster-aware 재구성):
  1. 폴리곤 펼치기 루프에서 `FNormalVertex`만 채운다 (bone 필드 없음).
  2. **Control-point → expanded vertex index map** 빌드 (`TArray<TArray<uint32>> CPToExpanded`). 한 control point가 폴리곤 펼치기로 N개 expanded vertex가 되므로 map은 `[N]` 형태.
  3. Section 등록 (현재는 단일 "Default" 슬롯 — 머티리얼 분할은 후속 작업).
  4. FBX의 각 `FbxCluster`마다 `FBoneCluster` 인스턴스 직접 생성:
     - `BoneIndex = OutMesh->GetBoneIndex(name)`
     - IBP 계산: `IBP = TransformLinkMatrix.Inverse() * TransformMatrix` (이전과 동일 수식, 단 본이 아닌 cluster에 저장).
     - `(CPIndex, Weight)` 쌍을 expanded vertex 단위로 펼쳐 `VertexIndices`/`Weights`에 push.

- **`ExtractStaticMesh`** (신규):
  - Mesh node의 `EvaluateGlobalTransform()`을 `FbxMatrixToFMatrix`로 변환하여 `BakeXform`으로 사용.
  - Vertex 추출 시 pos는 `BakeXform.TransformPositionWithW(localPos)`, normal은 `BakeXform.TransformVector(localN)`로 평탄화. → static mesh는 actor transform과 독립적으로 world-space 기준 bind된 결과를 가짐.
  - 결과를 누적 가능한 단일 `FStaticMesh`에 push (한 FBX 파일의 모든 unskinned mesh node가 하나의 static asset에 합쳐짐).

- **`NormalizeClusterWeights`** (신규):
  - 1pass: per-vertex total weight 합산.
  - 2pass: 각 cluster의 `Weights[i] /= Totals[VertexIndices[i]]` — 기존 `ExtractMesh:303-306` 의 정규화 동작과 동일한 출력 보장.

- **`ExtractSkeleton`**: IBP 저장 코드(`Bones[i].InverseBindMatrix = ...`) 제거. 이제 본은 parent + S·R·T만 다룸. 진단 로그는 유지.

### 3.3 동작 변경 요약
- **0-weight vertex**: cluster `VertexIndices`에 한 번도 등장하지 않음 → 스키닝 누적 단계에서 자연스럽게 bind-pose passthrough.
- **>4 bone influence**: 더 이상 truncate되지 않음 (cluster는 vertex 수에 비례하는 크기로 자라기만 함).
- **Static-only FBX**: `OutStatic`만 주어진 호출에서 정상 동작 (예: future StaticMesh 측에서 FBX 사용 시).

---

## 4. Step C — Component / SceneProxy Refactor

### 4.1 [`Engine/Component/SkeletalMeshComponent.h`](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.h)
- `#include "Mesh/StaticMeshAsset.h"` 추가 (FNormalVertex 가시화).
- forward decl: `class UStaticMeshComponent;`.
- 멤버 변경:
  - **`SkinningMatrices` 멤버 삭제** (cluster 기반에서는 per-bone IBP 합성 불필요 — per-cluster 임시 행렬을 inline에서 만든다).
  - `SkinnedVertices`: `TArray<FSkeletalMeshVertex>` → `TArray<FNormalVertex>`.
  - **신규 `UStaticMeshComponent* EmbeddedStaticMeshComp = nullptr;`** — hybrid FBX의 static 파트를 같은 actor에 부착한 sibling.
- 신규 메서드: `SyncEmbeddedStaticMesh()` — `SetSkeletalMesh()` 끝에서 호출, lazy-create / detach 처리.

### 4.2 [`Engine/Component/SkeletalMeshComponent.cpp`](KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp)

**Includes 추가**: `Component/StaticMeshComponent.h`, `Mesh/StaticMesh.h`, `GameFramework/AActor.h`.

**`SetSkeletalMesh(InMesh)`**:
- `InMesh == nullptr` 분기에서도 `SyncEmbeddedStaticMesh()` 호출 (이전 child 정리).
- `DynamicVB.Create(Device, vertexCount, sizeof(FSkeletalMeshVertex))` → `sizeof(FNormalVertex)`.
- 본 카운트 기반 `SkinningMatrices.assign(BoneCount, Identity)` 호출 제거 (멤버 자체가 사라짐).
- 마지막에 `SyncEmbeddedStaticMesh()` 호출.

**`SyncEmbeddedStaticMesh()` (신규)**:
```cpp
AActor* Owner = GetOwner();
UStaticMesh* Embedded = SkeletalMesh ? SkeletalMesh->GetEmbeddedStaticMesh() : nullptr;
if (!Embedded) {
    if (EmbeddedStaticMeshComp && Owner) Owner->RemoveComponent(EmbeddedStaticMeshComp);
    EmbeddedStaticMeshComp = nullptr;
    return;
}
if (!Owner) return;                                 // actor 부착 전이면 다음 호출에 재시도
if (!EmbeddedStaticMeshComp)
    EmbeddedStaticMeshComp = Owner->AddComponent<UStaticMeshComponent>();
EmbeddedStaticMeshComp->SetStaticMesh(Embedded);
```
→ 1-component-per-proxy 제약을 유지하면서 hybrid asset을 동일 actor의 두 sibling component로 분담.

**`UpdateSkinning()`**: 단순 디스패치만 남김. 옛 bind-pose identity 진단 로그(per-bone IBP 검증) 제거 — cluster 모델에서는 의미가 다름. 옛 `SkinningMatrices[i] = Bones[i].InverseBindMatrix * Comp[i]` 라인 삭제.

**`UpdateSkinningCPU()`** — cluster 기반 재작성:
```cpp
const auto& Src      = Asset->Vertices;              // FNormalVertex 배열
const auto& Clusters = Asset->Clusters;
const auto& Comp     = ComponentSpaceMatrices;
SkinnedVertices = Src;                               // color/tex/tangent 보존

// 1) cluster에 잡힌 vertex 집합 표시
TArray<bool> bSkinned(N, false);
for (const FBoneCluster& C : Clusters)
    for (uint32 vi : C.VertexIndices) bSkinned[vi] = true;

// 2) skinned vertex만 pos/normal=0 초기화 (untouched vertex는 bind-pose passthrough)
for (vi: 0..N)
    if (bSkinned[vi]) { SkinnedVertices[vi].pos=0; SkinnedVertices[vi].normal=0; }

// 3) cluster 순회 — 행렬 한 번 계산 후 영향 vertex 누적
for (const FBoneCluster& C : Clusters) {
    FMatrix M = C.InverseBindMatrix * Comp[C.BoneIndex];
    for (i: 0..C.VertexIndices.size()) {
        SkinnedVertices[vi].pos    += M.TransformPositionWithW(Src[vi].pos)    * w;
        SkinnedVertices[vi].normal += M.TransformVector(Src[vi].normal)        * w;
    }
}
DynamicVB.Update(...);
```
→ 이전 모델의 (0,0,0) collapse 버그가 자연 해결됨.

**`GetMeshDataView()`**: `View.Stride = sizeof(FSkeletalMeshVertex)` → `sizeof(FNormalVertex)`. 픽킹/BVH 등에서 vertex view 가 48B 스트라이드로 일관됨.

**`CalcDynamicLocalBounds()`**: `V.Position` → `V.pos`.

**`UpdateSkinningGPU()`**: stub 주석 갱신 — cluster 모델에서 GPU 스키닝은 vertex shader가 cluster SRV(또는 bone-matrix palette)를 받아야 하므로 별도 파이프라인 설계가 필요함을 명시.

### 4.3 [`Engine/Render/Proxy/SkeletalSceneProxy.cpp`](KraftonEngine/Source/Engine/Render/Proxy/SkeletalSceneProxy.cpp)
- 코드 변경 없음 (stride는 `FMeshBuffer::GetVertexBuffer().GetStride()` / `FDynamicVertexBuffer::GetStride()`로 auto-adapt).
- GPU 모드 분기 주석만 갱신 — bind-pose VB라는 점, cluster SRV 슬롯 필요성 명시.

---

## 5. 의미 있는 동작 변경

| # | 동작 | Before | After |
|---|---|---|---|
| 1 | 0-weight vertex 위치 | (0,0,0) collapse | bind-pose 위치 그대로 |
| 2 | bone 5개 이상에 영향받는 vertex | 4개로 truncate (visual glitch) | 모든 영향 반영 |
| 3 | skin 없는 FBX prop mesh | import 누락 / origin collapse | 별도 `FStaticMesh`로 추출 |
| 4 | hybrid FBX → actor에 부착 | skeletal 1개 component만 | skeletal + static 2개 sibling component |
| 5 | `.bin` 캐시 v1 로드 | undefined behavior (포맷 mismatch) | `Serialize` false 반환 → FBXManager가 감지 → 강제 재import |
| 6 | CPU 스키닝 vertex stride | 96B | 48B (대역폭 ~50% 절감) |
| 7 | FBXManager 의 importer 호출 | 구 API `ImportSkeletalMesh(path, skel)` | `ImportFbx(path, skel, pendingStatic)` — hybrid 결과 수신 |
| 8 | FBX 머티리얼/텍스처 추출 | 미지원 — 단일 `"Default"` section 만 등록 | FBX surface material → `.mat` JSON → `UMaterial` → 텍스처 binding 자동 |
| 9 | Section 분할 | 단일 section (mesh 전체) | polygon-material 매핑에 따라 머티리얼별 다중 section |
| 10 | 한자/한글 FBX 텍스처 경로 매칭 | `path(const char*)` Windows ACP 가정으로 mojibake → 매칭 실패 | `FPaths::ToWide(UTF-8)` → wide path → 매칭 성공 |

---

## 6. Step D — Asset/Manager 통합

### 6.1 [`Engine/SkeletalMesh/SkeletalMesh.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMesh.cpp)

**Includes 추가**: `Mesh/StaticMesh.h`, `Object/ObjectFactory.h`.

**`USkeletalMesh::Serialize(Ar)` 갱신**:
- `SkeletalMeshAsset->Serialize(Ar)` 가 **false** 를 반환하면 (= 캐시 버전 mismatch) → 임시 객체 delete, `SkeletalMeshAsset = nullptr` 로 두고 **early return**.
  - 호출자(FBXManager)가 `GetSkeletalMeshAsset() == nullptr` 을 재빌드 신호로 사용.
- 정상 분기에서 `StaticMaterials` / `BoneNames` 직렬화 직후, **`EmbeddedStaticMesh` 도 함께 직렬화**:
  - `bool bHasEmbedded` flag → loading 시 `UObjectManager::Get().CreateObject<UStaticMesh>()` 로 생성 → `EmbeddedStaticMesh->Serialize(Ar)` 위임.
  - `UStaticMesh::Serialize` 는 `Super::Serialize` 를 호출하지 않으므로 UObject base 중복 직렬화 위험 없음 (`StaticMesh.cpp:26-56` 확인).

**`USkeletalMesh::InitResources(Device)` 갱신**:
- 기존 `SkeletalMeshAsset` GPU 버퍼 생성 후, `EmbeddedStaticMesh` 가 있으면 `EmbeddedStaticMesh->InitResources(InDevice)` 도 호출 → hybrid 의 static 파트도 GPU 에 올라감.

### 6.2 [`Engine/SkeletalMesh/FBXManager.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/FBXManager.cpp)

**Includes 추가**: `Mesh/StaticMesh.h`, `Object/ObjectFactory.h`.

**`FFBXManager::LoadSkeletalMesh` 갱신** — 두 분기:

1. **캐시 로드 후 무효 감지**:
   ```cpp
   SkeletalMesh->Serialize(Reader);
   if (!SkeletalMesh->GetSkeletalMeshAsset()) {
       UE_LOG("Skeletal mesh cache invalid (version mismatch?), rebuilding: %s", BinPath.c_str());
       bNeedRebuild = true;
   }
   ```
   → v1 캐시는 첫 로드에서 자동으로 폐기되고 v2 로 다시 구워진다.

2. **재빌드 분기 — `ImportFbx` 호출**:
   ```cpp
   UStaticMesh* PendingStatic = UObjectManager::Get().CreateObject<UStaticMesh>();
   if (FFbxImporter::ImportFbx(FbxPath, SkeletalMesh, PendingStatic)) {
       if (PendingStatic->GetStaticMeshAsset())            // static 데이터가 실제로 들어왔을 때만
           SkeletalMesh->SetEmbeddedStaticMesh(PendingStatic);
       // 빈 객체는 UObjectManager GC에 위임 (ObjManager 와 동일 패턴)

       FWindowsBinWriter Writer(BinPath);
       if (Writer.IsValid()) SkeletalMesh->Serialize(Writer);
   }
   ```

**`FFBXManager::ReleaseAllGPU` 갱신**:
- 기존 `SkeletalMeshAsset->RenderBuffer->Release()` 외에, `EmbeddedStaticMesh->GetStaticMeshAsset()->RenderBuffer` 도 함께 해제. LOD 버퍼는 LOD 활성화 시 함께 처리(현재 LOD 생성은 ObjManager 측에서도 주석 처리됨).

### 6.3 hybrid FBX 동작 정리

캐시 흐름:
```
LoadSkeletalMesh
  ├── 메모리 캐시 hit → 그대로 반환
  ├── 디스크 .bin v2 hit → Serialize → InitResources (skel + embedded) → cache 등록
  ├── 디스크 .bin v1 hit → Serialize false → 재빌드 분기
  └── 재빌드 분기:
       PendingStatic = CreateObject<UStaticMesh>()
       ImportFbx(path, skel, PendingStatic)
         ├── skinned mesh node → skel 에 cluster 채움
         └── unskinned mesh node → PendingStatic 에 FStaticMesh 채움
       PendingStatic 에 데이터 있으면 → skel->SetEmbeddedStaticMesh()
       Serialize(Writer)  # v2 캐시 생성, EmbeddedStaticMesh 포함
       InitResources(Device)  # skel + embedded 둘 다 GPU 업로드
       cache 등록
```

runtime 렌더 흐름:
```
USkeletalMeshComponent::SetSkeletalMesh(skel)
  ├── 기존 동작 (cluster 기반 스키닝, DynamicVB, etc.)
  └── SyncEmbeddedStaticMesh()
       └── skel->GetEmbeddedStaticMesh() 있으면
            owner actor->AddComponent<UStaticMeshComponent>()
            child->SetStaticMesh(embedded)
```

---

## 6.5. Step F — SkeletalMesh 머티리얼/텍스처 적용

### 6.5.1 [`Engine/SkeletalMesh/FBXImporter.h/.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.cpp)

**신규 import 컨텍스트**:
```cpp
struct FFbxImportContext {
    FString FbxFilePath;
    FString FbxStem;                            // .mat 슬롯 키 prefix (`<FbxStem>__<MatName>`)
    TMap<FString, FString> TextureIndex;        // lowercase basename → 프로젝트-루트-상대 경로
    TArray<FStaticMaterial> SkeletalMaterials;  // ExtractSkeletalMesh 누적 결과
    TArray<FStaticMaterial> StaticMaterials;    // ExtractStaticMesh 누적 결과
};
```
`ProcessNode` / `ExtractSkeletalMesh` / `ExtractStaticMesh` 모두 컨텍스트를 인자로 받아 동일한 텍스처 인덱스 / 슬롯 배열을 공유.

**신규 헬퍼 (anonymous namespace)**:
- `BuildFbxTextureSearchIndex(FbxFilePath)` — FBX 파일이 위치한 디렉토리 (D) 를 root 로 `recursive_directory_iterator` 순회 → lowercase basename → 프로젝트-루트-상대 경로 매핑. 디렉토리 이름은 가정하지 않음 (사용자 요구: `tex/`, `.fbm/` 등 하드코딩 X).
- `ResolveFbxTexturePath(FbxFileTexture*, Index)` — `GetFileName()` → basename → 매칭, 실패 시 `GetRelativeFileName()` 재시도.
- `ExtractDiffuseTexturePath(FbxSurfaceMaterial*, Index)` — `sDiffuse` 속성에 연결된 첫 `FbxFileTexture` 추출.
- `ExtractDiffuseColor(FbxSurfaceMaterial*)` — `Lambert/Phong` 의 Diffuse RGB, 없으면 (1,0,1) 마젠타.
- `MakeMatSlotKey(FbxStem, MaterialName)` — `<FbxStem>__<SanitizedMatName>` 형식 (Windows 파일명 안전 문자만).
- `ConvertFbxMaterialToMat(SlotKey, DiffusePath, DiffuseColor)` — `Asset/Materials/Auto/<SlotKey>.mat` 생성, 이미 존재하면 덮어쓰지 않음 (사용자 편집 보존, ObjImporter 와 동일 정책).
- `CollectNodeMaterials(FbxNode*, OutSlots, FbxStem, Index)` — mesh node 의 `FbxMatCount` 만큼 surface material 을 슬롯 배열에 등록(중복 없이), 로컬 인덱스 → 슬롯 배열 인덱스 매핑 반환.
- `EnsureDefaultMaterialSlot(OutSlots)` — 머티리얼이 하나도 없는 mesh node 폴백.
- `ExtractPolygonMaterialIndices(FbxMesh*, FbxMatCount)` — `eAllSame` / `eByPolygon` mapping mode 처리하여 polygon 별 node-local material index 반환.

### 6.5.2 `ExtractSkeletalMesh` / `ExtractStaticMesh` 갱신

기존 단일 `"Default"` section 만 만들던 코드가 다음 3-phase 로 변경:

**Phase A** — node 의 머티리얼을 슬롯 배열에 등록 (`CollectNodeMaterials`).
**Phase B** — polygon 별 material 인덱스 추출 (`ExtractPolygonMaterialIndices`).
**Phase C** — polygon expansion 루프에서 vertex 는 그대로 `RawMesh->Vertices` 에 push, **index 는 머티리얼별 임시 버퍼** `PerMatIndices[matIdx]` 에 누적. 모든 polygon 처리 후 머티리얼 슬롯 순서로 `RawMesh->Indices` 에 flush + `FStaticMeshSection`(FirstIndex/NumTriangles/MaterialIndex 캐시) 등록.

**Cluster 보존 보장**: vertex array 의 push 순서는 polygon expansion 순서 그대로 — `CPToExpanded` 매핑 / `FBoneCluster::VertexIndices` 영향 없음. 머티리얼 분할은 **index buffer 의 순서만 바꾸는** 변경.

### 6.5.3 [`Engine/SkeletalMesh/SkeletalMesh.cpp`](KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMesh.cpp)

- 신규 `CacheSectionMaterialIndices(Asset, Materials)` — slot name 기반으로 `Section.MaterialIndex` 재계산.
- `SetSkeletalMeshAsset(...)` / `SetStaticMaterials(...)` 가 setter 호출 시 자동 재캐싱.
- `Serialize` 로딩 분기 끝에도 `CacheSectionMaterialIndices` 명시 호출 (setter 를 거치지 않으므로).

### 6.5.4 `.mat` JSON 스키마 (ObjImporter 와 동일)
```json
{
  "PathFileName": "Asset/Materials/Auto/Furina__体.mat",
  "Origin": "FbxImport",
  "ShaderPath": "Shaders/Geometry/UberLit.hlsl",
  "RenderPass": "Opaque",
  "Textures":   { "DiffuseTexture": "Data/FurinaFBX/体.png" },
  "Parameters": { "SectionColor": [1.0, 1.0, 1.0, 1.0] }
}
```
텍스처 미발견 시 `Textures` 를 비우고 `Parameters.SectionColor` 에 머티리얼의 `Diffuse` 컬러를 작성.

### 6.5.5 동작 흐름

```
ImportFbx(path, skel, static)
  ├── Ctx.TextureIndex = BuildFbxTextureSearchIndex(path)        # 폴더 단위 1회 빌드
  ├── Ctx.FbxStem      = stem(path)
  ├── ExtractSkeleton(...)
  ├── ProcessNode(...)
  │    ├── (skinned)   → ExtractSkeletalMesh → CollectNodeMaterials → 슬롯/section 분할
  │    └── (unskinned) → ExtractStaticMesh   → 동일
  └── OutSkeletal/OutStatic ->SetStaticMaterials(Ctx.*Materials)

FBXManager 로드 → SkeletalMesh->Serialize / InitResources
                → USkeletalMeshComponent::SetSkeletalMesh
                → OverrideMaterials 가 슬롯에서 초기화 (이미 동작)
                → FSkeletalSceneProxy 가 Section.MaterialIndex 매칭 → UMaterial 바인딩
                → UMaterial 의 SRV (텍스처) 가 셰이더로 전달됨 (이미 동작)
```

### 6.5.6 변경 파일 (Step F)
- `Engine/SkeletalMesh/FBXImporter.h` — `ProcessNode/Extract*` 시그니처에 컨텍스트 인자 추가.
- `Engine/SkeletalMesh/FBXImporter.cpp` — 컨텍스트 / 헬퍼 / 머티리얼 분할 로직.
- `Engine/SkeletalMesh/SkeletalMesh.cpp` — `CacheSectionMaterialIndices` + setter / Serialize 통합.

### 6.5.7 명시적 비범위 (후속 작업 후보)
- Normal / Specular / AO 텍스처 (현재는 Diffuse 만).
- PBR 머티리얼 인스턴스 (현재 Lambert/Phong Diffuse 만).
- 머티리얼 슬롯 GC (`Asset/Materials/Auto/` 에 사용되지 않는 .mat 누적 가능).

---

## 6.6. Step G — 한자/한글 FBX 텍스처 경로 매칭 encoding fix

### 6.6.1 증상

Step F 적용 후 Furina.fbx (중국어 머티리얼/텍스처 이름 사용) 실측:
- ASCII 텍스처 (`spa_h.png`) → sDiffuse 매칭 성공.
- 한자 머티리얼 (`颜`, `髮`, `髮2`, `体`) → **material-name fallback** 으로만 매칭 성공 (폴더에 동명 파일 존재).
- 그 외 한자 머티리얼 다수 (`颜2`, `二重`, `星目`, `白目`, `服饰`, `体2` …) → `[Tex] FBX-reported texture not found in folder` → `COLOR ONLY`.

→ FBX 가 sDiffuse 슬롯에 한자 텍스처 경로 (`Furina.fbm/颜.png` 등) 를 정확히 갖고 있음에도 매칭 실패.

### 6.6.2 원인

`ResolveFbxTexturePath` 내부의 `std::filesystem::path P(RawPath)` 한 줄:
- C++17 표준상 `path::path(const char*)` 는 Windows 에서 입력을 **system ACP (한국 Windows = CP-949)** 로 해석.
- 그러나 FBX SDK 의 char* 경로는 **UTF-8**.
- → UTF-8 multibyte sequence (`颜` = `E9 A2 9C`) 가 ACP 로 잘못 디코딩되어 `P.filename().wstring()` 이 mojibake wide string. 이후 `FPaths::ToUtf8` 로 변환해도 원본 UTF-8 와 다른 byte 시퀀스 → 인덱스 매칭 실패.

`BuildFbxTextureSearchIndex` (directory_iterator 가 native wide path 반환) / `MakeMatSlotKey` (char* UTF-8 → FString 직접 저장) 는 ACP 변환을 거치지 않으므로 정상.

### 6.6.3 변경 — `Engine/SkeletalMesh/FBXImporter.cpp`

`std::filesystem::path` 의 char-ctor 직접 사용을 제거하고, 프로젝트 convention 인 [`FPaths::ToWide`](KraftonEngine/Source/Engine/Platform/Paths.cpp) (CP_UTF8 기반 `MultiByteToWideChar`) 를 거쳐 wstring 으로 변환한 뒤 `path(wstring)` 으로 생성:

```cpp
// ResolveFbxTexturePath 내부 TryFind:
std::filesystem::path P(FPaths::ToWide(std::string(RawPath)));   // UTF-8 → wide → path
```

진단 로그 강화: 매칭 실패 시 우리가 검색한 lowercase basename 키도 함께 출력하여 잔존 케이스 디버깅 가능.

### 6.6.4 캐시 무효화 — `FSkeletalMesh::SerializeVersion` v3 → v4

이전 import 의 `.bin` 캐시에는 텍스처 정보 없이 저장된 `.mat` 경로가 박혀 있으므로, encoding fix 효과를 보려면 importer 재실행이 필요. 버전 bump 한 줄로 자동 처리:
- v3 .bin 로드 시 `FSkeletalMesh::Serialize` 가 false → `USkeletalMesh::Serialize` 가 `SkeletalMeshAsset = nullptr` → FBXManager 가 재import → 새 fix 적용된 importer 가 .mat 갱신 (`"Origin": "FbxImport"` 라 덮어쓰기 가능).

### 6.6.5 변경 없음 (이미 동작)

- **timestamp 기반 자동 재빌드**: `.fbx` 가 `.bin` 보다 새로우면 자동 재import — 이미 [FBXManager.cpp:177-185](KraftonEngine/Source/Engine/SkeletalMesh/FBXManager.cpp) 에 존재.
- `BuildFbxTextureSearchIndex`, `MakeMatSlotKey` — 이미 UTF-8 일관성 유지.

### 6.6.6 콘솔 출력 mojibake (별개)

콘솔의 `é¢œ` 같은 출력은 Windows 콘솔이 기본 ACP 출력이라서 발생 — **매칭 동작에는 영향 없음** (raw byte 는 정상 UTF-8). 가독성을 원하면 엔진 시작 시 `SetConsoleOutputCP(CP_UTF8)` 1회 호출이면 해결되지만, 이번 fix 범위 외 (로깅 인프라).

---

## 7. 미진행 / 후속 작업

### Step E — 검증 (예정)
- Regression: 기존 skeletal 캐릭터 .fbx — 본 수/시각 동일, v1 캐시 자동 재빌드 후 정상 렌더.
- Pure static FBX: `UStaticMesh` 만 생성, sibling static component 가 actor 에 부착되어 렌더.
- Hybrid FBX: 두 sibling component, 둘 다 렌더.
- Zero-weight vertex .fbx: bind-pose 위치 확인.
- OBJ regression: `Engine/Mesh/` 미변경 검증.
- **Furina.fbx** (Step F): `Asset/Materials/Auto/Furina__*.mat` 자동 생성, viewport 에서 텍스처 binding 확인.

### GPU 스키닝 (이번 PR 스코프 밖)
- vertex에 bone 정보가 더 이상 없으므로, vertex shader가 cluster index buffer + bone-matrix palette를 SRV로 받도록 파이프라인 확장 필요.
- 현 단계는 CPU 스키닝까지만 지원. 구조는 추후 확장을 위해 stub 형태로 유지.

---

## 8. 수정/생성 파일 목록

### 수정
- `Engine/Render/Types/VertexTypes.h`
- `Engine/SkeletalMesh/SkeletalMeshAsset.h`
- `Engine/SkeletalMesh/SkeletalMesh.h`
- `Engine/SkeletalMesh/SkeletalMesh.cpp`
- `Engine/SkeletalMesh/FBXImporter.h`
- `Engine/SkeletalMesh/FBXImporter.cpp`
- `Engine/SkeletalMesh/FBXManager.cpp` *(Step D 신규)*
- `Engine/Component/SkeletalMeshComponent.h`
- `Engine/Component/SkeletalMeshComponent.cpp`
- `Engine/Render/Proxy/SkeletalSceneProxy.cpp` (주석만)

### 변경 없음 (재사용)
- `Engine/Mesh/*` 전체 (StaticMesh / OBJ 파이프라인)
- `Engine/Component/StaticMeshComponent.*`
- `Engine/Render/Proxy/StaticMeshSceneProxy.*`
- `Engine/Render/Resource/Buffer.h`

---

## 9. 참고 — 새/사라진 타입 정리

**신규**:
- `FBoneCluster` — bone 1개와 vertex 영향 목록을 묶은 단위 (FbxCluster 1:1 대응).

**삭제**:
- `FSkeletalMeshVertex` — 96B 정점 구조체. `FNormalVertex` (48B)가 대체.
- `FBone::InverseBindMatrix` 멤버 — `FBoneCluster`로 이동.
- `TSkeletalData`, `FSkeletalData` alias — 사용처 없는 dead code.
- `USkeletalMeshComponent::SkinningMatrices` 멤버 — per-cluster transient 행렬로 대체.

**기존 유지 (재사용)**:
- `FNormalVertex`, `FStaticMeshSection`, `FStaticMaterial`, `FMeshBuffer`, `FVertexBuffer`, `FIndexBuffer`, `FDynamicVertexBuffer`, `TMeshData<>`.
- `UStaticMesh`, `UStaticMeshComponent`, `FStaticMeshSceneProxy`, `FStaticMesh`.

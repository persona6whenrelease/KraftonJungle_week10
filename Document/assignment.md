# FBX 시스템 4인 역할 분담 (최종)

## 역할 분담 개요

| 담당자 | 역할 | 주요 산출물 | 부담도 |
|--------|------|------------|--------|
| **A팀** | 렌더링 인프라 + 수학 구조체 | FMeshBuffer 동적 확장, FDrawCommand BoneCB, FStateCache Dirty 플래그, Matrix4x4/Vector3 | ★★★★☆ |
| **B팀** | 스켈레탈 메시 컴포넌트 + CPU Skinning | USkinnedMeshComponent, USkeletalMeshComponent, SkinVertexPosition 연산 엔진 | ★★★★☆ |
| **C팀** | FBX SDK 연동 + Asset Pipeline | FbxImporter (Skeletal + Static), Binary Baker (.mesh/.skel) | ★★★★☆ |
| **D팀** | 에디터 UI + 에셋 설계 + Viewer | USkeletalMesh 에셋 클래스, Skeletal Mesh Viewer 패널, Bone Tree UI, ShowFlag, Gizmo 연동 | ★★★☆☆ |

> **조정 내역 (기존 대비):**
> - B팀의 수학 구조체 구현 → **A팀으로 이관** (B팀 피크 부하 감소, Skinning 구현 집중)
> - Binary Baker는 **C팀이 유지** (FBX 데이터 구조와 직렬화가 하나의 파이프라인)
> - D팀에 **Skeletal Mesh Viewer 패널 + ShowFlag** 추가 (FBX.md 2.2 누락 항목 반영)
> - C팀에 **StaticMesh FBX Importer** 추가 (FBX.md 2.3 누락 항목 반영)

---

## A팀 — 렌더링 인프라 + 수학 구조체

**담당 이유:** `FMeshBuffer` 동적 확장은 전체 프로젝트의 Critical Path입니다.
B팀이 CPU Skinning 결과를 GPU에 올리려면 A팀의 `Update` 인터페이스가 먼저
확정되어야 합니다. 수학 구조체는 이 인터페이스 설계와 함께 진행하기 자연스럽고,
B팀이 Skinning 알고리즘 구현에 집중할 수 있도록 Week 1 말까지 선행 제공합니다.

**핵심 책임:**

- `FMeshBuffer`에 `D3D11_USAGE_DYNAMIC` + `D3D11_CPU_ACCESS_WRITE` 플래그 기반
  버퍼 생성 분기 추가, `Update(const void* data, size_t size)` 인터페이스 구현 (Map/Unmap 래핑)
- `FDrawCommand`에 `BoneCB` 슬롯 (b4) 필드 추가 및 `FDrawCommandList` 제출 로직 반영
- `FStateCache`에 `bBoneDirty` Per-Frame Dirty 플래그 추가 —
  포인터가 동일해도 본 행렬 데이터가 변경되었음을 인지하고 매 프레임 강제 갱신
- `Vector3` 연산자 오버로딩 (+, *, 내적), `Matrix4x4` 곱셈 및
  `TransformPosition(Vector3)` 구현 → **B팀에 헤더 파일로 제공**

```cpp
// FMeshBuffer 예시 인터페이스
class FMeshBuffer {
public:
    static FMeshBuffer* Create(bool bDynamic = false);
    void Update(const void* data, size_t size); // Dynamic 모드 전용
};

// 수학 구조체 예시
struct Matrix4x4 {
    float m[4][4];
    Vector3 TransformPosition(const Vector3& v) const;
    Matrix4x4 operator*(const Matrix4x4& other) const;
};
```

**FBX 학습 접점:** Dynamic Buffer 설계를 통해 "CPU가 매 프레임 Skinned 정점을
연산하고 GPU에 업로드하는 전체 흐름"을 렌더링 파이프라인 관점에서 이해합니다.
수학 구조체 구현으로 Skinning Matrix 연산의 기초도 자연스럽게 습득합니다.

---

## B팀 — 스켈레탈 메시 컴포넌트 + CPU Skinning

**담당 이유:** FBX의 수학적 핵심인 CPU Skinning 알고리즘 구현에 집중합니다.
수학 구조체는 A팀이 선행 제공하므로 B팀은 Skinning 로직 자체에만 집중할 수 있습니다.

**핵심 책임:**

- `UMeshComponent`를 상속받는 `USkinnedMeshComponent` 베이스 클래스 구현
  (`USkeletalMesh*` 에셋 참조 및 런타임 Dynamic Vertex Buffer 관리)
- `USkeletalMeshComponent` 구현 — `TickComponent`에서 매 프레임 CPU Skinning 호출,
  A팀의 `FMeshBuffer::Update`로 결과를 GPU에 업로드, `FDrawCommand` 생성 및 BoneCB 설정
- `SkinVertexPosition` CPU Skinning Processor 구현

**반드시 이해해야 하는 수식:**

```
SkinningMatrix_i = BoneWorldTransform_i × InverseBindPose_i
최종_정점 = Σ (weight_i × SkinningMatrix_i × 원본_정점)
```

`InverseBindPose`가 필요한 이유: 원본 정점은 메시 로컬 좌표계에 있고,
본의 Transform은 월드 좌표계에 있습니다. InverseBindPose로 정점을 본의
바인드 포즈 공간으로 먼저 끌어온 뒤 현재 본 Transform을 적용해야 올바른 변환이 됩니다.

```cpp
Vector3 SkinVertexPosition(const Vertex& vertex, const std::vector<Bone>& bones) {
    Vector3 result = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        int boneIndex = vertex.boneIndices[i];
        float weight  = vertex.boneWeights[i];
        if (weight > 0.0f && boneIndex >= 0 && boneIndex < (int)bones.size()) {
            Vector3 transformed = bones[boneIndex].skinningMatrix.TransformPosition(vertex.position);
            result = result + (transformed * weight);
        }
    }
    return result;
}
```

**FBX 학습 접점:** Bone, Weight, InverseBindPose, SkinningMatrix의 상관관계를
직접 구현하며 완전히 이해합니다. 4개의 학습목표 중 가장 많은 항목을 커버합니다.

---

## C팀 — FBX SDK 연동 + Asset Pipeline

**담당 이유:** FBX 파일에서 데이터를 추출하는 "입구" 전담입니다.
C팀이 확정하는 데이터 스키마가 B팀과 D팀이 사용할 `USkeletalMesh` 구조를 결정합니다.
Binary Baker는 추출한 데이터를 직렬화하는 작업으로 파싱 파이프라인과 하나의 흐름이므로
C팀이 함께 담당합니다.

**핵심 책임:**

- `vcpkg.json`에 FBX SDK 종속성 추가 및 개발 환경 구축 (팀 전체 공유)
- `FbxImporter` 구현 — 기존 `ObjImporter` 패턴 계승

| 추출 데이터 | FBX SDK API | 사용처 |
|------------|------------|--------|
| Vertex Position/UV/Normal | `FbxMesh::GetControlPoints()` | B팀 Skinning 입력 |
| Bone Hierarchy | `FbxNode` 트리 순회 | D팀 Tree UI |
| Bind Pose (InverseBindPose) | `FbxPose` | B팀 SkinningMatrix 계산 |
| Bone Weights/Indices | `FbxSkin`, `FbxCluster` | B팀 Skinning 루프 |

- **StaticMesh FBX Importer 추가** — 리깅 데이터가 없는 StaticMesh 임포트 분기 구현
  (FBX.md 2.3 요구사항)
- **Binary Baker 구현** — 추출 데이터를 `.mesh` / `.skel` 바이너리로 직렬화 (`FArchive` 활용),
  런타임에 FBX SDK 없이 역직렬화하여 파싱 비용 제거

**FBX Node 계층 → 선형 배열(Linear Array) 변환 필수:**

```cpp
// FBX 트리 순회 후 선형 배열로 재구성
struct FBoneInfo {
    int       parentIndex;      // -1이면 루트 본
    FString   name;
    Matrix4x4 inverseBindPose;
};
TArray<FBoneInfo> Bones; // 인덱스로만 접근 → 런타임 스키닝 최적화
```

**FBX 학습 접점:** FBX SDK의 Node Hierarchy, Deformer, Skin, Cluster 개념을
직접 API를 호출하며 이해합니다. FBX 자료 구조 학습목표를 가장 깊이 커버합니다.

---

## D팀 — 에디터 UI + 에셋 설계 + Skeletal Mesh Viewer

**담당 이유:** 에디터 UI는 B팀 CPU Skinning 완성 전에 더미 데이터로 선행 구현이 가능합니다.
`EditorMainPanel` 기반 인프라가 이미 구축되어 있어 Viewer 패널 추가가 수월합니다.
`USkeletalMesh` 에셋 클래스 확정은 모든 팀의 병렬 진행을 여는 열쇠이므로 최우선입니다.

**핵심 책임:**

- **`USkeletalMesh` 에셋 클래스 설계 (최우선 — Week 1 말까지 확정 필수)**
  모든 팀이 의존하는 공용 데이터 스키마

```cpp
class USkeletalMesh : public UObject {
public:
    TArray<FBoneInfo>  Bones;            // 선형 배열 (C팀과 협의)
    TArray<FVertex>    Vertices;         // Position, UV, Normal, BoneIndices[4], BoneWeights[4]
    TArray<uint32>     Indices;
    TArray<Matrix4x4>  InverseBindPoses;
};
```

- **Skeletal Mesh Viewer 패널 구현** (FBX.md 2.2 요구사항)
  `EditorMainPanel`에 전용 뷰어 창 추가 — 에디터 내부에서 리소스를 확인하고
  편집할 수 있는 ImGui 패널. Standalone이 아닌 에디터 통합 방식.
- **ShowFlag 토글** — `SkeletalMesh` 표시/숨김 제어 (FBX.md 2.1 요구사항)
- **ImGui Bone Hierarchy Tree UI** — `ImGui::TreeNode` 기반 부모-자식 본 관계 시각화
- **`SelectionManager` 확장** — 현재 액터 단위 선택을 본(Bone) 단위로 확장
- **`GizmoComponent` 연동** — 선택된 본에 Gizmo 부착, Transform(위치/회전/스케일) 조작

**병렬 가능 이유:** Viewer 패널과 Bone Tree UI는 B팀의 CPU Skinning 완성 전에
더미 Bone 데이터로 UI 자체를 먼저 구현할 수 있습니다. 렌더링 파이프라인과 독립적입니다.

**FBX 학습 접점:** Bone Hierarchy의 부모-자식 관계를 UI로 직접 시각화하면서
Bone의 계층 구조와 Transform 개념을 직관적으로 이해합니다.

---

## 단계별 타임라인

### Phase 1 (Week 1~2) — 병렬 착수

| 담당 | 작업 |
|------|------|
| A팀 | FMeshBuffer 동적 확장 + **수학 구조체 (Matrix4x4, Vector3) 구현 및 B팀 제공** |
| B팀 | USkinnedMeshComponent 클래스 골격 설계 (A팀 수학 구조체 수령 후 연산 착수) |
| C팀 | FBX SDK 환경 구축 + FbxScene 트리 순회 학습 |
| D팀 | **USkeletalMesh 에셋 스키마 확정** (C팀과 협의 후 전체 팀 배포) |

### Phase 2 (Week 2~3) — 인터페이스 연결

| 담당 | 작업 |
|------|------|
| A팀 | FDrawCommand BoneCB(b4) 추가 + FStateCache Dirty 플래그 |
| B팀 | CPU Skinning Processor 구현 (SkinVertexPosition) |
| C팀 | FbxImporter 파싱 로직 (Skeletal + StaticMesh 분기) |
| D팀 | Skeletal Mesh Viewer 패널 + Bone Hierarchy Tree UI + ShowFlag |

### Phase 3~4 (Week 3~4) — 통합 및 검증

| 담당 | 작업 |
|------|------|
| A팀 | B팀 CPU Skinning 출력 수신 및 파이프라인 연동 검증 |
| B팀 | A팀 Dynamic Buffer 연동, Reference Pose 렌더링 확인 |
| C팀 | Binary Baker 구현 (.mesh/.skel 직렬화) |
| D팀 | SelectionManager 확장 + GizmoComponent 본 단위 연동 |

---

## 핵심 동기화 포인트 (Sync Points)

### Week 1 말 — 가장 중요

| 제공 팀 | 수령 팀 | 내용 |
|---------|---------|------|
| A팀 | B팀 | `Matrix4x4`, `Vector3` 헤더 파일 제공 |
| A팀 | B팀 | `FMeshBuffer::Update(data, size)` 시그니처 확정 |
| C팀 + D팀 | 전체 | `USkeletalMesh` 데이터 스키마 확정 및 공유 |

### Week 3 초 — 통합 테스트

```
C팀 FbxImporter → USkeletalMesh 에셋 생성
          ↓
B팀 CPU Skinning 입력 → SkinVertexPosition 연산
          ↓
A팀 FMeshBuffer::Update → GPU 업로드
          ↓
화면에 Reference Pose 렌더링 확인
```

이 통합이 성공하면 D팀의 Viewer 및 Gizmo 연동, C팀의 Binary Baker는
독립적으로 붙일 수 있습니다.

### Week 4 — 에디터 연동 및 전체 통합 테스트

---

## FBX.md 요구사항 반영 체크리스트

| FBX.md 항목 | 담당 | 반영 여부 |
|------------|------|---------|
| FBX SDK 통합 | C팀 | ✅ |
| Reference Pose 렌더링 | B팀 + A팀 | ✅ |
| CPU Skinning (버퍼 업데이트) | B팀 + A팀 | ✅ |
| ShowFlag 제어 | D팀 | ✅ |
| Skeletal Mesh Viewer 패널 | D팀 | ✅ |
| Bone Hierarchy Tree UI | D팀 | ✅ |
| Transform Gizmo | D팀 | ✅ |
| StaticMesh FBX Importer | C팀 | ✅ |
| Binary Baking (.mesh/.skel) | C팀 | ✅ |
| Matrix4x4 / Vector3 수학 구조체 | A팀 | ✅ |
| USkinnedMeshComponent / USkeletalMeshComponent | B팀 | ✅ |

---

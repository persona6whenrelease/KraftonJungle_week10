# FBX 시스템 4인 병렬 역할 분담

## 역할 분담 개요

| 담당자 | 역할 | 주요 산출물 |
|--------|------|------------|
| **A팀** | 렌더링 인프라 | FMeshBuffer 동적 확장, FDrawCommand BoneCB, FStateCache Dirty 플래그 |
| **B팀** | 스켈레탈 메시 컴포넌트 + CPU Skinning | USkeletalMeshComponent, SkinVertexPosition 연산 엔진 |
| **C팀** | FBX SDK 연동 + Asset Pipeline | FbxImporter, Binary Baker (.mesh/.skel) |
| **D팀** | 에디터 UI + 에셋 클래스 설계 | USkeletalMesh 에셋, Bone Tree UI, Gizmo 연동 |

---

## A팀 — 렌더링 인프라 (Rendering Infra)

**왜 이 사람이 가장 중요한가:** `RenderPipeLineProblem.md`에 명시된 것처럼 `FMeshBuffer`의 동적 업데이트 인터페이스가 없으면 B팀이 CPU Skinning 결과를 GPU에 올릴 방법 자체가 없습니다. 즉, A팀의 인터페이스 확정이 전체 병렬 진행의 전제 조건입니다.

**핵심 책임:**
- `FMeshBuffer`에 `D3D11_USAGE_DYNAMIC` 모드 분기 추가, `Update(void* data, size_t size)` 인터페이스 공개
- `FDrawCommand`에 `BoneCB` 슬롯(b4) 추가
- `FStateCache`에 Per-Frame Dirty 플래그 로직 추가

**FBX 학습 접점:** Dynamic Buffer의 필요성을 직접 구현하면서 "CPU가 매 프레임 Skinned 정점을 계산하고 GPU에 업로드하는 흐름" 전체를 설계 관점에서 이해하게 됩니다.

---

## B팀 — 스켈레탈 메시 컴포넌트 + CPU Skinning

**이 역할의 핵심:** FBX의 수학적 핵심인 CPU Skinning 알고리즘을 직접 구현합니다. `FBX.md` 섹션 3.3의 `SkinVertexPosition` 함수가 이 팀의 주 산출물입니다.

**CPU Skinning 수식 (이 팀이 반드시 이해해야 하는 것):**

```
최종_정점 = Σ (weight_i × SkinningMatrix_i × 원본_정점)

SkinningMatrix_i = BoneWorldTransform_i × InverseBindPose_i
```

`InverseBindPose`가 필요한 이유: 원본 정점은 메시 로컬 좌표계에 있고, 본의 Transform은 월드/로컬 좌표계에 있습니다. 두 공간을 맞추기 위해 정점을 먼저 본 바인드 포즈 공간의 역변환으로 끌어온 뒤 현재 본 변환을 적용하는 것입니다.

**핵심 책임:**
- `USkinnedMeshComponent`, `USkeletalMeshComponent` 클래스 구현
- `SkinVertexPosition` 연산 엔진 구현
- A팀의 `FMeshBuffer::Update`를 통해 결과를 GPU에 업로드

---

## C팀 — FBX SDK 연동 + Asset Pipeline

**이 역할의 핵심:** FBX 파일에서 데이터를 추출하는 "입구"를 담당합니다. C팀이 추출하는 데이터 스키마가 B팀과 D팀이 사용할 `USkeletalMesh` 에셋의 구조를 결정합니다.

**FBX에서 추출해야 하는 데이터:**

| 데이터 | 의미 | 사용처 |
|--------|------|--------|
| Vertex Position/UV/Normal | 원본 메시 정점 | B팀 Skinning 입력 |
| Bone Hierarchy | 본의 부모-자식 관계 | D팀 트리 UI |
| Bind Pose (InverseBindPose) | T-Pose 시 본의 역행렬 | B팀 SkinningMatrix 계산 |
| Bone Weights/Indices | 각 정점에 영향을 주는 본과 가중치 | B팀 Skinning 루프 |

**핵심 책임:**
- `vcpkg.json`에 FBX SDK 추가, 개발환경 구축
- `FbxImporter` 구현 (기존 `ObjImporter` 패턴 계승)
- `.mesh` / `.skel` 바이너리 베이킹 (`FArchive` 활용)

**병렬 진행의 전제:** C팀은 FBX SDK 문서를 보며 혼자 진행 가능하기 때문에 A/B팀의 완료를 기다릴 필요가 없습니다. Week 1부터 동시에 진행합니다.

---

## D팀 — 에디터 UI + 에셋 클래스 설계

**이 역할의 핵심:** `USkeletalMesh` 에셋 클래스의 데이터 구조를 C팀과 협의하여 먼저 확정하고, 이후 에디터 UI를 구현합니다. 에셋 구조는 모든 팀이 의존하는 공용 인터페이스입니다.

**핵심 책임:**
- `USkeletalMesh` 리소스 클래스 설계 (Bone Hierarchy, BindPose, Weights 포함)
- ImGui 기반 Bone Hierarchy Tree 뷰 구현
- `SelectionManager` 확장을 통한 개별 본 선택 기능
- `GizmoComponent` 연결로 본 트랜스폼 조작 지원

**병렬 가능 이유:** 에디터 UI는 B팀의 CPU Skinning이 완성되지 않아도 더미 Bone 데이터를 만들어 UI 자체를 먼저 구현할 수 있습니다. 렌더링 파이프라인과 독립적입니다.

---

## 단계별 타임라인

### Phase 1 (Week 1~2) — 병렬 착수

| 담당 | 작업 |
|------|------|
| A팀 | FMeshBuffer 동적 확장 (D3D11_USAGE_DYNAMIC + Map/Unmap) |
| B팀 | 수학 구조체 보강 (Matrix4x4, Vector3 연산) |
| C팀 | FBX SDK 환경 구축 (vcpkg 통합, 개발환경 세팅) |
| D팀 | USkeletalMesh Asset 설계 (Hierarchy, BindPose, Weights) |

### Phase 2 (Week 2~3) — 인터페이스 연결

| 담당 | 작업 |
|------|------|
| A팀 | FDrawCommand 확장 (BoneCB b4 + Dirty 플래그) |
| B팀 | USkeletalMeshComponent + CPU Skinning Processor 구현 |
| C팀 | FBX Importer 구현 (Mesh/Bone/Weight/BindPose 추출) |
| D팀 | Bone Hierarchy Tree UI (ImGui 트리 뷰 + ShowFlag) |

### Phase 3~4 (Week 3~4) — 통합 및 검증

| 담당 | 작업 |
|------|------|
| A팀 | 파이프라인 연동 검증 (B팀 CPU Skinning 출력 수신) |
| B팀 | 동적 버퍼 연동 (A팀 인터페이스로 결과 업로드) |
| C팀 | Binary Baker `.mesh`/`.skel` (FArchive 직렬화) |
| D팀 | Gizmo + SelectionManager (Bone 단위 트랜스폼 조작) |

---

## 핵심 동기화 포인트 (Sync Points)

전체 프로젝트의 병목은 두 가지 인터페이스 확정 시점에 달려 있습니다.

### Week 1 말 (가장 중요)

- **A팀 → B팀:** `FMeshBuffer::Update(data, size)` 시그니처 확정.
  이것이 없으면 B팀은 CPU Skinning 결과를 올릴 곳이 없습니다.
- **C팀 → D팀:** `USkeletalMesh`의 Bone 데이터 스키마 확정.
  D팀이 UI를 실제 데이터에 맞게 연결할 수 있습니다.

### Week 3 초 — 통합 테스트

```
C팀 FBX Importer → USkeletalMesh 에셋 생성
    ↓
B팀 CPU Skinning 입력
    ↓
A팀 Dynamic Buffer → GPU 업로드
    ↓
화면에 Reference Pose 렌더링 확인
```

이 통합 테스트가 성공하면 이후 D팀의 에디터 기능은 독립적으로 붙일 수 있습니다.

### Week 4 — 에디터 연동 및 전체 통합 테스트
# SkeletalMesh Viewer — Bone-Driven Gizmo 설계

`feature/TreeStructure` 브랜치 기준, Content Browser에서 FBX 더블클릭 시 열리는 SkeletalMesh Viewer 윈도우에서 bone을 직접 조작할 수 있도록 gizmo를 연결한 작업의 설계 메모. 메인 에디터의 gizmo 인프라를 재사용하되, bone이 `USceneComponent`가 아닌 행렬 인덱스라는 본질적 차이를 어떻게 흡수했는지를 중심으로 정리한다.

---

## 1. 한 줄 요약

> Bone tree click 또는 viewport 메시 raycast → 선택된 bone의 mesh-space 행렬을 숨겨진 **`USceneComponent` proxy의 RelativeTransform으로 동기화** → gizmo는 그 proxy를 target으로 잡아 표준 조작 → 드래그 결과 proxy의 새 relative transform을 `parent_mesh_space`의 역행렬과 곱해 **`SetBoneLocalPose`로 환산**.

---

## 2. 문제 정의

메인 에디터의 `UGizmoComponent`는 **`USceneComponent`만 target으로 받는다** ([GizmoComponent.cpp:555](../KraftonEngine/Source/Engine/Component/GizmoComponent.cpp#L555)). 드래그가 발생하면 `TranslateTarget`/`RotateTarget`/`ScaleTarget` 내부에서 `TargetComponent->AddWorldOffset` ([:308](../KraftonEngine/Source/Engine/Component/GizmoComponent.cpp#L308)) / `SetRelativeRotation` ([:347](../KraftonEngine/Source/Engine/Component/GizmoComponent.cpp#L347)) / `SetRelativeScale` ([:407](../KraftonEngine/Source/Engine/Component/GizmoComponent.cpp#L407)) 같은 표준 SceneComponent API로 결과를 기록한다.

하지만 **bone은 SceneComponent가 아니다.** `USkinnedMeshComponent` 내부의 `LocalBonePoseMatrices[]` / `MeshSpaceBoneMatrices[]` 배열에 인덱스로 존재하며, 갱신 API는 `SetBoneLocalPose(int32, FMatrix)` ([SkinnedMeshComponent.h:28](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h#L28)) 하나뿐이다.

> **결론**: Gizmo 자체를 bone-aware로 만들거나, bone ↔ SceneComponent를 **adapter**로 잇는 두 갈래. 후자가 gizmo 내부를 건드리지 않고 끝나므로 채택.

---

## 3. 핵심 설계 — Bone Proxy Component Adapter

### 3-1. 토폴로지

```
PreviewActor (AActor)
└── PreviewMeshComponent (USkeletalMeshComponent)     ← root
    └── BoneProxy (USceneComponent)                    ← gizmo의 TargetComponent
```

`BoneProxy`는 viewport client가 만든 익명 SceneComponent로, **PreviewMeshComponent의 자식**으로 attach된다 ([SkeletalMeshViewerViewportClient.cpp:147](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L147)). 렌더링되지 않지만 transform 계산은 정상으로 한다.

```cpp
// SkeletalMeshViewerViewportClient.cpp — SetTrackedMesh
BoneProxy = UObjectManager::Get().CreateObject<USceneComponent>();
BoneProxy->AttachToComponent(TrackedMesh);
```

### 3-2. Mesh-Space ↔ Bone-Local 변환

엔진의 합성식 ([SkinnedMeshComponent.cpp:331-333](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp#L331)):

```
MeshSpace[i] = LocalPose[i] * MeshSpace[parent(i)]      (parent 존재 시, row-major / DirectX 관행)
             = LocalPose[i]                              (root bone)
```

역산 (드래그 후 새 local 추출):

```
NewLocalPose = NewMeshSpace * Inverse(MeshSpace[parent])    (parent 존재 시)
             = NewMeshSpace                                  (root bone)
```

`BoneProxy`가 PreviewMeshComponent의 자식이므로 `BoneProxy.RelativeMatrix == BoneProxy의 mesh-space matrix` (mesh component 기준 한 단계 부모 관계). 따라서 proxy의 relative transform이 곧 bone의 mesh-space transform이고, 위 변환식이 그대로 적용된다.

구현 위치: [SkeletalMeshViewerViewportClient.cpp:172-208](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L172) (`ApplyGizmoEditToBone`).

```cpp
const int32 ParentIndex = MeshAsset->Bones[SelectedBoneIndex].ParentIndex;
const FMatrix NewMeshSpace = BoneProxy->GetRelativeMatrix();
FMatrix NewLocalPose;
if (ParentIndex < 0)
    NewLocalPose = NewMeshSpace;
else
    NewLocalPose = NewMeshSpace * MeshSpace[ParentIndex].GetInverse();
TrackedMesh->SetBoneLocalPose(SelectedBoneIndex, NewLocalPose);
```

`SetBoneLocalPose` 내부에서 자동으로 `RebuildMeshSpaceBoneMatrices`가 호출되어 ([SkinnedMeshComponent.cpp:106](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp#L106)) 자식 bone들의 mesh-space도 갱신된다 → 스키닝이 자연스럽게 따라온다.

### 3-3. 동기화 정책 (피드백 루프 회피)

매 프레임 흐름:

```
┌────────────────────────────┐
│ if (Gizmo->IsHolding())    │   ← drag 진행 중
│   Gizmo->UpdateDrag(...)   │   ← gizmo가 proxy.Relative 수정
│   ApplyGizmoEditToBone()   │   ← proxy → bone 변환·기록
│ else                       │
│   SyncProxyFromBone(idx)   │   ← bone → proxy 재동기화 (외부 변경 흡수)
│   Gizmo->UpdateGizmoTransform()  ← gizmo 위치 = proxy 위치
└────────────────────────────┘
```

**핵심 invariant**: holding 중에는 proxy ← bone 방향 동기화를 **건너뛴다** ([SkeletalMeshViewerViewportClient.cpp:497-501](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L497)). 그렇지 않으면 같은 프레임 내에서 (gizmo가 쓴 proxy) → (proxy가 bone에 반영) → (다음 줄에서 bone이 다시 proxy로 덮어쓰기)가 일어나 드래그 입력이 사라진다.

드래그 종료 시 `Gizmo->DragEnd()` 직후 `SyncProxyFromBone(SelectedBoneIndex)`을 한 번 실행해 모든 drift를 정리한다 ([:481-483](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L481)).

---

## 4. Bone 선택 진입점 (두 경로, 동일 수렴점)

두 경로 모두 `FSkeletalMeshViewerViewportClient::SelectBone(int32)`로 수렴한다 — single source of truth.

### 4-1. Bone Hierarchy 트리 클릭

[EditorSkeletalMeshViewerWidget.cpp:488](../KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp#L488)의 `RenderBoneTreeNode`:

```cpp
if (ImGui::IsItemClicked() && PreviewClient)
    PreviewClient->SelectBone(BoneIndex);
```

트리에서 표시되는 `Selected` 하이라이트도 `PreviewClient->GetSelectedBoneIndex()`를 읽어 표시 — 위젯이 별도로 `SelectedBoneIndex`를 보관하지 않는다 (이전 구현에서 제거).

### 4-2. Viewport 메시 직접 클릭 (raycast)

[SkeletalMeshViewerViewportClient.cpp:423-457](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L423)의 LMB 처리:

```
LMB Press
  ├─ (a) Gizmo 핸들 raycast    FRayUtils::RaycastComponent(Gizmo, Ray, Hit)
  │       hit → SetPressedOnHandle(true), 이후 드래그 흐름 진입
  │
  └─ miss → (b) 메시 raycast   FRayUtils::RaycastTriangles(
                                    Ray, MeshWorld, MeshInvWorld,
                                    &Vertices[0].pos, sizeof(FSkeletalVertex),
                                    Indices, Hit)
                hit → ResolveBoneFromTriangle(MeshAsset, Hit.FaceIndex)
                    → SelectBone(BoneIdx)
```

**우선순위가 중요**: gizmo 핸들이 메시 위에 떠 있는 경우, 핸들 픽킹이 메시 픽킹을 가린다. 자연스러운 UX.

#### Triangle → Bone 결정 알고리즘

`FRayUtils::RaycastTriangles`는 hit triangle의 시작 인덱스를 `FHitResult.FaceIndex`에 기록 ([RayUtils.cpp:128](../KraftonEngine/Source/Engine/Collision/RayUtils.cpp#L128)). 세 정점이 가진 4-bone weight를 합산해 최대 weight의 bone을 채택 ([SkeletalMeshViewerViewportClient.cpp:210-256](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp#L210)):

```cpp
for (uint32 VI : {Indices[F], Indices[F+1], Indices[F+2]}) {
    const FSkeletalVertex& V = Vertices[VI];
    for (int j = 0; j < 4; ++j)
        if (V.BoneWeights[j] > 0.f)
            WeightByBone[V.BoneIDs[j]] += V.BoneWeights[j];
}
// argmax → BestBone
```

`FSkeletalVertex` ([SkeletalMeshAsset.h:11](../KraftonEngine/Source/Engine/Mesh/SkeletalMeshAsset.h#L11))의 `BoneIDs[4]`/`BoneWeights[4]` 구조를 활용.

---

## 5. Gizmo 라이프사이클 & 등록

`UGizmoComponent`는 Actor 없이 독립 생성 가능하도록 이미 설계되어 있다 — `SetScene(FScene*)` 진입점 보유 ([GizmoComponent.h:80](../KraftonEngine/Source/Engine/Component/GizmoComponent.h#L80)).

```cpp
// Initialize
Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
Gizmo->SetVisibility(false);

// 위젯에서 호출: PreviewWorld 생성 직후
Client->SetGizmoScene(&PreviewWorld->GetScene());
  → Gizmo->SetScene(Scene)
  → Gizmo->CreateRenderState()   ← Outer + Inner proxy 두 개 등록

// 메시 로드 시
Client->SetTrackedMesh(PreviewMeshComponent)
  → BoneProxy 생성 + Mesh 자식으로 attach

// 사용자가 bone 선택
Client->SelectBone(idx)
  → SyncProxyFromBone(idx)         ← proxy.Relative = MeshSpace[idx]
  → Gizmo->SetTarget(BoneProxy)    ← gizmo 위치/가시성 자동 갱신

// 선택 해제
Client->SelectBone(-1)
  → Gizmo->SetTarget((AActor*)nullptr)  ← SetVisibility(false)

// Shutdown
Gizmo->DestroyRenderState();
UObjectManager destroy ...
```

`FEditorRenderPipeline::RenderPreviewViewport`는 `PreviewWorld->GetScene().GetAllProxies()`를 순회해 그리므로 ([EditorRenderPipeline.cpp:209-227](../KraftonEngine/Source/Editor/EditorRenderPipeline.cpp#L209)), gizmo를 PreviewWorld의 Scene에 등록만 하면 별도 처리 없이 viewer 안에서 렌더된다.

---

## 6. Snap 설정 분리

**문제**: 기존 viewer toolbar는 메인 에디터의 전역 `FEditorSettings`를 직접 읽고 썼고 ([EditorSkeletalMeshViewerWidget.cpp 기존 L280-315]), 사이드 이펙트로 메인 에디터의 snap이 viewer 조작에 의해 바뀌었다.

**해결**: viewport client가 자체 `FViewerSnapSettings`를 멤버로 보유 ([SkeletalMeshViewerViewportClient.h:15-23](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.h#L15)):

```cpp
struct FViewerSnapSettings {
    bool  bEnableTranslationSnap = false;  float TranslationSnapSize = 0.1f;
    bool  bEnableRotationSnap    = false;  float RotationSnapSize    = 15.0f;  // degrees
    bool  bEnableScaleSnap       = false;  float ScaleSnapSize       = 0.1f;
};
```

Toolbar는 `PreviewClient->GetSnapSettings()`를 직접 편집하고, 매 프레임 `PreviewClient->ApplySnapSettingsToGizmo()`로 gizmo에 반영한다. 메인 에디터 `FEditorSettings`와 완전 독립.

또한 toolbar 시그니처를 `RenderViewerTransformToolbar(FSkeletalMeshViewerViewportClient*)`로 바꿔 (이전: `UEditorEngine*`), Translate/Rotate/Scale 토글이 viewer의 gizmo만 조작하도록 했다.

---

## 7. 입력 우선순위 & 카메라 충돌 회피

viewer는 RMB/MMB가 눌리면 카메라 제어로 진입 (`bIsCapturing = true`). 이 동안 LMB로 bone을 picking하는 것은 의도와 어긋나므로 gizmo 입력 자체를 차단:

```cpp
// SkeletalMeshViewerViewportClient.cpp
const bool bGizmoInputAllowed =
    (bViewportHovered && !bIsCapturing) ||
    (Gizmo && Gizmo->IsHolding());
```

`IsHolding()`이 true일 때만 예외적으로 통과시키는 이유 — 드래그 도중 마우스가 뷰포트를 벗어나거나 다른 키가 눌려도 **드래그 종료까지는 처리**해야 하기 때문 (메인 에디터 패턴과 동일).

휠 줌은 캡쳐와 무관하게 hover만으로 허용 (기존 동작 유지).

---

## 8. 파일별 책임

| 파일 | 책임 |
|---|---|
| [SkeletalMeshViewerViewportClient.h](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.h) | `FViewerSnapSettings` 정의, viewport client 인터페이스 |
| [SkeletalMeshViewerViewportClient.cpp](../KraftonEngine/Source/Editor/Viewport/SkeletalMeshViewerViewportClient.cpp) | Gizmo/Proxy 라이프사이클, LMB 픽킹 → 변환 → bone 갱신 모든 로직 |
| [EditorSkeletalMeshViewerWidget.cpp](../KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp) | Tree click → `SelectBone`, viewport rect 매 프레임 전달, toolbar viewer-local 결선 |
| [EditorSkeletalMeshViewerWidget.h](../KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.h) | 중복 `SelectedBoneIndex` 멤버 제거 (single source = viewport client) |
| (변경 없음) [GizmoComponent.h/cpp](../KraftonEngine/Source/Engine/Component/GizmoComponent.h) | `SetScene` + `SetTarget(USceneComponent*)`만 사용 — gizmo 자체는 bone-aware가 아님 |
| (변경 없음) [SkinnedMeshComponent.h/cpp](../KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.h) | `GetMeshSpaceBoneMatrices` + `SetBoneLocalPose`만 사용 |

---

## 9. 알려진 한계 & Follow-up

### 9-1. Viewport raycast의 bind-pose 한계

`FRayUtils::RaycastTriangles`에 전달되는 정점 좌표는 `FSkeletalVertex::pos` (**bind-pose** 좌표)다. Bone을 한 번 움직이면 렌더링된 형상과 raycast 대상 triangle이 어긋나므로, 조작 후의 viewport 클릭 정확도가 떨어진다. **Tree click은 항상 정확**한 fallback으로 남는다.

개선 옵션 (필요 시):
- (i) CPU skinning 결과를 캐시하여 raycast 시 동기화
- (ii) Per-bone bounding sphere를 현재 pose 기준으로 갱신해 sphere raycast로 대체

### 9-2. World/Local 모드의 Scale 강제

메인 에디터는 `ApplyTransformSettingsToGizmo`에서 Scale 모드일 때 강제로 local 좌표계로 전환한다 ([EditorEngine.cpp:230](../KraftonEngine/Source/Editor/EditorEngine.cpp#L230)). Viewer toolbar의 World/Local 토글은 이를 따르지 않고 사용자 의도 그대로 반영 — 의도된 차별점인지 재확인 필요.

### 9-3. Transform Panel

[EditorSkeletalMeshViewerWidget.cpp:992-1019](../KraftonEngine/Source/Editor/UI/EditorSkeletalMeshViewerWidget.cpp#L992)의 Transform Panel은 여전히 `Bone.LocalBindPose`를 표시한다 (조작 후의 현재 pose가 아님). 사용자 피드백 개선이 필요하다면 `USkinnedMeshComponent::GetLocalBonePose(int32)` getter를 추가하고 이를 표시해야 한다.

### 9-4. Ortho 뷰포트 보정

`Gizmo->ApplyScreenSpaceScaling(CamLoc, bIsOrtho, OrthoWidth)`에 viewer 카메라의 `IsOrthogonal()` / `GetOrthoWidth()`를 그대로 전달했다. Viewer는 perspective 진입이 기본이라 큰 문제는 없지만, ortho 모드에서 gizmo 크기가 어색하면 카메라의 `OrthoWidth` 초기값 조정 검토.

---

## 10. 검증 체크리스트 (실행 시 확인)

- [ ] Content Browser → FBX 더블클릭 → viewer 창 (회귀 없음)
- [ ] 좌측 Bone Hierarchy에서 bone 클릭 → 해당 위치에 gizmo 등장
- [ ] Viewport 메시 직접 클릭 → 가까운 bone 선택 + 트리 하이라이트 동기화
- [ ] Translate/Rotate/Scale 토글이 viewer gizmo만 변경 (메인 에디터 무영향)
- [ ] LMB 드래그 시 해당 bone 이동/회전/스케일 + 자식 bone들 자연스러운 추종
- [ ] 드래그 종료 후 다른 bone 선택 → gizmo 즉시 점프
- [ ] Viewer snap on/off + 값 변경이 메인 에디터 snap에 무영향 (양방향)
- [ ] RMB/MMB 카메라 캡쳐 중 LMB는 gizmo에 도달하지 않음
- [ ] 메시 변경 / 리소스 전환 시 bone 선택 해제 + gizmo 사라짐

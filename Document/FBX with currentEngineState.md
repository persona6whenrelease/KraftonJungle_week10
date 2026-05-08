# 엔진 분석 보고서: FBX 시스템 통합 및 인프라 점검

## 1. 매크로 기반 엔진 모드 전환 (`EngineLoop::CreateEngine`)

현재 `EngineLoop.cpp`는 전처리기 매크로를 활용하여 실행 환경에 따른 엔진 인스턴스를 동적으로 생성하고 있습니다.

| 매크로 | 생성 클래스 | 목적 |
| :--- | :--- | :--- |
| `IS_GAME_CLIENT` | `UGameClientEngine` | 게임 클라이언트 실행 환경 |
| `IS_OBJ_VIEWER` | `UObjViewerEngine` | 독립형 오브젝트 뷰어 환경 |
| `WITH_EDITOR` | `UEditorEngine` | 에디터 통합 환경 |

*   **인프라 확인:** `UEngine`을 상속받는 클래스 구조가 정립되어 있어, `FBX.md`에서 요구하는 에디터 내 내부 뷰어 및 독립형 뷰어 기능 분리 구현이 가능한 구조입니다.
*   **결합도:** 엔진 코어(`UEngine`)가 공통 로직을 수행하고 모드별 특수 기능은 서브클래스에서 처리하므로 낮은 결합도를 유지하고 있습니다.

---

## 2. FBX 요구사항 구현을 위한 인프라 점검 (FBX.md 대비)

### 2.1 FBX SDK 통합 및 임포터
*   **현황:** `vcpkg.json`에 FBX SDK 미포함. 현재 `ObjImporter`만 존재.
*   **분석:** 기존 `ObjImporter`의 `FStaticMesh` 생성 패턴을 활용하여 `FbxImporter` 추가가 용이함.
*   **과제:** FBX SDK의 노드 계층 구조(Node hierarchy)와 `UObject` 시스템 간의 매핑 설계 필요.

### 2.2 컴포넌트 시스템 (Skeletal Mesh)
*   **현황:** `UMeshComponent` → `UStaticMeshComponent` 구조 기구축.
*   **분석:** 요구사항인 `USkinnedMeshComponent` 및 `USkeletalMeshComponent`는 `UMeshComponent`를 상속받아 구현하기에 적합함.
*   **연동:** `PrimitiveSceneProxy` 시스템을 통한 렌더링 파이프라인 연결 구조 확보됨.

### 2.3 CPU Skinning 및 데이터 구조
*   **현황:** `UStaticMeshComponent::GetMeshDataView()`를 통한 정점 데이터 접근 지원.
*   **분석:** 현재 `FMeshBuffer`는 정적(Static) 버퍼 위주로 설계되어 있음.
*   **과제:** CPU 스키닝 결과 반영을 위한 **Dynamic Vertex Buffer** 업데이트 로직 추가 필수.

### 2.4 Binary Baking (직렬화)
*   **현황:** `UStaticMesh::Serialize` 기반 바이너리 I/O 인프라 존재.
*   **분석:** `FArchive` 시스템 확장을 통해 `.mesh`, `.skel` 등 자체 바이너리 포맷 구현 즉시 가능.

---

## 3. 주요 결합도 문제 및 위험 요소

### 3.1 렌더러 계층 결합
*   **위험 요인:** `FRenderer`와 `FMeshBuffer`가 정적 메시에 최적화되어 있음.
*   **기술적 부재:** 본(Bone) 정보 및 스키닝 행렬 전달을 위한 **Constant Buffer** 구조 미비.
*   **영향:** 향후 GPU 스키닝 확장 시 Renderer 레이어의 수정 불가피.

### 3.2 에디터 UI 의존성
*   **현황:** Bone Hierarchy Tree 및 Transform Gizmo 구현 시 `ImGui` 및 `GizmoComponent` 의존성 발생.
*   **분석:** 기구축된 `EditorMainPanel` 시스템을 활용하므로 결합도 문제는 낮을 것으로 판단됨.

---

## 4. 결론

현재 엔진은 매크로 기반 모드 분리 및 컴포넌트 구조가 안정적으로 구축되어 있어 FBX 시스템 수용이 가능합니다. 단, 다음 작업이 선행되어야 합니다.
1. FBX SDK 라이브러리 종속성 추가.
2. **Dynamic Buffer** 업데이트 인터페이스 구현.
3. 본(Bone) 계층 구조 처리를 위한 엔진 코어 데이터 구조 정의.
# FBX 및 스켈레탈 메시(Skeletal Mesh) 시스템 구현 로드맵

## 1. 필수 구현 핵심 기능 (Core Features)

### ① 동적 자원 관리 시스템 (Dynamic Resource Infra)
*   **Dynamic Mesh Buffer:** CPU 스키닝 결과를 매 프레임 GPU에 업데이트하기 위한 `D3D11_USAGE_DYNAMIC` 기반 버퍼 시스템.
*   **Bone Matrix Constant Buffer:** 수십 개의 본 행렬 데이터를 셰이더에 전달하기 위한 전용 상수 버퍼(CB) 구조.
*   **Per-Frame State Dirty 체크:** 포인터가 동일하더라도 내부 데이터가 변경되었음을 인지하고 갱신하는 `FStateCache` 확장 로직.

### ② 스켈레탈 메시 엔진 인프라 (Skeletal Mesh Infra)
*   **USkeletalMesh Asset:** 본 계층 구조(Hierarchy), 바인드 포즈(Bind Pose), 정점 가중치(Weights) 정보를 포함하는 리소스 클래스.
*   **USkeletalMeshComponent:** 월드 내 배치, 애니메이션 제어 및 `USkeletalMesh` 렌더링 담당 컴포넌트.
*   **CPU Skinning Processor:** 본 행렬과 정점 데이터를 결합하여 최종 정점 위치를 계산하는 연산 엔진.

### ③ 리소스 파이프라인 (Asset Pipeline)
*   **FBX Importer (Skeletal 전용):** FBX SDK를 활용하여 메시, 골격, 가중치 데이터를 추출하는 모듈.
*   **Binary Baker:** 런타임 성능 최적화를 위해 FBX 원본 데이터를 엔진 전용 바이너리 포맷(.mesh, .skel)으로 직렬화.

---

## 2. 단계별 할 일 (To-Do List)

### [1단계] 렌더링 인프라 확장 (가장 시급)
1.  **FMeshBuffer 수정:** `Create` 함수 내 정적/동적 모드 선택 옵션 추가 및 동적 업데이트를 위한 `Update(Map/Unmap)` 인터페이스 구현.
2.  **FDrawCommand 확장:** 애니메이션 본 데이터 전달용 `BoneCB` 슬롯(예: b4) 추가 및 `DrawCommandList` 반영.
3.  **FStateCache 업데이트:** 매 프레임 본 데이터 갱신 필요성을 판단하는 상태 플래그 로직 추가.

### [2단계] 데이터 구조 및 컴포넌트 구현
1.  **수학 구조체 정의:** 본 행렬 연산을 위한 `Matrix4x4` 및 `Vector3` 연산 기능 보강.
2.  **Skeletal Mesh 관련 클래스 생성:**
    *   `USkinnedMeshComponent`: 스키닝 기능의 기본 베이스 클래스.
    *   `USkeletalMeshComponent`: 애니메이션 틱 처리 및 드로우 콜 생성.
3.  **CPU Skinning 로직 작성:** 정점 변환 알고리즘 및 가중치 연산 함수 구현.

### [3단계] 리소스 파이프라인 구축 (FBX SDK 연동)
1.  **FBX SDK 통합:** 외부 라이브러리 링크 및 개발 환경 구축.
2.  **FBX Importer 고도화:** `ObjImporter` 패턴을 계승하여 스켈레탈 데이터 추출 로직 구현.
3.  **직렬화(Baking) 구현:** `FArchive`를 활용한 데이터 바이너리 저장 및 로드 기능 추가.

### [4단계] 에디터 및 시각화 기능
1.  **Skeletal Mesh Viewer:** 본 구조 시각화가 가능한 전용 뷰어 패널 제작.
2.  **Bone Hierarchy Tree:** `ImGui`를 활용한 본의 부모-자식 관계 트리 UI 구현.
3.  **Gizmo 연동:** `SelectionManager`와 `GizmoComponent`를 연결하여 개별 본의 트랜스폼 조작 기능 구현.

---

## 3. 성공적인 구현을 위한 기술적 제언

*   **선형 배열(Linear Array) 활용:** 런타임 연산 시 복잡한 계층 구조 순회를 피하기 위해 모든 본을 인덱스로 접근 가능한 평평한 배열로 관리하여 스키닝 속도를 극대화하십시오.
*   **병목 지점 관리:** CPU 스키닝은 정점 수에 비례하여 부하가 급증합니다. 초기에는 동작 검증에 집중하되, 향후 Vertex Shader 기반의 GPU 스키닝으로 전환이 용이하도록 파이프라인의 유연성을 확보하는 것이 중요합니다.
# Skeletal Mesh 시스템 구현 로드맵 (Version 2 - 인프라 통합 이후)

## 0. 현재 상태 요약 (Current Status)
*   **렌더링 파이프라인 (완료)**: `FGPUGeometryView` 도입으로 `DrawCommandBuilder`와 `SceneProxy` 간의 다형성 통합 완료. 정적/동적 메시 공통 경로 사용.
*   **컴포넌트 인프라 (완료)**: `USkinnedMeshComponent` -> `USkeletalMeshComponent` 상속 구조 및 데이터 관리 체계 확립.
*   **연산 엔진 (완료)**: `UpdateSkinning()`을 통한 CPU Skinning 및 `FDynamicVertexBuffer`를 통한 GPU 업로드 로직 가동 준비 완료.

---

## 1. 완료 및 설계 변경 항목 (Completed & Obsolete)

### [완료] 필수 구현 핵심 기능
*   [x] **Dynamic Mesh Buffer**: `FDynamicVertexBuffer` 구현 및 프록시 연동 완료.
*   [x] **Geometry View 다형성**: `GetGeometryView()`를 통한 렌더러 디커플링 완료.
*   [x] **CPU Skinning Processor**: 본 가중치 기반 정점 변환 알고리즘 구현 완료.

### [삭제/보류] 설계 변경으로 불필요해진 항목
*   [-] **Bone Matrix Constant Buffer (Todo 1-2)**: 현재 CPU Skinning 방식에서는 정점이 이미 변환되어 전달되므로 셰이더용 본 CB가 불필요함. (GPU Skinning 전환 시 재도입 예정)
*   [-] **FStateCache 확장 (Todo 1-3)**: `MAP_WRITE_DISCARD` 방식의 동적 업데이트를 사용하므로 별도의 데이터 변경 감지 로직이 필요 없음.

---

## 2. 향후 단계별 할 일 (To-Do List)

### [1단계] 리소스 파이프라인 고도화 (현재 최우선)
1.  **FBX Skeletal Importer**: `ObjImporter`를 확장하여 FBX SDK로부터 본 구조(Bone Hierarchy)와 정점 가중치(Vertex Weights)를 추출하는 로직 완성.
2.  **Binary Serialization**: 추출된 스켈레탈 데이터를 엔진 전용 바이너리 포맷(`.skel`)으로 저장하고 로드하는 `FArchive` 확장.
3.  **애니메이션 데이터 구조 정의**: 본별 키프레임 데이터를 담는 전용 에셋 타입 정의 및 로드 로직.

### [2단계] 에디터 및 시각화 (사용성 개선)
1.  **Bone Debug Drawing**: 프록시에서 본의 위치를 라인(Line)으로 그려주는 디버그 시각화 기능 (`FPrimitiveSceneProxy::UpdatePerViewport` 활용).
2.  **Bone Hierarchy Tree UI**: `ImGui`를 사용하여 에디터 상세 패널에서 본 구조를 트리 형태로 표시.
3.  **Skeleton Viewer**: 에셋 브라우저에서 스켈레탈 메시만 단독으로 확인하고 포즈를 취해볼 수 있는 뷰어 모드.

### [3단계] 성능 최적화 및 기능 확장 (고도화)
1.  **CPU Skinning SIMD 최적화**: 수만 개의 정점 연산을 가속하기 위해 SSE/AVX 명령어를 활용한 벡터 연산 적용.
2.  **GPU Skinning 전환 (옵션)**: 정점 수가 매우 많은 메시를 위해 본 행렬을 Constant Buffer로 넘기고 Vertex Shader에서 스킹을 수행하는 모드 추가.
3.  **Animation Blending**: 두 개 이상의 애니메이션 포즈를 선형 보간(LERP)하여 부드러운 동작 전환 구현.

---

## 3. 기술적 제언 및 주의사항

*   **메모리 관리**: `FDynamicVertexBuffer`는 매 프레임 재할당을 피하기 위해 `EnsureCapacity`를 적극 활용하십시오.
*   **데이터 정합성**: FBX에서 가져온 본 인덱스와 컴포넌트의 `ComponentSpaceMatrices` 인덱스가 완벽히 일치하도록 임포트 시점에 인덱스 맵핑 테이블을 정교하게 구축해야 합니다.

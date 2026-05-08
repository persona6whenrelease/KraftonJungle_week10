# FBX 시스템 구축 프로젝트: 팀별 과제 및 현실성 분석 보고서

## 1. 과제 현실성 점검 (Feasibility Check)

*   **아키텍처 부합성:** 현재 엔진의 `UWorld - AActor - UActorComponent` 체계와 `FDrawCommand` 기반 렌더링 파이프라인은 Skeletal Mesh 컴포넌트를 추가하기에 최적화된 구조입니다.
*   **기술적 난이도:** 초기 구현 단계를 **CPU Skinning**으로 설정하여 GPU 셰이더 복잡도를 낮춘 것은 논리 검증 측면에서 매우 현실적인 선택입니다.
*   **핵심 변수:** A팀의 `FMeshBuffer` 동적 확장 인터페이스 확정 여부가 전체 프로젝트 병렬 프로세스의 성패를 결정하는 임계 경로(Critical Path)입니다.

---

## 2. 파트별 핵심 점검 사항

### 팀 A: 렌더링 인프라 (Rendering Infra)
> **목표: 데이터 통로 개척**
*   **기술 스택:** D3D11 `ID3D11Buffer` 플래그(`D3D11_USAGE_DYNAMIC`, `D3D11_CPU_ACCESS_WRITE`), `Map/Unmap` 매커니즘.
*   **핵심 로직:** `FDrawCommand` 정렬 후 `FStateCache`에서 동적 버퍼의 데이터 변경을 인지할 수 있는 **'Dirty' 플래그** 처리 로직 반영.

### 팀 B: 컴포넌트 & 스키닝 (Component & Skinning)
> **목표: 변환 로직의 심장 구현**
*   **기술 스택:** `InverseBindPose` 수학적 연산, 행렬 곱셈 순서(Row/Column-major), 가중치 합산 알고리즘.
*   **연동 필수:** `TickComponent` 내 스키닝 연산 결과물을 A팀의 `FMeshBuffer::Update`로 전달하는 실시간 루프 동기화.

### 팀 C: FBX SDK & 파이프라인 (Asset Pipeline)
> **목표: 데이터 공급망 구축**
*   **기술 스택:** `FbxScene` 트리 순회, `Control Points` 및 `Deformer` 정보 추출 API.
*   **데이터 가공:** FBX의 복잡한 계층 구조를 **선형 배열(Linear Array)**로 재구성(Flattening)하고, `FArchive` 기반의 엔진 전용 바이너리 규격 확립.

### 팀 D: 에디터 & 에셋 설계 (Editor & Asset)
> **목표: 사용자 인터페이스 및 데이터 스키마**
*   **기술 스택:** `ImGui` 트리 노드, `FSelectionManager` 내 본(Bone) 단위 등록, `UObject` 직렬화.
*   **우선순위:** `USkeletalMesh` 에셋 클래스의 멤버 변수 구조를 최우선으로 확정하여 타 팀의 병렬 작업 가이드라인 제공.

---

## 3. 프로젝트 전반 제언

| 구분 | 주요 내용 |
| :--- | :--- |
| **성능 병목** | CPU 스키닝 부하를 고려하여 초기 테스트는 저사양 모델(예: Chicken.bin)로 진행 권장. |
| **자원 할당** | `FDrawCommand` 내 본 행렬 전용 상수 버퍼 슬롯 **b4**(최대 70~100개 본) 확보 선행 필요. |
| **병렬 워크플로우** | **Day 1:** 에셋 스키마 확정(D팀) 및 동적 버퍼 인터페이스 선언(A팀) 완료 필수. |

## 결론
본 프로젝트는 **A팀의 인프라 준비**와 **D팀의 데이터 스키마 확정**이 동시 수행된다는 전제하에, 4인 병렬 작업을 통한 높은 완성도의 FBX 시스템 구축이 가능할 것으로 판단됩니다.
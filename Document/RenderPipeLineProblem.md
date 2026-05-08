# FBX 시스템 통합 분석 보고서: 정적 파이프라인의 한계와 개선 방향

## 1. 핵심 문제점: 정적 자원 중심 파이프라인의 경직성

현재 엔진은 `FDrawCommand`와 `FStateCache`를 필두로 **"상태 변화 최소화"**를 지향하는 정적 메시 최적화에 특화되어 있습니다. 이는 매 프레임 데이터가 변하는 **Skeletal Mesh(FBX)** 도입 시 다음과 같은 기술적 충돌을 야기합니다.

*   **Dynamic Vertex Buffer 인프라 부재:** `CPU Skinning` 구현을 위해서는 CPU에서 연산된 정점 데이터를 GPU로 전송해야 하지만, 현재 `FMeshBuffer`는 초기화 시 데이터가 고정되는 정적 구조입니다. 매 프레임 `Map/Unmap` 처리가 가능한 동적 버퍼 관리 로직의 부재가 확인됩니다.
*   **DrawCommand 구조의 확장성 한계:** 현재 `FDrawCommand`는 `PerObjectCB(b1)`, `PerShaderCB(b2, b3)` 슬롯만 제공합니다. 애니메이션을 위한 대량의 본 행렬(Bone Matrices) 데이터를 효율적으로 전달할 상수 버퍼 슬롯이나 `Structured Buffer` 등의 구조가 누락되어 있습니다.
*   **상태 캐싱(FStateCache) 논리 충돌:** `FStateCache`는 자원 포인터의 동일 여부로 바인딩을 생략합니다. 애니메이션은 포인터가 같더라도 내부 데이터(본 행렬 등)가 매 프레임 갱신되므로, 이를 인지할 수 있는 정교한 **'Per-Frame Dirty'** 체크 로직이 필수적입니다.

---

## 2. 구현 시 주요 고려 사항

### 2.1 자원 생명주기 및 동적 업데이트 전략
*   **CPU Skinning 성능 최적화:** 수천 개의 정점을 CPU에서 매 프레임 연산하고 복사하는 병목을 줄여야 합니다. `D3D11_USAGE_DYNAMIC` 및 `Discard` 방식을 지원하도록 `FMeshBuffer`를 확장해야 합니다.
*   **멀티 버퍼링 전략:** CPU의 쓰기 작업과 GPU의 읽기 작업 간의 자원 경합(Resource Contention)을 방지하기 위해 **Double/Triple Buffering** 도입이 검토되어야 합니다.

### 2.2 애니메이션 데이터와 인스턴싱의 결합
*   **정렬 효율 저하:** Skeletal Mesh는 동일 메시라도 인스턴스마다 애니메이션 포즈가 달라 `FStateCache`에 의한 병합 효율이 낮습니다.
*   **데이터 인스턴싱:** 본 데이터를 `Texture Buffer`나 `Structured Buffer`에 적재하여, 개별 본 정보 교체 비용을 최소화하는 설계가 장기적으로 요구됩니다.

### 2.3 FBX 계층 구조와 엔진 UObject 시스템 매핑
*   **구조적 오버헤드:** FBX SDK의 복잡한 노드 계층을 `USceneComponent`와 1:1로 매핑하는 것은 런타임 성능에 불리합니다.
*   **런타임 최적화 구조:** 임포트(Baking) 단계에서 계층 구조를 **선형 배열(Linear Array)** 형태의 본 인덱스 구조로 재구성하여, 런타임에는 인덱스 참조만으로 스키닝이 가능하도록 설계해야 합니다.

### 2.4 에디터 통합 및 기즈모 조작
*   **선택 시스템 확장:** 현재 `GizmoComponent` 인프라는 액터 단위 조작에 최적화되어 있습니다. 액터 내부의 특정 '본(Bone)'을 클릭하여 선택하고 기즈모를 부착할 수 있도록 `SelectionManager`의 확장이 수반되어야 합니다.

---

## 요약 및 제언

현재의 `RenderPass/DrawCommand` 체계는 정적 데이터 처리에 매우 견고하지만, 동적 데이터 대응력은 보완이 시급합니다. FBX 시스템 통합의 최우선 과제는 **FMeshBuffer의 동적 업데이트 인터페이스 구축**과 **FDrawCommand 내 애니메이션 전용 데이터 슬롯 확보**가 될 것입니다.
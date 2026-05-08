# KraftonEngine Class Structure & Logic

이 문서는 KraftonEngine의 핵심 클래스 구조, 엔진의 월드/컴포넌트 관리 방식, 그리고 에디터 엔진의 동작 및 PIE(Play In Editor) 로직을 설명합니다.

## 1. Engine의 World 및 Component 관리 구조

엔진은 계층적인 구조를 통해 월드와 그 내부의 오브젝트(Actor) 및 기능 단위(Component)를 관리합니다.

- **UEngine**: 최상위 관리 클래스로, 하나 이상의 `FWorldContext`를 소유합니다.
- **FWorldContext**: `UWorld` 인스턴스와 그 타입(`Editor`, `Game`, `PIE`), 고유 핸들을 묶어서 관리합니다. 이를 통해 엔진은 여러 월드를 동시에 로드하거나 전환할 수 있습니다.
- **UWorld**: 시뮬레이션이 일어나는 실제 공간입니다. 내부적으로 `ULevel`(PersistentLevel)을 가지며, `FScene`(렌더링 프록시 관리)과 `FSpatialPartition`(공간 분할/충돌)을 소유합니다.
- **AActor**: 월드에 배치될 수 있는 기본 단위입니다. 여러 `UActorComponent`를 소유하며, 그 중 하나를 `RootComponent`로 설정하여 위치 정보를 가집니다.
- **UActorComponent**: 액터의 재사용 가능한 기능 단위입니다. `USceneComponent`는 트랜스폼을 가지며, `UPrimitiveComponent`는 렌더링 및 충돌 데이터(SceneProxy 등)를 생성합니다.

**Tick 흐름:**
`UEngine::Tick` -> `UEngine::WorldTick` -> 각 월드의 `UWorld::Tick` -> `AActor::Tick` -> `UActorComponent::TickComponent` 순으로 로직이 업데이트됩니다.

## 2. UEditorEngine과 UEngine의 관계 및 역할

| 클래스 | 역할 | 주요 특징 |
| :--- | :--- | :--- |
| **UEngine** | 런타임 코어 | 윈도우 생성, 렌더러 초기화, 월드 컨텍스트 관리 등 일반적인 엔진 수명 주기 담당 |
| **UEditorEngine** | 에디터 확장 | `UEngine`을 상속받아 기즈모(`UGizmoComponent`), 선택 관리(`FSelectionManager`), 에디터 UI(`MainPanel`), 뷰포트 레이아웃 관리 기능을 추가 |

**프로그램 실행 시 역할:**
- **UEngine**: 엔진 루프(`FEngineLoop`)에서 호출되어 기본적인 시스템(입력, 렌더링, 시간)을 유지합니다.
- **UEditorEngine**: 사용자가 에디터 상에서 액터를 배치, 선택, 수정할 수 있는 환경을 제공하며, 현재 편집 중인 "에디터 월드"를 관리합니다.

## 3. PIE (Play In Editor) 동작 Logic

`UEditorEngine`은 에디터 내에서 게임을 즉시 실행해볼 수 있는 PIE 기능을 관리합니다.

1.  **세션 요청**: `RequestPlaySession`을 통해 실행 요청이 큐에 쌓입니다.
2.  **세션 시작**: 다음 프레임 Tick에서 `StartQueuedPlaySessionRequest`가 호출되어 `StartPlayInEditorSession`을 실행합니다.
3.  **월드 복제**: 현재 편집 중인 에디터 월드를 `UWorld::DuplicateAs(EWorldType::PIE)`를 통해 복제합니다. 이는 편집 데이터에 영향을 주지 않는 독립적인 샌드박스를 생성하기 위함입니다.
4.  **제어 모드**:
    *   **Possessed**: 사용자가 게임 캐릭터나 카메라를 직접 조작하는 상태.
    *   **Ejected**: 게임은 돌아가고 있지만, 사용자가 에디터 카메라로 빠져나와 객체를 디버깅하거나 수정할 수 있는 상태.
5.  **종료**: `EndPlayMap` 호출 시 PIE 월드를 정리하고 다시 에디터 월드로 포커스를 복구합니다.

## 4. Overall Class Diagram (Mermaid)

```mermaid
classDiagram
    class UObject {
        <<Base>>
    }

    class UEngine {
        +WorldList: TArray~FWorldContext~
        +ActiveWorldHandle: FName
        +Init(Window)
        +Tick(DeltaTime)
        +CreateWorldContext()
    }

    class UEditorEngine {
        +SelectionManager: FSelectionManager
        +MainPanel: FEditorMainPanel
        +ViewportLayout: FLevelViewportLayout
        +RequestPlaySession()
        +StartPlayInEditorSession()
    }

    class UWorld {
        +PersistentLevel: ULevel*
        +Scene: FScene
        +WorldType: EWorldType
        +Tick(DeltaTime)
        +SpawnActor()
    }

    class FWorldContext {
        +World: UWorld*
        +Type: EWorldType
        +Handle: FName
    }

    class AActor {
        +RootComponent: USceneComponent*
        +OwnedComponents: TArray~UActorComponent*~
        +Tick(DeltaTime)
        +AddComponent()
    }

    class UActorComponent {
        +Owner: AActor*
        +TickComponent()
        +CreateRenderState()
    }

    class USceneComponent {
        +Location: FVector
        +Rotation: FRotator
        +Scale: FVector
    }

    class UPrimitiveComponent {
        +SceneProxy: FPrimitiveSceneProxy*
    }

    UObject <|-- UEngine
    UEngine <|-- UEditorEngine
    UObject <|-- UWorld
    UObject <|-- AActor
    UObject <|-- UActorComponent
    UActorComponent <|-- USceneComponent
    USceneComponent <|-- UPrimitiveComponent

    UEngine "1" *-- "many" FWorldContext : manages
    FWorldContext "1" o-- "1" UWorld : points to
    UWorld "1" *-- "1" ULevel : contains
    ULevel "1" *-- "many" AActor : contains
    AActor "1" *-- "many" UActorComponent : owns
    AActor "1" o-- "1" USceneComponent : RootComponent

## 6. Runtime Delegate & Event System Architecture (Proposed)

현재 프로젝트에 런타임 이벤트 관리를 위한 Delegate 시스템 도입을 위한 설계입니다.

### 6.1. 설계 원칙
1. **중앙 집중형 관리 (Engine-side)**: `UEngine`은 전역적인 이벤트 버스(Global Event Bus) 역할을 수행하는 `FGlobalEventManager`를 소유합니다. 이를 통해 월드 간 경계를 넘나드는 이벤트나 전역적인 모니터링이 가능해집니다.
2. **분산형 구독 (Component-side)**: 각 `UActorComponent`는 자신만의 고유한 `TMulticastDelegate` 멤버를 가집니다. 특정 컴포넌트의 변화에만 관심이 있는 객체는 해당 컴포넌트에 직접 구독합니다.
3. **유연한 연결**: `std::function` 또는 함수 포인터를 활용하여 컴파일 타임 의존성을 줄이고 런타임에 동적으로 이벤트를 연결합니다.

### 6.2. UEngine의 역할 (Event Handler)
`UEngine`이 이벤트 처리자를 관리하는 것이 적절한 이유는 다음과 같습니다:
- **Global Visibility**: `GEngine` 싱글턴을 통해 어디서든 접근 가능하여 시스템 간 통신 허브 역할을 할 수 있습니다.
- **Lifecycle Sync**: 엔진의 수명 주기(`Init` ~ `Shutdown`)와 연동되어 이벤트 시스템의 안정적인 생성 및 소멸을 보장합니다.
- **Cross-World Communication**: 에디터 월드와 PIE 월드 간의 통신이나, 전역적인 시스템 알림(예: `OnResourceLoaded`)을 처리하기에 최적의 위치입니다.

### 6.3. 예상 구조 (Mermaid)
```mermaid
classDiagram
    class UEngine {
        +FGlobalEventManager EventManager
    }
    class FGlobalEventManager {
        +TMap~FName, FMulticastDelegate~ GlobalEvents
        +Broadcast(EventName, Args)
        +Subscribe(EventName, Callback)
    }
    class UActorComponent {
        +TMulticastDelegate OnComponentEvent
    }
    class TMulticastDelegate~Args~ {
        +TArray~Callback~ Callbacks
        +Broadcast(Args)
        +Add(Callback)
    }

    UEngine *-- FGlobalEventManager : 소유 및 관리
    UActorComponent *-- TMulticastDelegate : 개별 이벤트 소유
```

## 7. New Class File Diagram (Delegate 포함)

```text
KraftonEngine/Source/Engine/
├── Core/
│   ├── Delegate.h               // TDelegate, TMulticastDelegate 템플릿 정의
│   └── EventManager.h (.cpp)    // FGlobalEventManager 정의
├── Runtime/
│   └── Engine.h (.cpp)          // EventManager 멤버 추가 및 초기화
└── Component/
    └── ActorComponent.h         // 기본 Delegate 멤버 추가 예시
```
```

## 5. Class File Diagram

```text
KraftonEngine/Source/
├── Engine/
│   ├── Runtime/
│   │   ├── Engine.h (.cpp)          // UEngine 정의
│   │   └── EngineLoop.h (.cpp)      // 메인 루프 진입점
│   ├── GameFramework/
│   │   ├── World.h (.cpp)           // UWorld 정의
│   │   ├── WorldContext.h           // FWorldContext, EWorldType 정의
│   │   ├── Level.h (.cpp)           // ULevel 정의
│   │   └── AActor.h (.cpp)          // AActor 정의
│   └── Component/
│       ├── ActorComponent.h (.cpp)  // UActorComponent 정의
│       ├── SceneComponent.h (.cpp)  // USceneComponent 정의
│       └── PrimitiveComponent.h     // UPrimitiveComponent 정의
└── Editor/
    ├── EditorEngine.h (.cpp)        // UEditorEngine 정의
    ├── PIE/
    │   └── PIETypes.h               // PIE 관련 구조체 정의
    ├── Selection/
    │   └── SelectionManager.h       // FSelectionManager 정의
    └── Viewport/
        └── FLevelViewportLayout.h   // 에디터 뷰포트 관리
```

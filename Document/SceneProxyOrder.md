1. 설계 결정: 상속 구조 결정: FSkeletalMeshSceneProxy는 FPrimitiveSceneProxy를 상속받아야 합니다.
- 이유: 현재 엔진의 Renderer 및 DrawCommandBuilder는 FPrimitiveSceneProxy 인터페이스를 통해 모든 그리기 대상을 관리합니다. 이를 상속받지 않으면 기존의 렌더링 파이프라인(가시성 판단, 섀도우 맵 생성, 오패크 패스 등)을 완전히 새로 짜야 하는 비효율이 발생합니다.
- --

2. 구현 명세서

[A] 동적 버퍼 활용 (FDynamicVertexBuffer)
  SkeletalMesh의 CPU Skinning 결과물은 매 프레임 위치가 변하므로, 기존에 구현된 `FDynamicVertexBuffer`를 활용합니다.
  * FVertexBuffer를 수정하는 대신, `FDynamicVertexBuffer`의 `Update` 및 `EnsureCapacity` 기능을 사용하여 효율적으로 GPU에 데이터를 업로드합니다.
  * 정적 버퍼(`FVertexBuffer`)는 기존의 정적 메시용으로 유지하여 안정성을 확보합니다.

[B] FSkeletalMeshSceneProxy (명세)
  USkeletalMeshComponent의 데이터를 받아 실제 렌더링 명령을 준비하는 클래스입니다.

* 상속: public FPrimitiveSceneProxy
* 소유 데이터:
  * FDynamicVertexBuffer InternalVB: CPU Skinning된 정점 데이터를 담는 동적 버퍼.
  * FIndexBuffer InternalIB: 에셋의 인덱스 데이터를 담는 정적 버퍼 (인덱스는 변하지 않음).
  * TArray<FMeshSectionDraw> SectionDraws: 에셋의 섹션 정보를 기반으로 한 드로우 호출 정보.
* 주요 함수:
  1. Constructor: 컴포넌트로부터 초기 메시 데이터를 받아 InternalIB를 초기화하고 InternalVB의 초기 용량을 설정.
  2. UpdateMesh (Override): 컴포넌트에서 계산된 스킨닝 정점(SkinnedVertices)을 전달받아 InternalVB.Update() 호출.
  3. UpdateMaterial (Override): 컴포넌트의 머티리얼 설정을 프록시로 동기화.
* 렌더링 흐름:
  * DrawCommandBuilder가 이 프록시의 InternalVB와 InternalIB를 참조하여 드로우 콜을 생성합니다.

[C] USkeletalMeshComponent와의 연동 (명세)
   * TickComponent 등에서 CPU Skinning 연산(UpdateSkinning) 수행.
   * 연산이 끝나면 MarkProxyDirty(EDirtyFlag::Mesh)를 호출하여 렌더링 스레드에 갱신 알림.
   * 렌더링 스레드에서 프록시의 UpdateMesh가 호출될 때 최종 계산된 정점을 GPU로 전송.

- --

3. 작업 순서 제안
   1. FDynamicVertexBuffer를 이용
   2. FSkeletalMeshSceneProxy 구현: 헤더 및 CPP 작성.
   3. USkeletalMeshComponent 연동: 프록시 생성 및 업데이트 로직 연결.

위 명세서 내용이 의도와 일치하는지 확인 부탁드립니다. 승인해 주시면 단계별로 구현을 시작하겠습니다.
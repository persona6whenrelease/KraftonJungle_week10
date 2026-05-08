## ✦ Skeletal Mesh System 구현 가이드라인 (전체 통합본)

### 1. 상속 구조 설계
제안하신 계층 구조를 기반으로 역할을 분담합니다.

1. **UPrimitiveComponent** (기본 물리/렌더링 인터페이스)
2. **└── UMeshComponent** (메시 공통 로직)
3. **    └── USkinnedMeshComponent** (스킨닝 공통 데이터 관리: 본 행렬, 포즈 등)
4. **        └── USkeletalMeshComponent** (에셋 연동, CPU Skinning 계산, 프록시 생성)

---

### 2. USkinnedMeshComponent 가이드라인 (데이터 관리)
이 클래스는 특정 에셋에 의존하기보다, 스킨닝된 메시들이 공통으로 가져야 할 "현재 상태(포즈)"를 관리합니다. Unreal Assistant의 조언(LocalTransforms와 ComponentSpaceMatrices의 분리)은 애니메이션 시스템의 정석적인 설계 방식이며, 우리 프로젝트에도 매우 적합하고 필수적인 구조입니다.

#### **핵심 멤버 및 역할 구분**
*   **TArray<FTransform> LocalTransforms (상대 포즈)**
    *   **의미**: 부모 뼈를 기준으로 한 현재 뼈의 위치/회전/스케일입니다.
    *   **용도**: 애니메이션 블렌딩(Blending)이나 IK(Inverse Kinematics) 연산 시 사용합니다.
    *   **장점**: 팔꿈치를 회전시키면 하위의 손목과 손가락이 자동으로 따라가게 설계하기 매우 쉽습니다. (부모-자식 계층 구조 유지)
*   **TArray<FMatrix> ComponentSpaceMatrices (최종 행렬)**
    *   **의미**: 모델의 루트(Root)를 기준으로 한 각 뼈의 최종 월드(컴포넌트) 좌표계 행렬입니다.
    *   **용도**: 실제 CPU Skinning 계산에 직접 사용됩니다.
    *   **계산 방식**: 부모의 ComponentSpaceMatrix와 자신의 LocalTransform을 곱하여 계층적으로 계산합니다.
*   **Inverse Bind Pose (IBP) (추가 핵심 요소)**
    *   **이유**: 정점 데이터는 처음에 "T-Pose" 상태의 로컬 좌표로 저장되어 있습니다. 이 정점을 뼈의 움직임에 맞추려면 [정점을 뼈의 원점으로 이동] → [뼈의 애니메이션 행렬 적용] 과정을 거쳐야 하는데, 이때 "뼈의 원점으로 이동"시키는 행렬이 바로 Inverse Bind Pose입니다.

#### **헤더 구조 예시**

    class USkinnedMeshComponent : public UMeshComponent
    {
        DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

    protected:
        // 1. 애니메이션/포즈 제어용 (상대 좌표)
        TArray<FTransform> LocalTransforms;

        // 2. 실제 스킨닝 계산용 (루트 기준 최종 행렬)
        // 연산식: Vertex * InverseBindPose[i] * ComponentSpaceMatrices[i]
        TArray<FMatrix> ComponentSpaceMatrices;

        // 3. 에셋에서 가져온 역 바인드 포즈 (상수 데이터)
        // 보통 USkeletalMesh 에셋 안에 들어있어야 함
        // virtual const TArray<FMatrix>& GetInverseBindPoses() const = 0;

    public:
        // 핵심 함수: 애니메이션 또는 포즈 데이터를 바탕으로 행렬을 갱신하는 가상 함수.
        virtual void UpdateBoneMatrices();
    };

---

### 3. USkeletalMeshComponent 가이드라인 (실행 및 렌더링)
실제 USkeletalMesh 에셋을 들고 있으며, CPU Skinning 연산을 수행합니다.

#### **핵심 멤버:**
*   **USkeletalMesh* SkeletalMesh**: 사용할 스켈레탈 메시 에셋.
*   **TArray<FSkeletalMeshVertex> SkinnedVertices**: CPU 연산 결과가 담길 정점 배열.
*   **FDynamicVertexBuffer DynamicVB**: 갱신된 정점을 GPU로 쏠 동적 버퍼 (또는 프록시 내부에서 관리).

#### **CPU Skinning 로직 (TickComponent 또는 UpdateSkinning):**
1.  에셋의 원본 정점(FSkeletalMeshVertex) 순회.
2.  각 정점의 BoneIndices와 BoneWeights를 이용해 ComponentSpaceMatrices 적용.
3.  SkinnedVertices에 결과값 저장.
4.  SceneProxy를 통해 GPU 버퍼 갱신 요청.

#### **Override 필수 함수:**
*   **CreateSceneProxy()**: FSkeletalMeshSceneProxy를 생성하여 반환.
*   **GetEditableProperties()**: 에디터에서 메시 파일을 선택할 수 있도록 구현.
*   **Serialize()**: 배치된 컴포넌트의 설정값 저장.

---

### 4. 렌더링 연동 가이드 (Proxy)
CPU Skinning의 경우, 프록시는 매 프레임 데이터가 변하는 동적 메시로 동작해야 합니다.

#### **FSkeletalMeshSceneProxy:**
*   StaticMeshSceneProxy와 달리 FDynamicVertexBuffer를 소유.
*   컴포넌트에서 계산이 끝나면 UpdateDynamicBuffer(SkinnedVertices)를 호출받아 GPU로 전송.

---

### 5. 구현 시 주의사항 (프로젝트 컨벤션)
*   **DECLARE_CLASS 매크로**: 프로젝트의 리플렉션/타입 시스템을 위해 반드시 클래스 상단에 추가해야 합니다.
*   **FMeshDataView**: 현재 엔진은 picking이나 물리 처리를 위해 GetMeshDataView()를 사용합니다. 스킨닝된 결과에 맞춰 이 뷰를 반환할 수 있도록 고려해야 합니다.
*   **성능**: CPU Skinning은 정점 개수가 많으면 무거우므로, 나중에 SIMD나 병렬 처리를 도입할 수 있도록 루프 구조를 깔끔하게 짜는 것이 좋습니다.
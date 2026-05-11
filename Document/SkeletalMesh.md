# Skeletal Mesh & Bone Configuration 상태 보고서

## 1. 현재 상황 요약
현재 KraftonEngine의 Skeletal Mesh 시스템은 행렬 연산 체계(Row-Major)와 좌표계(Left-Handed, Z-Up)를 맞추는 작업을 진행했으나, **바인드 포즈와 엔진 런타임 포즈 간의 수학적 불일치**로 인해 메시가 정상적으로 출력되지 않는 상태입니다.

### 핵심 문제: Bind Pose Identity Test 실패
- **로그 결과**: `Bone[0] Matrix Error - Row3 (Translation): 0.0000, -0.8444, 5.8700`
- **원인 분석**:
    1. **Pose Mismatch**: `EvaluateLocalTransform(0)`으로 가져온 초기 포즈와 FBX Cluster에 기록된 바인드 시점의 본 위치가 서로 다름.
    2. **Origin Mismatch**: 메시 노드의 글로벌 변환과 본 계층 구조의 루트 변환이 동일한 기준점(World Origin)을 공유하지 않음.
    3. **Hierarchy Gap**: Blender에서 FBX 추출 시 본이 아닌 일반 노드(Empty 등)가 계층 구조 사이에 섞여 있어, 엔진이 이를 건너뛸 때 변환 정보가 유실됨.

---

## 2. 주요 확인 및 수정 파일 (Relevant Files)

### 1) SkeletalMesh 관련 핵심 로직
- `KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.cpp`
    - FBX 데이터 추출, 좌표계 변환(`ConvertScene`), IBP 계산 담당.
- `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp`
    - `UpdateSkinning` 로직 및 최종 스키닝 행렬 계산(`IBP * CS`) 담당. 현재 Identity Test 로그가 위치함.
- `KraftonEngine/Source/Engine/Component/SkinnedMeshComponent.cpp`
    - 본 계층 구조 합성(`RecalcComponentSpaceMatrices`) 담당.

### 2) 수학 및 정점 정의
- `KraftonEngine/Source/Engine/Math/Matrix.h / .cpp`
    - Row-Major 행렬 연산 및 `IsIdentity()` 등 검증 함수 포함.
- `KraftonEngine/Source/Engine/Render/Types/VertexTypes.h`
    - `FSkeletalMeshVertex` 및 `FBone` 구조체 정의.

### 3) 에셋 구조
- `KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMesh.h / .cpp`
    - USkeletalMesh 에셋 클래스 및 본 이름 매핑 관리.
- `KraftonEngine/Source/Engine/SkeletalMesh/SkeletalMeshAsset.h`
    - `FSkeletalMesh` 원본 데이터 구조체 정의.

---

## 3. 기수행된 수정 사항 (Applied Fixes)
1. **Transpose 제거**: `FbxMatrixToFMatrix`에서 불필요한 전치(Transpose)를 제거. 실측 결과 FBX(FbxAMatrix)와 엔진(FMatrix) 모두 Row 3에 Translation을 저장하는 동일한 메모리 레이아웃을 사용함을 확인.
2. **연산 순서 교정**: 
    - 계층 구조: `Local * Parent`
    - 스키닝: `InverseBind * ComponentSpace`
3. **데이터 초기화**: `FSkeletalMeshVertex` 초기화 누락 수정 (가중치 쓰레기 값 방지).
4. **좌표계 변환**: `FbxAxisSystem`을 통해 **Z-Up, Left-Handed, X-Forward**로 씬 자동 변환 적용.

---

## 4. 향후 과제 (Next Actions)
1. **바인드 포즈 역산**: `EvaluateLocalTransform` 대신 `TransformLinkMatrix`를 부모의 글로벌 행렬로 나누어 완벽한 바인드 시점의 `LocalTransform`을 강제 주입.
2. **계층 구조 전수 조사**: `eSkeleton` 속성 외에 중간에 섞인 노드들의 트랜스폼을 누적해서 처리할 수 있도록 `GatherJoints` 및 `ProcessNode` 로직 개선.
3. **Geometry Offset 재검토**: Identity Test가 성공한 이후에도 위치가 어긋난다면, `GetGeometricTranslation` 등의 메시 전용 오프셋을 다시 검토.

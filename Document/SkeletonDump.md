# Skeletal Mesh 및 Bone 구성 수정 명세서

본 문서는 현재 KraftonEngine에서 발견된 본(Bone) 구성 및 스키닝(Skinning) 깨짐 현상을 해결하기 위한 기술 명세서입니다.

## 1. 행렬 컨벤션 변환 (FBX SDK -> Engine)

### 현상
- FBX SDK(FbxAMatrix)는 내부적으로 Column-Vector convention을 따를 수 있으나, 메모리 레이아웃은 Row 3에 Translation을 저장하는 Row-Major 형식을 사용함.
- 엔진의 `FMatrix` 또한 Row 3에 Translation을 저장하므로, 두 데이터 간의 **메모리 레이아웃은 동일**함.

### 수정 사항
- `FBXImporter.cpp`의 `FbxMatrixToFMatrix` 함수에서 **전치(Transpose) 없이 직접 복사**하도록 수정.
- 데이터를 복사할 때 `OutMat.M[r][c] = (float)FbxMat.Get(r, c)`와 같이 순차적으로 복사하여 데이터 무결성 유지.

---

## 2. 정점 데이터 초기화 및 가중치 안정화

### 현상
- `ExtractMesh`에서 `FSkeletalMeshVertex`를 초기화 없이 선언하여, 영향력이 4개 미만인 본의 가중치/인덱스에 쓰레기 값이 잔류함.

### 수정 사항
- `FBXImporter.cpp`의 `ExtractMesh` 내 루프에서 `FSkeletalMeshVertex Vertex = {};` 또는 `memset`을 사용하여 0으로 초기화.
- 모든 정점의 `boneIndices` 및 `boneWeights`가 명확한 초기값을 갖도록 보장.

---

## 3. 본 계층 구조 합성 순서 (Component Space)

### 현상
- `SkinnedMeshComponent.cpp`에서 `ComponentSpaceMatrices[Parent] * Local` 순서로 합성 중.
- Row-Major(V * M) 방식에서는 로컬 변환을 먼저 수행하고 부모 변환을 나중에 적용해야 함.

### 수정 사항
- `RecalcComponentSpaceMatrices` 함수 내 합성 순서 변경:
  - **AS-IS**: `CS[i] = CS[Parent] * Local`
  - **TO-BE**: `CS[i] = Local * CS[Parent]`

---

## 4. 최종 스키닝 행렬 계산 순서

### 현상
- `SkeletalMeshComponent.cpp`에서 `ComponentSpaceMatrices[i] * InverseBindMatrix` 순서로 계산 중.
- 정점을 Bind Pose 로컬로 보낸 뒤 애니메이션 공간으로 가져오는 연산 순서가 뒤섞임.

### 수정 사항
- `UpdateSkinning` 함수 내 계산 순서 변경:
  - **AS-IS**: `SkinningMatrices[i] = CS[i] * IBP[i]`
  - **TO-BE**: `SkinningMatrices[i] = IBP[i] * CS[i]`

---

## 5. FBX Inverse Bind Pose(IBP) 추출 로직

### 현상
- `ExtractMesh` 내 IBP 계산 수식이 Column-Major 기준으로 작성됨 (`IBP = LinkInv * Mesh`).

### 수정 사항
- Row-Major 엔진 컨벤션에 맞게 수정:
  - **TO-BE**: `FbxAMatrix BindPoseMatrix = TransformMatrix * TransformLinkMatrix.Inverse();`
  - 또는 각 FbxAMatrix를 먼저 `FbxMatrixToFMatrix`로 변환한 뒤, 엔진의 `FMatrix` 곱셈 연산(`Mesh * LinkInv`) 수행.

---

## 6. 검증 계획
1. **T-Pose 검증**: 애니메이션이 없는 상태에서 메시가 FBX 원본과 동일하게 출력되는지 확인.
2. **Bone Hierarchy 검증**: 디버그 드로잉을 통해 본의 계층 구조가 올바른 위치에 형성되는지 확인.
3. **CPU Skinning 검증**: 특정 본을 회전시켰을 때 메시가 의도한 방향으로 변형되는지 확인.

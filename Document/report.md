# Skeletal Mesh — Bind Pose Identity Test 해결 보고서

## 0. 결과 요약

**최종 상태**:
```
Bind Pose Identity Test: SUCCESS (maxAbsDiff over 63 bones = 0.000002)
```
- maxAbsDiff = 2×10⁻⁶ (부동소수점 정밀도 수준, 임계값 1×10⁻⁴ 충분히 통과)
- T-Pose 메시가 의도한 본 위치로 정상 렌더링 확인

**결정적 버그**: `FbxMatrixToFMatrix` 헬퍼의 **불필요한 transpose 적용**.
FbxAMatrix와 엔진 FMatrix 모두 row 3에 Translation을 저장하는 메모리 컨벤션을 사용하는데, 헬퍼가 row↔column을 뒤바꿔서 Translation을 row 3 → column 3으로 옮겨버렸음. 그 결과 엔진이 본의 -위치를 IBP에서 읽지 못하고 0으로 인식.

---

## 1. 초기 문제

### 증상
- 임포트 직후 `Bone[0] Matrix Error - Row3 (Translation): 0.0000, -0.8444, 5.8700`
- T-Pose에서 `IBP × ComponentSpace ≠ Identity`
- 메시가 정상 위치에 렌더되지 않음

### 환경
- 좌표계: Z-Up, Left-Handed, X-Forward (`ConvertScene`으로 강제 변환)
- 행렬: Row-Major, Row-Vector convention
- 본 계층 누적: `Local * Parent`
- 스키닝: `InverseBind * ComponentSpace`

---

## 2. 진단 과정

### STEP A — 진단 로그 추가
**목적**: 잔차 패턴을 보고 원인 분류

추가한 진단:
- `[IBP Diag]` — 본별 `M` (메시 노드 변환), `L` (TransformLinkMatrix), 계산된 IBP의 Translation
- `[BindPose Diag]` — cluster 보유 본 수 / fallback 수
- 본 트리 dump — 각 본의 `parent`, `T(local)`
- `[Hierarchy Diag]` — 비루트에서 `parent=-1`인 본 수
- Identity Test 보강 — `maxAbsDiff over N bones` + Top-3

### STEP B — IBP 공식 보정

**문제**: 기존 코드는 `IBP = TransformLinkMatrix.Inverse()` 한 줄로, **메시 노드 변환 M을 무시**.

**수정**:
```cpp
FbxAMatrix IBP_Fbx = TransformLinkMatrix.Inverse() * TransformMatrix;
RawMesh->Bones[BoneIndex].InverseBindMatrix = FbxMatrixToFMatrix(IBP_Fbx);
```

**수식**:
- FBX의 본 mesh-space bind 글로벌 = `M⁻¹ · L`
- 따라서 `IBP_FBX = (M⁻¹ · L)⁻¹ = L⁻¹ · M`

**1차 결과**:
```
Top1 Bone[52] Row3=( 0.05, -0.85, 5.87)
Top2 Bone[56] Row3=( 0.05, -0.85, 5.87)
Top3 Bone[54] Row3=(-0.00, -0.84, 5.87)
```
모든 본에 거의 동일한 잔차 → 공통 부모(Armature) 변환 누락 시그널.

### STEP C/D — TLM 기반 LocalTransform 역산

**문제**: `RecalcComponentSpaceMatrices`가 본 트리만 누적하므로 Armature 노드 변환이 CS에 들어가지 않음. CS_col은 "본 부모 누적"이지 "mesh space 글로벌"이 아님.

**해결책**: 본의 LocalTransform을 본 트리 누적용이 아니라 cluster TLM에서 직접 역산:
- 루트 본: `Local_col = M⁻¹ · L_root`
- 비루트 본: `Local_col = L_parent⁻¹ · L_child`
- cluster 없음: `EvaluateLocalTransform()` fallback

이러면 텔레스코핑으로 `CS_col(bone) = M⁻¹ · L_bone` (mesh space)이 됨 → `CS · IBP = Identity`.

추가로 부모 검색을 단일 부모 검사 → 체인 거슬러 올라가기로 보강 (Empty 노드 통과).

**2차 결과** (Blender export 옵션 변경 후):
```
Top1 Bone[58] maxAbsDiff=14232 Row3=(290, 330, -2467)
```
잔차 폭증 — 본별로 다른 패턴, 본 mesh-space 위치 자체가 그대로 잔차로 출력. **IBP가 본 위치를 빼주지 못함**.

### STEP E — 로그 출력 환경 보강
Top-3 제한 해제(전체 본 출력), `[IBP Diag]`를 모든 본에 대해 출력하도록 변경. 진단 패턴 식별에 필수.

### STEP F — 결정적 수정 (`FbxMatrixToFMatrix` transpose 제거)

전체 본 `[IBP Diag]` 로그에서 결정적 단서:
```
[IBP Diag] Bone[0] hips M=(-0.0, -0.0, -0.0) L=(0.131, 0.000, 2.852) IBP.Row3=(0.0, 0.0, 0.0)
```
**모든 본의 `IBP.Row3 = (0, 0, 0)`** — IBP에서 Translation이 통째로 빠짐.

수식 예상: hips는 (0.131, 0, 2.852)에 있으므로 `IBP.Row3 ≈ (-0.131, 0, -2.852)`여야 함. 실제로는 0.

**원인 식별**:
| 객체 | Translation 저장 위치 |
|---|---|
| FbxAMatrix | row 3 (`Get(3, 0..2)`) — 실측 확인 |
| 엔진 FMatrix | row 3 (`M[3][0..2]`) |

두 메모리 컨벤션이 **동일**한데, `FbxMatrixToFMatrix`는 의도적으로 transpose:
```cpp
OutMat.M[c][r] = (float)FbxMat.Get(r, c);   // row↔col 뒤바꿈
```
결과: Translation이 row 3 → column 3 (`M[0][3], M[1][3], M[2][3]`)으로 이동. 엔진의 row-major 곱셈은 row 3의 Translation만 사용하므로, 변환 후 IBP의 Translation을 못 봄.

**수정**:
```cpp
OutMat.M[r][c] = (float)FbxMat.Get(r, c);   // 직접 복사
```

**3차 결과**:
```
Bind Pose Identity Test: SUCCESS (maxAbsDiff over 63 bones = 0.000002)
```
✓ 통과.

---

## 3. 최종 수정 사항 정리

### 3.1 `KraftonEngine/Source/Engine/SkeletalMesh/FBXImporter.cpp`

#### (1) `FbxMatrixToFMatrix` — transpose 제거 (결정적 수정)
```cpp
// 변경 전
OutMat.M[c][r] = (float)FbxMat.Get(r, c);

// 변경 후
OutMat.M[r][c] = (float)FbxMat.Get(r, c);
```

#### (2) `FBindPoseInfo` 구조체 + `GatherBindPoseInfo` 헬퍼 추가
Scene 전체 재귀로 모든 `FbxCluster`의 `(TransformLinkMatrix, TransformMatrix)` 쌍을 `TMap<FbxNode*, FBindPoseInfo>`에 수집. LocalTransform TLM 역산에 사용.

#### (3) `ExtractMesh` — IBP 공식 보정
```cpp
FbxAMatrix FbxTransformMatrix;
Cluster->GetTransformMatrix(FbxTransformMatrix);            // M
FbxAMatrix FbxTransformLinkMatrix;
Cluster->GetTransformLinkMatrix(FbxTransformLinkMatrix);    // L

FbxAMatrix IBP_Fbx = FbxTransformLinkMatrix.Inverse() * FbxTransformMatrix;
RawMesh->Bones[BoneIndex].InverseBindMatrix = FbxMatrixToFMatrix(IBP_Fbx);
```

#### (4) `ExtractSkeleton` — 부모 검색 체인 보강 + LocalTransform TLM 역산
```cpp
// 부모 검색: Empty/Null 노드를 통과해 본까지 거슬러 올라감
while (ParentNode) {
    for (j ...) if (JointNodes[j] == ParentNode) { ParentIndex = j; break; }
    if (found) break;
    ParentNode = ParentNode->GetParent();
}

// LocalTransform 계산
if (cluster O) {
    if (parent O + parent cluster O) Local = ParentTLM.Inverse() * SelfTLM;
    else if (root)                   Local = SelfTM.Inverse() * SelfTLM;
    else                             Local = EvaluateLocalTransform();  // fallback
} else {
    Local = EvaluateLocalTransform();  // fallback
}
```

#### (5) 진단 로그 추가
- `[IBP Diag]` 모든 본 한 줄 출력 (M, L, IBP Translation)
- `[IBP Verify]` Bone[0] 한정 (L.GetT(), L.Get(0..2, 3), IBP.Col3)
- `[BindPose Diag]` cluster 카운터
- 본 트리 dump
- `[Hierarchy Diag]` 비루트 parent=-1 경고

### 3.2 `KraftonEngine/Source/Engine/Component/SkeletalMeshComponent.cpp`

#### Identity Test 로그 전체 본 출력
- `TopN = std::min<size_t>(3, ...)` → `TopN = Diffs.size()` (전체)
- 라벨 "Top%d" 제거 → `Bone[%d] maxAbsDiff=...`
- SUCCESS/FAILED 양쪽에 `maxAbsDiff over N bones` 첨부

---

## 4. 핵심 교훈

### 4.1 메모리 컨벤션은 의미 컨벤션과 별개
FBX SDK 문서/관습은 "column-vector convention"이라고 명시되지만, **메모리 저장 레이아웃은 row 3에 Translation을 두는 형태** (DirectX와 같음). 코드에서 `Get(3, 0..2)`를 호출하면 Translation이 나옴.

엔진 row-major + row-vector convention도 row 3에 Translation. 두 객체의 **메모리 레이아웃이 같다면 메모리상 transpose는 불필요**. 곱셈 의미만 다르더라도 위치 인덱스가 같으면 그대로 복사가 정답.

### 4.2 잔차 패턴이 원인을 직접 가리킴
- "모든 본 동일 잔차" → 공통 부모 변환 1개 누락
- "본별 다른, 본 mesh-space 위치 비례 잔차" → IBP 무력화 (Translation 못 봄)
- "본 Z 좌표에 비례하는 잔차" → 본 계층 누적 부족

전체 본 출력이 없으면 이 패턴을 식별할 수 없으므로 진단 로그를 무제한으로 두는 게 중요.

### 4.3 FBX 캐시는 진단의 적
`FFBXManager::LoadSkeletalMesh`의 2단계 캐시 (메모리 + 디스크 `.bin`)가 임포트를 건너뛸 수 있음. import 측 로그가 안 보이면 캐시 hit을 의심하고 `Asset/MeshCache/*.bin`을 삭제할 것.

### 4.4 단계적 디버그 + Identity Test 자동 판정의 가치
T-Pose에서 `IBP × CS = Identity` 라는 수학적 invariant를 자동으로 검사하는 로직 덕분에 매 수정 단계의 효과를 즉시 판정할 수 있었음. 시각 검증보다 훨씬 신뢰성 높음.

---

## 5. 잔존 과제

### 5.1 Hierarchy Warning (비치명적)
```
[Hierarchy Diag] WARNING: 2 non-root bones have ParentIndex=-1.
[59] shin.L.001  parent=-1 (<root>) T=(0.641, 0.139, 0.351)
[61] shin.R.001  parent=-1 (<root>) T=(-0.641, 0.139, 0.351)
```
Blender의 IK 잔재 본이 Only Deform Bones를 거쳐도 export에 포함됨. Identity Test는 통과했으므로 스키닝 동작에는 영향 없지만, 추후:
- Blender 측에서 본 정리
- 또는 import 시 자식 본이 없거나 가중치 0인 본을 자동 제거

### 5.2 `FbxAMatrix` convention 문서화
헬퍼 `FbxMatrixToFMatrix`에 짧은 주석으로 "FbxAMatrix와 FMatrix 모두 row-major-호환 메모리 레이아웃 (row 3 = Translation)" 명시 완료. 추가로 코드베이스 다른 곳에서 FbxAMatrix를 다룬다면 같은 가정을 따라야 함을 팀에 공유 필요.

### 5.3 FBX 캐시 정책
사용자가 export 옵션을 바꾸거나 import 코드를 수정한 후에도 디스크 `.bin` 캐시가 살아 있으면 옛 결과가 나옴. 다음 중 하나 검토:
- `.bin` 파일에 import 코드 버전 stamp를 박아서 mismatch 시 강제 재빌드
- 디버그 빌드에서는 캐시 비활성화
- `ForceReimport` 메뉴 추가

---

## 6. 검증 기준점

향후 회귀 발생 시 다음 값들을 비교하면 어디서 깨졌는지 빠르게 식별 가능:

| 지표 | 정상값 | 의미 |
|---|---|---|
| `Bones with cluster` | Total과 동일, fallback=0 | TLM 역산 100% 적용 |
| `non-root parent=-1` | 0 (또는 알려진 부수 본 수) | Hierarchy 완전성 |
| `Bone[0] hips IBP.Row3` | `(-T.X, -T.Y, -T.Z)` of hips L | IBP 공식 정확성 |
| `Bone[0] IBP.Col3` | 모두 0 | transpose 미적용 (메모리 컨벤션 일치) |
| `maxAbsDiff over N bones` | < 1×10⁻⁴ | Identity Test 통과 |

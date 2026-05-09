# FBX Importer Implementation Specification

본 문서는 FBX SDK를 사용하여 Skeletal Mesh 데이터를 추출하고 `USkeletalMesh` 에셋으로 변환하는 `FFbxImporter`의 구현 명세서입니다.

## 1. 개요
`FFbxImporter`는 외부 FBX 파일을 읽어 엔진 내부의 `USkeletalMesh` 및 `FSkeletalMesh` 구조로 변환하는 역할을 합니다. 주요 변환 대상은 기하 데이터(Mesh), 스켈레톤 구조(Skeleton), 그리고 스키닝 가중치(Skinning Weights)입니다.

## 2. 외부 라이브러리 참조
- **경로**: `KraftonEngine/ThirdParty/FBX/`
- **Include**: `#include <fbxsdk.h>`
- **Library**: `libfbxsdk-md.lib` (또는 프로젝트 설정에 따른 버전)

## 3. 핵심 데이터 추출 프로세스

### 3.1 FBX Scene 초기화
1. `FbxManager` 및 `FbxIOSettings` 생성.
2. `FbxImporter`를 통해 파일 로드 및 `FbxScene`에 데이터 채우기.
3. **Axis & Unit Conversion**: 엔진 표준(예: DirectX 좌측 좌표계, Centimeters)에 맞춰 `FbxAxisSystem` 및 `FbxSystemUnit` 변환 수행. (Project는 Forward X , Z up File도 우선은 project에 맞춰서 생성)

### 3.2 스켈레톤(Skeleton) 추출
1. Scene의 루트 노드부터 재귀적으로 순회하여 `FbxSkeleton` 타입을 가진 노드를 식별.
2. 본의 계층 구조를 파악하여 `BoneNames` 리스트와 `FBone` 배열 생성.
3. 각 본의 **Inverse Bind Matrix** 추출:
   - `FbxCluster::GetTransformLinkMatrix` (본의 Pose Matrix)
   - `FbxCluster::GetTransformMatrix` (메시의 원본 Transform)
   - 위 행렬들을 조합하여 정점을 본 공간으로 보낼 역행렬 계산.

### 3.3 메시(Mesh) 및 스키닝 데이터 추출
1. `FbxMesh` 노드를 찾아 정점(Control Points) 데이터 접근.
2. **Skinning 파싱**:
   - `FbxSkin`과 `FbxCluster`를 순회.
   - 각 정점 인덱스별로 영향을 주는 본 인덱스와 가중치를 수집.
   - 한 정점당 최대 4개의 본으로 제한하고 가중치 합이 1이 되도록 정규화(Normalize).
3. **Vertex 생성**:
   - `Control Points` + `Normal/UV/Tangent` + `Skinning Data`를 조합하여 `FSkeletalMeshVertex` 배열 구축.
   - `StaticMesh`와 동일한 방식의 인덱스 버퍼 생성.

## 4. 클래스 설계: FFbxImporter

```cpp
class FFbxImporter
{
public:
    /** 
     * FBX 파일을 읽어 USkeletalMesh 에셋을 생성합니다.
     * @param FilePath FBX 파일 경로
     * @param OutMesh 결과물이 담길 에셋 포인터
     * @return 임포트 성공 여부
     */
    static bool ImportSkeletalMesh(const FString& FilePath, class USkeletalMesh* OutMesh);

private:
    // SDK 내부 객체 초기화/해제
    static bool InitializeSDK();
    static void ReleaseSDK();

    // 재귀적으로 노드를 순회하며 데이터 수집
    static void ProcessNode(FbxNode* Node, class USkeletalMesh* OutMesh, struct FSkeletalMesh* RawMesh);
    
    // 세부 데이터 추출 함수
    static void ExtractMesh(FbxMesh* Mesh, struct FSkeletalMesh* RawMesh, class USkeletalMesh* OutMesh);
    static void ExtractSkeleton(FbxScene* Scene, class USkeletalMesh* OutMesh, struct FSkeletalMesh* RawMesh);
};
```

## 5. 예외 처리 및 주의 사항
- **Triangulation**: 메시가 삼각형이 아닐 경우 `FbxGeometryConverter::Triangulate`를 사용하여 변환 필요.
- **Multiple Skins**: 하나의 메시가 여러 Skin을 가질 경우 통합 처리 로직 필요.
- **Memory Management**: 파싱 완료 후 `FbxScene` 및 `FbxManager`를 안전하게 `Destroy` 해야 함.

---
*작성일: 2026-05-09*
*상태: Draft (Ready for Implementation)*

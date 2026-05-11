# FBXManager Implementation Specification

본 문서는 KraftonEngine의 스켈레탈 메시 에셋을 관리하는 `FBXManager`의 구현 상세를 정의합니다. 기존 `FObjManager`의 설계를 계승하여 원본 FBX 파일의 임포트, 바이너리 캐싱, 에디터 UI 연동을 담당합니다.

## 1. 개요
`FBXManager`는 엔진 내에서 `USkeletalMesh` 에셋의 생명주기를 관리하는 싱글톤 성격의 정적 클래스입니다. 데이터 로딩 속도 최적화를 위해 원본 FBX 데이터를 자체 바이너리 포맷(`.bin`)으로 캐싱하여 관리합니다.

## 2. 주요 역할
- **에셋 스캔**: `Data/` 폴더의 원본 `.fbx` 파일과 `Asset/MeshCache/`의 캐시된 `.bin` 파일을 탐색하여 리스트업합니다.
- **에셋 로드 및 캐싱**: 요청된 경로의 메시를 로드하며, 이미 로드된 에셋은 캐시에서 반환합니다.
- **바이너리 빌드**: 원본 FBX 파일이 변경되었거나 캐시가 없는 경우 `FFbxImporter`를 호출하여 데이터를 파싱하고 `.bin` 파일로 저장합니다.
- **에디터 UI 지원**: `EditorPropertyWidget`에서 사용할 수 있는 에셋 리스트(`FMeshAssetListItem`)를 제공합니다.

## 3. 주요 데이터 구조

### 3.1 에셋 캐시 (Internal)
- `static TMap<FString, USkeletalMesh*> SkeletalMeshCache`: 로드된 에셋의 메모리 캐시. Key는 바이너리 파일 경로입니다.

### 3.2 UI용 리스트 아이템
- `TArray<FMeshAssetListItem> AvailableMeshFiles`: `.bin` 캐시 파일 목록.
- `TArray<FMeshAssetListItem> AvailableFbxFiles`: 원본 `.fbx` 파일 목록.

## 4. 핵심 API

### 4.1 로딩 및 관리
- `static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName, ID3D11Device* InDevice)`
    - 경로를 입력받아 메시를 반환합니다. 
    - 내부적으로 `GetBinaryFilePath()`를 통해 캐시 경로를 결정하고, 타임스탬프를 비교하여 원본으로부터 재빌드할지 결정합니다.
- `static void ReleaseAllGPU()`
    - 엔진 종료 시 로드된 모든 메시의 GPU 리소스를 해제합니다.

### 4.2 스캐닝
- `static void ScanMeshAssets()`: `Asset/MeshCache/*.bin` 파일을 검색하여 `AvailableMeshFiles`를 갱신합니다.
- `static void ScanFbxSourceFiles()`: `Data/*.fbx` 파일을 검색하여 `AvailableFbxFiles`를 갱신합니다.

### 4.3 경로 유틸리티
- `static FString GetBinaryFilePath(const FString& OriginalPath)`: 원본 경로를 기반으로 대응하는 `.bin` 캐시 파일 경로를 생성합니다.

## 5. 워크플로우 (Loading Logic)
1. 요청된 경로(`PathFileName`)가 `.bin`인지 `.fbx`인지 확인합니다.
2. 대응하는 캐시 경로(`BinPath`)를 생성합니다.
3. **캐시 유효성 검사**: `BinPath`가 존재하고 원본보다 최신인 경우 `USkeletalMesh::Serialize`를 통해 즉시 로드합니다.
4. **재빌드 필요 시**: 
    - `FFbxImporter::ImportSkeletalMesh`를 호출하여 FBX를 파싱합니다.
    - 파싱 결과를 `BinPath`에 저장(Serialize)합니다.
5. `InitResources()`를 호출하여 GPU 버퍼를 생성한 후 캐시에 등록하고 반환합니다.

---
*작성일: 2026-05-10*
*상태: Implementation Specification Ready*

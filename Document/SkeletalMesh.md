# Skeletal Mesh Implementation Report

본 문서는 KraftonEngine의 Skeletal Mesh 시스템 구현 내용(Asset, Mesh Data, Importer)을 정리한 보고서입니다.

## 1. 시스템 아키텍처 개요
Skeletal Mesh 시스템은 기존 `StaticMesh`의 설계를 계승하면서도, 애니메이션과 스키닝을 위한 확장된 데이터 구조를 가집니다.

- **FSkeletalMesh**: 기하 데이터 및 본 정보를 담는 실체.
- **USkeletalMesh**: 엔진 내에서 에셋으로 관리되는 인터페이스.
- **FFbxImporter**: 외부 FBX 파일을 엔진 전용 데이터로 변환하는 파이프라인.

---

## 2. 데이터 구조 구현 (Mesh & Asset)

### 2.1 FSkeletalMesh (SkeletalMeshAsset.h)
메시의 정점, 인덱스, 본 정보를 소유하는 핵심 데이터 구조체입니다.
- **FSkeletalMeshVertex**: 위치, 노멀, UV 외에 `boneIndices[4]`와 `boneWeights[4]`를 포함합니다.
- **FBone**: `ParentIndex`와 함께 초기 `LocalTransform` 및 `InverseBindMatrix`를 가집니다.
- **수동 직렬화(Manual Serialization)**: 
    - `FMatrix`, `FVector` 등 Non-Trivial 타입을 위해 `Ar.Serialize()`를 직접 호출하여 `Archive.h`의 템플릿 제약을 우회하고 성능과 안정성을 확보했습니다.

### 2.2 USkeletalMesh (SkeletalMesh.h / .cpp)
`UObject`를 상속받아 엔진 시스템(에디터, 직렬화)과 소통하는 클래스입니다.
- **머티리얼 관리**: `TArray<FStaticMaterial>`을 통해 섹션별 재질을 매핑합니다.
- **본 이름 매핑**: `TMap<FName, int32>`를 유지하여 애니메이션 시 이름 기반으로 본을 빠르게 찾을 수 있도록 지원합니다.

---

## 3. FBX 임포트 파이프라인 (FFbxImporter)

FBX SDK를 활용하여 데이터를 추출하는 로직이 `FFbxImporter`에 구현되었습니다.

### 3.1 추출 순서
1. **Skeleton First**: 메시를 읽기 전 Scene 전체의 `eSkeleton` 노드를 먼저 순회하여 본 계층 구조와 이름을 확립합니다.
2. **Mesh & Skinning**: `FbxSkin`과 `FbxCluster`를 분석하여 정점별로 영향을 주는 본 인덱스와 가중치를 추출하고 정규화합니다.

### 3.2 좌표계 및 데이터 변환
- **UV Flip**: DirectX 표준에 맞춰 UV의 Y축을 반전(`1.0 - UV.y`) 처리합니다.
- **Matrix Conversion**: `FbxAMatrix`를 엔진의 `FMatrix`로 변환하는 헬퍼 함수를 통해 수학적 일관성을 유지합니다.

---

## 4. 기술적 해결 사항 및 특이 사항

- **Serialization**: `static_assert` 이슈를 해결하기 위해 저수준 헤더의 의존성을 제거하고, 에셋 클래스 수준에서 메모리 블록 단위로 직접 읽고 쓰는 방식을 채택했습니다.
- **Hashing Support**: `TMap<FName, ...>` 사용을 위해 `std::hash<FName>` 특수화를 추가하여 표준 라이브러리 호환성을 확보했습니다.
- **Rendering Compatibility**: `SceneProxy` 구현 시 `FGPUGeometryView`를 통해 에셋의 정적 IB와 컴포넌트의 동적 VB를 조합할 수 있는 구조를 확립했습니다.

---
*작성일: 2026-05-09*
*상태: Core Implementation Completed*

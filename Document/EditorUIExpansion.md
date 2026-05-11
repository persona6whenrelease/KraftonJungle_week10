# Editor UI Expansion Specification: Skeletal Mesh Support

본 문서는 에디터의 `Property Window`에서 `SkeletalMesh` 에셋을 시각적으로 관리하고 편집하기 위한 UI 확장 명세를 정의합니다.

## 1. 개요
기존 `StaticMesh` 편집 기능과 동일한 사용자 경험을 제공하기 위해 `FEditorPropertyWidget`을 확장합니다. 이를 통해 사용자는 스켈레탈 메쉬 에셋을 선택하거나 신규 FBX 파일을 임포트할 수 있습니다.

## 2. 주요 변경 사항

### 2.1 프로퍼티 타입 추가 연동
- **대상**: `FEditorPropertyWidget::RenderPropertyWidget`
- **로직**: `EPropertyType::SkeletalMeshRef` 케이스를 추가하여 전용 위젯을 렌더링합니다.

### 2.2 SkeletalMeshRef 위젯 인터페이스 (FBX 중심 관리)
- **표시**: 현재 컴포넌트에 할당된 스켈레탈 메시의 **원본 FBX 파일 이름**을 표시합니다.
- **선택 (Source-First)**: 
    - `FFBXManager::GetAvailableFbxFiles()`를 통해 프로젝트 내의 모든 `.fbx` 원본 파일 리스트를 드롭다운으로 제공합니다.
    - 사용자는 익숙한 작업물 이름(.fbx)을 보고 선택합니다.
- **연동 로직**:
    - 사용자가 FBX를 선택하면 컴포넌트의 `SkeletalMeshPath`에 해당 경로가 저장됩니다.
    - `FFBXManager::LoadSkeletalMesh`가 호출되며, 내부적으로 대응하는 `.bin` 캐시가 최신인지 확인합니다.
    - 캐시가 유효하면 즉시 로드하고, 유효하지 않으면 자동으로 임포트 과정을 거쳐 캐시를 갱신합니다.
- **임포트**: 프로젝트 외부의 파일을 가져오기 위한 "Import FBX" 버튼을 유지하여 신규 에셋 추가를 지원합니다.

## 3. 세부 구현 상세

### 3.1 위젯 구조 (ImGui)
1. **Label**: 프로퍼티 이름 ("Skeletal Mesh").
2. **Combo Box**: 
    - 클릭 시 `FFBXManager`가 스캔한 에셋 목록 출력.
    - 선택 시 컴포넌트의 경로 변수(`SkeletalMeshPath`) 업데이트 및 `PostEditProperty` 호출.
3. **Button ("Import FBX")**:
    - 클릭 시 윈도우 파일 다이얼로그(`GetOpenFileName`)를 열어 `.fbx` 파일 선택.
    - 선택된 파일을 `FFBXManager::LoadSkeletalMesh`로 전달하여 임포트 및 바이너리 빌드 수행.

### 3.2 머티리얼 슬롯 지원
- `SkeletalMesh`의 섹션 정보(`FStaticMaterial`)를 기반으로 동적 머티리얼 리스트를 생성합니다.
- 기존 `EPropertyType::MaterialSlot` 위젯을 재사용하여 구현합니다.

## 4. 관련 클래스 및 API 연동
- **`FFBXManager`**: 에셋 리스트 획득 및 로드 담당.
- **`FFbxImporter`**: 원본 FBX 파싱 및 에셋 생성 담당.
- **`FEditorFileUtils`**: 파일 오픈 다이얼로그 제공.

## 5. 예상 결과물
- **Details 패널**: `SkeletalMeshComponent` 선택 시 "Skeletal Mesh" 항목이 나타나며, 드롭다운을 통해 메쉬를 교체할 수 있습니다.
- **실시간 반영**: 메쉬 교체 즉시 뷰포트의 캐릭터 모델이 갱신됩니다.

---
*작성일: 2026-05-10*
*상태: UI Expansion Specification Ready*

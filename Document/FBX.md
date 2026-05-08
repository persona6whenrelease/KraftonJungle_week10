import os

# Define the content for the Markdown file
md_content = """# FBX SDK 통합 및 Skeletal Mesh 시스템 구현 계획서

## 1. 학습 목표
* **FBX SDK 통합**: 자체 엔진 내 FBX SDK 라이브러리 환경 구축 및 데이터 로드 프로세스 정립.
* **FBX 데이터 구조 이해**: Node hierarchy, Mesh, Material, Animation, Skinning(Deformer) 데이터 추출 방식 파악.
* **Vertex Skinning 메커니즘**: Bone, Weight, Bind Pose, Skinning Matrix의 상관관계 이해.
* **CPU Skinning 구현**: 하드웨어 가속 전 단계로 CPU 상에서 정점 변환 로직 구현 및 검증.

## 2. 주요 기능 및 요구 사항

### 2.1. 렌더링 및 스키닝
* **Reference Pose 렌더링**: 애니메이션 데이터가 적용되지 않은 기본 T-Pose 상태의 모델 렌더링.
* **CPU Skinning**: 정점 셰이더가 아닌 CPU 연산을 통해 연산된 정점 위치를 버퍼에 업데이트.
* **ShowFlag 제어**: `SkeletalMesh` 표시 여부를 제어하는 토글 기능 구현.

### 2.2. Skeletal Mesh Viewer (Editor Integration)
* **내부 뷰어 구현**: Standalone이 아닌 엔진 에디터 내에서 리소스를 확인하고 편집할 수 있는 UI 창 구현.
* **Bone Hierarchy Tree**: Bone 간의 부모-자식 관계를 트리 구조로 시각화.
* **Transform Gizmo**: 선택된 Bone의 Transform(위치, 회전, 스케일)을 에디터 뷰포트 내 기즈모로 표시 및 조작.

### 2.3. FBX Importer 고도화
* **StaticMesh 지원**: 기존 SkeletalMesh 외에 리깅 데이터가 없는 StaticMesh 임포트 기능 추가.
* **Binary Baking**: FBX 파싱 비용을 줄이기 위해 자체 바이너리 포맷(`.mesh`, `.skel`)으로 변환 및 저장.

## 3. 핵심 알고리즘 및 구조 (Pseudo Code)

### 3.1. 기초 수학 구조체
```cpp
struct Vector3 {
    float x, y, z;

    Vector3 operator+(const Vector3& other) const {
        return { x + other.x, y + other.y, z + other.z };
    }

    Vector3 operator*(float scalar) const {
        return { x * scalar, y * scalar, z * scalar };
    }
};

struct Matrix4x4 {
    float m[4][4];

    Vector3 TransformPosition(const Vector3& v) const {
        return {
            m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3],
            m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3],
            m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3]
        };
    }
};
```

### 3.2. 스키닝 데이터 구조
C++
struct Vertex {
    Vector3 position;
    int boneIndices[4];
    float boneWeights[4];
};

struct Bone {
    // SkinningMatrix = BoneWorldTransform * InverseBindPose
    Matrix4x4 skinningMatrix; 
};

### 3.3. CPU Skinning 연산 로직
C++
Vector3 SkinVertexPosition(const Vertex& vertex, const std::vector<Bone>& bones) {
    Vector3 result = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 4; ++i) {
        int boneIndex = vertex.boneIndices[i];
        float weight = vertex.boneWeights[i];

        if (weight > 0.0f && boneIndex >= 0 && boneIndex < (int)bones.size()) {
            const Matrix4x4& skinMat = bones[boneIndex].skinningMatrix;
            Vector3 transformed = skinMat.TransformPosition(vertex.position);
            result = result + (transformed * weight);
        }
    }

    return result;
};


### 4. 컴포넌트 클래스 설계
스키닝 처리가 필요한 메쉬와 일반 스켈레탈 메쉬의 계층 구조를 다음과 같이 설계한다.

C++
class USkinnedMeshComponent : public UMeshComponent
{
protected:
    USkeletalMesh* SkeletalMesh;
    // 스키닝 연산에 필요한 데이터 및 런타임 버퍼 관리
};

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    // 애니메이션 제어, 본 트랜스폼 업데이트 등 확장 기능 수행
};

### 5. 리소스 파이프라인 최적화
FBX Parsing: FBX SDK를 사용하여 데이터를 추출한다.

Data Extraction: Vertex(Position, UV, Normal, Weights, Indices), Bone Hierarchy, Bind Pose 데이터를 추출한다.

Baking: 추출된 데이터를 엔진 전용 바이너리 포맷으로 직렬화(Serialization)한다.

Runtime Loading: 런타임에는 FBX SDK 의존성 없이 바이너리 파일을 즉시 역직렬화하여 로드 속도를 최적화한다.
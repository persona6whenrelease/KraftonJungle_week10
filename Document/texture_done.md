# FBX → 텍스처 매핑 로직

`feat/gizmo` 브랜치 기준, KraftonEngine이 FBX 파일을 임포트해 텍스처를 머티리얼에 바인딩하기까지의 데이터 흐름을 정리한 기술 메모. 외부 사양서가 아니라 코드 추적용 메모이므로 함수·구조체는 `파일 경로:라인` 형식으로 표기한다.

---

## 1. 한 줄 요약

> FBX SDK로 파싱 → `FFbxImportMeta`에 머티리얼/텍스처 경로 적재 → `Asset/Materials/Auto/*.mat` JSON 자동 생성 → `UMaterial` 로드 시 `UTexture2D::LoadFromFile`로 텍스처 로드 → `UMaterial::Bind`에서 픽셀 셰이더 `t0~t7` 레지스터에 SRV 바인딩.

---

## 2. 전체 데이터 흐름

```
[ .fbx 파일 ]
      │
      ▼
FBXImporter::ImportFbxAsset()                          ── KraftonEngine/Source/Engine/Mesh/FBX/FBXImporter.h
      │  (FBXSDK Manager/Scene 초기화 → LoadScene)
      ▼
FFbxMetaParser::BuildFbxMeta(FbxScene*)                ── FbxMetaParser.cpp:652
      │  (노드/메시/스킨/머티리얼 메타 등록)
      ▼
FFbxMetaParser::RegisterMaterial(FbxSurfaceMaterial*)  ── FbxMetaParser.cpp:833
      │
      ├──▶ ReadDiffuseColor()                          ── FbxMetaParser.cpp:90
      ├──▶ ReadTexturePathForProperties(Diffuse  …)    ── FbxMetaParser.cpp:617
      ├──▶ ReadTexturePathForProperties(Normal   …)
      ├──▶ ReadTexturePathForProperties(Specular …)
      └──▶ ReadDiffuseUVSetName()                      ── FbxMetaParser.cpp:632
                │
                ▼
       FFbxMaterialInfo                                ── FBXImportMeta.h:120
       {  DiffuseTexturePath, NormalTexturePath,
          SpecularTexturePath, EmissiveTexturePath,
          DiffuseColor, DiffuseUVSetName  }
                │
                ▼
FbxMaterialImportUtils::BuildStaticMaterials()         ── FbxMaterialImportUtils.cpp:137
FbxMaterialImportUtils::BuildSkeletalMaterials()       ── FbxMaterialImportUtils.cpp:179
      │
      ├── ConvertFbxMaterialInfoToMat()                ── FbxMaterialImportUtils.cpp:49
      │       → "Asset/Materials/Auto/{Slot}.mat" JSON 생성
      │       → JsonData["Textures"]["DiffuseTexture"] = "..." 등
      │
      └── FMaterialManager::GetOrCreateMaterial(MatPath)
                │
                ▼
       UMaterial 로드 (Material.cpp)
                │
                ▼
       UMaterial::SetTextureParameter(Slot, UTexture2D*)
                │  ← UTexture2D::LoadFromFile(TexturePath, Device)
                │       (Texture2D.cpp:54)
                │       → DirectX::CreateWICTextureFromFileEx → ID3D11ShaderResourceView
                │
                ▼
       UMaterial::RebuildCachedSRVs()                  ── Material.h:190
                │  → CachedSRVs[(int)EMaterialTextureSlot::Max]
                ▼
       UMaterial::Bind(Context)                        ── Material.h:128
                │  → 픽셀 셰이더 t0..t7 레지스터에 SRV 바인딩
                ▼
              [ 렌더링 ]
```

메시 섹션이 자신의 `MaterialIndex`를 통해 `FMeshMaterial`을 조회하고, 그 `MaterialInterface->Bind()`가 호출되는 구조이다.

---

## 3. 핵심 자료구조

| 타입 | 위치 | 역할 |
|---|---|---|
| `FFbxMaterialInfo` | [FBXImportMeta.h:120](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h) | FBX 머티리얼 1개분의 텍스처 경로/색상 메타 |
| `FFbxImportMeta` | [FBXImportMeta.h:133](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h) | 한 FBX 파일 전체의 노드/메시/머티리얼 인덱스 테이블 |
| `FFbxMeshMeta` | [FBXImportMeta.h:25](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h) | 메시별 `MaterialSlotIds/Names`, `MaterialUVSetNames`, `MaterialIds` |
| `FMeshSection` / `FStaticMeshSection` | [MeshCommonTypes.h:15](../KraftonEngine/Source/Engine/Mesh/MeshCommonTypes.h) | 메시 섹션 ↔ `MaterialIndex` + `MaterialSlotName` |
| `FMeshMaterial` | [MeshCommonTypes.h:29](../KraftonEngine/Source/Engine/Mesh/MeshCommonTypes.h) | 슬롯 이름과 `UMaterial*` 포인터를 묶음 |
| `EMaterialTextureSlot` | [MaterialTextureSlot.h:8](../KraftonEngine/Source/Engine/Render/Types/MaterialTextureSlot.h) | `Diffuse, Normal, Roughness, Metallic, Emissive, AO, Custom0/1, Max(=8)` — 셰이더 t0~t7 슬롯과 1:1 |
| `UMaterial` | [Material.h:73](../KraftonEngine/Source/Engine/Materials/Material.h) | `TextureParameters` 맵 + `CachedSRVs[Max]` |
| `UTexture2D` | [Texture2D.h](../KraftonEngine/Source/Engine/Texture/Texture2D.h) | `ID3D11ShaderResourceView` 보유, 전역 `TextureCache` |

### `FFbxMaterialInfo` 필드 ([FBXImportMeta.h:120-131](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h))

```cpp
struct FFbxMaterialInfo
{
    int32   MaterialId = -1;
    FString MaterialSlotName = "None";
    FString MaterialAssetPath;            // "Asset/Materials/Auto/{Slot}.mat"
    FVector DiffuseColor = (1, 0, 1);     // 폴백 색 (텍스처 없을 때)
    FString DiffuseTexturePath;
    FString NormalTexturePath;
    FString SpecularTexturePath;
    FString EmissiveTexturePath;          // (현재 임포트 시 채워지지 않음 — 폴백 자리)
    FString DiffuseUVSetName;
};
```

> Emissive는 구조체 필드는 있으나 `RegisterMaterial`에서 현재 명시적으로 채우지 않는다. JSON 출력단(`ConvertFbxMaterialInfoToMat`)은 비어 있으면 키를 생략한다.

---

## 4. FBX SDK에서 텍스처 경로 추출

진입점 `FFbxMetaParser::RegisterMaterial` ([FbxMetaParser.cpp:833](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp))은 `FbxSurfaceMaterial*` 하나에 대해 `FFbxMaterialInfo`를 만들고, 텍스처 슬롯별로 경로를 채운다:

```cpp
MaterialInfo.DiffuseTexturePath  = ReadTexturePathForProperties(SurfaceMaterial, ..., DiffusePropertyNames,  "Diffuse");
MaterialInfo.NormalTexturePath   = ReadTexturePathForProperties(SurfaceMaterial, ..., NormalPropertyNames,   "Normal");
MaterialInfo.SpecularTexturePath = ReadTexturePathForProperties(SurfaceMaterial, ..., SpecularPropertyNames, "Specular");
```

### 4.1 FBX 프로퍼티 이름 매칭 테이블

각 슬롯에 대해 아래 프로퍼티 이름들을 순차로 탐색한다 ([FbxMetaParser.cpp:110](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) 부근):

| 슬롯 | 탐색 프로퍼티 이름 |
|---|---|
| Diffuse | `Diffuse`, `DiffuseColor`, `BaseColor`, `Maya\|baseColor`, `Maya\|DiffuseColor` |
| Normal | `NormalMap`, `Bump`, `Maya\|normalCamera`, `NormalCamera` |
| Specular | `Specular`, `SpecularColor`, `Maya\|specularColor` |
| Emissive | `Emissive`, `EmissiveColor`, `EmissionColor`, `Maya\|emissionColor` |

### 4.2 텍스처 후보 찾기

| 함수 | 위치 | 역할 |
|---|---|---|
| `IsFbxFileTextureObject` | [FbxMetaParser.cpp:138](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | 객체가 `FbxFileTexture` 인지 클래스명으로 식별 |
| `FindFileTextureRecursive` | [FbxMetaParser.cpp:191](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | `GetSrcObject(...)` 트리를 재귀로 내려 `FbxFileTexture` 1개를 찾음 |
| `CollectFileTexturesRecursive` | [FbxMetaParser.cpp:215](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | 트리 전체에서 텍스처를 수집 |
| `FindFileTextureForProperties` | [FbxMetaParser.cpp:240](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | 슬롯별 `FbxProperty` 이름 배열을 순회하며 첫 번째로 발견되는 텍스처 반환 |
| `TextureMatchesRole` | [FbxMetaParser.cpp:304](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | 텍스처 이름/슬롯명에서 "diffuse/normal/spec" 등 키워드 매칭 |
| `FindFileTextureByRoleFallback` | [FbxMetaParser.cpp:344](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp) | 프로퍼티 매칭이 실패하면 머티리얼의 모든 텍스처를 훑어 역할(role) 추정으로 폴백 |

`TextureMatchesRole` 키워드 (파일명/슬롯명에 포함되면 매칭):
- **Diffuse**: `base_color`, `basecolor`, `diffuse`, `albedo`, `_d.`, `-d.`
- **Normal**: `normalmap`, `normal`, `_n.`, `-n.`
- **Specular**: `specular`, `spec`
- **Emissive**: `emission`, `emissive`, `_e.`, `-e.`

### 4.3 텍스처 경로 해상도 — `ReadTexturePath` ([FbxMetaParser.cpp:562](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp))

FBX 파일 안에 박혀 있는 텍스처 경로는 절대 경로(`GetFileName()`)와 FBX 기준 상대 경로(`GetRelativeFileName()`) 두 가지가 존재한다. 두 값을 가지고 다음 순서로 디스크에서 실제 파일을 찾는다.

1. `BuildTextureSearchBaseDirs(SourceFbxDir)` ([FbxMetaParser.cpp:376](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp)) — FBX 파일이 있는 디렉토리와 그 주변(인접 폴더) 후보를 검색 베이스로 구성. 후보 폴더명: `textures/`, `texture/`, `tex/`, `maps/`, `materials/`, `images/`.
2. `TryResolveTextureCandidate(SearchBaseDirs, RawFileName)` ([FbxMetaParser.cpp:457](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp)) — 절대 경로 시도 (case-insensitive 검색 포함).
3. 실패하면 `TryResolveTextureCandidate(SearchBaseDirs, RawRelativeFileName)`.
4. 그래도 실패하면 `ResolveTexturePathByFileNameHeuristic` ([FbxMetaParser.cpp:493](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp)) — **파일명만** 가지고 검색 베이스 디렉토리 트리를 훑음.
5. 모두 실패하면 빈 문자열을 반환하고 로그 `[FBXImporter] Texture path not found. ...`를 남김.

성공 시 로그:
- `[FBXImporter] Resolved {Role} texture. ... Path=...`
- 휴리스틱 폴백 성공 시: `[FBXImporter] Resolved {Role} texture by filename search. ...`

마지막으로 해상된 디스크 경로는 `FPaths::TryResolvePackagePath`의 역과정을 통해 `Asset/...` 패키지 경로로 다시 표현되어 `.mat` JSON에 기록된다.

### 4.4 UV 세트

`ReadDiffuseUVSetName` ([FbxMetaParser.cpp:632](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMetaParser.cpp))이 디퓨즈 텍스처의 `FbxFileTexture::UVSet`을 읽어 `FFbxMaterialInfo::DiffuseUVSetName`에 보관한다. 메시 단의 `FFbxMeshMeta::MaterialUVSetNames` ([FBXImportMeta.h:25](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h) 부근)도 함께 채워져, 메시 빌더가 UV 채널을 결정할 때 사용한다.

---

## 5. 자동 `.mat` 자산 생성

머티리얼 데이터를 셰이더가 쓰기 좋은 형태(JSON 머티리얼 파일)로 변환한다.

### 5.1 `ConvertFbxMaterialInfoToMat` ([FbxMaterialImportUtils.cpp:49](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMaterialImportUtils.cpp))

- 슬롯 이름을 sanitize하여 (`SanitizeMaterialFileName`, [FbxMaterialImportUtils.cpp:20](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMaterialImportUtils.cpp)) 경로 결정: `Asset/Materials/Auto/{SlotName}.mat`
- `FPaths::TryResolvePackagePath` ([Paths.h:45](../KraftonEngine/Source/Engine/Platform/Paths.h))로 디스크 경로 해석
- 이미 존재하면 **덮어쓰지 않는다** (로그: `[FBXImporter] Auto material exists; skip overwrite.`, [FbxMaterialImportUtils.cpp:71](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMaterialImportUtils.cpp))
- 기본 JSON 필드:

  ```json
  { "PathFileName": "...",
    "Origin":       "FbxImport",
    "ShaderPath":   "Shaders/Geometry/UberLit.hlsl",
    "RenderPass":   "Opaque" }
  ```

- 텍스처가 하나라도 있으면 `Textures` 객체와 흰색 `SectionColor`를 함께 기록:

  ```json
  "Textures": {
    "DiffuseTexture":  "Asset/Textures/model_diffuse.png",
    "NormalTexture":   "Asset/Textures/model_normal.png",
    "SpecularTexture": "Asset/Textures/model_specular.png"
  },
  "Parameters": { "SectionColor": [1, 1, 1, 1] }
  ```

- 텍스처가 전혀 없으면 `DiffuseColor`만 `Parameters.SectionColor`로 기록 (폴백):

  ```json
  "Parameters": { "SectionColor": [R, G, B, 1.0] }
  ```

- 디스크 쓰기는 `#if !IS_GAME_CLIENT` 가드가 있어, **에디터/임포트 환경에서만** 실제로 `.mat` 파일이 생성된다.

### 5.2 `BuildStaticMaterials` / `BuildSkeletalMaterials` ([FbxMaterialImportUtils.cpp:137](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMaterialImportUtils.cpp), [:179](../KraftonEngine/Source/Engine/Mesh/FBX/FbxMaterialImportUtils.cpp))

- 메시 섹션의 `MaterialSlotName` 기준으로 중복 제거 (`AddedSlotNames` TSet)
- 각 슬롯에 대해:
  1. `FindMaterialInfoBySlotName` → 슬롯명을 normalize한 키로 `ImportMeta.MaterialNameToMaterialId` 조회
  2. `ConvertFbxMaterialInfoToMat` → `.mat` 경로 확정 (필요하면 디스크에 쓰기)
  3. `FMaterialManager::GetOrCreateMaterial(MatPath)` → `UMaterial*`
  4. 결과를 `FMeshMaterial`에 담아 `OutMaterials`에 추가
- 매칭에 실패하거나 슬롯이 하나도 없으면 `"None"` 폴백 머티리얼을 1개 추가하여 빈 배열을 만들지 않는다.

---

## 6. 텍스처 로드 — `UTexture2D`

위치: [Texture2D.cpp](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp), [Texture2D.h](../KraftonEngine/Source/Engine/Texture/Texture2D.h).

### 6.1 `LoadFromFile` ([Texture2D.cpp:54](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp))

- 정적 전역 `TextureCache` ([Texture2D.cpp:12](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp), `std::map<FString, UTexture2D*>`)에서 경로로 캐시 히트 검사 → 히트 시 즉시 반환.
- 미스 시 `UObjectManager::Get().CreateObject<UTexture2D>()`로 객체 생성 후 `LoadInternal`. 실패하면 객체를 파괴.

### 6.2 `LoadInternal` ([Texture2D.cpp:90](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp))

1. `FPaths::TryResolvePackagePath(FilePath, WidePath)` — `Asset/...` → 디스크 절대 경로 (wide). 실패 시 `Invalid texture path: ...` 로그 ([Texture2D.cpp:96](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp)).
2. `DirectX::CreateWICTextureFromFileEx(...)` — D3D11 SRV 생성. 플래그 `WIC_LOADER_IGNORE_SRGB`로 **UNORM 강제** (sRGB 메타데이터 무시). 실패 시 `Failed to load texture: ...` 로그 ([Texture2D.cpp:113](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp)).
3. `ID3D11Texture2D` 인터페이스로 `Width/Height` 추출.
4. `MemoryStats::CalculateTextureMemory` → `AddTextureMemory`로 GPU 메모리 추적.
5. `SourceFilePath = FilePath` 저장.

WIC가 지원하는 포맷이라면 PNG/JPG/DDS/BMP/TIFF 등이 모두 가능하다.

### 6.3 해제

- 소멸자 ([Texture2D.cpp:14](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp))에서 SRV Release + `MemoryStats::SubTextureMemory` + `TextureCache`에서 자기 자신 제거.
- 일괄 정리: `UTexture2D::ReleaseAllGPU()` ([Texture2D.cpp:36](../KraftonEngine/Source/Engine/Texture/Texture2D.cpp)) — 디바이스 lost/리셋 시 사용.

---

## 7. 머티리얼 ↔ 텍스처 바인딩

위치: [Material.h](../KraftonEngine/Source/Engine/Materials/Material.h), [Material.cpp](../KraftonEngine/Source/Engine/Materials/Material.cpp).

### 7.1 보관

- `TMap<FString, UTexture2D*> TextureParameters` ([Material.h:87](../KraftonEngine/Source/Engine/Materials/Material.h)) — **슬롯 이름** 키 (예: `"DiffuseTexture"`, `"NormalTexture"` 등 `.mat` JSON의 키)로 텍스처를 관리.
- `ID3D11ShaderResourceView* CachedSRVs[(int)EMaterialTextureSlot::Max]` ([Material.h:95](../KraftonEngine/Source/Engine/Materials/Material.h)) — 렌더 패스의 map lookup을 피하기 위한 평면 배열. `Max = 8`이므로 정확히 t0~t7과 매칭.

### 7.2 변경 API

- `SetTextureParameter(ParamName, UTexture2D*)` ([Material.h:117](../KraftonEngine/Source/Engine/Materials/Material.h)) — `TextureParameters` 갱신 (캐시 재구축은 별도 호출).
- `RebuildCachedSRVs()` ([Material.h:190](../KraftonEngine/Source/Engine/Materials/Material.h)) — `TextureParameters` 맵을 `CachedSRVs[Slot]` 배열로 펼침. 텍스처 로드 직후나 머티리얼 생성 직후 호출.
- `SetCachedSRV(Slot, SRV)` ([Material.h:193](../KraftonEngine/Source/Engine/Materials/Material.h)) — `UTexture2D` 없이 raw SRV만 바인딩하는 우회 경로. 실제 사용처는 `feat/gizmo` 기준:
  - [DecalSceneProxy.cpp](../KraftonEngine/Source/Engine/Decal/DecalSceneProxy.cpp) — `DecalMaterial`의 `CachedSRVs`를 프록시 머티리얼로 복사하여 사용
  - [SubUVComponent.cpp](../KraftonEngine/Source/Engine/Particles/SubUVComponent.cpp) — 파티클 텍스처를 `EMaterialTextureSlot::Diffuse`에 직결
  - (참고) `GizmoSceneProxy`는 **텍스처를 쓰지 않는다**. 색/축/선택 상태는 `BindPerShaderCB<FGizmoConstants>`로 상수 버퍼로 전달되므로 t0~t7 슬롯과 무관하다.

### 7.3 바인딩

- `UMaterial::Bind(Context)` ([Material.h:128](../KraftonEngine/Source/Engine/Materials/Material.h)) — 상수 버퍼 업로드 + `CachedSRVs`를 픽셀 셰이더 t0..t7에 바인딩.
- 메시 섹션 단에서는 `Section.MaterialIndex` → `FMeshMaterial.MaterialInterface->Bind()` 형태로 도달.

### 7.4 메시 섹션과 머티리얼 슬롯의 연결

```
FMeshSection.MaterialSlotName            (메시 섹션이 가리키는 슬롯 이름)
        │
        ▼  (BuildStaticMaterials / BuildSkeletalMaterials가 이름 기준 매칭)
FMeshMaterial.MaterialSlotName + MaterialInterface (UMaterial*)
        │
        ▼  (UMaterial 내부, TextureParameters 의 key)
"DiffuseTexture" / "NormalTexture" / ... → UTexture2D*
        │
        ▼  (RebuildCachedSRVs)
CachedSRVs[EMaterialTextureSlot::Diffuse..AO/Custom0/1]
        │
        ▼  (UMaterial::Bind → PS t0..t7)
픽셀 셰이더
```

---

## 8. 경로 해상도 — `FPaths`

위치: [Paths.h](../KraftonEngine/Source/Engine/Platform/Paths.h).

- `FPaths::TryResolvePackagePath(PackagePath, OutWide, OutErr)` ([Paths.h:45](../KraftonEngine/Source/Engine/Platform/Paths.h)) — `Asset/...` 형식의 패키지 경로를 프로젝트 루트 기준 디스크 경로로 해석. `.mat` 디스크 쓰기, `UTexture2D` 로드, 일반 자산 로드에서 공통으로 사용.
- FBX 내부에 들어 있는 텍스처 상대 경로는 패키지 경로가 아니라 **FBX 파일 위치 기준**이므로 `BuildTextureSearchBaseDirs(SourceFbxDir)`로 별도 검색을 거쳐 디스크에서 실제 위치를 찾은 뒤, 그 결과를 `Asset/...` 패키지 경로로 다시 표현해 `.mat` JSON에 기록한다.

---

## 9. 씬 트리 구조와의 관계

씬 트리 구조와 텍스처 매핑은 **직교**한다 (트리는 노드 계층, 텍스처는 머티리얼 슬롯). 다만 임포트 산출물은 트리 형태로 보존되어, 한 줄로 묶으면:

> **트리의 컴포넌트 → 메시 → 메시 섹션 → 머티리얼 슬롯 → 텍스처**

관련 자료구조:

- `FFBXSceneComponentDesc` ([FBXImporter.h](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImporter.h)) — FBX 노드별 컴포넌트 기술자. `Type`, `Name`, `SourceNodeId`, `RelativeTransform`, `StaticMeshAssetIndex` / `SkeletalMeshAssetIndex`.
- `FFbxNodeMeta::ParentNodeId / ChildNodeIds` ([FBXImportMeta.h:7](../KraftonEngine/Source/Engine/Mesh/FBX/FBXImportMeta.h) 부근) — 원본 FBX 노드 계층 그대로 보존.
- `UFBXSceneAsset::SceneComponents` ([FBXSceneAsset.h](../KraftonEngine/Source/Engine/Mesh/FBX/FBXSceneAsset.h)) — 직렬화된 씬.

---

## 10. 로그 키워드 (디버깅용 grep 포인트)

| 키워드 | 의미 |
|---|---|
| `[FBXImporter] Resolved {Role} texture.` | 텍스처 디스크 경로 해상도 성공 (절대/상대 경로 매칭) |
| `[FBXImporter] Resolved {Role} texture by filename search.` | 파일명만으로 폴더 트리 검색하여 폴백 성공 |
| `[FBXImporter] Texture path not found.` | 해상도 실패. 검색 디렉토리 목록도 함께 출력됨 |
| `[FBXImporter] Auto material exists; skip overwrite.` | `Asset/Materials/Auto/{Slot}.mat` 이미 존재해 덮어쓰지 않음 |
| `[FBXImporter] Skeletal material. Index=...` | 스켈레탈 메시 머티리얼 빌드 결과 (슬롯명/해석된 머티리얼 경로) |
| `Invalid texture path: ...` (Texture2D) | `FPaths::TryResolvePackagePath` 실패 |
| `Failed to load texture: ...` (Texture2D) | WIC 디코딩 실패 (포맷/손상 등) |

---

## 11. 텍스처가 안 보일 때 점검 순서

1. **임포트 로그**부터: `Texture path not found.`가 찍히면 FBX 옆 폴더에 텍스처가 실제로 있는지, 검색 디렉토리에 포함되는지 (`BuildTextureSearchBaseDirs` 기준 — FBX 파일이 있는 디렉토리와 그 주변 `textures/`, `tex/`, `maps/` 등).
2. **자동 `.mat` JSON** (`Asset/Materials/Auto/{Slot}.mat`)을 열어 `Textures` 객체가 채워져 있는지 확인. 없으면 4번 단계가 실패한 것.
3. **`UTexture2D::LoadFromFile`**에서 `Invalid texture path` / `Failed to load texture` 로그 확인 → 패키지 경로 매핑(`FPaths::TryResolvePackagePath`) 점검.
4. **셰이더 슬롯 매칭**: `EMaterialTextureSlot` enum 순서(t0~t7)와 셰이더가 기대하는 텍스처 레지스터가 일치하는지. `RebuildCachedSRVs`가 머티리얼 변경 후 호출되었는지.
5. **메시 섹션 ↔ 머티리얼 슬롯 이름 매칭**: `FMeshSection.MaterialSlotName`과 `FMeshMaterial.MaterialSlotName`이 같은 normalize 규칙을 거치는지 (`FbxMaterialImportUtils::NormalizeMaterialSlotName`).

---

## 12. 본 문서가 다루지 않는 것

- `UberLit.hlsl` 내부 셰이딩 로직 — 텍스처 바인딩 이후의 PS 단계.
- 스켈레톤/스키닝 파이프라인 (`FFbxSkinMeta`, `FFbxClusterMeta`, `FFbxBoneMeta` 등) — 별도 문서가 적절.
- 빌드/프로젝트 설정.
- Gizmo 렌더링 자체 — 텍스처를 사용하지 않으므로 본 문서 범위 밖. ([GizmoSceneProxy.cpp](../KraftonEngine/Source/Engine/Gizmo/GizmoSceneProxy.cpp) 참고)

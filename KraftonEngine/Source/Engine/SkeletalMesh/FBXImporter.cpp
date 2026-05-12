#include "FBXImporter.h"
#include "SkeletalMesh.h"
#include "SkeletalMeshAsset.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Engine/Platform/Paths.h"
#include "SimpleJSON/json.hpp"
#include "Core/Log.h"

// FBX SDK Header
#ifndef FBXSDK_SHARED
#define FBXSDK_SHARED
#endif
#include <fbxsdk.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

// ---------------------------------------------------------------------------
// Import 컨텍스트: 하나의 FBX 임포트 동안 공유되는 부수 데이터.
// ---------------------------------------------------------------------------
struct FFbxImportContext
{
	FString FbxFilePath;
	FString FbxStem;                            // 확장자 제거된 FBX 파일 이름 (slot key prefix용)
	TMap<FString, FString> TextureIndex;        // lowercase basename → 프로젝트 루트 상대 경로

	TArray<FStaticMaterial> SkeletalMaterials;  // ExtractSkeletalMesh 누적
	TArray<FStaticMaterial> StaticMaterials;    // ExtractStaticMesh 누적
};

// ---------------------------------------------------------------------------
// Material / Texture helpers (anonymous)
// ---------------------------------------------------------------------------
namespace
{
	// lowercase basename
	static std::string ToLowerBasename(const std::filesystem::path& P)
	{
		std::string Base = FPaths::ToUtf8(P.filename().wstring());
		std::transform(Base.begin(), Base.end(), Base.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
		return Base;
	}

	// 파일명에서 안전하지 않은 문자를 '_'로 치환 (Windows 파일 시스템 호환)
	static FString SanitizeForFilename(const FString& In)
	{
		FString Out = In;
		for (auto& c : Out)
		{
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
			    c == '"' || c == '<' || c == '>' || c == '|')
			{
				c = '_';
			}
		}
		return Out;
	}

	static bool IsImageExtension(const std::wstring& Ext)
	{
		std::wstring E = Ext;
		std::transform(E.begin(), E.end(), E.begin(), ::towlower);
		return E == L".png" || E == L".jpg" || E == L".jpeg" || E == L".tga" ||
		       E == L".bmp" || E == L".dds"  || E == L".tif"  || E == L".tiff" ||
		       E == L".gif" || E == L".hdr"  || E == L".exr"  || E == L".webp";
	}

	// FBX 파일이 위치한 디렉토리(D)를 root로 recursive_directory_iterator 순회 후
	// lowercase basename + lowercase stem(확장자 제거) → 프로젝트 루트 상대 경로 맵 구축.
	// 디렉토리 이름은 가정하지 않음 (사용자 요구).
	// 이미지 파일만 인덱싱 — .meta / .txt 등은 노이즈로 무시.
	static TMap<FString, FString> BuildFbxTextureSearchIndex(const FString& FbxFilePath)
	{
		TMap<FString, FString> Out;

		std::wstring FbxDisk;
		FString ResolveError;
		if (!FPaths::TryResolvePackagePath(FbxFilePath, FbxDisk, &ResolveError))
		{
			FbxDisk = FPaths::ToWide(FbxFilePath);
		}

		std::filesystem::path FbxPath(FbxDisk);
		std::filesystem::path Dir = FbxPath.parent_path();
		if (Dir.empty() || !std::filesystem::exists(Dir))
		{
			UE_LOG("[TextureIndex] FBX folder not found: %s", FbxFilePath.c_str());
			return Out;
		}

		const std::filesystem::path ProjectRoot(FPaths::RootDir());

		int IndexedCount = 0;
		try
		{
			for (const auto& Entry : std::filesystem::recursive_directory_iterator(Dir))
			{
				if (!Entry.is_regular_file()) continue;
				const std::filesystem::path& P = Entry.path();
				if (!IsImageExtension(P.extension().wstring())) continue;

				FString Rel = FPaths::ToUtf8(P.lexically_relative(ProjectRoot).generic_wstring());

				// basename (확장자 포함) 키
				FString BaseKey = ToLowerBasename(P);
				if (Out.find(BaseKey) == Out.end()) Out[BaseKey] = Rel;

				// stem (확장자 제외) 키 — material/section 이름 fallback 매칭용
				std::string StemKey = FPaths::ToUtf8(P.stem().wstring());
				std::transform(StemKey.begin(), StemKey.end(), StemKey.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				if (Out.find(StemKey) == Out.end()) Out[StemKey] = Rel;

				++IndexedCount;
			}
		}
		catch (...) { /* ignore filesystem errors */ }

		UE_LOG("[TextureIndex] FBX folder '%s': %d image files indexed.",
			FPaths::ToUtf8(Dir.wstring()).c_str(), IndexedCount);
		return Out;
	}

	// Tex의 GetFileName / GetRelativeFileName 에서 basename만 떼서 인덱스에 매칭.
	//
	// FBX SDK 의 char* 경로는 UTF-8 이지만, Windows 에서
	//   std::filesystem::path(const char*)
	// 는 입력을 system ACP (한국 Windows = CP-949) 로 해석한다 (C++17 표준).
	// → 한자/한글 byte sequence 가 ACP 로 잘못 디코딩되어 mojibake wstring 이 만들어지고,
	//   인덱스(UTF-8 기반)와 매칭 실패.
	// 프로젝트 convention 인 FPaths 변환 헬퍼 (CP_UTF8 기반) 로 wstring 화한 뒤 path 를 만든다.
	static FString ResolveFbxTexturePath(const fbxsdk::FbxFileTexture* Tex,
	                                     const TMap<FString, FString>& Index)
	{
		if (!Tex) return FString();

		auto TryFind = [&](const char* RawPath) -> FString {
			if (!RawPath || !*RawPath) return FString();
			std::filesystem::path P(FPaths::ToWide(std::string(RawPath)));
			FString Key = ToLowerBasename(P);
			auto It = Index.find(Key);
			if (It != Index.end()) return It->second;
			return FString();
		};

		FString R = TryFind(Tex->GetFileName());
		if (!R.empty()) return R;
		R = TryFind(Tex->GetRelativeFileName());
		return R;
	}

	// FbxSurfaceMaterial의 Diffuse 속성에 연결된 첫 번째 FbxFileTexture 경로 추출.
	static FString ExtractDiffuseTexturePath(const fbxsdk::FbxSurfaceMaterial* Mat,
	                                        const TMap<FString, FString>& Index)
	{
		if (!Mat) return FString();
		fbxsdk::FbxProperty Prop = Mat->FindProperty(fbxsdk::FbxSurfaceMaterial::sDiffuse);
		if (!Prop.IsValid()) return FString();

		int TexCount = Prop.GetSrcObjectCount<fbxsdk::FbxFileTexture>();
		for (int i = 0; i < TexCount; ++i)
		{
			fbxsdk::FbxFileTexture* Tex = Prop.GetSrcObject<fbxsdk::FbxFileTexture>(i);
			FString Resolved = ResolveFbxTexturePath(Tex, Index);
			if (!Resolved.empty()) return Resolved;
			if (Tex)
			{
				// 진단: FBX 가 보고한 경로 + 우리가 인덱스에서 찾은 lookup key 를 함께 출력.
				// 인덱스에 등록된 키 목록과 비교 가능 (encoding mismatch 등 잔존 케이스 디버깅용).
				const char* RawPath = Tex->GetFileName();
				std::filesystem::path P(FPaths::ToWide(std::string(RawPath ? RawPath : "")));
				std::string LookupKey = ToLowerBasename(P);
				UE_LOG("[Tex] FBX-reported texture not found in folder: %s  (lookup key='%s')",
					RawPath ? RawPath : "", LookupKey.c_str());
			}
		}
		return FString();
	}

	// Fallback: material/section 이름과 lowercase 매칭되는 텍스처를 인덱스에서 검색.
	// FBX 가 sDiffuse 슬롯에 텍스처를 들지 않은 경우(흔함)에 사용한다.
	// 인덱스는 basename + stem 두 키를 모두 등록해 두므로 확장자 무관 매칭.
	static FString ResolveTextureByMaterialName(const FString& MatName,
	                                            const TMap<FString, FString>& Index)
	{
		if (MatName.empty()) return FString();
		FString Key = MatName;
		std::transform(Key.begin(), Key.end(), Key.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
		auto It = Index.find(Key);
		if (It != Index.end()) return It->second;
		return FString();
	}

	// Lambert/Phong의 Diffuse RGB. 없으면 (1, 0, 1) 마젠타 폴백.
	static FVector ExtractDiffuseColor(const fbxsdk::FbxSurfaceMaterial* Mat)
	{
		if (!Mat) return FVector(1.f, 0.f, 1.f);

		if (Mat->GetClassId().Is(fbxsdk::FbxSurfaceLambert::ClassId) ||
		    Mat->GetClassId().Is(fbxsdk::FbxSurfacePhong::ClassId))
		{
			const fbxsdk::FbxSurfaceLambert* Lambert = (const fbxsdk::FbxSurfaceLambert*)Mat;
			fbxsdk::FbxDouble3 D = Lambert->Diffuse.Get();
			return FVector((float)D[0], (float)D[1], (float)D[2]);
		}
		return FVector(1.f, 0.f, 1.f);
	}

	static FString MakeMatSlotKey(const FString& FbxStem, const FString& MaterialName)
	{
		return SanitizeForFilename(FbxStem) + "__" + SanitizeForFilename(MaterialName);
	}

	// .mat JSON 파일 생성.
	// 정책:
	//   - 파일이 없으면 새로 생성.
	//   - 이미 있고 Origin이 "FbxImport" 면 자동 생성된 파일이므로 갱신 (텍스처 자동 매핑 결과 반영).
	//   - 이미 있고 Origin이 다르면(사용자 편집 등) 보존하고 그 경로만 반환.
	static FString ConvertFbxMaterialToMat(const FString& MatSlotKey,
	                                      const FString& DiffusePath,
	                                      const FVector& DiffuseColor)
	{
		FString MatPath = "Asset/Materials/Auto/" + MatSlotKey + ".mat";

		std::wstring MatDiskPath;
		FString Error;
		if (!FPaths::TryResolvePackagePath(MatPath, MatDiskPath, &Error))
		{
			return "";
		}

		std::filesystem::path DiskPath(MatDiskPath);
		if (std::filesystem::exists(DiskPath))
		{
			// Origin 검사 — SimpleJSON 으로 정확히 파싱 (dump 시 공백/줄바꿈 형식 차이에 견고).
			// SimpleJSON 의 기본 dump 는 `"Origin" : "FbxImport"` (콜론 양쪽 공백) 이라 단순
			// substring 매칭은 부정확.
			std::ifstream Probe(DiskPath, std::ios::binary);
			std::stringstream Buf;
			Buf << Probe.rdbuf();
			json::JSON Existing = json::JSON::Load(Buf.str());

			const bool bAutoGenerated =
				!Existing.IsNull()
				&& Existing.hasKey("Origin")
				&& Existing["Origin"].ToString() == "FbxImport";

			if (!bAutoGenerated)
			{
				return MatPath; // 사용자가 편집한 머티리얼 — 보존
			}
			// auto-generated → 아래에서 다시 쓴다.
		}

		std::filesystem::create_directories(DiskPath.parent_path());

		json::JSON JsonData;
		JsonData["PathFileName"] = MatPath;
		JsonData["Origin"]       = "FbxImport";
		JsonData["ShaderPath"]   = "Shaders/Geometry/UberLit.hlsl";
		JsonData["RenderPass"]   = "Opaque";

		if (!DiffusePath.empty())
		{
			JsonData["Textures"]["DiffuseTexture"] = DiffusePath;
			JsonData["Parameters"]["SectionColor"][0] = 1.0f;
			JsonData["Parameters"]["SectionColor"][1] = 1.0f;
			JsonData["Parameters"]["SectionColor"][2] = 1.0f;
			JsonData["Parameters"]["SectionColor"][3] = 1.0f;
		}
		else
		{
			JsonData["Parameters"]["SectionColor"][0] = DiffuseColor.X;
			JsonData["Parameters"]["SectionColor"][1] = DiffuseColor.Y;
			JsonData["Parameters"]["SectionColor"][2] = DiffuseColor.Z;
			JsonData["Parameters"]["SectionColor"][3] = 1.0f;
		}

#if IS_GAME_CLIENT
		return MatPath;
#else
		std::ofstream File(std::filesystem::path(MatDiskPath), std::ios::binary);
		File << JsonData.dump();
		return MatPath;
#endif
	}

	// mesh node가 가진 FBX 머티리얼들을 OutMaterials 슬롯 배열에 등록(중복 없이).
	// FbxMat 로컬 인덱스 → OutMaterials 인덱스로 매핑된 배열을 반환.
	static TArray<int32> CollectNodeMaterials(fbxsdk::FbxNode* Owner,
	                                          TArray<FStaticMaterial>& OutMaterials,
	                                          const FString& FbxStem,
	                                          const TMap<FString, FString>& TextureIndex)
	{
		const int FbxMatCount = Owner ? Owner->GetMaterialCount() : 0;
		TArray<int32> NodeMatToOut;
		NodeMatToOut.assign(FbxMatCount, -1);

		for (int i = 0; i < FbxMatCount; ++i)
		{
			fbxsdk::FbxSurfaceMaterial* FbxMat = Owner->GetMaterial(i);
			if (!FbxMat) continue;

			FString MatName = FbxMat->GetName();
			FString SlotKey = MakeMatSlotKey(FbxStem, MatName);

			int32 ExistingIdx = -1;
			for (size_t s = 0; s < OutMaterials.size(); ++s)
			{
				if (OutMaterials[s].MaterialSlotName == SlotKey)
				{
					ExistingIdx = (int32)s;
					break;
				}
			}
			if (ExistingIdx >= 0)
			{
				NodeMatToOut[i] = ExistingIdx;
				continue;
			}

			// 1차: FBX 의 sDiffuse 슬롯에서 텍스처 경로 추출.
			FString TexPath = ExtractDiffuseTexturePath(FbxMat, TextureIndex);
			const char* ResolveSource = "from FBX sDiffuse";

			// 2차 fallback: material 이름과 텍스처 이름이 일치하는 경우 자동 매핑.
			// (Furina.fbx 처럼 sDiffuse 가 비어 있고 폴더에 '体.png' 같은 동명 파일이 있을 때 핵심.)
			if (TexPath.empty())
			{
				TexPath = ResolveTextureByMaterialName(MatName, TextureIndex);
				if (!TexPath.empty()) ResolveSource = "by material-name auto-map";
			}

			FVector DiffCol = ExtractDiffuseColor(FbxMat);
			FString MatPath = ConvertFbxMaterialToMat(SlotKey, TexPath, DiffCol);

			FStaticMaterial NewSlot;
			NewSlot.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial(MatPath);
			NewSlot.MaterialSlotName  = SlotKey;
			OutMaterials.push_back(NewSlot);
			NodeMatToOut[i] = (int32)(OutMaterials.size() - 1);

			if (TexPath.empty())
			{
				UE_LOG("[Tex] slot '%s' (mat='%s') → COLOR ONLY (no texture matched)",
					SlotKey.c_str(), MatName.c_str());
			}
			else
			{
				UE_LOG("[Tex] slot '%s' (mat='%s') → '%s' (%s)",
					SlotKey.c_str(), MatName.c_str(), TexPath.c_str(), ResolveSource);
			}
		}

		return NodeMatToOut;
	}

	// "Default" 슬롯이 OutMaterials에 없으면 추가하고 그 인덱스를 반환.
	static int32 EnsureDefaultMaterialSlot(TArray<FStaticMaterial>& OutMaterials)
	{
		for (size_t i = 0; i < OutMaterials.size(); ++i)
		{
			if (OutMaterials[i].MaterialSlotName == "Default") return (int32)i;
		}
		FStaticMaterial Slot;
		Slot.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
		Slot.MaterialSlotName  = "Default";
		OutMaterials.push_back(Slot);
		return (int32)(OutMaterials.size() - 1);
	}

	// mesh의 polygon별 (node-local) material 인덱스 추출.
	// FbxMatCount==0 또는 element-material 미존재 시 -1로 채움.
	static TArray<int32> ExtractPolygonMaterialIndices(fbxsdk::FbxMesh* Mesh, int FbxMatCount)
	{
		const int PolygonCount = Mesh->GetPolygonCount();
		TArray<int32> PolyMatIdx;
		PolyMatIdx.assign(PolygonCount, -1);

		if (FbxMatCount <= 0) return PolyMatIdx;

		fbxsdk::FbxLayerElementMaterial* MatElem = Mesh->GetElementMaterial(0);
		if (!MatElem) return PolyMatIdx;

		auto MappingMode = MatElem->GetMappingMode();
		auto& IdxArray   = MatElem->GetIndexArray();

		if (MappingMode == fbxsdk::FbxLayerElement::eAllSame)
		{
			int32 Single = (IdxArray.GetCount() > 0) ? IdxArray.GetAt(0) : 0;
			for (int p = 0; p < PolygonCount; ++p) PolyMatIdx[p] = Single;
		}
		else if (MappingMode == fbxsdk::FbxLayerElement::eByPolygon)
		{
			for (int p = 0; p < PolygonCount && p < IdxArray.GetCount(); ++p)
				PolyMatIdx[p] = IdxArray.GetAt(p);
		}
		return PolyMatIdx;
	}
} // anonymous namespace

/**
 * FBX 전용 행렬(FbxAMatrix)을 엔진의 FMatrix로 변환.
 * FbxAMatrix는 row 3에 Translation을 저장하는 row-major-호환 메모리 컨벤션이므로
 * (엔진 FMatrix와 동일), transpose 없이 직접 복사한다.
 */
static FMatrix FbxMatrixToFMatrix(const fbxsdk::FbxAMatrix& FbxMat)
{
	FMatrix OutMat;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			OutMat.M[r][c] = (float)FbxMat.Get(r, c);
		}
	}
	return OutMat;
}

/**
 * Scene 내에서 eSkeleton 속성을 가진 모든 조인트 노드를 재귀적으로 수집.
 */
static void GatherJoints(fbxsdk::FbxNode* Node, TArray<fbxsdk::FbxNode*>& OutJoints)
{
	if (!Node) return;

	fbxsdk::FbxNodeAttribute* Attr = Node->GetNodeAttribute();
	if (Attr && Attr->GetAttributeType() == fbxsdk::FbxNodeAttribute::eSkeleton)
	{
		OutJoints.push_back(Node);
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		GatherJoints(Node->GetChild(i), OutJoints);
	}
}

/**
 * Cluster의 (TransformLinkMatrix L, TransformMatrix M)를 본 노드별로 수집.
 * LocalTransform을 TLM 역산으로 만들기 위한 사전 데이터.
 */
struct FBindPoseInfo
{
	fbxsdk::FbxAMatrix TransformLink;   // L: 본 글로벌
	fbxsdk::FbxAMatrix TransformMesh;   // M: 메시 노드 글로벌
	bool bValid = false;
};

static void GatherBindPoseInfo(fbxsdk::FbxNode* Node, TMap<fbxsdk::FbxNode*, FBindPoseInfo>& Out)
{
	if (!Node) return;

	fbxsdk::FbxNodeAttribute* Attr = Node->GetNodeAttribute();
	if (Attr && Attr->GetAttributeType() == fbxsdk::FbxNodeAttribute::eMesh)
	{
		fbxsdk::FbxMesh* Mesh = (fbxsdk::FbxMesh*)Attr;
		int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
		for (int i = 0; i < SkinCount; ++i)
		{
			fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(i, fbxsdk::FbxDeformer::eSkin);
			int ClusterCount = Skin->GetClusterCount();
			for (int j = 0; j < ClusterCount; ++j)
			{
				fbxsdk::FbxCluster* Cluster = Skin->GetCluster(j);
				fbxsdk::FbxNode* Link = Cluster->GetLink();
				if (!Link) continue;

				FBindPoseInfo Info;
				Cluster->GetTransformLinkMatrix(Info.TransformLink);
				Cluster->GetTransformMatrix(Info.TransformMesh);
				Info.bValid = true;
				Out[Link] = Info;
			}
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		GatherBindPoseInfo(Node->GetChild(i), Out);
	}
}

bool FFbxImporter::ImportFbx(const FString& FilePath,
                             USkeletalMesh* OutSkeletal,
                             UStaticMesh*   OutStatic)
{
	if (!OutSkeletal && !OutStatic) return false;

	// 1. FbxManager 생성
	FbxManager* SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		UE_LOG("Failed to create FBX Manager.");
		return false;
	}

	// 2. IO Settings 설정
	FbxIOSettings* ios = FbxIOSettings::Create(SdkManager, IOSROOT);
	SdkManager->SetIOSettings(ios);

	// 3. Importer 생성 및 파일 로드
	FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, SdkManager->GetIOSettings()))
	{
		UE_LOG("Failed to initialize FBX Importer: %s", FilePath.c_str());
		Importer->Destroy();
		SdkManager->Destroy();
		return false;
	}

	// 4. Scene 생성 및 데이터 채우기
	FbxScene* Scene = FbxScene::Create(SdkManager, "MyScene");
	Importer->Import(Scene);
	Importer->Destroy();

	// 5. 좌표계 변환 (엔진 규격: Z-Up, Left-Handed, X-Forward)
	FbxAxisSystem EngineAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityEven, FbxAxisSystem::eLeftHanded);
	EngineAxisSystem.ConvertScene(Scene);

	// 6. Raw output 컨테이너 준비
	FSkeletalMesh* RawSkel   = OutSkeletal ? new FSkeletalMesh() : nullptr;
	FStaticMesh*   RawStatic = OutStatic   ? new FStaticMesh()   : nullptr;
	if (RawSkel)   RawSkel->PathFileName   = FilePath;
	if (RawStatic) RawStatic->PathFileName = FilePath;

	// 6-b. Import 컨텍스트 셋업 — FBX 파일 폴더 기준 텍스처 인덱스 1회 빌드.
	FFbxImportContext Ctx;
	Ctx.FbxFilePath = FilePath;
	{
		std::filesystem::path P(FPaths::ToWide(FilePath));
		Ctx.FbxStem = FPaths::ToUtf8(P.stem().wstring());
	}
	Ctx.TextureIndex = BuildFbxTextureSearchIndex(FilePath);

	// 7. 스켈레톤 먼저 추출 (본 인덱스 매핑을 위해)
	if (OutSkeletal)
	{
		ExtractSkeleton(Scene, OutSkeletal, RawSkel);
	}

	// 8. 노드 순회 — skin 여부에 따라 분기
	FbxNode* RootNode = Scene->GetRootNode();
	if (RootNode)
	{
		ProcessNode(RootNode, OutSkeletal, RawSkel, OutStatic, RawStatic, Ctx);
	}

	// 9. cluster weight 정규화 (per-vertex sum = 1.0)
	if (RawSkel)
	{
		NormalizeClusterWeights(RawSkel);
	}

	// 10. 결과를 asset에 주입 (비어 있으면 해제)
	bool bHaveSkel   = (RawSkel   && !RawSkel->Vertices.empty());
	bool bHaveStatic = (RawStatic && !RawStatic->Vertices.empty());

	if (OutSkeletal)
	{
		if (bHaveSkel) OutSkeletal->SetSkeletalMeshAsset(RawSkel);
		else           { delete RawSkel; }

		if (!Ctx.SkeletalMaterials.empty())
		{
			OutSkeletal->SetStaticMaterials(std::move(Ctx.SkeletalMaterials));
		}
	}
	if (OutStatic)
	{
		if (bHaveStatic) OutStatic->SetStaticMeshAsset(RawStatic);
		else             { delete RawStatic; }

		if (!Ctx.StaticMaterials.empty())
		{
			OutStatic->SetStaticMaterials(std::move(Ctx.StaticMaterials));
		}
	}

	// 11. SDK 자원 해제
	SdkManager->Destroy();

	if (!bHaveSkel && !bHaveStatic)
	{
		UE_LOG("FBX import produced no mesh data: %s", FilePath.c_str());
		return false;
	}

	UE_LOG("Successfully imported FBX: %s (skeletal=%d, static=%d)",
		FilePath.c_str(), bHaveSkel ? 1 : 0, bHaveStatic ? 1 : 0);
	return true;
}

bool FFbxImporter::HasValidSkinDeformer(FbxMesh* Mesh, USkeletalMesh* OutMesh)
{
	if (!Mesh) return false;
	int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int i = 0; i < SkinCount; ++i)
	{
		FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(i, FbxDeformer::eSkin);
		int ClusterCount = Skin->GetClusterCount();
		for (int j = 0; j < ClusterCount; ++j)
		{
			FbxCluster* Cluster = Skin->GetCluster(j);
			FbxNode* Link = Cluster->GetLink();
			if (!Link) continue;
			// OutMesh가 주어진 경우 본 이름이 실제 스켈레톤에 매핑되는지 확인.
			if (OutMesh && OutMesh->GetBoneIndex(FName(Link->GetName())) < 0) continue;
			return true;
		}
	}
	return false;
}

void FFbxImporter::ProcessNode(FbxNode* Node,
                               USkeletalMesh* OutSkeletal, FSkeletalMesh* RawSkel,
                               UStaticMesh*   OutStatic,   FStaticMesh*   RawStatic,
                               FFbxImportContext& Ctx)
{
	if (!Node) return;

	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		FbxMesh* Mesh = (FbxMesh*)Attribute;
		const bool bSkinned = HasValidSkinDeformer(Mesh, OutSkeletal);

		if (bSkinned)
		{
			if (RawSkel && OutSkeletal)
			{
				ExtractSkeletalMesh(Mesh, RawSkel, OutSkeletal, Ctx);
			}
			else
			{
				UE_LOG("Skipping skinned mesh node '%s' (no skeletal output bound).", Node->GetName());
			}
		}
		else
		{
			if (RawStatic)
			{
				ExtractStaticMesh(Mesh, RawStatic, Ctx);
			}
			else
			{
				UE_LOG("Skipping unskinned mesh node '%s' (no static output bound).", Node->GetName());
			}
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)
	{
		ProcessNode(Node->GetChild(i), OutSkeletal, RawSkel, OutStatic, RawStatic, Ctx);
	}
}

void FFbxImporter::ExtractSkeletalMesh(FbxMesh* Mesh, FSkeletalMesh* RawMesh, USkeletalMesh* OutMesh,
                                       FFbxImportContext& Ctx)
{
	if (!Mesh || !RawMesh || !OutMesh) return;

	UE_LOG("Extracting Skeletal Mesh: %s", Mesh->GetName());

	int ControlPointsCount = Mesh->GetControlPointsCount();
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// Phase A: mesh node가 가진 머티리얼들을 Ctx.SkeletalMaterials 슬롯 배열에 등록.
	FbxNode* Owner = Mesh->GetNode();
	const int FbxMatCount = Owner ? Owner->GetMaterialCount() : 0;
	TArray<int32> NodeMatToOut = CollectNodeMaterials(Owner, Ctx.SkeletalMaterials,
	                                                  Ctx.FbxStem, Ctx.TextureIndex);
	const int32 DefaultOutIdx = (FbxMatCount > 0) ? -1
	                                              : EnsureDefaultMaterialSlot(Ctx.SkeletalMaterials);

	// Phase B: polygon별 머티리얼 인덱스(node-local) 추출.
	TArray<int32> PolyMatIdx = ExtractPolygonMaterialIndices(Mesh, FbxMatCount);

	// 1. 정점 추출 + 머티리얼별 임시 index buffer에 누적.
	//    동시에 control-point index → expanded vertex index 매핑을 만든다.
	TArray<TArray<uint32>> CPToExpanded;
	CPToExpanded.resize(ControlPointsCount);

	TArray<TArray<uint32>> PerMatIndices;
	PerMatIndices.resize(Ctx.SkeletalMaterials.size());

	int PolygonCount = Mesh->GetPolygonCount();
	for (int i = 0; i < PolygonCount; ++i)
	{
		int PolygonSize = Mesh->GetPolygonSize(i);
		if (PolygonSize != 3) continue; // 삼각형만 처리 (삼각형화 가정)

		// polygon → OutMaterials 인덱스 결정
		int32 OutMatIdx;
		if (FbxMatCount > 0)
		{
			int32 LocalMatIdx = PolyMatIdx[i];
			OutMatIdx = (LocalMatIdx >= 0 && LocalMatIdx < FbxMatCount)
			            ? NodeMatToOut[LocalMatIdx]
			            : -1;
			if (OutMatIdx < 0)
			{
				// node에 머티리얼이 있어도 polygon이 가리키는 슬롯이 비정상이면 Default 폴백
				int32 FbxFallback = EnsureDefaultMaterialSlot(Ctx.SkeletalMaterials);
				if (PerMatIndices.size() <= (size_t)FbxFallback) PerMatIndices.resize(FbxFallback + 1);
				OutMatIdx = FbxFallback;
			}
		}
		else
		{
			OutMatIdx = DefaultOutIdx;
			if (PerMatIndices.size() <= (size_t)OutMatIdx) PerMatIndices.resize(OutMatIdx + 1);
		}

		for (int j = 0; j < 3; ++j)
		{
			int CPIndex = Mesh->GetPolygonVertex(i, j);
			FNormalVertex Vertex = {}; // zero-init

			FbxVector4 Pos = ControlPoints[CPIndex];
			Vertex.pos = FVector((float)Pos[0], (float)Pos[1], (float)Pos[2]);

			FbxVector4 Normal;
			if (Mesh->GetPolygonVertexNormal(i, j, Normal))
			{
				Vertex.normal = FVector((float)Normal[0], (float)Normal[1], (float)Normal[2]);
			}

			FbxVector2 UV;
			bool bUnmapped;
			if (Mesh->GetPolygonVertexUV(i, j, "", UV, bUnmapped))
			{
				Vertex.tex = FVector2((float)UV[0], 1.0f - (float)UV[1]); // DirectX UV Flip
			}

			Vertex.color   = FVector4(1, 1, 1, 1);
			Vertex.tangent = FVector4(0, 0, 0, 1); // tangent 계산은 후속 작업

			uint32 ExpandedIndex = (uint32)RawMesh->Vertices.size();
			RawMesh->Vertices.push_back(Vertex);
			PerMatIndices[OutMatIdx].push_back(ExpandedIndex);
			CPToExpanded[CPIndex].push_back(ExpandedIndex);
		}
	}

	// 2. 머티리얼별로 OutMesh.Indices에 flush + Section 등록.
	//    (vertex 순서는 그대로 — cluster.VertexIndices 보존)
	for (size_t mi = 0; mi < PerMatIndices.size(); ++mi)
	{
		const auto& MatIndices = PerMatIndices[mi];
		if (MatIndices.empty()) continue;

		FStaticMeshSection Section;
		Section.MaterialSlotName = Ctx.SkeletalMaterials[mi].MaterialSlotName;
		Section.MaterialIndex    = (int32)mi;
		Section.FirstIndex       = (uint32)RawMesh->Indices.size();
		Section.NumTriangles     = (uint32)(MatIndices.size() / 3);

		for (uint32 idx : MatIndices) RawMesh->Indices.push_back(idx);
		RawMesh->Sections.push_back(Section);
	}

	// 3. Cluster 추출 → FBoneCluster 직접 생성
	int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int i = 0; i < SkinCount; ++i)
	{
		FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(i, FbxDeformer::eSkin);
		int ClusterCount = Skin->GetClusterCount();
		for (int j = 0; j < ClusterCount; ++j)
		{
			FbxCluster* Cluster = Skin->GetCluster(j);
			FbxNode* Link = Cluster->GetLink();
			if (!Link) continue;

			int32 BoneIndex = OutMesh->GetBoneIndex(FName(Link->GetName()));
			if (BoneIndex == -1) continue;

			// IBP 계산:
			//   FBX 공간에서 본의 메시 바인드 글로벌 = M^-1 * L 이므로
			//   IBP = (M^-1 * L)^-1 = L^-1 * M.
			FbxAMatrix FbxTransformMatrix;
			Cluster->GetTransformMatrix(FbxTransformMatrix);            // M
			FbxAMatrix FbxTransformLinkMatrix;
			Cluster->GetTransformLinkMatrix(FbxTransformLinkMatrix);    // L
			FbxAMatrix IBP_Fbx = FbxTransformLinkMatrix.Inverse() * FbxTransformMatrix;

			FBoneCluster NewCluster;
			NewCluster.BoneIndex         = BoneIndex;
			NewCluster.InverseBindMatrix = FbxMatrixToFMatrix(IBP_Fbx);

			int CPCount = Cluster->GetControlPointIndicesCount();
			int*    CPIndices = Cluster->GetControlPointIndices();
			double* CPWeights = Cluster->GetControlPointWeights();

			// expanded vertex 단위로 펼친다 (한 control point가 여러 expanded vertex로 복제됨)
			for (int k = 0; k < CPCount; ++k)
			{
				int cp = CPIndices[k];
				float w = (float)CPWeights[k];
				if (cp < 0 || cp >= (int)CPToExpanded.size()) continue;
				for (uint32 ev : CPToExpanded[cp])
				{
					NewCluster.VertexIndices.push_back(ev);
					NewCluster.Weights.push_back(w);
				}
			}

			if (!NewCluster.VertexIndices.empty())
			{
				RawMesh->Clusters.push_back(std::move(NewCluster));
			}
		}
	}

	UE_LOG("Extracted skel mesh '%s': total verts=%u, indices=%u, clusters=%d, sections=%d",
		Mesh->GetName(),
		(uint32)RawMesh->Vertices.size(),
		(uint32)RawMesh->Indices.size(),
		(int)RawMesh->Clusters.size(),
		(int)RawMesh->Sections.size());
}

void FFbxImporter::ExtractStaticMesh(FbxMesh* Mesh, FStaticMesh* RawMesh, FFbxImportContext& Ctx)
{
	if (!Mesh || !RawMesh) return;

	UE_LOG("Extracting Static Mesh: %s", Mesh->GetName());

	int ControlPointsCount = Mesh->GetControlPointsCount();
	FbxVector4* ControlPoints = Mesh->GetControlPoints();

	// Mesh node의 global transform을 vertex pos/normal에 베이크
	// (skeleton에 attach 되지 않으므로 actor transform과 함께 평탄화)
	FbxNode* Owner = Mesh->GetNode();
	FbxAMatrix NodeGlobal;
	NodeGlobal.SetIdentity();
	if (Owner)
	{
		NodeGlobal = Owner->EvaluateGlobalTransform();
	}
	const FMatrix BakeXform = FbxMatrixToFMatrix(NodeGlobal);

	// Phase A: 머티리얼 슬롯 등록
	const int FbxMatCount = Owner ? Owner->GetMaterialCount() : 0;
	TArray<int32> NodeMatToOut = CollectNodeMaterials(Owner, Ctx.StaticMaterials,
	                                                  Ctx.FbxStem, Ctx.TextureIndex);
	const int32 DefaultOutIdx = (FbxMatCount > 0) ? -1
	                                              : EnsureDefaultMaterialSlot(Ctx.StaticMaterials);

	// Phase B: polygon별 머티리얼 인덱스
	TArray<int32> PolyMatIdx = ExtractPolygonMaterialIndices(Mesh, FbxMatCount);

	// 1. 머티리얼별 임시 index buffer
	TArray<TArray<uint32>> PerMatIndices;
	PerMatIndices.resize(Ctx.StaticMaterials.size());

	int PolygonCount = Mesh->GetPolygonCount();
	for (int i = 0; i < PolygonCount; ++i)
	{
		int PolygonSize = Mesh->GetPolygonSize(i);
		if (PolygonSize != 3) continue;

		int32 OutMatIdx;
		if (FbxMatCount > 0)
		{
			int32 LocalMatIdx = PolyMatIdx[i];
			OutMatIdx = (LocalMatIdx >= 0 && LocalMatIdx < FbxMatCount)
			            ? NodeMatToOut[LocalMatIdx]
			            : -1;
			if (OutMatIdx < 0)
			{
				int32 FbxFallback = EnsureDefaultMaterialSlot(Ctx.StaticMaterials);
				if (PerMatIndices.size() <= (size_t)FbxFallback) PerMatIndices.resize(FbxFallback + 1);
				OutMatIdx = FbxFallback;
			}
		}
		else
		{
			OutMatIdx = DefaultOutIdx;
			if (PerMatIndices.size() <= (size_t)OutMatIdx) PerMatIndices.resize(OutMatIdx + 1);
		}

		for (int j = 0; j < 3; ++j)
		{
			int CPIndex = Mesh->GetPolygonVertex(i, j);
			FNormalVertex Vertex = {};

			FbxVector4 Pos = ControlPoints[CPIndex];
			FVector LocalPos((float)Pos[0], (float)Pos[1], (float)Pos[2]);
			Vertex.pos = BakeXform.TransformPositionWithW(LocalPos);

			FbxVector4 Normal;
			if (Mesh->GetPolygonVertexNormal(i, j, Normal))
			{
				FVector LocalN((float)Normal[0], (float)Normal[1], (float)Normal[2]);
				Vertex.normal = BakeXform.TransformVector(LocalN);
			}

			FbxVector2 UV;
			bool bUnmapped;
			if (Mesh->GetPolygonVertexUV(i, j, "", UV, bUnmapped))
			{
				Vertex.tex = FVector2((float)UV[0], 1.0f - (float)UV[1]);
			}

			Vertex.color   = FVector4(1, 1, 1, 1);
			Vertex.tangent = FVector4(0, 0, 0, 1);

			uint32 ExpandedIndex = (uint32)RawMesh->Vertices.size();
			RawMesh->Vertices.push_back(Vertex);
			PerMatIndices[OutMatIdx].push_back(ExpandedIndex);
		}
	}

	// 2. 머티리얼별 flush + Section 등록
	for (size_t mi = 0; mi < PerMatIndices.size(); ++mi)
	{
		const auto& MatIndices = PerMatIndices[mi];
		if (MatIndices.empty()) continue;

		FStaticMeshSection Section;
		Section.MaterialSlotName = Ctx.StaticMaterials[mi].MaterialSlotName;
		Section.MaterialIndex    = (int32)mi;
		Section.FirstIndex       = (uint32)RawMesh->Indices.size();
		Section.NumTriangles     = (uint32)(MatIndices.size() / 3);

		for (uint32 idx : MatIndices) RawMesh->Indices.push_back(idx);
		RawMesh->Sections.push_back(Section);
	}

	UE_LOG("Extracted static mesh '%s': total verts=%u, indices=%u, sections=%d",
		Mesh->GetName(),
		(uint32)RawMesh->Vertices.size(),
		(uint32)RawMesh->Indices.size(),
		(int)RawMesh->Sections.size());
}

void FFbxImporter::NormalizeClusterWeights(FSkeletalMesh* RawMesh)
{
	if (!RawMesh) return;
	if (RawMesh->Clusters.empty() || RawMesh->Vertices.empty()) return;

	// 1. per-vertex total weight 계산
	TArray<float> Totals;
	Totals.assign(RawMesh->Vertices.size(), 0.0f);
	for (const FBoneCluster& C : RawMesh->Clusters)
	{
		for (size_t i = 0; i < C.VertexIndices.size(); ++i)
		{
			uint32 vi = C.VertexIndices[i];
			if (vi < Totals.size()) Totals[vi] += C.Weights[i];
		}
	}

	// 2. 각 cluster의 weight를 vertex 기준으로 정규화
	for (FBoneCluster& C : RawMesh->Clusters)
	{
		for (size_t i = 0; i < C.VertexIndices.size(); ++i)
		{
			uint32 vi = C.VertexIndices[i];
			float  t  = (vi < Totals.size()) ? Totals[vi] : 0.0f;
			if (t > 0.0f) C.Weights[i] /= t;
		}
	}
}

void FFbxImporter::ExtractSkeleton(FbxScene* Scene, USkeletalMesh* OutMesh, FSkeletalMesh* RawMesh)
{
	UE_LOG("Extracting Skeleton from Scene.");

	// 1. 모든 조인트 노드 수집
	TArray<FbxNode*> JointNodes;
	GatherJoints(Scene->GetRootNode(), JointNodes);

	if (JointNodes.empty())
	{
		UE_LOG("No skeleton found in FBX scene.");
		return;
	}

	// 1-b. 본별 BindPose 정보(L, M) 수집 — TLM 역산용 (LocalTransform 산출에만 사용)
	TMap<FbxNode*, FBindPoseInfo> BoneBindPose;
	GatherBindPoseInfo(Scene->GetRootNode(), BoneBindPose);

	// 2. 본 이름 리스트 및 매핑 구축
	TArray<FName> BoneNames;
	for (FbxNode* JointNode : JointNodes)
	{
		BoneNames.push_back(FName(JointNode->GetName()));
	}
	OutMesh->SetBoneNames(std::move(BoneNames));

	// 3. FBone 데이터 생성 및 계층 구조 설정
	RawMesh->Bones.resize(JointNodes.size());

	int32 NumWithCluster = 0;
	int32 NumFallback    = 0;

	for (int32 i = 0; i < (int32)JointNodes.size(); ++i)
	{
		FbxNode* CurrentJoint = JointNodes[i];
		FBone& CurrentBone = RawMesh->Bones[i];

		// 부모 찾기 — Empty/Null 노드를 건너뛰고 본 트리 부모까지 거슬러 올라감
		CurrentBone.ParentIndex = -1;
		FbxNode* ParentNode = CurrentJoint->GetParent();
		while (ParentNode)
		{
			bool bFound = false;
			for (int32 j = 0; j < (int32)JointNodes.size(); ++j)
			{
				if (JointNodes[j] == ParentNode)
				{
					CurrentBone.ParentIndex = j;
					bFound = true;
					break;
				}
			}
			if (bFound) break;
			ParentNode = ParentNode->GetParent();
		}

		// LocalTransform 계산: cluster의 TLM을 부모/자식으로 역산
		//  - 루트 본:   Local = M^-1 * SelfTLM   (메시 노드 변환 흡수)
		//  - 비루트:    Local = ParentTLM^-1 * SelfTLM
		//  - cluster 없음: EvaluateLocalTransform() fallback
		FbxAMatrix LocalTransform;
		auto SelfIt = BoneBindPose.find(CurrentJoint);
		bool bUsedFallback = false;

		if (SelfIt != BoneBindPose.end() && SelfIt->second.bValid)
		{
			++NumWithCluster;
			const FbxAMatrix& SelfTLM = SelfIt->second.TransformLink;

			if (CurrentBone.ParentIndex >= 0)
			{
				FbxNode* ParentJoint = JointNodes[CurrentBone.ParentIndex];
				auto ParentIt = BoneBindPose.find(ParentJoint);
				if (ParentIt != BoneBindPose.end() && ParentIt->second.bValid)
				{
					LocalTransform = ParentIt->second.TransformLink.Inverse() * SelfTLM;
				}
				else
				{
					LocalTransform = CurrentJoint->EvaluateLocalTransform();
					bUsedFallback = true;
				}
			}
			else
			{
				LocalTransform = SelfIt->second.TransformMesh.Inverse() * SelfTLM;
			}
		}
		else
		{
			LocalTransform = CurrentJoint->EvaluateLocalTransform();
			bUsedFallback = true;
		}

		if (bUsedFallback) ++NumFallback;

		FbxVector4    T = LocalTransform.GetT();
		FbxQuaternion Q = LocalTransform.GetQ();
		FbxVector4    S = LocalTransform.GetS();

		CurrentBone.Translation = FVector((float)T[0], (float)T[1], (float)T[2]);
		CurrentBone.Rotation    = FQuat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]);
		CurrentBone.Scale       = FVector((float)S[0], (float)S[1], (float)S[2]);
		// IBP는 더 이상 FBone에 저장되지 않음 — cluster의 InverseBindMatrix로 이동.
	}

	UE_LOG("Successfully extracted %d bones.", RawMesh->Bones.size());
	UE_LOG("[BindPose Diag] Bones with cluster: %d / %d (fallback used: %d)",
		NumWithCluster, (int32)JointNodes.size(), NumFallback);

	// 진단 로그: 본 트리. parent=-1이 비루트에 나타나면 Hierarchy Gap 의심.
	int32 OrphanCount = 0;
	for (int32 i = 0; i < (int32)RawMesh->Bones.size(); ++i)
	{
		const FBone& B = RawMesh->Bones[i];
		const char* Name = JointNodes[i]->GetName();
		const char* ParentName = (B.ParentIndex >= 0) ? JointNodes[B.ParentIndex]->GetName() : "<root>";
		UE_LOG("  [%d] %s parent=%d (%s) T=(%.3f, %.3f, %.3f)",
			i, Name, B.ParentIndex, ParentName, B.Translation.X, B.Translation.Y, B.Translation.Z);
		if (i > 0 && B.ParentIndex == -1) ++OrphanCount;
	}
	if (OrphanCount > 0)
	{
		UE_LOG("[Hierarchy Diag] WARNING: %d non-root bones have ParentIndex=-1. "
			"Possible Empty/Null nodes between bones.", OrphanCount);
	}
}

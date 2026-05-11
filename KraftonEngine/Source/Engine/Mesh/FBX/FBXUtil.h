#pragma once
#include "Core/CoreTypes.h"
#include "Engine/Math/Vector.h"
#include "Engine/Math/Matrix.h"

namespace fbxsdk
{
	class FbxAMatrix;
	class FbxCluster;
	class FbxManager;
	class FbxMesh;
	class FbxNode;
	class FbxScene;
	class FbxSkin;
	class FbxSurfaceMaterial;
	class FbxVector2;
	class FbxVector4;
}

using FbxAMatrix = fbxsdk::FbxAMatrix;
using FbxCluster = fbxsdk::FbxCluster;
using FbxManager = fbxsdk::FbxManager;
using FbxMesh = fbxsdk::FbxMesh;
using FbxNode = fbxsdk::FbxNode;
using FbxScene = fbxsdk::FbxScene;
using FbxSkin = fbxsdk::FbxSkin;
using FbxSurfaceMaterial = fbxsdk::FbxSurfaceMaterial;
using FbxVector2 = fbxsdk::FbxVector2;
using FbxVector4 = fbxsdk::FbxVector4;

struct FFbxUVReadStats
{
	int32 PreferredSuccessCount = 0;
	int32 UVSetFallbackSuccessCount = 0;
	int32 ManualElementFallbackSuccessCount = 0;
	int32 GetPolygonVertexUVFailedCount = 0;
	int32 UnmappedCount = 0;
	int32 DefaultUVCount = 0;
};

class FBXUtil
{
public:
	static int32 GetNodeDepth(FbxNode* Node);
	static FVector ConvertFbxVector(const FbxVector4& V);
	static FVector2 ConvertFbxVector2(const FbxVector2& V);
	static FMatrix ConvertFbxMatrix(const FbxAMatrix& M);
	static int32 ReadMaterialIndex(FbxMesh* Mesh, int32 PolyIndex);
	static FVector ReadPosition(FbxMesh* Mesh, int32 ControlPointIndex);
	static FVector ReadNormal(FbxMesh* Mesh, int32 PolyIndex, int32 CornerIndex);
	static FVector2 ReadUV(
		FbxMesh* Mesh,
		int32 PolyIndex,
		int32 CornerIndex,
		int32 ControlPointIndex,
		int32 PolygonVertexCounter,
		const char* PreferredUVSetName,
		FFbxUVReadStats* Stats = nullptr);
	static FVector4 ReadTangent(FbxMesh* Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex);
	static int32 QuantizeFloat(float Value);
	static 	FString GetNodeName(FbxNode* Node);
};

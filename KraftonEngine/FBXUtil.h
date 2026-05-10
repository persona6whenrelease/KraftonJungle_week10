#pragma once
#include "Core/CoreTypes.h"
#include "Engine/Math/Vector.h"
#include "Engine/Math/Matrix.h"
#include <fbxsdk.h>
#include <cmath>

namespace FBXUtil
{
	inline const FVector DefaultNormal(0.0f, 0.0f, 1.0f);
	inline const FVector2 DefaultUV(0.0f, 0.0f);
	inline const FVector4 DefaultTangent(1.0f, 0.0f, 0.0f, 1.0f);

	inline int32 GetNodeDepth(FbxNode* Node)
	{
		int32 Depth = 0;
		while (Node && Node->GetParent())
		{
			++Depth;
			Node = Node->GetParent();
		}
		return Depth;
	}

	inline FVector ConvertFbxVector(const FbxVector4& V)
	{
		return FVector(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]),
			static_cast<float>(V[2]));
	}

	inline FVector2 ConvertFbxVector2(const FbxVector2& V)
	{
		return FVector2(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]));
	}

	inline FMatrix ConvertFbxMatrix(const FbxAMatrix& M)
	{
		FMatrix Result = FMatrix::Identity;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				Result.M[Row][Col] = static_cast<float>(M.Get(Row, Col));
			}
		}
		return Result;
	}

	inline int32 ReadMaterialIndex(FbxMesh* Mesh, int32 PolyIndex)
	{
		FbxLayerElementMaterial* MaterialElement = Mesh ? Mesh->GetElementMaterial() : nullptr;
		if (!MaterialElement)
		{
			return 0;
		}

		const auto MappingMode = MaterialElement->GetMappingMode();
		const auto ReferenceMode = MaterialElement->GetReferenceMode();

		if (MappingMode == FbxGeometryElement::eAllSame)
		{
			return MaterialElement->GetIndexArray().GetCount() > 0
				? MaterialElement->GetIndexArray().GetAt(0)
				: 0;
		}

		if (MappingMode == FbxGeometryElement::eByPolygon)
		{
			if (ReferenceMode == FbxGeometryElement::eIndexToDirect ||
				ReferenceMode == FbxGeometryElement::eIndex)
			{
				return (PolyIndex >= 0 && PolyIndex < MaterialElement->GetIndexArray().GetCount())
					? MaterialElement->GetIndexArray().GetAt(PolyIndex)
					: 0;
			}
			return PolyIndex;
		}

		return 0;
	}

	inline FVector ReadPosition(FbxMesh* Mesh, int32 ControlPointIndex)
	{
		FbxVector4* ControlPoints = Mesh ? Mesh->GetControlPoints() : nullptr;
		return ControlPoints ? ConvertFbxVector(ControlPoints[ControlPointIndex]) : FVector(0.0f, 0.0f, 0.0f);
	}

	inline FVector ReadNormal(FbxMesh* Mesh, int32 PolyIndex, int32 CornerIndex)
	{
		FbxVector4 FbxNormal;
		if (!Mesh || !Mesh->GetPolygonVertexNormal(PolyIndex, CornerIndex, FbxNormal))
		{
			return DefaultNormal;
		}

		FVector Normal = ConvertFbxVector(FbxNormal);
		if (Normal.IsNearlyZero())
		{
			return DefaultNormal;
		}
		return Normal.Normalized();
	}

	inline FVector2 ReadUV(FbxMesh* Mesh, int32 PolyIndex, int32 CornerIndex, const char* UVSetName)
	{
		if (!Mesh || !UVSetName)
		{
			return DefaultUV;
		}

		FbxVector2 FbxUV;
		bool bUnmapped = false;
		if (!Mesh->GetPolygonVertexUV(PolyIndex, CornerIndex, UVSetName, FbxUV, bUnmapped) || bUnmapped)
		{
			return DefaultUV;
		}

		return ConvertFbxVector2(FbxUV);
	}

	inline FVector4 ReadTangent(FbxMesh* Mesh, int32 ControlPointIndex, int32 PolygonVertexIndex)
	{
		FbxLayer* Layer = Mesh ? Mesh->GetLayer(0) : nullptr;
		if (!Layer)
		{
			return DefaultTangent;
		}

		FbxLayerElementTangent* TangentElement = Layer->GetTangents();
		if (!TangentElement)
		{
			return DefaultTangent;
		}

		int32 ElementIndex = 0;
		switch (TangentElement->GetMappingMode())
		{
		case FbxGeometryElement::eByControlPoint:
			ElementIndex = ControlPointIndex;
			break;
		case FbxGeometryElement::eByPolygonVertex:
			ElementIndex = PolygonVertexIndex;
			break;
		default:
			return DefaultTangent;
		}

		int32 DirectIndex = ElementIndex;
		if (TangentElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect ||
			TangentElement->GetReferenceMode() == FbxGeometryElement::eIndex)
		{
			if (ElementIndex < 0 || ElementIndex >= TangentElement->GetIndexArray().GetCount())
			{
				return DefaultTangent;
			}
			DirectIndex = TangentElement->GetIndexArray().GetAt(ElementIndex);
		}

		if (DirectIndex < 0 || DirectIndex >= TangentElement->GetDirectArray().GetCount())
		{
			return DefaultTangent;
		}

		const FbxVector4 FbxTangent = TangentElement->GetDirectArray().GetAt(DirectIndex);
		FVector4 Result(
			static_cast<float>(FbxTangent[0]),
			static_cast<float>(FbxTangent[1]),
			static_cast<float>(FbxTangent[2]),
			static_cast<float>(FbxTangent[3]));

		if (std::abs(Result.W) < 0.0001f)
		{
			Result.W = 1.0f;
		}

		return Result;
	}

	inline int32 QuantizeFloat(float Value)
	{
		constexpr float Scale = 100000.0f;
		return static_cast<int32>(std::round(Value * Scale));
	}
}

#include "FbxImporter.h"

#include "Core/Log.h"
#include "Engine/Platform/Paths.h"

#ifndef WITH_FBX_SDK
#define WITH_FBX_SDK 0
#endif

#if WITH_FBX_SDK
#include <fbxsdk.h>
#endif

#if WITH_FBX_SDK

static const char* GetFbxAttributeTypeName(FbxNodeAttribute::EType Type)
{
	switch (Type)
	{
	case FbxNodeAttribute::eMesh:
		return "Mesh";
	case FbxNodeAttribute::eSkeleton:
		return "Skeleton";
	case FbxNodeAttribute::eCamera:
		return "Camera";
	case FbxNodeAttribute::eLight:
		return "Light";
	case FbxNodeAttribute::eNull:
		return "Null";
	default:
		return "Other";
	}
}

static void TraverseFbxNode(FbxNode* Node, int32 Depth)
{
	if (!Node) return;

	std::string Indent(Depth * 2, ' ');
	
	const char* NodeName = Node->GetName();
	
	FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
	const char* AttributeTypeName = "NoAttribute";
	
	// 모든 Node가 Attribute를 가지는 것은 아님.
	// 그래서 검사 안 하면 Attribute가 없는 노드에서 null pointer 에러 날 수 있음.
	if (Attribute)
	{
		AttributeTypeName = GetFbxAttributeTypeName(Attribute->GetAttributeType());
		
		if(Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh* Mesh = Node->GetMesh();
			if (Mesh)
			{
				const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
				const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());

				UE_LOG("%s  MeshInfo: ControlPoints=%d, Polygons=%d", Indent.c_str(), ControlPointCount, PolygonCount);
			}
		}
	}

	UE_LOG("%s Node: %s, Type: %s", Indent.c_str(), NodeName, AttributeTypeName);
	
	const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
	for (int32 ChildIndex  = 0; ChildIndex  < ChildCount; ++ChildIndex)
	{
		TraverseFbxNode(Node->GetChild(ChildIndex), Depth + 1);
	}
}

#endif

bool FFbxImporter::CanLoadScene(const FString& FbxFilePath)
{
#if !WITH_FBX_SDK
	UE_LOG("FBX SDK is not configured. File: %s", FbxFilePath.c_str());
	return false;
#else
	std::wstring DiskPath;
	FString Error;

	if (!FPaths::TryResolvePackagePath(FbxFilePath, DiskPath, &Error))
	{
		UE_LOG("Invalid FBX file path: %s", Error.c_str());
		return false;
	}

	// FBX SDK를 쓰려면 FbxManager가 필요함.
	// FBX SDK 전체의 루트 객체 느낌. Scene, Importer, IOSettings 등을 관리.
	FbxManager* Manager = FbxManager::Create();
	if (!Manager)
	{
		UE_LOG("Failed to create FBX manager");
		return false;
	}
	
	// Importer가 파일을 읽을 때 어떤 설정들을 쓸지 Manager에 알림.
	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	Manager->SetIOSettings(IOSettings);

	FbxScene* Scene = FbxScene::Create(Manager, "ImportScene");
	FbxImporter* Importer = FbxImporter::Create(Manager, "FbxImporter");
	
	const FString Utf8DiskPath = FPaths::ToUtf8(DiskPath);
	if (!Importer->Initialize(Utf8DiskPath.c_str(), -1, Manager->GetIOSettings()))
	{
		UE_LOG("FBX Importer initialize failed: %s", Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		Manager->Destroy();	
		return false;
	}

	if(!Importer->Import(Scene))
	{
		UE_LOG("FBX scene import failed: %s", Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		Manager->Destroy();
		return false;
	}
	
	FbxNode* RootNode = Scene->GetRootNode();
	const int32 RootChildCount = RootNode ? static_cast<int32>(RootNode->GetChildCount()) : 0;
	
	UE_LOG("FBX scene loaded. File: %s. Root children: %d", FbxFilePath.c_str(), RootChildCount);
	
	if (RootNode)
	{
		TraverseFbxNode(RootNode, 0);
	}

	Importer->Destroy();
	Manager->Destroy();
	
	return true;
#endif
}
#pragma once

#include <string>
#include <filesystem>
#include <unordered_map>
#include "Core/CoreTypes.h"
#include "Platform/Paths.h"
#include "GameFramework/WorldContext.h"
#include "Math/Vector.h"

// Forward declarations
class UWorld;
class AActor;
class UActorComponent;
class USceneComponent;
class UCameraComponent;

namespace json
{
	class JSON;
}

struct FActorDeserializeOptions
{
	bool bAddToWorld = true;
	bool bInitDefaultComponentsIfMissing = true;

	// Scene loading must restore the serialized UUID so saved Actor references stay valid.
	// Prefab instancing must keep the constructor-generated UUID to avoid duplicate UUIDs.
	bool bRestoreActorUUID = true;

	// Optional old UUID -> new UUID map filled when bRestoreActorUUID is false.
	// Components that store Actor UUID references can use this map after deserialization.
	TMap<uint32, uint32>* ActorUUIDRemap = nullptr;
};

#include "Core/PropertyTypes.h"

using std::string;

// Perspective 뷰포트 카메라의 씬 스냅샷 — 씬 저장/로드 시 주고받는 순수 데이터
struct FPerspectiveCameraData
{
	FVector Location = FVector(0, 0, 0);
	FVector Rotation = FVector(0, 0, 0); // Euler (Roll, Pitch, Yaw) in degrees
	float   FOV      = 3.14159265f / 3.0f;
	float   AspectRatio = 16.0f / 9.0f;
	float   NearClip = 0.1f;
	float   FarClip  = 1000.0f;
	float   OrthoWidth = 10.0f;
	bool    bOrthographic = false;
	bool    bValid   = false;
};

class FSceneSaveManager
{
public:
	static constexpr const wchar_t* SceneExtension = L".Scene";

	static std::wstring GetSceneDirectory() { return FPaths::SceneDir(); }

	static void SaveSceneAsJSON(const string& SceneName, FWorldContext& WorldContext, UCameraComponent* PerspectiveCam = nullptr);
	static bool SaveWorldToJSONFile(const std::wstring& AbsoluteFilePath, FWorldContext& WorldContext, UCameraComponent* PerspectiveCam = nullptr);
	static void LoadSceneFromJSON(const string& filepath, FWorldContext& OutWorldContext, FPerspectiveCameraData& OutCam);

	static TArray<FString> GetSceneFileList();

	// ---- Actor/Component Serialization for Prefabs ----
	static json::JSON SerializeActor(AActor* Actor);
	static AActor* DeserializeActor(UWorld* World, json::JSON& ActorJSON, std::unordered_map<string, AActor*>* CreatedFromPrimitives = nullptr, const FActorDeserializeOptions& Options = FActorDeserializeOptions());
	static bool ApplyPrefabDataToActor(AActor* Actor, json::JSON& ActorJSON);
	
	static json::JSON SerializeSceneComponentTree(USceneComponent* Comp);
	static USceneComponent* DeserializeSceneComponentTree(json::JSON& Node, AActor* Owner);
	static void DeserializeSceneComponentIntoExisting(USceneComponent* Existing, json::JSON& Node, AActor* Owner);

	static json::JSON SerializeProperties(UActorComponent* Comp);
	static void DeserializeProperties(UActorComponent* Comp, json::JSON& PropsJSON);

	static json::JSON SerializePropertyValue(const FPropertyDescriptor& Prop);
	static void DeserializePropertyValue(FPropertyDescriptor& Prop, json::JSON& Value);

private:
	// ---- Serialization ----
	static json::JSON SerializeWorld(UWorld* World, const FWorldContext& Ctx, UCameraComponent* PerspectiveCam);

	// ---- Camera ----
	static json::JSON SerializeCamera(UCameraComponent* Cam);
	static void DeserializeCamera(json::JSON& CamJSON, FPerspectiveCameraData& OutCam);

	// ---- Primitives ----
	static void DeserializePrimitives(json::JSON& Primitives, UWorld* World, std::unordered_map<string, AActor*>& OutCreatedActors);

	static string GetCurrentTimeStamp();
};

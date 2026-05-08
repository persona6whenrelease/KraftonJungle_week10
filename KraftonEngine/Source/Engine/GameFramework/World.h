#pragma once
#include "Object/Object.h"
#include "Core/RayTypes.h"
#include "Core/CollisionTypes.h"
#include "Collision/WorldCollisionSystem.h"
#include "Collision/WorldCollisionBVH.h"
#include "Collision/WorldPrimitivePickingBVH.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Level.h"
#include "Component/CameraComponent.h"
#include "GameFramework/WorldContext.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/LODContext.h"
#include "Runtime/ActorPoolSystem.h"
#include <Collision/Octree.h>
#include <Collision/SpatialPartition.h>

class UCameraComponent;
class UPrimitiveComponent;
class UActorComponent;
class APawn;
class APlayerController;

class UWorld : public UObject {
public:
	DECLARE_CLASS(UWorld, UObject)
	UWorld() = default;
	~UWorld() override;

	// --- 월드 타입 ---
	EWorldType GetWorldType() const { return WorldType; }
	void SetWorldType(EWorldType InType) { WorldType = InType; }

	// 월드 복제 — 자체 Actor 리스트를 순회하며 각 Actor를 새 World로 Duplicate.
	// UObject::Duplicate는 Serialize 왕복만 수행하므로 UWorld처럼 컨테이너 기반 상태가 있는
	// 타입은 별도 오버라이드가 필요하다.
	UObject* Duplicate(UObject* NewOuter = nullptr) const override;

	// 지정된 WorldType으로 복제 — Actor 복제 전에 WorldType이 설정되므로
	// EditorOnly 컴포넌트의 CreateRenderState()에서 올바르게 판별 가능.
	UWorld* DuplicateAs(EWorldType InWorldType) const;

	// Actor lifecycle
	template<typename T>
	T* SpawnActor();
	template<typename T>
	T* AcquireActor(const FVector& Location, const FRotator& Rotation);
	template<typename T>
	T* AcquireActor(UClass* Class, const FVector& Location, const FRotator& Rotation);
	AActor* AcquirePrefab(const FString& PrefabPath, const FVector& Location, const FRotator& Rotation = FRotator());
	bool ReleaseActor(AActor* Actor);
	int32 WarmUpActorPool(UClass* Class, int32 Count);
	int32 WarmUpPrefabPool(const FString& PrefabPath, int32 Count);
	void DestroyActor(AActor* Actor);
	void AddActor(AActor* Actor);
	void MarkWorldPrimitivePickingBVHDirty();
	void InsertWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive);
	void RemoveWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive);
	void UpdateWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive);
	void BuildWorldPrimitivePickingBVHNow() const;
	void CollectWorldPrimitivePickingBVHDebugAABBs(TArray<FWorldPrimitivePickingBVH::FDebugAABB>& OutAABBs) const;
	void MarkWorldCollisionBVHDirty();
	void InsertWorldCollisionBVH(UPrimitiveComponent* Primitive);
	void RemoveWorldCollisionBVH(UPrimitiveComponent* Primitive);
	void UpdateWorldCollisionBVH(UPrimitiveComponent* Primitive);
	void BuildWorldCollisionBVHNow() const;
	void CollectWorldCollisionBVHDebugAABBs(TArray<FWorldCollisionBVH::FDebugAABB>& OutAABBs) const;
	void QueryPrimitivesInAABB(const FBoundingBox& Bounds, TArray<UPrimitiveComponent*>& OutCandidates) const;
	void BeginDeferredPickingBVHUpdate();
	void EndDeferredPickingBVHUpdate();
	void WarmupPickingData() const;
	bool RaycastPrimitives(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor, const FRaycastQueryParams& Params = FRaycastQueryParams()) const;

	const TArray<AActor*>& GetActors() const { return PersistentLevel->GetActors(); }

	// LOD 컨텍스트를 FFrameContext에 전달 (Collect 단계에서 LOD 인라인 갱신용)
	FLODUpdateContext PrepareLODContext(const UCameraComponent* Camera = nullptr);

	void InitWorld();      // Set up the world before gameplay begins
	void BeginPlay();      // Triggers BeginPlay on all actors
	void Tick(float GameDeltaTime, float RawDeltaTime, ELevelTick TickType);  // Drives the game loop every frame
	void EndPlay();        // Cleanup before world is destroyed

	bool HasBegunPlay() const { return bHasBegunPlay; }

	// Active Camera — EditorViewportClient 또는 PlayerController가 세팅
	void SetActiveCamera(UCameraComponent* InCamera);
	UCameraComponent* GetActiveCamera() const;

	// ViewCamera — CameraManager가 매 프레임 결정한 렌더 카메라
	void SetViewCamera(UCameraComponent* InCamera);
	UCameraComponent* GetViewCamera() const;

	// Gameplay camera / possession helpers. PIE와 Standalone 모두 같은 규칙을 쓰기 위한 공통 경로.
	bool IsActorInWorld(const AActor* Actor) const;
	bool IsComponentInWorld(const UActorComponent* Component) const;
	void CleanupActorReferences(AActor* Actor);
	void CleanupComponentReferences(UActorComponent* Component);
	UCameraComponent* FindFirstCamera() const;
	APawn* FindFirstPawn() const;
	AActor* FindFirstPossessableActor() const;
	AActor* FindActorByUUIDInWorld(uint32 ActorUUID) const;
	APlayerController* FindOrCreatePlayerController();
	void AutoWirePlayerController(APlayerController* PreferredController = nullptr);
	UCameraComponent* ResolveGameplayViewCamera(APlayerController* PreferredController = nullptr) const;

	// PlayerController 관리
	APlayerController* CreatePlayerController();
	APlayerController* GetPlayerController(int32 Index = 0) const;
	const TArray<APlayerController*>& GetPlayerControllers() const { return PlayerControllers; }

	// FScene — 렌더 프록시 관리자
	FScene& GetScene() { return Scene; }
	const FScene& GetScene() const { return Scene; }
	
	FSpatialPartition& GetPartition() { return Partition; }
	const FOctree* GetOctree() const { return Partition.GetOctree(); }
	void InsertActorToOctree(AActor* actor);
	void RemoveActorToOctree(AActor* actor);
	void UpdateActorInOctree(AActor* actor);

	void UpdateCollision();
	bool HasBlockingOverlapForActor(AActor* MovingActor, FHitResult* OutHit = nullptr);
	void ApplyCollisionDebugVisualization();
	void UpdatePlayerCameraManagers(float GameDeltaTime, float RawDeltaTime);

private:
	//TArray<AActor*> Actors;
	ULevel* PersistentLevel;

	UCameraComponent* ActiveCamera = nullptr;
	UCameraComponent* ViewCamera = nullptr;
	UCameraComponent* LastLODUpdateCamera = nullptr;
	TArray<APlayerController*> PlayerControllers;
	EWorldType WorldType = EWorldType::Editor;
	bool bHasBegunPlay = false;
	bool bHasLastFullLODUpdateCameraPos = false;
	mutable FWorldPrimitivePickingBVH WorldPrimitivePickingBVH;
	mutable FWorldCollisionSystem WorldCollisionSystem{this};
	int32 DeferredPickingBVHUpdateDepth = 0;
	bool bDeferredPickingBVHDirty = false;
	uint32 VisibleProxyBuildFrame = 0;
	FVector LastFullLODUpdateCameraForward = FVector(1, 0, 0);
	FVector LastFullLODUpdateCameraPos = FVector(0, 0, 0);
	FScene Scene;
	FTickManager TickManager;

	FSpatialPartition Partition;
};

template<typename T>
inline T* UWorld::SpawnActor()
{
	// create and register an actor
	T* Actor = UObjectManager::Get().CreateObject<T>(PersistentLevel);
	AddActor(Actor); // BeginPlay 트리거는 AddActor 내부에서 bHasBegunPlay 가드로 처리
	return Actor;
}

template<typename T>
inline T* UWorld::AcquireActor(const FVector& Location, const FRotator& Rotation)
{
	return FActorPoolSystem::Get().AcquireActor<T>(this, T::StaticClass(), Location, Rotation);
}

template<typename T>
inline T* UWorld::AcquireActor(UClass* Class, const FVector& Location, const FRotator& Rotation)
{
	return FActorPoolSystem::Get().AcquireActor<T>(this, Class, Location, Rotation);
}

#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Collision/PrimitiveCollision.h"
#include "Engine/Component/CameraComponent.h"
#include "Render/Types/LODContext.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Runtime/ActorPoolSystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Component/ActorComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/ControllerInputComponent.h"
#include "Object/Object.h"
#include <algorithm>
#include "Profiling/Stats.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/Log.h"

IMPLEMENT_CLASS(UWorld, UObject)

static void RemapActor(UWorld* NewWorld, const TMap<uint32, uint32>& ActorUUIDRemap)
{
	if (!NewWorld)
	{
		return;
	}

	for (AActor* Actor : NewWorld->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component)
			{
				Component->RemapActorReferences(ActorUUIDRemap);
			}
		}
		Actor->RemapActorReferences(ActorUUIDRemap);
	}
}

UWorld::~UWorld()
{
	if (PersistentLevel && !PersistentLevel->GetActors().empty())
	{
		EndPlay();
	}
}

UObject* UWorld::Duplicate(UObject* NewOuter) const
{
	// UE의 CreatePIEWorldByDuplication 대응 (간소화 버전).
	// 새 UWorld를 만들고, 소스의 Actor들을 하나씩 복제해 NewWorld를 Outer로 삼아 등록한다.
	// AActor::Duplicate 내부에서 Dup->GetTypedOuter<UWorld>() 경유 AddActor가 호출되므로
	// 여기서는 World 단위 상태만 챙기면 된다.
	UWorld* NewWorld = UObjectManager::Get().CreateObject<UWorld>();
	if (!NewWorld)
	{
		return nullptr;
	}
	NewWorld->SetOuter(NewOuter);
	NewWorld->InitWorld(); // Partition/VisibleSet 초기화 — 이거 없으면 복제 액터가 렌더링되지 않음

	TMap<uint32, uint32> ActorUUIDRemap;

	for (AActor* Src : GetActors())
	{
		if (!Src) continue;

		AActor* Dst = Cast<AActor>(Src->Duplicate(NewWorld));
		if (!Dst)
		{
			continue;
		}

		ActorUUIDRemap[Src->GetUUID()] = Dst->GetUUID();
	}

	RemapActor(NewWorld, ActorUUIDRemap);

	NewWorld->PostDuplicate();
	return NewWorld;
}

UWorld* UWorld::DuplicateAs(EWorldType InWorldType) const
{
	UWorld* NewWorld = UObjectManager::Get().CreateObject<UWorld>();
	if (!NewWorld) return nullptr;

	NewWorld->SetWorldType(InWorldType);
	NewWorld->InitWorld();

	TMap<uint32, uint32> ActorUUIDRemap;

	for (AActor* Src : GetActors())
	{
		if (!Src) continue;

		AActor* Dst = Cast<AActor>(Src->Duplicate(NewWorld));
		if (!Dst)
		{
			continue;
		}

		ActorUUIDRemap[Src->GetUUID()] = Dst->GetUUID();
	}

	RemapActor(NewWorld, ActorUUIDRemap);

	NewWorld->PostDuplicate();
	return NewWorld;
}

void UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor || !PersistentLevel) return;
	TickManager.RemoveTickFunction(&Actor->PrimaryActorTick);
	Actor->PrimaryActorTick.UnRegisterTickFunction();
	
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!Component)
		{
			continue;
		}

		TickManager.RemoveTickFunction(&Component->PrimaryComponentTick);
		Component->PrimaryComponentTick.UnRegisterTickFunction();
	}

	// 다른 시스템이 이 Actor/Pawn/Camera를 가리키고 있으면 파괴 전에 먼저 끊는다.
	CleanupActorReferences(Actor);
	FActorPoolSystem::Get().ForgetActor(Actor);
	Actor->SetPooledActorState(false, false);
	for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
	{
		RemoveWorldPrimitivePickingBVH(Primitive);
		RemoveWorldCollisionBVH(Primitive);
	}

	if (APlayerController* Controller = Cast<APlayerController>(Actor))
	{
		auto It = std::find(PlayerControllers.begin(), PlayerControllers.end(), Controller);
		if (It != PlayerControllers.end())
		{
			PlayerControllers.erase(It);
		}
	}

	Actor->EndPlay();
	// Remove from actor list
	PersistentLevel->RemoveActor(Actor);

	Partition.RemoveActor(Actor);

	// Mark for garbage collection
	UObjectManager::Get().DestroyObject(Actor);
}

bool UWorld::ReleaseActor(AActor* Actor)
{
	return FActorPoolSystem::Get().ReleaseActor(Actor);
}

AActor* UWorld::AcquirePrefab(const FString& PrefabPath, const FVector& Location, const FRotator& Rotation)
{
	return FActorPoolSystem::Get().AcquirePrefab(this, PrefabPath, Location, Rotation);
}

int32 UWorld::WarmUpActorPool(UClass* Class, int32 Count)
{
	return FActorPoolSystem::Get().WarmUp(this, Class, Count);
}

int32 UWorld::WarmUpPrefabPool(const FString& PrefabPath, int32 Count)
{
	return FActorPoolSystem::Get().WarmUpPrefab(this, PrefabPath, Count);
}

void UWorld::AddActor(AActor* Actor)
{
	if (!Actor || !PersistentLevel)
	{
		return;
	}

	PersistentLevel->AddActor(Actor);

	if (APlayerController* Controller = Cast<APlayerController>(Actor))
	{
		if (std::find(PlayerControllers.begin(), PlayerControllers.end(), Controller) == PlayerControllers.end())
		{
			PlayerControllers.push_back(Controller);
		}
	}

	InsertActorToOctree(Actor);
	for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
	{
		InsertWorldPrimitivePickingBVH(Primitive);
		InsertWorldCollisionBVH(Primitive);
	}

	// PIE 중 Duplicate(Ctrl+D)나 SpawnActor로 들어온 액터에도 BeginPlay를 보장.
	if (bHasBegunPlay && !Actor->HasActorBegunPlay() && !Actor->IsPooledActorInactive())
	{
		Actor->BeginPlay();
	}
}

void UWorld::MarkWorldPrimitivePickingBVHDirty()
{
	if (DeferredPickingBVHUpdateDepth > 0)
	{
		bDeferredPickingBVHDirty = true;
		return;
	}

	WorldPrimitivePickingBVH.MarkDirty();
}

void UWorld::InsertWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive)
{
	if (DeferredPickingBVHUpdateDepth > 0)
	{
		bDeferredPickingBVHDirty = true;
		return;
	}

	WorldPrimitivePickingBVH.InsertObject(Primitive);
}

void UWorld::RemoveWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive)
{
	if (DeferredPickingBVHUpdateDepth > 0)
	{
		bDeferredPickingBVHDirty = true;
		return;
	}

	WorldPrimitivePickingBVH.RemoveObject(Primitive);
}

void UWorld::UpdateWorldPrimitivePickingBVH(UPrimitiveComponent* Primitive)
{
	if (DeferredPickingBVHUpdateDepth > 0)
	{
		bDeferredPickingBVHDirty = true;
		return;
	}

	WorldPrimitivePickingBVH.UpdateObject(Primitive);
}

void UWorld::BuildWorldPrimitivePickingBVHNow() const
{
	WorldPrimitivePickingBVH.BuildNow(GetActors());
}

void UWorld::CollectWorldPrimitivePickingBVHDebugAABBs(TArray<FWorldPrimitivePickingBVH::FDebugAABB>& OutAABBs) const
{
	WorldPrimitivePickingBVH.EnsureBuilt(GetActors());
	WorldPrimitivePickingBVH.CollectDebugAABBs(OutAABBs);
}

void UWorld::MarkWorldCollisionBVHDirty()
{
	WorldCollisionSystem.MarkDirty();
}

void UWorld::InsertWorldCollisionBVH(UPrimitiveComponent* Primitive)
{
	WorldCollisionSystem.InsertObject(Primitive);
}

void UWorld::RemoveWorldCollisionBVH(UPrimitiveComponent* Primitive)
{
	WorldCollisionSystem.RemoveObject(Primitive);
}

void UWorld::UpdateWorldCollisionBVH(UPrimitiveComponent* Primitive)
{
	WorldCollisionSystem.UpdateObject(Primitive);
}

void UWorld::BuildWorldCollisionBVHNow() const
{
	WorldCollisionSystem.BuildNow(GetActors());
}

void UWorld::CollectWorldCollisionBVHDebugAABBs(TArray<FWorldCollisionBVH::FDebugAABB>& OutAABBs) const
{
	WorldCollisionSystem.EnsureBuilt(GetActors());
	WorldCollisionSystem.CollectDebugAABBs(OutAABBs);
}

void UWorld::QueryPrimitivesInAABB(const FBoundingBox& Bounds, TArray<UPrimitiveComponent*>& OutCandidates) const
{
	WorldCollisionSystem.EnsureBuilt(GetActors());
	WorldCollisionSystem.QueryAABB(Bounds, OutCandidates);
}

void UWorld::BeginDeferredPickingBVHUpdate()
{
	++DeferredPickingBVHUpdateDepth;
}

void UWorld::EndDeferredPickingBVHUpdate()
{
	if (DeferredPickingBVHUpdateDepth <= 0)
	{
		return;
	}

	--DeferredPickingBVHUpdateDepth;
	if (DeferredPickingBVHUpdateDepth == 0 && bDeferredPickingBVHDirty)
	{
		bDeferredPickingBVHDirty = false;
		BuildWorldPrimitivePickingBVHNow();
	}
}

void UWorld::WarmupPickingData() const
{
	for (AActor* Actor : GetActors())
	{
		if (!Actor || !Actor->IsVisible())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive || !Primitive->IsVisible() || !Primitive->IsA<UStaticMeshComponent>())
			{
				continue;
			}

			UStaticMeshComponent* StaticMeshComponent = static_cast<UStaticMeshComponent*>(Primitive);
			if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
			{
				StaticMesh->EnsureMeshTrianglePickingBVHBuilt();
			}
		}
	}

	BuildWorldPrimitivePickingBVHNow();
}

bool UWorld::RaycastPrimitives(const FRay& Ray, FHitResult& OutHitResult, AActor*& OutActor, const FRaycastQueryParams& Params) const
{
	//혹시라도 BVH 트리가 업데이트 되지 않았다면 업데이트
	WorldPrimitivePickingBVH.EnsureBuilt(GetActors());
	return WorldPrimitivePickingBVH.Raycast(Ray, OutHitResult, OutActor, Params);
}


void UWorld::InsertActorToOctree(AActor* Actor)
{
	Partition.InsertActor(Actor);
}

void UWorld::RemoveActorToOctree(AActor* Actor)
{
	Partition.RemoveActor(Actor);
}

void UWorld::UpdateActorInOctree(AActor* Actor)
{
	Partition.UpdateActor(Actor);
}

void UWorld::UpdateCollision()
{
	WorldCollisionSystem.UpdateCollision();
}

bool UWorld::HasBlockingOverlapForActor(AActor* MovingActor, FHitResult* OutHit)
{
	return WorldCollisionSystem.HasBlockingOverlapForActor(MovingActor, OutHit);
}

void UWorld::ApplyCollisionDebugVisualization()
{
	for (AActor* Actor : GetActors())
	{
		if (!Actor || Actor->IsPooledActorInactive() || !Actor->IsActorCollisionEnabled()) continue;
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (UShapeComponent* ShapeComp = Cast<UShapeComponent>(Primitive))
			{
				ShapeComp->SetDebugShapeColor(FColor::Green());
			}
		}
	}

	for (const FOverlapPairKey& Pair : WorldCollisionSystem.GetCurrentOverlaps())
	{
		if (UShapeComponent* ShapeA = Cast<UShapeComponent>(Pair.ComponentA))
		{
			ShapeA->SetDebugShapeColor(FColor::Red());
		}
		if (UShapeComponent* ShapeB = Cast<UShapeComponent>(Pair.ComponentB))
		{
			ShapeB->SetDebugShapeColor(FColor::Red());
		}
	}
}

FLODUpdateContext UWorld::PrepareLODContext(const UCameraComponent* Camera)
{
	const UCameraComponent* LODCamera = Camera ? Camera : GetViewCamera();
	if (!LODCamera) return {};

	const FVector CameraPos = LODCamera->GetWorldLocation();
	const FVector CameraForward = LODCamera->GetForwardVector();

	const uint32 LODUpdateFrame = VisibleProxyBuildFrame++;
	const uint32 LODUpdateSlice = LODUpdateFrame & (LOD_UPDATE_SLICE_COUNT - 1);
	const bool bShouldStaggerLOD = Scene.GetProxyCount() >= LOD_STAGGER_MIN_VISIBLE;

	const bool bForceFullLODRefresh =
		!bShouldStaggerLOD
		|| LastLODUpdateCamera != LODCamera
		|| !bHasLastFullLODUpdateCameraPos
		|| FVector::DistSquared(CameraPos, LastFullLODUpdateCameraPos) >= LOD_FULL_UPDATE_CAMERA_MOVE_SQ
		|| CameraForward.Dot(LastFullLODUpdateCameraForward) < LOD_FULL_UPDATE_CAMERA_ROTATION_DOT;

	if (bForceFullLODRefresh)
	{
		LastLODUpdateCamera = const_cast<UCameraComponent*>(LODCamera);
		LastFullLODUpdateCameraPos = CameraPos;
		LastFullLODUpdateCameraForward = CameraForward;
		bHasLastFullLODUpdateCameraPos = true;
	}

	FLODUpdateContext Ctx;
	Ctx.CameraPos = CameraPos;
	Ctx.LODUpdateFrame = LODUpdateFrame;
	Ctx.LODUpdateSlice = LODUpdateSlice;
	Ctx.bForceFullRefresh = bForceFullLODRefresh;
	Ctx.bValid = true;
	return Ctx;
}

void UWorld::InitWorld()
{
	Partition.Reset(FBoundingBox());
	PersistentLevel = UObjectManager::Get().CreateObject<ULevel>(this);
	PersistentLevel->SetWorld(this);
}

void UWorld::BeginPlay()
{
	bHasBegunPlay = true;

	if (PersistentLevel)
	{
		PersistentLevel->BeginPlay();
	}
}

void UWorld::Tick(float GameDeltaTime, float RawDeltaTime, ELevelTick TickType)
{
	{
		SCOPE_STAT_CAT("FlushPrimitive", "1_WorldTick");
		Partition.FlushPrimitive();
	}

	Scene.GetDebugDrawQueue().Tick(RawDeltaTime);

	TickManager.Tick(this, GameDeltaTime, TickType);

	UpdateCollision();

	UpdatePlayerCameraManagers(GameDeltaTime, RawDeltaTime);

	ApplyCollisionDebugVisualization();
}


void UWorld::UpdatePlayerCameraManagers(float GameDeltaTime, float RawDeltaTime)
{
	const bool bCanDriveWorldView = GetWorldType() != EWorldType::Editor || HasBegunPlay();

	for (APlayerController* Controller : PlayerControllers)
	{
		if (!Controller || !IsActorInWorld(Controller))
		{
			continue;
		}

		APlayerCameraManager& Manager = Controller->GetCameraManager();
		Manager.UpdateCamera(GameDeltaTime, RawDeltaTime);

		if (bCanDriveWorldView)
		{
			if (UCameraComponent* OutputCamera = Manager.GetOutputCameraIfValid())
			{
				SetViewCamera(OutputCamera);
				SetActiveCamera(OutputCamera);
			}
		}
	}
}

void UWorld::SetActiveCamera(UCameraComponent* InCamera)
{
	ActiveCamera = IsAliveObject(InCamera) ? InCamera : nullptr;
}

UCameraComponent* UWorld::GetActiveCamera() const
{
	return IsAliveObject(ActiveCamera) ? ActiveCamera : nullptr;
}

void UWorld::SetViewCamera(UCameraComponent* InCamera)
{
	ViewCamera = IsAliveObject(InCamera) ? InCamera : nullptr;
}

UCameraComponent* UWorld::GetViewCamera() const
{
	if (IsAliveObject(ViewCamera))
	{
		return ViewCamera;
	}
	return GetActiveCamera();
}

bool UWorld::IsActorInWorld(const AActor* Actor) const
{
	if (!Actor || !PersistentLevel || !IsAliveObject(Actor))
	{
		return false;
	}
	const TArray<AActor*>& Actors = PersistentLevel->GetActors();
	return std::find(Actors.begin(), Actors.end(), Actor) != Actors.end();
}

bool UWorld::IsComponentInWorld(const UActorComponent* Component) const
{
	if (!Component || !IsAliveObject(Component))
	{
		return false;
	}
	AActor* Owner = Component->GetOwner();
	if (!IsActorInWorld(Owner))
	{
		return false;
	}
	const TArray<UActorComponent*>& Components = Owner->GetComponents();
	return std::find(Components.begin(), Components.end(), Component) != Components.end();
}

void UWorld::CleanupActorReferences(AActor* Actor)
{
	if (!Actor || !IsAliveObject(Actor))
	{
		return;
	}

	TArray<APlayerController*> ControllerSnapshot = PlayerControllers;

	for (APlayerController* Controller : ControllerSnapshot)
	{
		if (!Controller || !IsAliveObject(Controller) || Controller == Actor)
		{
			continue;
		}

		Controller->ClearCameraReferencesForActor(Actor);

		if (Controller->GetPossessedActor() == Actor)
		{
			Controller->UnPossess();
		}
	}

	TArray<AActor*> ActorSnapshot = GetActors();

	for (AActor* OtherActor : ActorSnapshot)
	{
		if (!OtherActor || !IsAliveObject(OtherActor) || !IsActorInWorld(OtherActor))
		{
			continue;
		}

		const TArray<UActorComponent*>& Components = OtherActor->GetComponents();

		for (UActorComponent* Component : Components)
		{
			if (!Component || !IsAliveObject(Component))
			{
				continue;
			}

			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
			{
				Camera->ClearTargetActorIfMatches(Actor);
			}
		}
	}

	if (ViewCamera && IsAliveObject(ViewCamera) && ViewCamera->GetOwner() == Actor)
	{
		ViewCamera = nullptr;
	}

	if (ActiveCamera && IsAliveObject(ActiveCamera) && ActiveCamera->GetOwner() == Actor)
	{
		ActiveCamera = nullptr;
	}

	if (LastLODUpdateCamera && IsAliveObject(LastLODUpdateCamera) && LastLODUpdateCamera->GetOwner() == Actor)
	{
		LastLODUpdateCamera = nullptr;
		bHasLastFullLODUpdateCameraPos = false;
	}
}

void UWorld::CleanupComponentReferences(UActorComponent* Component)
{
	if (!Component || !IsAliveObject(Component))
	{
		return;
	}

	USceneComponent* RemovedSceneComponent = Cast<USceneComponent>(Component);

	TArray<APlayerController*> ControllerSnapshot = PlayerControllers;

	for (APlayerController* Controller : ControllerSnapshot)
	{
		if (!Controller || !IsAliveObject(Controller))
		{
			continue;
		}

		Controller->ClearCameraReferencesForComponent(Component);
	}

	if (ViewCamera == Component)
	{
		ViewCamera = nullptr;
	}

	if (ActiveCamera == Component)
	{
		ActiveCamera = nullptr;
	}

	if (LastLODUpdateCamera == Component)
	{
		LastLODUpdateCamera = nullptr;
		bHasLastFullLODUpdateCameraPos = false;
	}

	if (!RemovedSceneComponent)
	{
		return;
	}

	TArray<AActor*> ActorSnapshot = GetActors();

	for (AActor* Actor : ActorSnapshot)
	{
		if (!Actor || !IsAliveObject(Actor) || !IsActorInWorld(Actor))
		{
			continue;
		}

		const TArray<UActorComponent*>& Components = Actor->GetComponents();

		for (UActorComponent* OtherComponent : Components)
		{
			if (!OtherComponent || !IsAliveObject(OtherComponent))
			{
				continue;
			}

			if (UMovementComponent* Movement = Cast<UMovementComponent>(OtherComponent))
			{
				Movement->ClearUpdatedComponentIfMatches(RemovedSceneComponent);
			}
		}
	}
}

UCameraComponent* UWorld::FindFirstCamera() const
{
	if (!PersistentLevel)
	{
		return nullptr;
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
		{
			continue;
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
			{
				return Camera;
			}
		}
	}
	return nullptr;
}

APawn* UWorld::FindFirstPawn() const
{
	if (!PersistentLevel)
	{
		return nullptr;
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (APawn* Pawn = Cast<APawn>(Actor))
		{
			return Pawn;
		}
	}
	return nullptr;
}

AActor* UWorld::FindFirstPossessableActor() const
{
	if (!PersistentLevel) return nullptr;
	for (AActor* Actor : PersistentLevel->GetActors())
	{
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Cast<UMovementComponent>(Comp))
			{
				return Actor;
			}
		}
	}
	return nullptr;
}
AActor* UWorld::FindActorByUUIDInWorld(uint32 ActorUUID) const
{
	if (!PersistentLevel || ActorUUID == 0)
	{
		return nullptr;
	}
	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (Actor && Actor->GetUUID() == ActorUUID)
		{
			return Actor;
		}
	}
	return nullptr;
}

APlayerController* UWorld::CreatePlayerController()
{
	APlayerController* Controller = SpawnActor<APlayerController>();
	if (Controller)
	{
		Controller->InitDefaultComponents();
	}
	return Controller;
}

APlayerController* UWorld::FindOrCreatePlayerController()
{
	if (APlayerController* Existing = GetPlayerController(0))
	{
		Existing->InitDefaultComponents();
		return Existing;
	}
	return CreatePlayerController();
}

void UWorld::AutoWirePlayerController(APlayerController* PreferredController)
{
	APlayerController* Controller = (PreferredController && IsActorInWorld(PreferredController))
		? PreferredController
		: FindOrCreatePlayerController();
	if (!Controller || !IsActorInWorld(Controller))
	{
		return;
	}

	if (!Controller->GetPossessedActor())
	{
		if (APawn* Pawn = FindFirstPawn())
		{
			Controller->Possess(Pawn);
		}
		else if (AActor* Target = FindFirstPossessableActor())
		{
			Controller->Possess(Target);
		}
	}

	if (!Controller->GetActiveCamera())
	{
		Controller->SetActiveCameraFromPossessedPawn();
	}

	if (UCameraComponent* Camera = ResolveGameplayViewCamera(Controller))
	{
		SetViewCamera(Camera);
		SetActiveCamera(Camera);
	}
}

UCameraComponent* UWorld::ResolveGameplayViewCamera(APlayerController* PreferredController) const
{
	if (PreferredController && IsActorInWorld(PreferredController))
	{
		if (UCameraComponent* Camera = PreferredController->ResolveViewCamera())
		{
			return Camera;
		}
	}

	for (APlayerController* Controller : PlayerControllers)
	{
		if (!Controller || !IsActorInWorld(Controller))
		{
			continue;
		}
		if (UCameraComponent* Camera = Controller->ResolveViewCamera())
		{
			return Camera;
		}
	}

	if (UCameraComponent* Camera = GetActiveCamera())
	{
		return Camera;
	}

	return FindFirstCamera();
}

APlayerController* UWorld::GetPlayerController(int32 Index) const
{
	if (Index < 0)
	{
		return nullptr;
	}
	const size_t NativeIndex = static_cast<size_t>(Index);
	if (NativeIndex >= PlayerControllers.size())
	{
		return nullptr;
	}
	APlayerController* Controller = PlayerControllers[NativeIndex];
	return IsActorInWorld(Controller) ? Controller : nullptr;
}

void UWorld::EndPlay()
{
	if (!PersistentLevel)
	{
		return;
	}

	bHasBegunPlay = false;
	TickManager.Reset();

	ViewCamera = nullptr;
	ActiveCamera = nullptr;
	LastLODUpdateCamera = nullptr;
	bHasLastFullLODUpdateCameraPos = false;

	FActorPoolSystem::Get().ClearWorld(this);

	// 1. 모든 Actor가 아직 World에 살아 있는 상태에서 EndPlay만 먼저 호출
	PersistentLevel->EndPlay();

	// 2. Actor 목록은 파괴 중 변경되므로 반드시 스냅샷 사용
	TArray<AActor*> ActorsToDestroy = PersistentLevel->GetActors();

	// 3. 실제 파괴는 반드시 UWorld::DestroyActor() 경로로 통일
	for (AActor* Actor : ActorsToDestroy)
	{
		if (!Actor || !IsAliveObject(Actor))
		{
			continue;
		}

		if (Actor->GetWorld() != this)
		{
			PersistentLevel->RemoveActor(Actor);
			continue;
		}

		DestroyActor(Actor);
	}

	PlayerControllers.clear();

	// 4. 남아 있을 수 있는 런타임 인덱스/캐시를 최종 비움
	Partition.Reset(FBoundingBox());

	WorldPrimitivePickingBVH.Reset();

	// 아래 Reset은 FWorldCollisionSystem에 추가하는 것을 권장
	WorldCollisionSystem.Reset();

	UObjectManager::Get().DestroyObject(PersistentLevel);
	PersistentLevel = nullptr;
}

#include "Camera/PlayerCameraManager.h"

#include "Camera/CameraFadeModifier.h"
#include "Camera/CameraModifier.h"
#include "Component/CameraComponent.h"
#include "Component/ComponentReferenceUtils.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Object/FName.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>

#include "Camera/CameraShakeModifier.h"
#include "Camera/CameraVignetteModifier.h"

IMPLEMENT_CLASS(APlayerCameraManager, AActor)

APlayerCameraManager::APlayerCameraManager()
{
	SetSerializeToScene(false);
	bNeedsTick = false;
	SetActorTickEnabled(false);
}

APlayerCameraManager::~APlayerCameraManager()
{
	ClearCameraModifiers();
}

void APlayerCameraManager::InitDefaultComponents()
{
	EnsureOutputCamera();
}

void APlayerCameraManager::EndPlay()
{
	ClearCameraModifiers();
	ClearActiveCamera();
	OwnerController = nullptr;
	ViewTarget = FViewTarget();

	AActor::EndPlay();
}

namespace
{
	float ExpAlpha(float Speed, float DeltaTime)
	{
		if (Speed <= 0.0f || DeltaTime <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - std::exp(-Speed * DeltaTime);
	}

	float LerpFloat(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	FVector LerpVector(const FVector& A, const FVector& B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}
}

void APlayerCameraManager::Initialize(APlayerController* InOwner)
{
	OwnerController = InOwner;
	SetSerializeToScene(false);
	bNeedsTick = false;
	SetActorTickEnabled(false);

	EnsureOutputCamera();
}
void APlayerCameraManager::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	SerializeCameraState(Ar);
}

void APlayerCameraManager::SerializeCameraState(FArchive& Ar)
{
	Ar << ActiveCameraRef.OwnerActorUUID;
	Ar << ActiveCameraRef.ComponentPath;
	Ar << PendingCameraRef.OwnerActorUUID;
	Ar << PendingCameraRef.ComponentPath;

	if (Ar.IsLoading())
	{
		ActiveCameraCached = nullptr;
		PendingCameraCached = nullptr;
		OutputCameraComponent = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
		BlendElapsedTime = 0.0f;
		bIsBlending = false;

		ViewTarget = FViewTarget();
		ClearCameraModifiers();
	}
}

void APlayerCameraManager::SetActiveCamera(UCameraComponent* Camera, bool bBlend)
{
	if (!Camera)
	{
		ClearActiveCamera();
		return;
	}

	EnsureOutputCamera();

	if (!bBlend || !CurrentView.bValid)
	{
		ActiveCameraRef = MakeCameraReference(Camera);
		ActiveCameraCached = Camera;
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
		BlendElapsedTime = 0.0f;
		SnapToActiveCamera();
		return;
	}

	BlendFromView = CurrentView;
	PendingCameraRef = MakeCameraReference(Camera);
	PendingCameraCached = Camera;
	bIsBlending = true;
	BlendElapsedTime = 0.0f;
}

void APlayerCameraManager::ClearActiveCamera()
{
	ActiveCameraRef.Reset();
	PendingCameraRef.Reset();
	ActiveCameraCached = nullptr;
	PendingCameraCached = nullptr;

	CurrentView = FCameraView();
	BlendFromView = FCameraView();
	ViewTarget = FViewTarget();

	bIsBlending = false;
	BlendElapsedTime = 0.0f;
}

UCameraComponent* APlayerCameraManager::GetActiveCamera() const
{
	if (ActiveCameraCached && IsAliveObject(ActiveCameraCached))
	{
		return ActiveCameraCached;
	}
	return ResolveCameraReference(ActiveCameraRef);
}

bool APlayerCameraManager::HasValidOutputCamera() const
{
	return OutputCameraComponent
		&& IsAliveObject(OutputCameraComponent)
		&& CurrentView.bValid
		&& (ActiveCameraRef.IsSet() || PendingCameraRef.IsSet());
}

UCameraComponent* APlayerCameraManager::GetOutputCameraIfValid() const
{
	return HasValidOutputCamera() ? OutputCameraComponent : nullptr;
}

void APlayerCameraManager::UpdateCamera(float GameDeltaTime, float RawDeltaTime)
{
	if (!OwnerController || !IsAliveObject(OwnerController))
	{
		return;
	}

	EnsureOutputCamera();
	if (!OutputCameraComponent)
	{
		return;
	}

	UCameraComponent* TargetCamera = bIsBlending
		? (PendingCameraCached && IsAliveObject(PendingCameraCached) ? PendingCameraCached : ResolveCameraReference(PendingCameraRef))
		: (ActiveCameraCached && IsAliveObject(ActiveCameraCached) ? ActiveCameraCached : ResolveCameraReference(ActiveCameraRef));

	if (!TargetCamera)
	{
		if (bIsBlending)
		{
			PendingCameraRef.Reset();
			PendingCameraCached = nullptr;
			bIsBlending = false;
			BlendElapsedTime = 0.0f;
			TargetCamera = ActiveCameraCached && IsAliveObject(ActiveCameraCached)
				? ActiveCameraCached
				: ResolveCameraReference(ActiveCameraRef);
		}

		if (!TargetCamera)
		{
			ClearActiveCamera();
			return;
		}
	}

	FCameraView DesiredView;
	if (!TargetCamera->CalcCameraView(OwnerController, GameDeltaTime, DesiredView))
	{
		return;
	}

	if (!CurrentView.bValid)
	{
		CurrentView = DesiredView;

		ViewTarget.Target = TargetCamera ? TargetCamera->GetOwner() : nullptr;
		ViewTarget.Camera = TargetCamera;
		ViewTarget.POV = CurrentView;

		FCameraView FinalView = CurrentView;
		ApplyCameraModifiers(RawDeltaTime, FinalView);
		OutputCameraComponent->ApplyCameraView(FinalView);
		UpdateVignetteCenter(TargetCamera);

		return;
	}

	const FCameraTransitionSettings& Transition = TargetCamera->GetTransitionSettings();
	const FCameraSmoothingSettings& Smoothing = TargetCamera->GetSmoothingSettings();

	if (bIsBlending)
	{
		BlendElapsedTime += GameDeltaTime;
		const float Duration = Transition.BlendTime;
		const float RawAlpha = Duration > 0.0f ? Clamp(BlendElapsedTime / Duration, 0.0f, 1.0f) : 1.0f;
		const float Alpha = EvaluateBlendAlpha(RawAlpha, Transition.Function);
		CurrentView = BlendViews(BlendFromView, DesiredView, Alpha, Transition);

		if (RawAlpha >= 1.0f)
		{
			ActiveCameraRef = PendingCameraRef;
			ActiveCameraCached = PendingCameraCached;
			PendingCameraRef.Reset();
			PendingCameraCached = nullptr;
			bIsBlending = false;
			BlendElapsedTime = 0.0f;
		}
	}
	else if (Smoothing.bEnableSmoothing)
	{
		const float LocationAlpha = ExpAlpha(Smoothing.LocationLagSpeed, GameDeltaTime);
		const float RotationAlpha = ExpAlpha(Smoothing.RotationLagSpeed, GameDeltaTime);
		const float FOVAlpha = ExpAlpha(Smoothing.FOVLagSpeed, GameDeltaTime);
		const float OrthoAlpha = ExpAlpha(Smoothing.OrthoWidthLagSpeed, GameDeltaTime);

		CurrentView.Location = LerpVector(CurrentView.Location, DesiredView.Location, LocationAlpha);
		CurrentView.Rotation = FQuat::Slerp(CurrentView.Rotation, DesiredView.Rotation, RotationAlpha);

		FCameraState NewState = DesiredView.State;
		NewState.FOV = LerpFloat(CurrentView.State.FOV, DesiredView.State.FOV, FOVAlpha);
		NewState.OrthoWidth = LerpFloat(CurrentView.State.OrthoWidth, DesiredView.State.OrthoWidth, OrthoAlpha);
		CurrentView.State = NewState;
		CurrentView.PostProcess = DesiredView.PostProcess;
		CurrentView.bValid = true;
	}
	else
	{
		CurrentView = DesiredView;
	}

	ViewTarget.Target = TargetCamera ? TargetCamera->GetOwner() : nullptr;
	ViewTarget.Camera = TargetCamera;
	ViewTarget.POV = CurrentView;

	FCameraView FinalView = CurrentView;
	ApplyCameraModifiers(RawDeltaTime, FinalView);
	OutputCameraComponent->ApplyCameraView(FinalView);

	// static uint32 ChainLogCounter = 0;
	// if (ChainLogCounter++ % 30 == 0)
	// {
	// 	UE_LOG("[CameraMgr] Tgt=%u TgtPP=%.2f Desired=%.2f Current=%.2f Final=%.2f Blend=%d Smooth=%d Out=%u OutPP=%.2f",
	// 		TargetCamera ? TargetCamera->GetUUID() : 0,
	// 		TargetCamera ? TargetCamera->GetPostProcess().VignetteIntensity : -1.0f,
	// 		DesiredView.PostProcess.VignetteIntensity,
	// 		CurrentView.PostProcess.VignetteIntensity,
	// 		FinalView.PostProcess.VignetteIntensity,
	// 		bIsBlending ? 1 : 0,
	// 		Smoothing.bEnableSmoothing ? 1 : 0,
	// 		OutputCameraComponent ? OutputCameraComponent->GetUUID() : 0,
	// 		OutputCameraComponent ? OutputCameraComponent->GetPostProcess().VignetteIntensity : -1.0f);
	// }
	//
	UpdateVignetteCenter(TargetCamera);
}

void APlayerCameraManager::SnapToActiveCamera()
{
	EnsureOutputCamera();
	UCameraComponent* ActiveCamera = GetActiveCamera();
	if (!ActiveCamera || !OutputCameraComponent)
	{
		return;
	}

	FCameraView DesiredView;
	if (ActiveCamera->CalcCameraView(OwnerController, 0.0f, DesiredView))
	{
		CurrentView = DesiredView;

		ViewTarget.Target = ActiveCamera->GetOwner();
		ViewTarget.Camera = ActiveCamera;
		ViewTarget.POV = CurrentView;

		FCameraView FinalView = CurrentView;
		ApplyCameraModifiers(0.0f, FinalView);
		OutputCameraComponent->ApplyCameraView(FinalView);
		UpdateVignetteCenter(ActiveCamera);
	}
}

void APlayerCameraManager::RemapActorReferences(const TMap<uint32, uint32>& ActorUUIDRemap)
{
	auto RemapRef = [&ActorUUIDRemap](FCameraComponentReference& Ref)
	{
		if (Ref.OwnerActorUUID == 0)
		{
			return;
		}

		auto It = ActorUUIDRemap.find(Ref.OwnerActorUUID);
		if (It != ActorUUIDRemap.end())
		{
			Ref.OwnerActorUUID = It->second;
		}
		else
		{
			Ref.Reset();
		}
	};

	RemapRef(ActiveCameraRef);
	RemapRef(PendingCameraRef);
	ActiveCameraCached = nullptr;
	PendingCameraCached = nullptr;
	bIsBlending = false;
	BlendElapsedTime = 0.0f;
}

void APlayerCameraManager::ClearCameraReferencesForActor(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	const uint32 ActorUUID = Actor->GetUUID();
	if (ActiveCameraRef.OwnerActorUUID == ActorUUID)
	{
		ActiveCameraRef.Reset();
		ActiveCameraCached = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
	}
	if (PendingCameraRef.OwnerActorUUID == ActorUUID)
	{
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
	}
	if (OutputCameraComponent && OutputCameraComponent->GetOwner() == Actor)
	{
		OutputCameraComponent = nullptr;
	}
}

void APlayerCameraManager::ClearCameraReferencesForComponent(const UActorComponent* Component)
{
	const UCameraComponent* Camera = Cast<UCameraComponent>(Component);
	if (!Camera)
	{
		return;
	}

	const FCameraComponentReference RemovedRef = MakeCameraReference(const_cast<UCameraComponent*>(Camera));
	const bool bMatchesActiveRef = RemovedRef.IsSet()
		&& ActiveCameraRef.OwnerActorUUID == RemovedRef.OwnerActorUUID
		&& ActiveCameraRef.ComponentPath == RemovedRef.ComponentPath;
	const bool bMatchesPendingRef = RemovedRef.IsSet()
		&& PendingCameraRef.OwnerActorUUID == RemovedRef.OwnerActorUUID
		&& PendingCameraRef.ComponentPath == RemovedRef.ComponentPath;

	if (ActiveCameraCached == Camera || bMatchesActiveRef)
	{
		ActiveCameraRef.Reset();
		ActiveCameraCached = nullptr;
		CurrentView = FCameraView();
		BlendFromView = FCameraView();
	}
	if (PendingCameraCached == Camera || bMatchesPendingRef)
	{
		PendingCameraRef.Reset();
		PendingCameraCached = nullptr;
		bIsBlending = false;
	}
	if (OutputCameraComponent == Camera)
	{
		OutputCameraComponent = nullptr;
	}
}
UCameraShakeModifier* APlayerCameraManager::StartCameraShake(const FCameraShakeParams& Params)
{
	if (Params.bSingleInstance)
	{
		for (UCameraModifier* Modifier : ModifierList)
		{
			UCameraShakeModifier* ExistingShake = Cast<UCameraShakeModifier>(Modifier);
			if (!ExistingShake || !IsAliveObject(ExistingShake))
			{
				continue;
			}
			
			if (!ExistingShake->IsPendingRemove())
			{
				ExistingShake->StartShake(Params);
				return ExistingShake;
			}
		}
	}
	
	UCameraShakeModifier* NewShake = UObjectManager::Get().CreateObject<UCameraShakeModifier>(this);
	if (!NewShake)
	{
		return nullptr;
	}
	
	NewShake->SetPriority(128);
	NewShake->StartShake(Params);
	
	AddCameraModifier(NewShake);
	return NewShake;
}

UCameraComponent* APlayerCameraManager::ResolveCameraReference(const FCameraComponentReference& Ref) const
{
	if (!Ref.IsSet() || !OwnerController)
	{
		return nullptr;
	}

	UWorld* World = OwnerController->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AActor* OwnerActor = World->FindActorByUUIDInWorld(Ref.OwnerActorUUID);
	if (!OwnerActor)
	{
		return nullptr;
	}

	return Cast<UCameraComponent>(ComponentReferenceUtils::ResolveComponentPath(OwnerActor, Ref.ComponentPath));
}

FCameraComponentReference APlayerCameraManager::MakeCameraReference(UCameraComponent* Camera) const
{
	FCameraComponentReference Ref;
	if (!Camera || !Camera->GetOwner())
	{
		return Ref;
	}

	Ref.OwnerActorUUID = Camera->GetOwner()->GetUUID();
	Ref.ComponentPath = ComponentReferenceUtils::MakeComponentPath(Camera);
	return Ref;
}

FCameraView APlayerCameraManager::BlendViews(
	const FCameraView& From,
	const FCameraView& To,
	float Alpha,
	const FCameraTransitionSettings& Params) const
{
	if (!From.bValid || !To.bValid)
	{
		return To;
	}

	FCameraView Out = To;
	Out.bValid = true;

	if (Params.bBlendLocation)
	{
		Out.Location = LerpVector(From.Location, To.Location, Alpha);
	}
	if (Params.bBlendRotation)
	{
		Out.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, Alpha);
	}
	if (Params.bBlendFOV)
	{
		Out.State.FOV = LerpFloat(From.State.FOV, To.State.FOV, Alpha);
	}
	if (Params.bBlendOrthoWidth)
	{
		Out.State.OrthoWidth = LerpFloat(From.State.OrthoWidth, To.State.OrthoWidth, Alpha);
	}
	if (Params.bBlendNearFar)
	{
		Out.State.NearZ = LerpFloat(From.State.NearZ, To.State.NearZ, Alpha);
		Out.State.FarZ = LerpFloat(From.State.FarZ, To.State.FarZ, Alpha);
	}
	else
	{
		Out.State.NearZ = To.State.NearZ;
		Out.State.FarZ = To.State.FarZ;
	}

	Out.State.AspectRatio = To.State.AspectRatio;

	switch (Params.ProjectionSwitchMode)
	{
	case ECameraProjectionSwitchMode::SwitchAtStart:
		Out.State.bIsOrthogonal = To.State.bIsOrthogonal;
		break;
	case ECameraProjectionSwitchMode::SwitchAtEnd:
		Out.State.bIsOrthogonal = Alpha >= 1.0f ? To.State.bIsOrthogonal : From.State.bIsOrthogonal;
		break;
	case ECameraProjectionSwitchMode::SwitchAtHalf:
	default:
		Out.State.bIsOrthogonal = Alpha >= 0.5f ? To.State.bIsOrthogonal : From.State.bIsOrthogonal;
		break;
	}

	// PostProcess 보간 — 카메라 자체 baseline. modifier가 ApplyCameraModifiers에서 추가로 덮어씀.
	Out.PostProcess.VignetteCenter.X = LerpFloat(From.PostProcess.VignetteCenter.X, To.PostProcess.VignetteCenter.X, Alpha);
	Out.PostProcess.VignetteCenter.Y = LerpFloat(From.PostProcess.VignetteCenter.Y, To.PostProcess.VignetteCenter.Y, Alpha);
	Out.PostProcess.VignetteIntensity = LerpFloat(From.PostProcess.VignetteIntensity, To.PostProcess.VignetteIntensity, Alpha);
	Out.PostProcess.VignetteSmoothness = LerpFloat(From.PostProcess.VignetteSmoothness, To.PostProcess.VignetteSmoothness, Alpha);
	Out.PostProcess.VignetteColor = LerpVector(From.PostProcess.VignetteColor, To.PostProcess.VignetteColor, Alpha);
	Out.PostProcess.FadeColor = LerpVector(From.PostProcess.FadeColor, To.PostProcess.FadeColor, Alpha);
	Out.PostProcess.FadeAlpha = LerpFloat(From.PostProcess.FadeAlpha, To.PostProcess.FadeAlpha, Alpha);

	return Out;
}

float APlayerCameraManager::EvaluateBlendAlpha(float RawAlpha, ECameraBlendFunction Function) const
{
	const float A = Clamp(RawAlpha, 0.0f, 1.0f);
	switch (Function)
	{
	case ECameraBlendFunction::EaseIn:
		return A * A;
	case ECameraBlendFunction::EaseOut:
		return 1.0f - (1.0f - A) * (1.0f - A);
	case ECameraBlendFunction::EaseInOut:
		return A * A * (3.0f - 2.0f * A);
	case ECameraBlendFunction::Linear:
	default:
		return A;
	}
}

void APlayerCameraManager::EnsureOutputCamera()
{
	if (OutputCameraComponent && IsAliveObject(OutputCameraComponent))
	{
		return;
	}

	OutputCameraComponent = AddComponent<UCameraComponent>();
	if (OutputCameraComponent)
	{
		OutputCameraComponent->SetFName(FName("PlayerOutputCamera"));
		OutputCameraComponent->SetHiddenInComponentTree(true);
		OutputCameraComponent->SetAutoActivate(false);
	}
}

void APlayerCameraManager::AddCameraModifier(UCameraModifier* Modifier)
{
	if (!Modifier || !IsAliveObject(Modifier))
	{
		return;
	}

	if (std::find(ModifierList.begin(), ModifierList.end(), Modifier) != ModifierList.end())
	{
		return;
	}

	Modifier->Initialize(this);
	Modifier->OnAddedToCameraManager();

	ModifierList.push_back(Modifier);
	SortCameraModifiers();
}

void APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier, bool bImmediate)
{
	if (!Modifier)
	{
		return;
	}

	auto It = std::find(ModifierList.begin(), ModifierList.end(), Modifier);
	if (It == ModifierList.end())
	{
		return;
	}

	if (!bImmediate)
	{
		if (IsAliveObject(Modifier))
		{
			Modifier->MarkPendingRemove();
		}
		return;
	}

	if (IsAliveObject(Modifier))
	{
		Modifier->OnRemovedFromCameraManager();
		UObjectManager::Get().DestroyObject(Modifier);
	}

	ModifierList.erase(It);
}

void APlayerCameraManager::ClearCameraModifiers()
{
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (Modifier && IsAliveObject(Modifier))
		{
			Modifier->OnRemovedFromCameraManager();
			UObjectManager::Get().DestroyObject(Modifier);
		}
	}

	ModifierList.clear();
	FadeModifier = nullptr;
	VignetteModifier = nullptr;
}

UCameraFadeModifier* APlayerCameraManager::EnsureFadeModifier()
{
	if (FadeModifier && IsAliveObject(FadeModifier)
		&& std::find(ModifierList.begin(), ModifierList.end(), FadeModifier) != ModifierList.end())
	{
		return FadeModifier;
	}

	FadeModifier = UObjectManager::Get().CreateObject<UCameraFadeModifier>(this);
	if (FadeModifier)
	{
		AddCameraModifier(FadeModifier);
	}
	return FadeModifier;
}

UVignetteModifier* APlayerCameraManager::EnsureVignetteModifier()
{
	if (VignetteModifier && IsAliveObject(VignetteModifier)
		&& std::find(ModifierList.begin(), ModifierList.end(), VignetteModifier) != ModifierList.end())
	{
		return VignetteModifier;
	}

	VignetteModifier = UObjectManager::Get().CreateObject<UVignetteModifier>(this);
	if (VignetteModifier)
	{
		// Vignette는 보통 Fade보다 아래에 위치하도록 우선순위 조정 (낮을수록 나중에 적용되나 현재 sort 로직 확인 필요)
		// 현재 SortCameraModifiers는 PriorityA > PriorityB 순으로 정렬 (큰 값이 먼저 적용됨)
		// 렌더링 순서상 Fade가 마지막에 덮어야 하므로, Fade의 우선순위가 더 낮아야 함 (작은 값이 뒤에 옴)
		// 하지만 modifier chain은 누적 방식이므로 나중에 적용되는 것이 덮어씀.
		VignetteModifier->SetPriority(64);
		AddCameraModifier(VignetteModifier);
	}
	return VignetteModifier;
}

void APlayerCameraManager::StartFadeIn(float Duration, float TargetAlpha, const FVector& Color)
{
	if (UCameraFadeModifier* Mod = EnsureFadeModifier())
	{
		Mod->SetPriority(128); // Fade가 더 나중에 적용되어 Vignette를 덮도록 높은 우선순위 (수정: chain 순서 확인)
		Mod->StartFadeIn(Duration, TargetAlpha, Color);
	}
}

void APlayerCameraManager::StartFadeOut(float Duration)
{
	if (FadeModifier && IsAliveObject(FadeModifier))
	{
		FadeModifier->StartFadeOut(Duration);
	}
}

void APlayerCameraManager::StartVignette(float Intensity, const FVector& Color, float Duration, float Smoothness)
{
	if (UVignetteModifier* Mod = EnsureVignetteModifier())
	{
		Mod->StartVignette(Intensity, Color, Duration, Smoothness);
	}
}

void APlayerCameraManager::StopVignette(float Duration)
{
	if (VignetteModifier && IsAliveObject(VignetteModifier))
	{
		VignetteModifier->StopVignette(Duration);
	}
}


void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime, FCameraView& InOutView)
{
	if (!InOutView.bValid || ModifierList.empty())
	{
		return;
	}

	CleanupCameraModifiers();
	SortCameraModifiers();

	//여기서 순회하면서 modifier 적용
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (!Modifier || !IsAliveObject(Modifier))
		{
			continue;
		}

		const bool bContinueChain = Modifier->UpdateCameraModifier(DeltaTime, InOutView);
		if (!bContinueChain)
		{
			break;
		}
	}

	CleanupCameraModifiers();
}

void APlayerCameraManager::CleanupCameraModifiers()
{
	for (auto It = ModifierList.begin(); It != ModifierList.end(); )
	{
		UCameraModifier* Modifier = *It;

		if (!Modifier || !IsAliveObject(Modifier))
		{
			It = ModifierList.erase(It);
			continue;
		}

		if (Modifier->IsShouldDestroy())
		{
			Modifier->OnRemovedFromCameraManager();
			UObjectManager::Get().DestroyObject(Modifier);

			It = ModifierList.erase(It);
			continue;
		}

		++It;
	}
}

void APlayerCameraManager::SortCameraModifiers()
{
	std::stable_sort(
		ModifierList.begin(),
		ModifierList.end(),
		[](const UCameraModifier* A, const UCameraModifier* B)
		{
			const uint8 PriorityA = A && IsAliveObject(A) ? A->GetPriority() : 0;
			const uint8 PriorityB = B && IsAliveObject(B) ? B->GetPriority() : 0;

			return PriorityA > PriorityB;
		}
	);
}

void APlayerCameraManager::UpdateVignetteCenter(UCameraComponent* TargetCamera)
{
	if (!OutputCameraComponent || !TargetCamera)
	{
		return;
	}

	FCameraPostProcess& PP = OutputCameraComponent->GetMutablePostProcess();

	AActor* Subject = TargetCamera->GetSubjectActor(OwnerController);
	if (!Subject || !IsAliveObject(Subject))
	{
		PP.VignetteCenter = FVector2(0.5f, 0.5f);
		return;
	}

	const FMatrix VP = OutputCameraComponent->GetViewProjectionMatrix();
	const FVector NDC = VP.TransformPositionWithW(Subject->GetActorLocation());

	// D3D Reverse-Z: NDC.Z 범위는 [0, 1] (1=near, 0=far). 카메라 뒤면 W 부호 반전으로 NDC가 깨짐.
	if (NDC.Z < 0.0f || NDC.Z > 1.0f || NDC.X < -1.0f || NDC.X > 1.0f || NDC.Y < -1.0f || NDC.Y > 1.0f)
	{
		PP.VignetteCenter = FVector2(0.5f, 0.5f);
		return;
	}

	const float U = NDC.X * 0.5f + 0.5f;
	const float V = -NDC.Y * 0.5f + 0.5f;
	PP.VignetteCenter = FVector2(U, V);
}
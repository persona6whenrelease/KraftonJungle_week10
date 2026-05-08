#include "BoxComponent.h"
#include "Math/Matrix.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"
#include <cmath>
#include <cstring>

IMPLEMENT_CLASS(UBoxComponent, UShapeComponent)

namespace
{
	void AddBoxLines(FScene& Scene, const FVector (&P)[8], const FColor& Color)
	{
		Scene.AddDebugLine(P[0], P[1], Color);
		Scene.AddDebugLine(P[1], P[2], Color);
		Scene.AddDebugLine(P[2], P[3], Color);
		Scene.AddDebugLine(P[3], P[0], Color);

		Scene.AddDebugLine(P[4], P[5], Color);
		Scene.AddDebugLine(P[5], P[6], Color);
		Scene.AddDebugLine(P[6], P[7], Color);
		Scene.AddDebugLine(P[7], P[4], Color);

		Scene.AddDebugLine(P[0], P[4], Color);
		Scene.AddDebugLine(P[1], P[5], Color);
		Scene.AddDebugLine(P[2], P[6], Color);
		Scene.AddDebugLine(P[3], P[7], Color);
	}
}

FBoundingBox UBoxComponent::GetWorldAABB() const
{
	const FMatrix& WorldMatrix = GetWorldMatrix();
	const FVector Center = GetWorldLocation();

	const float Ex = std::abs(WorldMatrix.M[0][0]) * BoxExtent.X
		+ std::abs(WorldMatrix.M[1][0]) * BoxExtent.Y
		+ std::abs(WorldMatrix.M[2][0]) * BoxExtent.Z;
	const float Ey = std::abs(WorldMatrix.M[0][1]) * BoxExtent.X
		+ std::abs(WorldMatrix.M[1][1]) * BoxExtent.Y
		+ std::abs(WorldMatrix.M[2][1]) * BoxExtent.Z;
	const float Ez = std::abs(WorldMatrix.M[0][2]) * BoxExtent.X
		+ std::abs(WorldMatrix.M[1][2]) * BoxExtent.Y
		+ std::abs(WorldMatrix.M[2][2]) * BoxExtent.Z;

	return FBoundingBox(
		Center - FVector(Ex, Ey, Ez),
		Center + FVector(Ex, Ey, Ez));
}

void UBoxComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UShapeComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Box Extent", EPropertyType::Vec3, &BoxExtent, 0.0f, 10000.0f, 1.0f });
}

void UBoxComponent::PostEditProperty(const char* PropertyName)
{
	UShapeComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Box Extent") == 0)
	{
		MarkWorldBoundsDirty();
	}
}

void UBoxComponent::Serialize(FArchive& Ar)
{
	UShapeComponent::Serialize(Ar);
	Ar << BoxExtent;
}


void UBoxComponent::DrawDebugShape(FScene& Scene, const FColor& Color) const
{
	const FMatrix& WorldMatrix = GetWorldMatrix();
	const FVector E = BoxExtent;

	FVector P[8] = {
		FVector(-E.X, -E.Y, -E.Z) * WorldMatrix,
		FVector( E.X, -E.Y, -E.Z) * WorldMatrix,
		FVector( E.X,  E.Y, -E.Z) * WorldMatrix,
		FVector(-E.X,  E.Y, -E.Z) * WorldMatrix,
		FVector(-E.X, -E.Y,  E.Z) * WorldMatrix,
		FVector( E.X, -E.Y,  E.Z) * WorldMatrix,
		FVector( E.X,  E.Y,  E.Z) * WorldMatrix,
		FVector(-E.X,  E.Y,  E.Z) * WorldMatrix
	};

	AddBoxLines(Scene, P, Color);
}

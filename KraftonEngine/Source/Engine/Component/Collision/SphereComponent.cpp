#include "SphereComponent.h"
#include "Math/MathUtils.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(USphereComponent, UShapeComponent)

namespace
{
	constexpr int32 DebugCircleSegments = 32;

	void AddDebugCircle(FScene& Scene, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, const FColor& Color)
	{
		if (Radius <= 0.0f)
		{
			return;
		}

		FVector Previous = Center + AxisA * Radius;
		for (int32 Segment = 1; Segment <= DebugCircleSegments; ++Segment)
		{
			const float Angle = (2.0f * FMath::Pi * static_cast<float>(Segment)) / static_cast<float>(DebugCircleSegments);
			const FVector Current = Center + (AxisA * std::cos(Angle) + AxisB * std::sin(Angle)) * Radius;
			Scene.AddDebugLine(Previous, Current, Color);
			Previous = Current;
		}
	}
}

FBoundingBox USphereComponent::GetWorldAABB() const
{
	const FVector Center = GetWorldLocation();
	const FVector Scale = GetWorldScale();

	const float MaxScale = (std::max)({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
	const float Radius = SphereRadius * MaxScale;
	const FVector Extent(Radius, Radius, Radius);

	return FBoundingBox(Center - Extent, Center + Extent);
}

void USphereComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UShapeComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Sphere Radius", EPropertyType::Float, &SphereRadius, 0.0f, 10000.0f, 1.0f });
}

void USphereComponent::PostEditProperty(const char* PropertyName)
{
	UShapeComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Sphere Radius") == 0)
	{
		MarkWorldBoundsDirty();
	}
}

void USphereComponent::Serialize(FArchive& Ar)
{
	UShapeComponent::Serialize(Ar);
	Ar << SphereRadius;
}

void USphereComponent::DrawDebugShape(FScene& Scene, const FColor& Color) const
{
	const FVector Center = GetWorldLocation();
	const FVector Scale = GetWorldScale();
	const float MaxScale = (std::max)({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
	const float Radius = SphereRadius * MaxScale;

	AddDebugCircle(Scene, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), Radius, Color);
	AddDebugCircle(Scene, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Color);
	AddDebugCircle(Scene, Center, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Color);
}

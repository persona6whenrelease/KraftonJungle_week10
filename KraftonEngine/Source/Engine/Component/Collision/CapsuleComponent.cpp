#include "CapsuleComponent.h"
#include "Math/MathUtils.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UCapsuleComponent, UShapeComponent)

namespace
{
	constexpr int32 DebugCircleSegments = 32;

	float GetScaledCapsuleRadius(const UCapsuleComponent* Capsule)
	{
		const FVector Scale = Capsule->GetWorldScale();
		const float RadiusScale = (std::max)(std::abs(Scale.X), std::abs(Scale.Y));
		return Capsule->GetCapsuleRadius() * RadiusScale;
	}

	void BuildPerpendicularBasis(const FVector& Axis, FVector& OutRight, FVector& OutForward)
	{
		const FVector Candidate = (std::abs(Axis.Dot(FVector(1.0f, 0.0f, 0.0f))) < 0.95f)
			? FVector(1.0f, 0.0f, 0.0f)
			: FVector(0.0f, 1.0f, 0.0f);

		OutRight = Candidate.Cross(Axis);
		OutRight.Normalize();
		OutForward = Axis.Cross(OutRight);
		OutForward.Normalize();
	}

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

	void AddHemisphereArc(FScene& Scene, const FVector& Center, const FVector& RadialAxis, const FVector& CapsuleAxis, float Radius, bool bTopCap, const FColor& Color)
	{
		if (Radius <= 0.0f)
		{
			return;
		}

		const FVector CapAxis = bTopCap ? CapsuleAxis : CapsuleAxis * -1.0f;
		FVector Previous = Center + RadialAxis * Radius;
		for (int32 Segment = 1; Segment <= DebugCircleSegments / 2; ++Segment)
		{
			const float Angle = (FMath::Pi * static_cast<float>(Segment)) / static_cast<float>(DebugCircleSegments / 2);
			const FVector Current = Center + (RadialAxis * std::cos(Angle) + CapAxis * std::sin(Angle)) * Radius;
			Scene.AddDebugLine(Previous, Current, Color);
			Previous = Current;
		}
	}
}

void UCapsuleComponent::GetSegmentPoints(FVector& OutP0, FVector& OutP1) const
{
	const FVector Scale = GetWorldScale();
	const float RadiusScale = (std::max)(std::abs(Scale.X), std::abs(Scale.Y));
	const float HalfHeightScale = std::abs(Scale.Z);

	const float ScaledRadius = CapsuleRadius * RadiusScale;
	const float ScaledHalfHeight = CapsuleHalfHeight * HalfHeightScale;
	const float SegmentHalfLength = (std::max)(ScaledHalfHeight - ScaledRadius, 0.0f);

	const FVector Center = GetWorldLocation();
	const FVector Axis = GetUpVector();

	OutP0 = Center - Axis * SegmentHalfLength;
	OutP1 = Center + Axis * SegmentHalfLength;
}

FBoundingBox UCapsuleComponent::GetWorldAABB() const
{
	const FVector Scale = GetWorldScale();
	const float RadiusScale = (std::max)(std::abs(Scale.X), std::abs(Scale.Y));
	const float Radius = CapsuleRadius * RadiusScale;

	FVector P0;
	FVector P1;
	GetSegmentPoints(P0, P1);

	FVector Min(
		(std::min)(P0.X, P1.X),
		(std::min)(P0.Y, P1.Y),
		(std::min)(P0.Z, P1.Z));
	FVector Max(
		(std::max)(P0.X, P1.X),
		(std::max)(P0.Y, P1.Y),
		(std::max)(P0.Z, P1.Z));

	const FVector Expand(Radius, Radius, Radius);
	return FBoundingBox(Min - Expand, Max + Expand);
}

void UCapsuleComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UShapeComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Capsule Radius", EPropertyType::Float, &CapsuleRadius, 0.0f, 10000.0f, 1.0f });
	OutProps.push_back({ "Capsule Half Height", EPropertyType::Float, &CapsuleHalfHeight, 0.0f, 10000.0f, 1.0f });
}

void UCapsuleComponent::PostEditProperty(const char* PropertyName)
{
	UShapeComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Capsule Radius") == 0
		|| strcmp(PropertyName, "Capsule Half Height") == 0)
	{
		MarkWorldBoundsDirty();
	}
}

void UCapsuleComponent::Serialize(FArchive& Ar)
{
	UShapeComponent::Serialize(Ar);
	Ar << CapsuleHalfHeight;
	Ar << CapsuleRadius;
}

void UCapsuleComponent::DrawDebugShape(FScene& Scene, const FColor& Color) const
{
	const float Radius = GetScaledCapsuleRadius(this);
	if (Radius <= 0.0f)
	{
		return;
	}

	FVector P0;
	FVector P1;
	GetSegmentPoints(P0, P1);

	FVector Axis = P1 - P0;
	if (Axis.IsNearlyZero())
	{
		Axis = GetUpVector();
	}
	else
	{
		Axis.Normalize();
	}

	FVector Right;
	FVector Forward;
	BuildPerpendicularBasis(Axis, Right, Forward);

	AddDebugCircle(Scene, P0, Right, Forward, Radius, Color);
	AddDebugCircle(Scene, P1, Right, Forward, Radius, Color);

	Scene.AddDebugLine(P0 + Right * Radius, P1 + Right * Radius, Color);
	Scene.AddDebugLine(P0 - Right * Radius, P1 - Right * Radius, Color);
	Scene.AddDebugLine(P0 + Forward * Radius, P1 + Forward * Radius, Color);
	Scene.AddDebugLine(P0 - Forward * Radius, P1 - Forward * Radius, Color);

	AddHemisphereArc(Scene, P1, Right, Axis, Radius, true, Color);
	AddHemisphereArc(Scene, P1, Forward, Axis, Radius, true, Color);
	AddHemisphereArc(Scene, P0, Right, Axis, Radius, false, Color);
	AddHemisphereArc(Scene, P0, Forward, Axis, Radius, false, Color);
}

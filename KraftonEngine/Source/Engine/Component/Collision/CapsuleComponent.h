#pragma once
#include "ShapeComponent.h"
#include "CollisionTypes.h"

class UCapsuleComponent : public UShapeComponent
{
public:
	DECLARE_CLASS(UCapsuleComponent, UShapeComponent)
	virtual ECollisionShapeType GetCollisionShapeType() const override
	{
		return ECollisionShapeType::Capsule;
	}

	virtual FBoundingBox GetWorldAABB() const override;
	void DrawDebugShape(FScene& Scene, const FColor& Color) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	float GetCapsuleRadius() const { return CapsuleRadius; }
	float GetCapsuleHalfHeight() const { return CapsuleHalfHeight; }

	void SetCapsuleRadius(float InRadius)
	{
		CapsuleRadius = InRadius;
		MarkWorldBoundsDirty();
	}
	void SetCapsuleHalfHeight(float InHalfHeight)
	{
		CapsuleHalfHeight = InHalfHeight;
		MarkWorldBoundsDirty();
	}

	void GetSegmentPoints(FVector& OutP0, FVector& OutP1) const;

private:
	float CapsuleHalfHeight = 1.0f;
	float CapsuleRadius = 0.5f;
};

#pragma once
#include "ShapeComponent.h"
#include "CollisionTypes.h"

class USphereComponent : public UShapeComponent
{
public:
	DECLARE_CLASS(USphereComponent, UShapeComponent)
	virtual ECollisionShapeType GetCollisionShapeType() const override
	{
		return ECollisionShapeType::Sphere;
	}

	virtual FBoundingBox GetWorldAABB() const override;
	void DrawDebugShape(FScene& Scene, const FColor& Color) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	float GetSphereRadius() const { return SphereRadius; }
	void SetSphereRadius(float InRadius)
	{
		SphereRadius = InRadius;
		MarkWorldBoundsDirty();
	}

private:
	float SphereRadius = 0.5f;
};

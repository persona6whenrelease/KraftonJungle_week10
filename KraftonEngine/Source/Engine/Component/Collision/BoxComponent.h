#pragma once
#include "ShapeComponent.h"
#include "Math/Vector.h"
#include "CollisionTypes.h"

class UBoxComponent : public UShapeComponent
{
public:
	DECLARE_CLASS(UBoxComponent, UShapeComponent)
	virtual ECollisionShapeType GetCollisionShapeType() const override
	{
		return ECollisionShapeType::Box;
	}

	virtual FBoundingBox GetWorldAABB() const override;
	void DrawDebugShape(FScene& Scene, const FColor& Color) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	const FVector& GetBoxExtent() const { return BoxExtent; }
	void SetBoxExtent(const FVector& InExtent)
	{
		BoxExtent = InExtent;
		MarkWorldBoundsDirty();
	}

	void OnParry() override;

private:
	FVector BoxExtent = FVector(0.5f, 0.5f, 0.5f);
};

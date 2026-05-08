#pragma once
#include "Component/PrimitiveComponent.h"
#include "Engine/Runtime/Delegate.h"
class FScene;

class UShapeComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)
	const FColor& GetDebugShapeColor() const { return DebugShapeColor; }
	bool GetDrawDebugOnlyIfSelected() const { return bDrawDebugOnlyIfSelected; }

	void SetDebugShapeColor(const FColor& InColor) { DebugShapeColor = InColor; }
	void SetDrawDebugOnlyIfSelected(bool bInValue) { bDrawDebugOnlyIfSelected = bInValue; }

	virtual void DrawDebugShape(FScene& Scene, const FColor& Color) const = 0;
	void ContributeSelectedVisuals(FScene& Scene) const override;
	void Serialize(FArchive& Ar) override;
	TDelegate<> TestDelegate;

protected:
	FColor DebugShapeColor = FColor::Green(); //에디터 내에서 사용.
	bool bDrawDebugOnlyIfSelected = true;
};


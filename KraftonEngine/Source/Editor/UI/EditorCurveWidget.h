#pragma once

#include "Editor/UI/EditorWidget.h"

class FEditorCurveWidget : public FEditorWidget
{
public:
	virtual void Initialize(UEditorEngine* InEditorEngine);
	virtual void Render(float DeltaTime) override;
	bool OpenCurveAsset(const FString& CurvePath);
	bool SaveCurrentCurve();

private:
	float BezierPoints[5] = { 0.390f, 0.575f, 0.565f, 1.000f, 9.0f };
	FString CurrentCurvePath;
	FString StatusMessage;
	bool bHasOpenCurve = false;
	bool bDirty = false;
};

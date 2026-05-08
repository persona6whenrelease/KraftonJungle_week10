#pragma once

#include "Render/Pipeline/IRenderPipeline.h"
#include "Render/Pipeline/RenderCollector.h"
#include "Render/Types/FrameContext.h"

class FRenderer;
class UGameClientEngine;

class FGameClientRenderPipeline : public IRenderPipeline
{
public:
	FGameClientRenderPipeline(UGameClientEngine* InEngine, FRenderer& InRenderer);
	~FGameClientRenderPipeline() override = default;

	void Execute(float DeltaTime, FRenderer& Renderer) override;

private:
	void RenderGameViewport(FRenderer& Renderer);

private:
	UGameClientEngine* Engine = nullptr;
	FRenderCollector Collector;
	FFrameContext Frame;
};

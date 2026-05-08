#pragma once

class UGameViewportClient;
class UWorld;
struct FInputFrame;

struct FGameplayInputRouteContext
{
    UWorld* World = nullptr;
    UGameViewportClient* ViewportClient = nullptr;
    float DeltaTime = 0.0f;
    bool bAllowScriptInput = true;
    bool bAllowViewportInput = true;
};

class FGameplayInputRouter
{
public:
    static bool Route(FInputFrame& InputFrame, const FGameplayInputRouteContext& Context);

private:
    static void ApplyGuiCapture(FInputFrame& InputFrame);
};

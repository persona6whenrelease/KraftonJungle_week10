#include "Engine/Input/GameplayInputRouter.h"

#include "Engine/Input/InputFrame.h"
#include "GameFramework/World.h"
#include "Scripting/LuaInputLibrary.h"
#include "Scripting/LuaScriptSubsystem.h"
#include "Viewport/GameViewportClient.h"
#include "Core/Log.h"

void FGameplayInputRouter::ApplyGuiCapture(FInputFrame& InputFrame)
{
    InputFrame.ApplyGuiCapture("GameplayGuiCapture");
}

bool FGameplayInputRouter::Route(FInputFrame& InputFrame, const FGameplayInputRouteContext& Context)
{
    const bool bPossessedGameplay = Context.ViewportClient && Context.ViewportClient->IsPossessed();
    const bool bGameUiCapturing = Context.ViewportClient && Context.ViewportClient->GetUiLayer() &&
        (Context.ViewportClient->GetUiLayer()->WantsMouse() ||
         Context.ViewportClient->GetUiLayer()->WantsKeyboard());

    // Possessed gameplay에서는 이전 프레임의 에디터 ImGui 캡처가 남아 있어도
    // 마우스 룩/이동 입력을 소비하지 않는다. 실제 게임 UI가 떠 있을 때만 캡처한다.
    if (!bPossessedGameplay || bGameUiCapturing)
    {
        ApplyGuiCapture(InputFrame);
    }

    // 임시 진단: Lua가 보는 시점의 RBUTTON 상태와 소비 여부
    static bool sLastRBtn = false;
    const bool bRBtnIsDown = InputFrame.IsDown(VK_RBUTTON);
    const bool bRawSnapshot = InputFrame.GetSnapshot().KeyDown[VK_RBUTTON];
    const bool bConsumed = InputFrame.IsKeyConsumed(VK_RBUTTON);
    if (bRawSnapshot != sLastRBtn)
    {
        UE_LOG("[DIAG-Router] IsDown=%d, RawSnapshot=%d, Consumed=%d, MouseConsumed=%d, KbdConsumed=%d",
            bRBtnIsDown ? 1 : 0,
            bRawSnapshot ? 1 : 0,
            bConsumed ? 1 : 0,
            InputFrame.IsMouseConsumed() ? 1 : 0,
            InputFrame.IsKeyboardConsumed() ? 1 : 0);
        sLastRBtn = bRawSnapshot;
    }

    FLuaInputLibrary::SetCurrentFrame(&InputFrame);

    bool bViewportHandledInput = false;
    if (Context.bAllowScriptInput && Context.World)
    {
        FLuaScriptSubsystem::Get().CallInput(Context.World, Context.DeltaTime);
    }

    if (Context.bAllowViewportInput && Context.ViewportClient)
    {
        bViewportHandledInput = Context.ViewportClient->Tick(Context.DeltaTime, InputFrame);
    }

    FLuaInputLibrary::ClearCurrentFrame();
    return bViewportHandledInput;
}

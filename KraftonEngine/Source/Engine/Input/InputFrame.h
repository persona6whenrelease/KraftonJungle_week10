#pragma once

#include "Engine/Input/InputSystem.h"
#include "Core/CoreTypes.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Consumption is intentionally stored on the per-frame view, not on InputSystem.
// InputSystem remains the immutable source of truth for the OS/platform state;
// FInputFrame is the only object regular consumers should use to observe and consume input.
enum class EInputConsumeType : int32
{
    Key,
    Keyboard,
    Mouse,
    MouseButtons,
    MouseDelta,
    Scroll,
    Movement,
    Look,
    TextInput,
    All
};

struct FInputConsumeRecord
{
    EInputConsumeType Type = EInputConsumeType::Key;
    int Key = -1;
    FString Consumer;
    FString Reason;
};

struct FInputFrame
{
    explicit FInputFrame(const FInputSystemSnapshot& InSnapshot)
        : Snapshot(InSnapshot)
    {
    }

    // Prefer the consumed accessors below. This escape hatch is for centralized global shortcuts,
    // diagnostics, and telemetry that must reason about the raw physical input.
    const FInputSystemSnapshot& GetRawSnapshotForGlobalShortcuts() const { return Snapshot; }
    const FInputSystemSnapshot& GetRawSnapshotForDebug() const { return Snapshot; }

    // Kept for existing code, but new input consumers should not call this directly.
    const FInputSystemSnapshot& GetSnapshot() const { return Snapshot; }

    const TArray<FInputConsumeRecord>& GetConsumeRecords() const { return ConsumeRecords; }
    bool HasConsumeRecords() const { return !ConsumeRecords.empty(); }

    bool IsKeyConsumed(int VK) const
    {
        if (!IsValidKey(VK))
        {
            return true;
        }

        if (bKeyboardConsumed && !IsMouseButtonKey(VK))
        {
            return true;
        }

        if ((bMouseConsumed || bMouseButtonsConsumed) && IsMouseButtonKey(VK))
        {
            return true;
        }

        return KeyConsumed[VK];
    }

    void ConsumeKey(int VK, const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        if (IsValidKey(VK))
        {
            KeyConsumed[VK] = true;
            RecordConsume(EInputConsumeType::Key, VK, Consumer, Reason);
        }
    }

    void ConsumeKeyboard(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bKeyboardConsumed = true;
        for (int VK = 0; VK < 256; ++VK)
        {
            if (!IsMouseButtonKey(VK))
            {
                KeyConsumed[VK] = true;
            }
        }
        RecordConsume(EInputConsumeType::Keyboard, -1, Consumer, Reason);
    }

    void ConsumeMouse(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bMouseConsumed = true;
        bMouseButtonsConsumed = true;
        bMouseDeltaConsumed = true;
        bScrollConsumed = true;
        ConsumeMouseButtons(Consumer, Reason);
        RecordConsume(EInputConsumeType::Mouse, -1, Consumer, Reason);
    }

    void ConsumeMouseButtons(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bMouseButtonsConsumed = true;
        ConsumeKey(VK_LBUTTON, Consumer, Reason);
        ConsumeKey(VK_RBUTTON, Consumer, Reason);
        ConsumeKey(VK_MBUTTON, Consumer, Reason);
        ConsumeKey(VK_XBUTTON1, Consumer, Reason);
        ConsumeKey(VK_XBUTTON2, Consumer, Reason);
        RecordConsume(EInputConsumeType::MouseButtons, -1, Consumer, Reason);
    }

    void ConsumeMouseDelta(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bMouseDeltaConsumed = true;
        RecordConsume(EInputConsumeType::MouseDelta, -1, Consumer, Reason);
    }

    void ConsumeScroll(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bScrollConsumed = true;
        RecordConsume(EInputConsumeType::Scroll, -1, Consumer, Reason);
    }

    void ConsumeMovement(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bMovementConsumed = true;
        ConsumeKey('W', Consumer, Reason);
        ConsumeKey('A', Consumer, Reason);
        ConsumeKey('S', Consumer, Reason);
        ConsumeKey('D', Consumer, Reason);
        ConsumeKey('Q', Consumer, Reason);
        ConsumeKey('E', Consumer, Reason);
        ConsumeKey(VK_SPACE, Consumer, Reason);
        ConsumeKey(VK_SHIFT, Consumer, Reason);
        ConsumeKey(VK_LSHIFT, Consumer, Reason);
        ConsumeKey(VK_RSHIFT, Consumer, Reason);
        ConsumeKey(VK_CONTROL, Consumer, Reason);
        ConsumeKey(VK_LCONTROL, Consumer, Reason);
        ConsumeKey(VK_RCONTROL, Consumer, Reason);
        RecordConsume(EInputConsumeType::Movement, -1, Consumer, Reason);
    }

    void ConsumeLook(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bLookConsumed = true;
        ConsumeMouseDelta(Consumer, Reason);
        RecordConsume(EInputConsumeType::Look, -1, Consumer, Reason);
    }

    void ConsumeTextInput(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        bTextInputConsumed = true;
        ConsumeKeyboard(Consumer, Reason);
        RecordConsume(EInputConsumeType::TextInput, -1, Consumer, Reason);
    }

    void ConsumeAll(const char* Consumer = nullptr, const char* Reason = nullptr)
    {
        ConsumeKeyboard(Consumer, Reason);
        ConsumeMouse(Consumer, Reason);
        bMovementConsumed = true;
        bLookConsumed = true;
        bTextInputConsumed = true;
        RecordConsume(EInputConsumeType::All, -1, Consumer, Reason);
    }

    void ApplyGuiCapture(const char* Consumer = "GuiCapture")
    {
        if (IsGuiUsingTextInput())
        {
            ConsumeTextInput(Consumer, "GUI text input capture");
        }
        else if (IsGuiUsingKeyboard())
        {
            ConsumeKeyboard(Consumer, "GUI keyboard capture");
        }

        if (IsGuiUsingMouse())
        {
            ConsumeMouse(Consumer, "GUI mouse capture");
        }
    }

    bool IsMovementConsumed() const { return bMovementConsumed; }
    bool IsLookConsumed() const { return bLookConsumed; }
    bool IsKeyboardConsumed() const { return bKeyboardConsumed; }
    bool IsMouseConsumed() const { return bMouseConsumed; }
    bool IsMouseButtonsConsumed() const { return bMouseButtonsConsumed; }
    bool IsMouseDeltaConsumed() const { return bMouseDeltaConsumed || bLookConsumed || bMouseConsumed; }
    bool IsScrollConsumed() const { return bScrollConsumed || bMouseConsumed; }
    bool IsTextInputConsumed() const { return bTextInputConsumed; }

    bool IsDown(int VK) const
    {
        return IsValidKey(VK) && !IsKeyConsumed(VK) && Snapshot.KeyDown[VK];
    }

    bool WasPressed(int VK) const
    {
        return IsValidKey(VK) && !IsKeyConsumed(VK) && Snapshot.KeyPressed[VK];
    }

    bool WasReleased(int VK) const
    {
        return IsValidKey(VK) && !IsKeyConsumed(VK) && Snapshot.KeyReleased[VK];
    }

    bool IsLeftDragging() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_LBUTTON) && Snapshot.bLeftDragging; }
    bool WasLeftDragStarted() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_LBUTTON) && Snapshot.bLeftDragStarted; }
    bool WasLeftDragEnded() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_LBUTTON) && Snapshot.bLeftDragEnded; }
    bool IsRightDragging() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_RBUTTON) && Snapshot.bRightDragging; }
    bool WasRightDragStarted() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_RBUTTON) && Snapshot.bRightDragStarted; }
    bool WasRightDragEnded() const { return !IsMouseConsumed() && !IsKeyConsumed(VK_RBUTTON) && Snapshot.bRightDragEnded; }

    POINT GetLeftDragVector() const
    {
        return IsLeftDragging() || WasLeftDragEnded() ? Snapshot.LeftDragVector : POINT{ 0, 0 };
    }

    POINT GetRightDragVector() const
    {
        return IsRightDragging() || WasRightDragEnded() ? Snapshot.RightDragVector : POINT{ 0, 0 };
    }

    int GetMouseDeltaX() const
    {
        return IsMouseDeltaConsumed() ? 0 : Snapshot.MouseDeltaX;
    }

    int GetMouseDeltaY() const
    {
        return IsMouseDeltaConsumed() ? 0 : Snapshot.MouseDeltaY;
    }

    int GetScrollDelta() const
    {
        return IsScrollConsumed() ? 0 : Snapshot.ScrollDelta;
    }

    float GetScrollNotches() const
    {
        return GetScrollDelta() / static_cast<float>(WHEEL_DELTA);
    }

    POINT GetMousePosition() const
    {
        return Snapshot.MousePos;
    }

    bool IsGuiUsingMouse() const { return Snapshot.bGuiUsingMouse; }
    bool IsGuiUsingKeyboard() const { return Snapshot.bGuiUsingKeyboard; }
    bool IsGuiUsingTextInput() const { return Snapshot.bGuiUsingTextInput; }
    bool IsWindowFocused() const { return Snapshot.bWindowFocused; }
    bool IsMouseCaptured() const { return Snapshot.bUsingRawMouse; }

private:
    static bool IsValidKey(int VK)
    {
        return VK >= 0 && VK < 256;
    }

    static bool IsMouseButtonKey(int VK)
    {
        return VK == VK_LBUTTON
            || VK == VK_RBUTTON
            || VK == VK_MBUTTON
            || VK == VK_XBUTTON1
            || VK == VK_XBUTTON2;
    }

    void RecordConsume(EInputConsumeType Type, int Key, const char* Consumer, const char* Reason)
    {
        FInputConsumeRecord Record;
        Record.Type = Type;
        Record.Key = Key;
        Record.Consumer = Consumer ? Consumer : "Unknown";
        Record.Reason = Reason ? Reason : "";
        ConsumeRecords.push_back(Record);
    }

private:
    FInputSystemSnapshot Snapshot;
    bool KeyConsumed[256] = {};
    bool bKeyboardConsumed = false;
    bool bMouseConsumed = false;
    bool bMouseButtonsConsumed = false;
    bool bMouseDeltaConsumed = false;
    bool bScrollConsumed = false;
    bool bMovementConsumed = false;
    bool bLookConsumed = false;
    bool bTextInputConsumed = false;
    TArray<FInputConsumeRecord> ConsumeRecords;
};

#pragma once

struct FInputFrame;

namespace sol { class state; }

class FLuaInputLibrary
{
public:
    static void RegisterInputBinding(sol::state& Lua);
    static void SetCurrentFrame(FInputFrame* InputFrame);
    static void ClearCurrentFrame();
};

#include "LuaInputLibrary.h"
#include "SolInclude.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"

#include <cctype>
#include <string>
#include <unordered_map>

namespace
{
    FInputFrame* GCurrentFrame = nullptr;

    int NameToVK(const std::string& Name)
    {
        if (Name.empty())
        {
            return -1;
        }

        if (Name.size() == 1)
        {
            const char C = static_cast<char>(toupper(static_cast<unsigned char>(Name[0])));
            if (C >= 'A' && C <= 'Z')
            {
                return C;
            }
            if (C >= '0' && C <= '9')
            {
                return C;
            }
        }

        static const std::unordered_map<std::string, int> KeyMap = {
            { "SPACE", VK_SPACE },
            { "ENTER", VK_RETURN },
            { "RETURN", VK_RETURN },
            { "ESCAPE", VK_ESCAPE },
            { "ESC", VK_ESCAPE },
            { "SHIFT", VK_SHIFT },
            { "LSHIFT", VK_LSHIFT },
            { "RSHIFT", VK_RSHIFT },
            { "CTRL", VK_CONTROL },
            { "CONTROL", VK_CONTROL },
            { "LCTRL", VK_LCONTROL },
            { "RCTRL", VK_RCONTROL },
            { "ALT", VK_MENU },
            { "LALT", VK_LMENU },
            { "RALT", VK_RMENU },
            { "TAB", VK_TAB },
            { "BACKSPACE", VK_BACK },
            { "DELETE", VK_DELETE },
            { "INSERT", VK_INSERT },
            { "HOME", VK_HOME },
            { "END", VK_END },
            { "PAGEUP", VK_PRIOR },
            { "PAGEDOWN", VK_NEXT },
            { "LEFT", VK_LEFT },
            { "RIGHT", VK_RIGHT },
            { "UP", VK_UP },
            { "DOWN", VK_DOWN },
            { "F1", VK_F1 },
            { "F2", VK_F2 },
            { "F3", VK_F3 },
            { "F4", VK_F4 },
            { "F5", VK_F5 },
            { "F6", VK_F6 },
            { "F7", VK_F7 },
            { "F8", VK_F8 },
            { "F9", VK_F9 },
            { "F10", VK_F10 },
            { "F11", VK_F11 },
            { "F12", VK_F12 },
            { "MOUSE1", VK_LBUTTON },
            { "MOUSE2", VK_RBUTTON },
            { "MOUSE3", VK_MBUTTON },
            { "MOUSE4", VK_XBUTTON1 },
            { "MOUSE5", VK_XBUTTON2 },
        };

        std::string Upper = Name;
        for (char& C : Upper)
        {
            C = static_cast<char>(toupper(static_cast<unsigned char>(C)));
        }

        auto It = KeyMap.find(Upper);
        return (It != KeyMap.end()) ? It->second : -1;
    }
}

void FLuaInputLibrary::SetCurrentFrame(FInputFrame* InputFrame)
{
    GCurrentFrame = InputFrame;
}

void FLuaInputLibrary::ClearCurrentFrame()
{
    GCurrentFrame = nullptr;
}

// LuaBindings.h의 free function 선언에 대응하는 wrapper
void RegisterInputBinding(sol::state& Lua)
{
    FLuaInputLibrary::RegisterInputBinding(Lua);
}

void FLuaInputLibrary::RegisterInputBinding(sol::state& Lua)
{
    sol::table Input = Lua.create_named_table("Input");

    Input.set_function("GetKey", [](const std::string& KeyName) -> bool
    {
        const int VK = NameToVK(KeyName);
        return GCurrentFrame && GCurrentFrame->IsDown(VK);
    });

    Input.set_function("GetKeyDown", [](const std::string& KeyName) -> bool
    {
        const int VK = NameToVK(KeyName);
        return GCurrentFrame && GCurrentFrame->WasPressed(VK);
    });

    Input.set_function("GetKeyUp", [](const std::string& KeyName) -> bool
    {
        const int VK = NameToVK(KeyName);
        return GCurrentFrame && GCurrentFrame->WasReleased(VK);
    });

    Input.set_function("ConsumeKey", [](const std::string& KeyName) -> bool
    {
        const int VK = NameToVK(KeyName);
        if (!GCurrentFrame || VK < 0 || VK >= 256)
        {
            return false;
        }
        GCurrentFrame->ConsumeKey(VK, "Lua", "Input.ConsumeKey");
        return true;
    });

    Input.set_function("IsKeyConsumed", [](const std::string& KeyName) -> bool
    {
        const int VK = NameToVK(KeyName);
        return GCurrentFrame ? GCurrentFrame->IsKeyConsumed(VK) : true;
    });

    Input.set_function("ConsumeKeyboard", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeKeyboard("Lua", "Input.ConsumeKeyboard");
        }
    });

    Input.set_function("ConsumeMouse", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeMouse("Lua", "Input.ConsumeMouse");
        }
    });

    Input.set_function("ConsumeMouseDelta", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeMouseDelta("Lua", "Input.ConsumeMouseDelta");
        }
    });

    Input.set_function("ConsumeScroll", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeScroll("Lua", "Input.ConsumeScroll");
        }
    });

    Input.set_function("ConsumeMovement", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeMovement("Lua", "Input.ConsumeMovement");
        }
    });

    Input.set_function("ConsumeLook", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeLook("Lua", "Input.ConsumeLook");
        }
    });

    Input.set_function("ConsumeAll", []()
    {
        if (GCurrentFrame)
        {
            GCurrentFrame->ConsumeAll("Lua", "Input.ConsumeAll");
        }
    });

    Input.set_function("GetMouseDelta", [](sol::this_state TS) -> sol::table
    {
        sol::state_view Lua(TS);
        sol::table T = Lua.create_table();
        T["x"] = GCurrentFrame ? static_cast<float>(GCurrentFrame->GetMouseDeltaX()) : 0.0f;
        T["y"] = GCurrentFrame ? static_cast<float>(GCurrentFrame->GetMouseDeltaY()) : 0.0f;
        return T;
    });

    Input.set_function("GetMousePosition", [](sol::this_state TS) -> sol::table
    {
        sol::state_view Lua(TS);
        sol::table T = Lua.create_table();
        const POINT MousePos = GCurrentFrame ? GCurrentFrame->GetMousePosition() : POINT{ 0, 0 };
        T["x"] = static_cast<float>(MousePos.x);
        T["y"] = static_cast<float>(MousePos.y);
        return T;
    });

    Input.set_function("GetScroll", []() -> float
    {
        return GCurrentFrame ? static_cast<float>(GCurrentFrame->GetScrollDelta()) : 0.0f;
    });

    Input.set_function("IsGuiUsingMouse", []() -> bool
    {
        return GCurrentFrame ? GCurrentFrame->IsGuiUsingMouse() : false;
    });

    Input.set_function("IsGuiUsingKeyboard", []() -> bool
    {
        return GCurrentFrame ? GCurrentFrame->IsGuiUsingKeyboard() : false;
    });

    Input.set_function("IsWindowFocused", []() -> bool
    {
        return GCurrentFrame ? GCurrentFrame->IsWindowFocused() : false;
    });

    Input.set_function("IsMouseCaptured", []() -> bool
    {
        return GCurrentFrame ? GCurrentFrame->IsMouseCaptured() : false;
    });

    Input.set_function("SetMouseCaptured", [](bool bCapture)
    {
        InputSystem::Get().SetUseRawMouse(bCapture);
    });
}

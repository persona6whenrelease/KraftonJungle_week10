#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class FLoadingScreen
{
public:
    void Begin(HWND InHWnd);
    void Update(const wchar_t* StatusText);
    void End();

private:
    void AnimationLoop();
    void Render(const wchar_t* StatusText, int Frame);

    HWND HWnd = nullptr;
    std::atomic<bool> bRunning{ false };
    std::mutex StatusMutex;
    std::wstring CurrentStatus{ L"초기화 중..." };
    std::thread AnimThread;
};

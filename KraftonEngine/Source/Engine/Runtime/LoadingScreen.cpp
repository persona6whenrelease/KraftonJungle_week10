#include "Engine/Runtime/LoadingScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

void FLoadingScreen::Begin(HWND InHWnd)
{
    HWnd = InHWnd;
    bRunning.store(true);
    AnimThread = std::thread(&FLoadingScreen::AnimationLoop, this);
}

void FLoadingScreen::Update(const wchar_t* StatusText)
{
    std::lock_guard<std::mutex> Lock(StatusMutex);
    CurrentStatus = StatusText;
}

void FLoadingScreen::End()
{
    bRunning.store(false);
    if (AnimThread.joinable())
    {
        AnimThread.join();
    }
    HWnd = nullptr;
}

void FLoadingScreen::AnimationLoop()
{
    int Frame = 0;
    while (bRunning.load())
    {
        std::wstring Status;
        {
            std::lock_guard<std::mutex> Lock(StatusMutex);
            Status = CurrentStatus;
        }
        Render(Status.c_str(), Frame++);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}

void FLoadingScreen::Render(const wchar_t* StatusText, int InFrame)
{
    if (!HWnd)
    {
        return;
    }

    RECT ClientRect;
    GetClientRect(HWnd, &ClientRect);
    const int W = ClientRect.right;
    const int H = ClientRect.bottom;
    if (W <= 0 || H <= 0)
    {
        return;
    }

    // 실제 보이는 영역 기준으로 중앙 계산
    RECT WindowRect;
    GetWindowRect(HWnd, &WindowRect);
    HMONITOR Monitor = MonitorFromWindow(HWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO MonInfo = { sizeof(MONITORINFO) };
    GetMonitorInfo(Monitor, &MonInfo);
    const RECT& Work = MonInfo.rcWork;

    const int VisLeft   = (std::max)(0, static_cast<int>(Work.left   - WindowRect.left));
    const int VisTop    = (std::max)(0, static_cast<int>(Work.top    - WindowRect.top));
    const int VisRight  = (std::min)(W, static_cast<int>(Work.right  - WindowRect.left));
    const int VisBottom = (std::min)(H, static_cast<int>(Work.bottom - WindowRect.top));

    const int CX = (VisLeft + VisRight) / 2;
    const int CY = (VisTop + VisBottom) / 2;

    // 더블 버퍼링
    HDC Hdc = GetDC(HWnd);
    HDC MemDC = CreateCompatibleDC(Hdc);
    HBITMAP MemBitmap = CreateCompatibleBitmap(Hdc, W, H);
    HBITMAP OldBitmap = static_cast<HBITMAP>(SelectObject(MemDC, MemBitmap));

    // 배경
    HBRUSH BgBrush = CreateSolidBrush(RGB(20, 20, 28));
    FillRect(MemDC, &ClientRect, BgBrush);
    DeleteObject(BgBrush);

    SetBkMode(MemDC, TRANSPARENT);

    // 제목
    HFONT TitleFont = CreateFontW(
        52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(MemDC, TitleFont);
    SetTextColor(MemDC, RGB(240, 240, 250));
    RECT TitleRect = { 0, CY - 100, W, CY - 40 };
    DrawTextW(MemDC, L"Game Tech Lab", -1, &TitleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(TitleFont);

    // 스피너: 8개 점이 원을 따라 회전
    const int NumDots    = 8;
    const int SpinRadius = 22;
    const int DotR       = 4;
    const int SpinCY     = CY + 10;
    const int ActiveDot  = InFrame % NumDots;

    HPEN NullPen = CreatePen(PS_NULL, 0, 0);
    HPEN OldPen  = static_cast<HPEN>(SelectObject(MemDC, NullPen));

    for (int i = 0; i < NumDots; i++)
    {
        // 12시 방향에서 시작
        float Angle = static_cast<float>(i) / NumDots * 6.28318f - 1.5708f;
        int DotX = CX      + static_cast<int>(cosf(Angle) * SpinRadius);
        int DotY = SpinCY  + static_cast<int>(sinf(Angle) * SpinRadius);

        // 활성 점에서 멀수록 어두워짐
        int Dist = (ActiveDot - i + NumDots) % NumDots;
        float T  = 1.0f - static_cast<float>(Dist) / NumDots;

        int R = 35 + static_cast<int>((79  - 35) * T);
        int G = 50 + static_cast<int>((193 - 50) * T);
        int B = 65 + static_cast<int>((233 - 65) * T);

        HBRUSH DotBrush = CreateSolidBrush(RGB(R, G, B));
        HBRUSH OldBrush = static_cast<HBRUSH>(SelectObject(MemDC, DotBrush));
        Ellipse(MemDC, DotX - DotR, DotY - DotR, DotX + DotR, DotY + DotR);
        SelectObject(MemDC, OldBrush);
        DeleteObject(DotBrush);
    }

    SelectObject(MemDC, OldPen);
    DeleteObject(NullPen);

    // 상태 텍스트
    HFONT StatusFont = CreateFontW(
        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(MemDC, StatusFont);
    SetTextColor(MemDC, RGB(130, 140, 165));
    RECT StatusRect = { 0, SpinCY + SpinRadius + 16, W, SpinCY + SpinRadius + 48 };
    DrawTextW(MemDC, StatusText, -1, &StatusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(StatusFont);

    BitBlt(Hdc, 0, 0, W, H, MemDC, 0, 0, SRCCOPY);

    SelectObject(MemDC, OldBitmap);
    DeleteObject(MemBitmap);
    DeleteDC(MemDC);
    ReleaseDC(HWnd, Hdc);
}

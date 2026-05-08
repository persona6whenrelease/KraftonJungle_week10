#include "Engine/Runtime/WindowsWindow.h"

void FWindowsWindow::Initialize(HWND InHWindow)
{
	HWindow = InHWindow;

	RECT Rect;
	GetClientRect(HWindow, &Rect);
	Width = static_cast<float>(Rect.right - Rect.left);
	Height = static_cast<float>(Rect.bottom - Rect.top);
}

void FWindowsWindow::OnResized(unsigned int InWidth, unsigned int InHeight)
{
	Width = static_cast<float>(InWidth);
	Height = static_cast<float>(InHeight);
}

void FWindowsWindow::SetTitle(const wchar_t* Title)
{
	if (HWindow)
	{
		SetWindowTextW(HWindow, Title ? Title : L"");
	}
}

void FWindowsWindow::ResizeClient(unsigned int InWidth, unsigned int InHeight)
{
	if (!HWindow || InWidth == 0 || InHeight == 0)
	{
		return;
	}

	RECT Rect = { 0, 0, static_cast<LONG>(InWidth), static_cast<LONG>(InHeight) };
	const DWORD Style = static_cast<DWORD>(GetWindowLongPtr(HWindow, GWL_STYLE));
	const DWORD ExStyle = static_cast<DWORD>(GetWindowLongPtr(HWindow, GWL_EXSTYLE));
	AdjustWindowRectEx(&Rect, Style, FALSE, ExStyle);

	SetWindowPos(
		HWindow,
		nullptr,
		0,
		0,
		Rect.right - Rect.left,
		Rect.bottom - Rect.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	OnResized(InWidth, InHeight);
}

POINT FWindowsWindow::ScreenToClientPoint(POINT ScreenPoint) const
{
	ScreenToClient(HWindow, &ScreenPoint);
	return ScreenPoint;
}

void FWindowsWindow::ToggleFullscreen()
{
	bIsFullscreen = !bIsFullscreen;

	if (bIsFullscreen)
	{
		GetWindowRect(HWindow, &SavedWindowRect);

		MONITORINFO MonitorInfo = {};
		MonitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(MonitorFromWindow(HWindow, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo);
		const RECT& R = MonitorInfo.rcMonitor;

		SetWindowLongPtr(HWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(HWindow, HWND_TOP,
			R.left, R.top,
			R.right - R.left, R.bottom - R.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);
		ShowWindow(HWindow, SW_MAXIMIZE);
	}
	else
	{
		SetWindowLongPtr(HWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW);
		SetWindowPos(HWindow, HWND_NOTOPMOST,
			SavedWindowRect.left, SavedWindowRect.top,
			SavedWindowRect.right - SavedWindowRect.left,
			SavedWindowRect.bottom - SavedWindowRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE);
		ShowWindow(HWindow, SW_NORMAL);
	}
}

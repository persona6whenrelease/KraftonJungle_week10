#include <Windows.h>
#include <objbase.h>
#include "Engine/Runtime/Launch.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nShowCmd)
{
	// STA를 먼저 선점해 OpenAL-Soft WASAPI 백엔드가 MTA로 변경하지 못하도록 한다.
	// MTA로 초기화된 스레드에서 IFileDialog::Show()를 호출하면 COM 데드락이 발생한다.
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const int Result = Launch(hInstance, nShowCmd);
	CoUninitialize();
	return Result;
}

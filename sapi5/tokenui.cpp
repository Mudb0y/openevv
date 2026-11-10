#include "tokenui.h"

#include <cwchar>

#include "dialog.h"

// The module the dialog's template is in, which the server keeps.
HMODULE OpenEvvModule();
void OpenEvvObjectReleased();

namespace openevv {

TokenUI::~TokenUI() {
	OpenEvvObjectReleased();
}

STDMETHODIMP TokenUI::QueryInterface(REFIID riid, void** object) {
	if (!object)
		return E_POINTER;
	*object = nullptr;
	if (riid == IID_IUnknown || riid == __uuidof(ISpTokenUI)) {
		*object = static_cast<ISpTokenUI*>(this);
		AddRef();
		return S_OK;
	}
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) TokenUI::AddRef() {
	return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) TokenUI::Release() {
	const LONG left = InterlockedDecrement(&references_);
	if (left == 0)
		delete this;
	return static_cast<ULONG>(left);
}

STDMETHODIMP TokenUI::IsUISupported(LPCWSTR type, void*, ULONG, IUnknown*,
                                    BOOL* supported) {
	if (!supported)
		return E_POINTER;
	// The one kind there is: the engine's settings. Everything else a host
	// might ask about -- training, enrolment, the microphone -- belongs to
	// recognition.
	*supported = type && std::wcscmp(type, SPDUI_EngineProperties) == 0;
	return S_OK;
}

STDMETHODIMP TokenUI::DisplayUI(HWND parent, LPCWSTR, LPCWSTR type, void*,
                                ULONG, ISpObjectToken*, IUnknown*) {
	if (!type || std::wcscmp(type, SPDUI_EngineProperties) != 0)
		return E_INVALIDARG;
	// The settings are shared by every voice, so which token asked does not
	// change what is shown.
	ShowSettingsDialog(OpenEvvModule(), parent);
	return S_OK;
}

}  // namespace openevv

/* What makes this a COM server: the factories SAPI asks for objects through,
 * and the registration that puts the voices where SAPI finds them.
 *
 * Registration asks the engine rather than a list. Every language the
 * library beside the voice answers eciGetAvailableLanguages with, and that
 * languages.cpp knows a locale for, gets its eight presets as eight tokens,
 * under the names the engine gives them. A library built with fewer
 * languages registers fewer voices, and registering again after the library
 * has changed takes away the tokens of a language it no longer has: every
 * token naming this voice's class is removed before any is written.
 *
 * It all goes in HKEY_LOCAL_MACHINE, which is where SAPI hosts look and the
 * only place some older ones look, and which needs administrator; that is
 * the installer's business rather than the voice's. Each word size writes
 * its own view of the registry, so the thirty-two bit voice registered by
 * the thirty-two bit regsvr32 lands where thirty-two bit programs look.
 */

#include <windows.h>

#include <olectl.h>

#include <new>
#include <string>
#include <vector>

#include "engine.h"
#include "guids.h"
#include "languages.h"
#include "text.h"
#include "tokenui.h"
#include "voice.h"

namespace {

HMODULE g_module = nullptr;
LONG g_objects = 0;
LONG g_locks = 0;

constexpr wchar_t kTokensKey[] = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens";
constexpr wchar_t kClassesKey[] = L"SOFTWARE\\Classes\\CLSID";

// SAPI's age for each of the eight, which the engine has no setting for.
// The presets are the same eight in the same order in every language: two
// adult men, an adult woman, a child, another man, another woman, and an
// elderly woman and man.
const wchar_t* const kAges[8] = {
    L"Adult", L"Adult", L"Child", L"Adult",
    L"Adult", L"Adult", L"Senior", L"Senior",
};

std::wstring GuidText(REFGUID guid) {
	wchar_t buffer[64] = {0};
	StringFromGUID2(guid, buffer, 64);
	return std::wstring(buffer);
}

std::wstring ModulePath() {
	wchar_t path[MAX_PATH] = {0};
	GetModuleFileNameW(g_module, path, MAX_PATH);
	return std::wstring(path);
}

bool SetValue(HKEY key, const wchar_t* name, const std::wstring& value) {
	return RegSetValueExW(key, name, 0, REG_SZ,
	                      reinterpret_cast<const BYTE*>(value.c_str()),
	                      static_cast<DWORD>((value.size() + 1) *
	                                         sizeof(wchar_t))) == ERROR_SUCCESS;
}

// A key, made if it is not there, or nothing.
HKEY CreateKey(const std::wstring& path) {
	HKEY key = nullptr;
	if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr,
	                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
	                    nullptr) != ERROR_SUCCESS)
		return nullptr;
	return key;
}

// One COM class: the library that has it and the threading it takes.
bool RegisterClass(REFGUID clsid, const wchar_t* description) {
	const std::wstring base =
	    std::wstring(kClassesKey) + L"\\" + GuidText(clsid);
	HKEY key = CreateKey(base);
	if (!key)
		return false;
	SetValue(key, nullptr, description);
	RegCloseKey(key);

	key = CreateKey(base + L"\\InprocServer32");
	if (!key)
		return false;
	SetValue(key, nullptr, ModulePath());
	// Both, so that a host which insists on either finds what it expects.
	// What the voice keeps is an instance tied to a thread, and it makes
	// another when it is called on a different one.
	SetValue(key, L"ThreadingModel", L"Both");
	RegCloseKey(key);
	return true;
}

// Every token whose CLSID is this voice's, gone.
void RemoveTokens() {
	HKEY tokens = nullptr;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kTokensKey, 0,
	                  KEY_READ | KEY_WRITE, &tokens) != ERROR_SUCCESS)
		return;
	const std::wstring ours = GuidText(CLSID_OpenEvvVoice);
	std::vector<std::wstring> doomed;
	for (DWORD i = 0;; ++i) {
		wchar_t name[256];
		DWORD length = 256;
		if (RegEnumKeyExW(tokens, i, name, &length, nullptr, nullptr, nullptr,
		                  nullptr) != ERROR_SUCCESS)
			break;
		wchar_t clsid[64] = {0};
		DWORD size = sizeof clsid - sizeof(wchar_t);
		if (RegGetValueW(tokens, name, L"CLSID", RRF_RT_REG_SZ, nullptr, clsid,
		                 &size) == ERROR_SUCCESS &&
		    _wcsicmp(clsid, ours.c_str()) == 0)
			doomed.push_back(name);
	}
	// Deleted after the walk, which deleting would otherwise disturb.
	for (const std::wstring& name : doomed)
		RegDeleteTreeW(tokens, name.c_str());
	RegCloseKey(tokens);
}

// One voice, as SAPI reads a token: a name, the class that speaks it, what
// the voice reads back to know which it is, and the attributes a program
// filters a voice list by.
bool RegisterVoice(const openevv::Language& language, int preset,
                   const std::wstring& presetName, bool female) {
	const std::wstring id = std::wstring(L"OpenEVV-") + language.tag + L"-" +
	                        std::to_wstring(preset);
	const std::wstring name =
	    L"OpenEVV " + presetName + L" - " + language.name;
	const std::wstring base = std::wstring(kTokensKey) + L"\\" + id;

	HKEY key = CreateKey(base);
	if (!key)
		return false;
	wchar_t number[16];
	swprintf(number, 16, L"%x", language.lcid);
	SetValue(key, nullptr, name);
	SetValue(key, number, name);
	SetValue(key, L"CLSID", GuidText(CLSID_OpenEvvVoice));
	swprintf(number, 16, L"%x", static_cast<unsigned>(language.id));
	SetValue(key, L"OpenEVVLanguage", number);
	SetValue(key, L"OpenEVVPreset", std::to_wstring(preset));
	RegCloseKey(key);

	key = CreateKey(base + L"\\Attributes");
	if (!key)
		return false;
	swprintf(number, 16, L"%x", language.lcid);
	SetValue(key, L"Name", name);
	SetValue(key, L"Gender", female ? L"Female" : L"Male");
	SetValue(key, L"Age", kAges[preset - 1]);
	SetValue(key, L"Vendor", L"OpenEVV");
	SetValue(key, L"Language", number);
	RegCloseKey(key);

	// The Settings button the speech control panel and some other hosts show.
	key = CreateKey(base + L"\\UI\\EngineProperties");
	if (!key)
		return false;
	SetValue(key, L"CLSID", GuidText(CLSID_OpenEvvTokenUI));
	RegCloseKey(key);
	return true;
}

// A language's eight, named as its own instance names them. False if the
// engine would not make one.
bool RegisterLanguage(const openevv::Language& language) {
	openevv::Engine engine;
	if (!engine.Open(language.id))
		return false;
	for (int preset = 1; preset <= 8; ++preset) {
		std::wstring presetName =
		    openevv::Decode(engine.PresetName(preset), language.codePage);
		if (presetName.empty())
			presetName = L"Voice " + std::to_wstring(preset);
		const bool female = engine.GetVoiceParam(preset, eciGender) == 1;
		if (!RegisterVoice(language, preset, presetName, female))
			return false;
	}
	return true;
}

// A minimal factory: nothing to aggregate and nothing to configure, so it
// makes one object and hands it over.
template <typename T>
class Factory : public IClassFactory {
 public:
	STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
		if (!object)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IClassFactory) {
			*object = static_cast<IClassFactory*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	// It lives as long as the library does.
	STDMETHODIMP_(ULONG) AddRef() override { return 2; }
	STDMETHODIMP_(ULONG) Release() override { return 1; }

	STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
	                            void** object) override {
		if (!object)
			return E_POINTER;
		*object = nullptr;
		if (outer)
			return CLASS_E_NOAGGREGATION;
		InterlockedIncrement(&g_objects);
		T* made = new (std::nothrow) T();
		if (!made) {
			InterlockedDecrement(&g_objects);
			return E_OUTOFMEMORY;
		}
		const HRESULT hr = made->QueryInterface(riid, object);
		made->Release();
		return hr;
	}

	STDMETHODIMP LockServer(BOOL lock) override {
		if (lock)
			InterlockedIncrement(&g_locks);
		else
			InterlockedDecrement(&g_locks);
		return S_OK;
	}
};

Factory<openevv::Voice> g_voiceFactory;
Factory<openevv::TokenUI> g_tokenUIFactory;

}  // namespace

void OpenEvvObjectReleased() {
	InterlockedDecrement(&g_objects);
}

HMODULE OpenEvvModule() {
	return g_module;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
	if (!object)
		return E_POINTER;
	*object = nullptr;
	if (clsid == CLSID_OpenEvvVoice)
		return g_voiceFactory.QueryInterface(riid, object);
	if (clsid == CLSID_OpenEvvTokenUI)
		return g_tokenUIFactory.QueryInterface(riid, object);
	return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
	return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
	// Without the engine there is nothing to offer, and a voice list full of
	// voices that say nothing is worse than none.
	const std::vector<int> languages = openevv::Engine::Languages();
	if (languages.empty())
		return SELFREG_E_CLASS;

	if (!RegisterClass(CLSID_OpenEvvVoice, L"OpenEVV Voice"))
		return SELFREG_E_CLASS;
	if (!RegisterClass(CLSID_OpenEvvTokenUI, L"OpenEVV Voice Settings"))
		return SELFREG_E_CLASS;

	RemoveTokens();
	int registered = 0;
	for (int id : languages) {
		const openevv::Language* language = openevv::FindLanguage(id);
		if (!language)
			continue;
		if (!RegisterLanguage(*language))
			return SELFREG_E_CLASS;
		++registered;
	}
	return registered > 0 ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
	RemoveTokens();
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" +
	                                    GuidText(CLSID_OpenEvvVoice))
	                                       .c_str());
	RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(kClassesKey) + L"\\" +
	                                    GuidText(CLSID_OpenEvvTokenUI))
	                                       .c_str());
	return S_OK;
}

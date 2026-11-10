/* Drive the voice without registering it.
 *
 * The library is loaded by hand, the class asked for through
 * DllGetClassObject, and a small site and a small token stand in for SAPI's.
 * That checks what the voice does -- the token, the fragments, the rate, the
 * bookmarks, the abort, every language the engine has -- without touching
 * the registry, which is worth having apart from whether registration works.
 *
 * It finds the engine where the voice does, beside itself, so it is run from
 * the folder the voice is staged in:
 *
 *     voice_test openevv_sapi5.dll [out.wav]
 *     voice_test32 openevv_sapi5_x86.dll
 */

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#include "languages.h"
#include "sapiddk.h"

#ifndef SPERR_NOT_FOUND
#define SPERR_NOT_FOUND ((HRESULT)0x8004503AL)
#endif

namespace {

// {7CAE4ACA-3E08-4B9B-A38E-D7911D77485E}, written out rather than taken from
// guids.cpp so that a change there is caught here.
const CLSID kVoiceClsid = {0x7cae4aca, 0x3e08, 0x4b9b,
                           {0xa3, 0x8e, 0xd7, 0x91, 0x1d, 0x77, 0x48, 0x5e}};

// The least a site can be and still be one: it takes the samples, answers
// what the voice asks, and remembers what it was told.
class TestSite : public ISpTTSEngineSite {
 public:
	explicit TestSite(long rate = 0) : rate_(rate) {}

	STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
		if (riid == IID_IUnknown || riid == __uuidof(ISpTTSEngineSite)) {
			*object = static_cast<ISpTTSEngineSite*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return 2; }
	STDMETHODIMP_(ULONG) Release() override { return 1; }

	STDMETHODIMP AddEvents(const SPEVENT* events, ULONG count) override {
		for (ULONG i = 0; i < count; ++i) {
			if (events[i].eEventId == SPEI_TTS_BOOKMARK) {
				bookmarks.push_back(
				    reinterpret_cast<const wchar_t*>(events[i].lParam));
				bookmarkOffsets.push_back(events[i].ullAudioStreamOffset);
			}
		}
		return S_OK;
	}
	STDMETHODIMP GetEventInterest(ULONGLONG* interest) override {
		*interest = SPFEI_ALL_TTS_EVENTS;
		return S_OK;
	}

	STDMETHODIMP_(DWORD) GetActions() override {
		return (abortAfter >= 0 && writes >= abortAfter) ? SPVES_ABORT
		                                                 : SPVES_CONTINUE;
	}
	STDMETHODIMP Write(const void* data, ULONG bytes, ULONG* written) override {
		const short* samples = static_cast<const short*>(data);
		audio.insert(audio.end(), samples, samples + bytes / sizeof(short));
		++writes;
		if (written)
			*written = bytes;
		return S_OK;
	}
	STDMETHODIMP GetRate(long* rate) override {
		*rate = rate_;
		return S_OK;
	}
	STDMETHODIMP GetVolume(USHORT* volume) override {
		*volume = 100;
		return S_OK;
	}
	STDMETHODIMP GetSkipInfo(SPVSKIPTYPE* type, long* items) override {
		*type = SPVST_SENTENCE;
		*items = 0;
		return S_OK;
	}
	STDMETHODIMP CompleteSkip(long) override { return S_OK; }

	std::vector<short> audio;
	std::vector<std::wstring> bookmarks;
	std::vector<ULONGLONG> bookmarkOffsets;
	int writes = 0;
	int abortAfter = -1;

 private:
	long rate_ = 0;
};

// A token that holds the two values registration writes and nothing else.
class TestToken : public ISpObjectToken {
 public:
	TestToken(int language, int preset) {
		wchar_t text[16];
		std::swprintf(text, 16, L"%x", static_cast<unsigned>(language));
		language_ = text;
		preset_ = std::to_wstring(preset);
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
		if (riid == IID_IUnknown || riid == __uuidof(ISpDataKey) ||
		    riid == __uuidof(ISpObjectToken)) {
			*object = static_cast<ISpObjectToken*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return 2; }
	STDMETHODIMP_(ULONG) Release() override { return 1; }

	STDMETHODIMP GetStringValue(LPCWSTR name, LPWSTR* value) override {
		const std::wstring* found = nullptr;
		if (name && std::wcscmp(name, L"OpenEVVLanguage") == 0)
			found = &language_;
		else if (name && std::wcscmp(name, L"OpenEVVPreset") == 0)
			found = &preset_;
		if (!found)
			return SPERR_NOT_FOUND;
		const size_t bytes = (found->size() + 1) * sizeof(wchar_t);
		*value = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
		std::memcpy(*value, found->c_str(), bytes);
		return S_OK;
	}

	STDMETHODIMP SetData(LPCWSTR, ULONG, const BYTE*) override { return E_NOTIMPL; }
	STDMETHODIMP GetData(LPCWSTR, ULONG*, BYTE*) override { return E_NOTIMPL; }
	STDMETHODIMP SetStringValue(LPCWSTR, LPCWSTR) override { return E_NOTIMPL; }
	STDMETHODIMP SetDWORD(LPCWSTR, DWORD) override { return E_NOTIMPL; }
	STDMETHODIMP GetDWORD(LPCWSTR, DWORD*) override { return E_NOTIMPL; }
	STDMETHODIMP OpenKey(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
	STDMETHODIMP CreateKey(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
	STDMETHODIMP DeleteKey(LPCWSTR) override { return E_NOTIMPL; }
	STDMETHODIMP DeleteValue(LPCWSTR) override { return E_NOTIMPL; }
	STDMETHODIMP EnumKeys(ULONG, LPWSTR*) override { return E_NOTIMPL; }
	STDMETHODIMP EnumValues(ULONG, LPWSTR*) override { return E_NOTIMPL; }
	STDMETHODIMP SetId(LPCWSTR, LPCWSTR, WINBOOL) override { return E_NOTIMPL; }
	STDMETHODIMP GetId(LPWSTR*) override { return E_NOTIMPL; }
	STDMETHODIMP GetCategory(ISpObjectTokenCategory**) override { return E_NOTIMPL; }
	STDMETHODIMP CreateInstance(IUnknown*, DWORD, REFIID, void**) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP GetStorageFileName(REFCLSID, LPCWSTR, LPCWSTR, ULONG,
	                                LPWSTR*) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP RemoveStorageFileName(REFCLSID, LPCWSTR, WINBOOL) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP Remove(const CLSID*) override { return E_NOTIMPL; }
	STDMETHODIMP IsUISupported(LPCWSTR, void*, ULONG, IUnknown*,
	                           WINBOOL*) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP DisplayUI(HWND, LPCWSTR, LPCWSTR, void*, ULONG,
	                       IUnknown*) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP MatchesAttributes(LPCWSTR, WINBOOL*) override {
		return E_NOTIMPL;
	}

 private:
	std::wstring language_;
	std::wstring preset_;
};

SPVTEXTFRAG Fragment(SPVACTIONS action, const wchar_t* text) {
	SPVTEXTFRAG fragment = {};
	fragment.State.eAction = action;
	fragment.State.Volume = 100;
	fragment.pTextStart = text;
	fragment.ulTextLen = text ? static_cast<ULONG>(std::wcslen(text)) : 0;
	return fragment;
}

bool WriteWave(const char* path, const std::vector<short>& audio, int rate) {
	FILE* file = std::fopen(path, "wb");
	if (!file)
		return false;
	const unsigned data = static_cast<unsigned>(audio.size() * 2);
	const unsigned riff = 36 + data, fmt = 16, byteRate = rate * 2;
	const unsigned short pcm = 1, mono = 1, align = 2, bits = 16;
	std::fwrite("RIFF", 1, 4, file);
	std::fwrite(&riff, 4, 1, file);
	std::fwrite("WAVEfmt ", 1, 8, file);
	std::fwrite(&fmt, 4, 1, file);
	std::fwrite(&pcm, 2, 1, file);
	std::fwrite(&mono, 2, 1, file);
	std::fwrite(&rate, 4, 1, file);
	std::fwrite(&byteRate, 4, 1, file);
	std::fwrite(&align, 2, 1, file);
	std::fwrite(&bits, 2, 1, file);
	std::fwrite("data", 1, 4, file);
	std::fwrite(&data, 4, 1, file);
	std::fwrite(audio.data(), 1, data, file);
	std::fclose(file);
	return true;
}

int failures = 0;

void Check(const char* what, bool ok) {
	std::printf("  %-56s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok)
		++failures;
}

double Seconds(const std::vector<short>& audio, int rate) {
	return static_cast<double>(audio.size()) / rate;
}

// A fresh voice, for a token or for none.
ISpTTSEngine* MakeVoice(IClassFactory* factory, ISpObjectToken* token) {
	ISpTTSEngine* voice = nullptr;
	if (FAILED(factory->CreateInstance(nullptr, __uuidof(ISpTTSEngine),
	                                   reinterpret_cast<void**>(&voice))))
		return nullptr;
	if (token) {
		ISpObjectWithToken* withToken = nullptr;
		if (SUCCEEDED(voice->QueryInterface(
		        __uuidof(ISpObjectWithToken),
		        reinterpret_cast<void**>(&withToken)))) {
			withToken->SetObjectToken(token);
			withToken->Release();
		}
	}
	return voice;
}

// What a language says as a sample of itself.
const wchar_t* SampleText(int language) {
	switch (language >> 16) {
		case 0x02: return L"Buenos d\x00edas, \x00bfc\x00f3mo est\x00e1s?";
		case 0x03: return L"Bonjour, comment \x00e7a va\x202f?";
		case 0x04: return L"Guten Tag, wie geht es Ihnen?";
		case 0x05: return L"Buongiorno, come stai?";
		case 0x07: return L"Bom dia, como voc\x00ea est\x00e1?";
		case 0x08: return L"\x3053\x3093\x306b\x3061\x306f\x3001\x5143\x6c17\x3067\x3059\x304b\x3002";
		case 0x09: return L"Hyv\x00e4\x00e4 p\x00e4iv\x00e4\x00e4, mit\x00e4 kuuluu?";
		case 0x11: return L"Dzie\x0144 dobry, jak si\x0119 masz?";
		default:   return L"Good morning, how are you?";
	}
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: voice_test <openevv_sapi5.dll> [out.wav]\n");
		return 2;
	}
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	const HMODULE dll = LoadLibraryA(argv[1]);
	if (!dll) {
		std::fprintf(stderr, "could not load %s (%lu)\n", argv[1], GetLastError());
		return 1;
	}
	using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
	auto getClassObject = reinterpret_cast<GetClassObjectFn>(
	    reinterpret_cast<void*>(GetProcAddress(dll, "DllGetClassObject")));
	Check("it exports DllGetClassObject", getClassObject != nullptr);
	if (!getClassObject)
		return 1;
	for (const char* name :
	     {"DllCanUnloadNow", "DllRegisterServer", "DllUnregisterServer"})
		if (!GetProcAddress(dll, name))
			Check(name, false);

	Check("the engine is where the voice looks for it",
	      openevv::Engine::Available());
	std::printf("       %ls, version %s\n",
	            openevv::Engine::LibraryPath().c_str(),
	            openevv::Engine::Version().c_str());

	IClassFactory* factory = nullptr;
	Check("the class factory is there",
	      SUCCEEDED(getClassObject(kVoiceClsid, IID_IClassFactory,
	                               reinterpret_cast<void**>(&factory))) &&
	          factory);
	if (!factory)
		return 1;

	ISpTTSEngine* engine = MakeVoice(factory, nullptr);
	Check("it makes an ISpTTSEngine", engine != nullptr);
	if (!engine)
		return 1;

	GUID formatId = {};
	WAVEFORMATEX* format = nullptr;
	Check("it says what it makes",
	      SUCCEEDED(engine->GetOutputFormat(nullptr, nullptr, &formatId,
	                                        &format)) &&
	          format);
	const int rate = format ? static_cast<int>(format->nSamplesPerSec) : 11025;
	Check("11,025 samples a second, sixteen bits, mono",
	      format && rate == 11025 && format->wBitsPerSample == 16 &&
	          format->nChannels == 1);

	{
		TestSite site;
		SPVTEXTFRAG fragment = Fragment(SPVA_Speak, L"Hello from the SAPI voice.");
		Check("a voice with no token speaks",
		      SUCCEEDED(engine->Speak(0, formatId, format, &fragment, &site)) &&
		          !site.audio.empty());
		std::printf("       %zu samples, %.2f s\n", site.audio.size(),
		            Seconds(site.audio, rate));
		if (argc > 2)
			WriteWave(argv[2], site.audio, rate);
	}

	{
		TestSite slow(-6), fast(6);
		SPVTEXTFRAG a = Fragment(SPVA_Speak, L"The quick brown fox.");
		SPVTEXTFRAG b = a;
		engine->Speak(0, formatId, format, &a, &slow);
		engine->Speak(0, formatId, format, &b, &fast);
		std::printf("       rate -6: %.2f s, rate +6: %.2f s\n",
		            Seconds(slow.audio, rate), Seconds(fast.audio, rate));
		Check("a higher rate is quicker", fast.audio.size() < slow.audio.size());
	}

	{
		TestSite plain, spelled;
		SPVTEXTFRAG a = Fragment(SPVA_Speak, L"cat");
		SPVTEXTFRAG b = Fragment(SPVA_SpellOut, L"cat");
		engine->Speak(0, formatId, format, &a, &plain);
		engine->Speak(0, formatId, format, &b, &spelled);
		Check("spelling takes longer than saying",
		      spelled.audio.size() > plain.audio.size());
	}

	{
		TestSite site;
		SPVTEXTFRAG third = Fragment(SPVA_Speak, L" and after it.");
		SPVTEXTFRAG second = Fragment(SPVA_Bookmark, L"here");
		SPVTEXTFRAG first = Fragment(SPVA_Speak, L"Before the mark");
		first.pNext = &second;
		second.pNext = &third;
		engine->Speak(0, formatId, format, &first, &site);
		Check("the bookmark comes back",
		      site.bookmarks.size() == 1 && site.bookmarks[0] == L"here");
		Check("and in the middle, not at either end",
		      !site.bookmarkOffsets.empty() && site.bookmarkOffsets[0] > 0 &&
		          site.bookmarkOffsets[0] < site.audio.size() * sizeof(short));
	}

	{
		TestSite without, with;
		SPVTEXTFRAG plain = Fragment(SPVA_Speak, L"one two");
		SPVTEXTFRAG second = Fragment(SPVA_Speak, L" two");
		SPVTEXTFRAG pause = Fragment(SPVA_Silence, nullptr);
		pause.State.SilenceMSecs = 500;
		SPVTEXTFRAG first = Fragment(SPVA_Speak, L"one");
		first.pNext = &pause;
		pause.pNext = &second;
		engine->Speak(0, formatId, format, &plain, &without);
		engine->Speak(0, formatId, format, &first, &with);
		Check("silence lengthens it", with.audio.size() > without.audio.size());
	}

	{
		// A backtick is text unless voice tags are on, which they are not by
		// default: `p is not a pause here, so the utterance is no longer.
		TestSite without, with;
		SPVTEXTFRAG plain = Fragment(SPVA_Speak, L"one two");
		SPVTEXTFRAG tagged = Fragment(SPVA_Speak, L"one `p2000 two");
		engine->Speak(0, formatId, format, &plain, &without);
		engine->Speak(0, formatId, format, &tagged, &with);
		Check("a backtick in the text is not a command",
		      with.audio.size() < without.audio.size() + 2 * rate);
	}

	{
		// Abandoned after the first buffer: nothing more is written, and the
		// next utterance starts from a clean engine rather than the rest of
		// this one.
		TestSite whole, cut, after;
		SPVTEXTFRAG a = Fragment(SPVA_Speak,
		    L"This sentence is long enough to need several buffers of audio.");
		SPVTEXTFRAG b = a;
		SPVTEXTFRAG c = Fragment(SPVA_Speak, L"Hello from the SAPI voice.");
		engine->Speak(0, formatId, format, &a, &whole);
		cut.abortAfter = 1;
		engine->Speak(0, formatId, format, &b, &cut);
		engine->Speak(0, formatId, format, &c, &after);
		Check("an abort stops the audio",
		      cut.writes <= 1 && cut.audio.size() < whole.audio.size());
		Check("and the next utterance speaks", !after.audio.empty());
	}

	if (format)
		CoTaskMemFree(format);
	engine->Release();

	// Every language the engine has, each through a token as registration
	// would write it. US English is the baseline: the same text in another
	// language has to come out as something else, or the token was ignored.
	const std::vector<int> languages = openevv::Engine::Languages();
	std::printf("\n  the engine has %zu languages\n", languages.size());
	Check("it has at least one", !languages.empty());
	std::vector<short> english;
	for (int id : languages) {
		const openevv::Language* language = openevv::FindLanguage(id);
		if (!language) {
			std::printf("  0x%x has no locale here and is not offered\n", id);
			continue;
		}
		TestToken token(id, 2);
		ISpTTSEngine* voice = MakeVoice(factory, &token);
		TestSite site;
		SPVTEXTFRAG fragment = Fragment(SPVA_Speak, SampleText(id));
		const bool spoke =
		    voice && SUCCEEDED(voice->Speak(0, SPDFID_WaveFormatEx, nullptr,
		                                    &fragment, &site)) &&
		    Seconds(site.audio, rate) > 0.5;
		char what[96];
		std::snprintf(what, sizeof what, "%ls speaks (%.2f s)", language->name,
		              Seconds(site.audio, rate));
		Check(what, spoke);
		// The first utterance of a fresh instance either way, since an
		// instance's second utterance is not its first.
		if (id == 0x10000)
			english = site.audio;
		if (voice)
			voice->Release();
	}
	if (!english.empty()) {
		TestToken token(0x40000, 2);
		ISpTTSEngine* voice = MakeVoice(factory, &token);
		TestSite site;
		SPVTEXTFRAG fragment = Fragment(SPVA_Speak, SampleText(0x10000));
		if (voice) {
			voice->Speak(0, SPDFID_WaveFormatEx, nullptr, &fragment, &site);
			voice->Release();
		}
		bool hasGerman = false;
		for (int id : languages)
			hasGerman |= id == 0x40000;
		if (hasGerman)
			Check("English text in German is not English",
			      !site.audio.empty() && site.audio != english);
	}

	factory->Release();
	CoUninitialize();

	std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
	return failures ? 1 : 0;
}

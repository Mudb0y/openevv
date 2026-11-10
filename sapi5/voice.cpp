#include "voice.h"

#include <cstdio>
#include <cwchar>
#include <map>

#include "text.h"

// The server counts the objects it has handed out, so it can say when the
// library may be unloaded.
void OpenEvvObjectReleased();

namespace openevv {
namespace {

WAVEFORMATEX MakeFormat() {
	WAVEFORMATEX format = {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = kSampleRate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 2;
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	format.cbSize = 0;
	return format;
}

int Clamp(int value, int low, int high) {
	return value < low ? low : (value > high ? high : value);
}

}  // namespace

// ---- where the audio goes ---------------------------------------------

class Voice::Site : public Engine::Sink {
 public:
	explicit Site(ISpTTSEngineSite* site) : site_(site) {}

	bool OnAudio(const short* samples, int count) override {
		// SAPI says here whether the caller has given up. The engine cannot
		// be made to, so the rest of the utterance is synthesised into nothing.
		if (site_->GetActions() & SPVES_ABORT)
			return false;
		ULONG written = 0;
		if (FAILED(site_->Write(samples,
		                        static_cast<ULONG>(count) * sizeof(short),
		                        &written)))
			return false;
		bytesWritten_ += written;
		return true;
	}

	void OnMark(int mark) override {
		const auto found = bookmarks_.find(mark);
		if (found == bookmarks_.end())
			return;
		// Placed where the listener is rather than where the engine is: the
		// audio in front of the mark has been written, and its length is the
		// offset SAPI wants.
		SPEVENT event = {};
		event.eEventId = SPEI_TTS_BOOKMARK;
		event.elParamType = SPET_LPARAM_IS_STRING;
		event.ullAudioStreamOffset = bytesWritten_;
		event.lParam = reinterpret_cast<LPARAM>(found->second.c_str());
		event.wParam = static_cast<WPARAM>(_wtol(found->second.c_str()));
		site_->AddEvents(&event, 1);
	}

	// Remember what a mark stands for. The string has to outlive the call,
	// because SAPI reads it out of the event after AddEvents returns.
	int AddBookmark(const std::wstring& text) {
		const int mark = nextMark_++;
		bookmarks_[mark] = text;
		return mark;
	}

 private:
	ISpTTSEngineSite* site_ = nullptr;
	int nextMark_ = 1;
	ULONGLONG bytesWritten_ = 0;
	std::map<int, std::wstring> bookmarks_;
};

// ---- the object --------------------------------------------------------

Voice::Voice() : language_(&DefaultLanguage()) {}

Voice::~Voice() {
	if (token_)
		token_->Release();
	OpenEvvObjectReleased();
}

STDMETHODIMP Voice::QueryInterface(REFIID riid, void** object) {
	if (!object)
		return E_POINTER;
	*object = nullptr;
	if (riid == IID_IUnknown || riid == __uuidof(ISpTTSEngine))
		*object = static_cast<ISpTTSEngine*>(this);
	else if (riid == __uuidof(ISpObjectWithToken))
		*object = static_cast<ISpObjectWithToken*>(this);
	else
		return E_NOINTERFACE;
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) Voice::AddRef() {
	return static_cast<ULONG>(InterlockedIncrement(&references_));
}

STDMETHODIMP_(ULONG) Voice::Release() {
	const LONG left = InterlockedDecrement(&references_);
	if (left == 0)
		delete this;
	return static_cast<ULONG>(left);
}

STDMETHODIMP Voice::SetObjectToken(ISpObjectToken* token) {
	if (!token)
		return E_INVALIDARG;
	if (token_)
		token_->Release();
	token_ = token;
	token_->AddRef();

	// Which language and which preset, as registration wrote them. A token
	// that says neither is US English's first voice rather than an error, so
	// a token made by hand still speaks.
	const Language* language = &DefaultLanguage();
	int preset = 1;
	LPWSTR value = nullptr;
	if (SUCCEEDED(token_->GetStringValue(L"OpenEVVLanguage", &value)) && value) {
		if (const Language* found =
		        FindLanguage(static_cast<int>(wcstoul(value, nullptr, 16))))
			language = found;
		CoTaskMemFree(value);
	}
	value = nullptr;
	if (SUCCEEDED(token_->GetStringValue(L"OpenEVVPreset", &value)) && value) {
		const int asked = _wtoi(value);
		if (asked >= 1 && asked <= 8)
			preset = asked;
		CoTaskMemFree(value);
	}

	// A token is normally set once, before anything is said. Should it change
	// after, the instance is made again for the new one.
	if (language != language_ || preset != preset_)
		engine_.Close();
	language_ = language;
	preset_ = preset;
	return S_OK;
}

STDMETHODIMP Voice::GetObjectToken(ISpObjectToken** token) {
	if (!token)
		return E_POINTER;
	*token = token_;
	if (token_)
		token_->AddRef();
	return token_ ? S_OK : S_FALSE;
}

STDMETHODIMP Voice::GetOutputFormat(const GUID*, const WAVEFORMATEX*,
                                    GUID* outputFormatId,
                                    WAVEFORMATEX** outputWaveFormat) {
	if (!outputFormatId || !outputWaveFormat)
		return E_POINTER;
	*outputWaveFormat =
	    static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
	if (!*outputWaveFormat)
		return E_OUTOFMEMORY;
	**outputWaveFormat = MakeFormat();
	*outputFormatId = SPDFID_WaveFormatEx;
	return S_OK;
}

// ---- speaking ----------------------------------------------------------

bool Voice::EnsureEngine() {
	const DWORD here = GetCurrentThreadId();
	if (engine_.IsOpen() && engineThread_ == here)
		return true;
	// Moved to another thread: start again here rather than call across.
	engine_.Close();
	if (!engine_.Open(language_->id))
		return false;
	engineThread_ = here;

	engine_.LoadPreset(preset_);
	presetSpeed_ = engine_.GetVoiceParam(0, eciSpeed);
	presetPitch_ = engine_.GetVoiceParam(0, eciPitchBaseline);
	return true;
}

int Voice::RateToSpeed(long rate) const {
	// Nought is the voice as its preset was designed, which is the least
	// surprising thing for it to mean.
	//
	// The ends are not the ends of what the engine takes, because its scale
	// is far from linear: its own maximum of 250 is some twenty times
	// normal, which is not fast speech but unusable speech. So the top of
	// SAPI's range is put at about four times, which is fast even for people
	// who like Eloquence fast, and the bottom at about half speed. A program
	// asking for +10 gets something that can still be listened to.
	constexpr int kSlowest = 20;
	constexpr int kFastest = 120;
	const int asked = Clamp(static_cast<int>(rate), -10, 10);
	if (asked >= 0)
		return presetSpeed_ + (kFastest - presetSpeed_) * asked / 10;
	return presetSpeed_ + (presetSpeed_ - kSlowest) * asked / 10;
}

int Voice::PitchToBaseline(long pitch) const {
	const int asked = Clamp(static_cast<int>(pitch), -10, 10);
	if (asked >= 0)
		return presetPitch_ + (100 - presetPitch_) * asked / 10;
	return presetPitch_ + presetPitch_ * asked / 10;
}

STDMETHODIMP Voice::Speak(DWORD, REFGUID, const WAVEFORMATEX*,
                          const SPVTEXTFRAG* fragments,
                          ISpTTSEngineSite* site) {
	if (!site)
		return E_INVALIDARG;
	if (!EnsureEngine())
		return E_FAIL;

	const Settings settings = Settings::Load();
	// Nought is the abbreviation dictionary on, one is off.
	engine_.SetParam(eciDictionary, settings.abbreviations ? 0 : 1);
	engine_.SetParam(eciPauseMode, settings.pauseMode);

	Site sink(site);

	// The state the utterance starts in. These are annotations, and an
	// annotation takes effect where it sits, so they carry what was asked
	// for rather than being set through eciSetVoiceParam -- which would apply
	// at once and then be overruled by whatever the text said next.
	USHORT volume = 100;
	site->GetVolume(&volume);
	long rate = 0;
	site->GetRate(&rate);
	engine_.AddText(Preamble(settings, presetPitch_,
	                         Clamp(RateToSpeed(rate), 0, 250),
	                         Clamp(static_cast<int>(volume), 0, 100)));

	for (const SPVTEXTFRAG* fragment = fragments; fragment;
	     fragment = fragment->pNext) {
		if (site->GetActions() & SPVES_ABORT)
			break;
		SpeakFragment(fragment, site, &sink, settings);
	}

	engine_.Speak(&sink);
	return S_OK;
}

void Voice::SpeakFragment(const SPVTEXTFRAG* fragment, ISpTTSEngineSite* site,
                          Site* sink, const Settings& settings) {
	const SPVSTATE& state = fragment->State;

	switch (state.eAction) {
		case SPVA_Speak:
		case SPVA_SpellOut: {
			if (!fragment->pTextStart || fragment->ulTextLen == 0)
				break;

			// SAPI keeps adjustments both on the site and on each fragment,
			// and they compound. Written into the stream in front of the text
			// they apply to, so a document that changes voice part way changes
			// it from that word rather than from the start.
			long rate = 0;
			site->GetRate(&rate);
			USHORT volume = 100;
			site->GetVolume(&volume);
			char prosody[64];
			std::snprintf(
			    prosody, sizeof prosody, "`vb%d `vs%d `vv%d ",
			    Clamp(PitchToBaseline(state.PitchAdj.MiddleAdj), 0, 100),
			    Clamp(RateToSpeed(rate + state.RateAdj), 0, 250),
			    Clamp(static_cast<int>(volume) * static_cast<int>(state.Volume) /
			              100,
			          0, 100));
			engine_.AddText(prosody);

			if (state.eAction == SPVA_SpellOut)
				engine_.AddText("`ts1 ");
			engine_.AddText(ProcessText(fragment->pTextStart,
			                            fragment->ulTextLen,
			                            language_->codePage, settings));
			if (state.eAction == SPVA_SpellOut)
				engine_.AddText(" `ts0 ");
			break;
		}

		case SPVA_Silence: {
			// SAPI counts silence in milliseconds and the engine's pause
			// annotation counts something nearer syllables, worth less the
			// faster the voice goes. Twice the milliseconds is about right at
			// the middle of the range.
			char pause[32];
			std::snprintf(pause, sizeof pause, " `p%d ",
			              Clamp(static_cast<int>(state.SilenceMSecs) * 2, 1, 32000));
			engine_.AddText(pause);
			break;
		}

		case SPVA_Bookmark: {
			// The mark goes into the stream here and is reported when the
			// audio in front of it has been written.
			if (!fragment->pTextStart || fragment->ulTextLen == 0)
				break;
			engine_.InsertMark(sink->AddBookmark(
			    std::wstring(fragment->pTextStart, fragment->ulTextLen)));
			break;
		}

		default:
			// Pronunciation by phoneme, sections and tags SAPI did not know:
			// nothing here acts on them yet.
			break;
	}
}

}  // namespace openevv

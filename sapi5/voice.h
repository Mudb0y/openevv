/* The voice, as SAPI wants it: an object handed a token that says which
 * language and which of that language's eight presets it is, and then asked
 * to speak fragments of text into a site.
 */

#pragma once

#include <windows.h>

#include <string>

#include "engine.h"
#include "languages.h"
#include "sapiddk.h"
#include "settings.h"

namespace openevv {

class Voice : public ISpTTSEngine, public ISpObjectWithToken {
 public:
	Voice();
	virtual ~Voice();

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;

	// ISpTTSEngine
	STDMETHODIMP Speak(DWORD flags, REFGUID formatId,
	                   const WAVEFORMATEX* waveFormat,
	                   const SPVTEXTFRAG* fragments,
	                   ISpTTSEngineSite* site) override;
	STDMETHODIMP GetOutputFormat(const GUID* targetFormatId,
	                             const WAVEFORMATEX* targetWaveFormat,
	                             GUID* outputFormatId,
	                             WAVEFORMATEX** outputWaveFormat) override;

	// ISpObjectWithToken
	STDMETHODIMP SetObjectToken(ISpObjectToken* token) override;
	STDMETHODIMP GetObjectToken(ISpObjectToken** token) override;

 private:
	// Where the samples go and where an abort is noticed.
	class Site;

	// Make sure there is an instance and that it belongs to this thread. SAPI
	// may call Speak on a thread other than the one before, and an instance's
	// callbacks arrive on whichever thread drives it.
	bool EnsureEngine();

	// One fragment's state as annotations, and its text.
	void SpeakFragment(const SPVTEXTFRAG* fragment, ISpTTSEngineSite* site,
	                   Site* sink, const Settings& settings);

	// SAPI's -10 to +10 onto the engine's own numbers, anchored so that nought
	// is the voice as its preset made it.
	int RateToSpeed(long rate) const;
	int PitchToBaseline(long pitch) const;

	LONG references_ = 1;
	ISpObjectToken* token_ = nullptr;
	const Language* language_ = nullptr;
	int preset_ = 1;

	Engine engine_;
	DWORD engineThread_ = 0;
	// What the preset itself asks for, read once it is loaded. Every
	// utterance is anchored to these rather than to what the last one left.
	int presetSpeed_ = 50;
	int presetPitch_ = 65;
};

}  // namespace openevv

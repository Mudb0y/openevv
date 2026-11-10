/* The engine, as the voice needs it.
 *
 * The voice does not link the engine. It loads eci.dll by name, the way any
 * other program written against IBM's interface does, from a folder beside
 * itself named for its word size: eci-x86_64 for the sixty-four bit voice
 * and eci-x86 for the thirty-two bit one, which is how the release lays the
 * two libraries out. Both voices then sit in one folder, and the library is
 * the same file every other caller is checked against rather than a third
 * build of the engine.
 *
 * What is awkward here is written down in docs/using.md and docs/quirks.md,
 * and the voice keeps to it:
 *
 *  - The callback runs on the thread that calls eciSynchronize, so an
 *    instance is driven from one thread and its audio arrives there.
 *
 *  - Nothing makes the engine abandon an utterance. A cancelled one is let
 *    finish with its audio thrown away, which costs the same as any other
 *    way of stopping and is some two hundred times quicker than playing it.
 *
 *  - Annotations are only obeyed with eciInputType at one. Otherwise a
 *    backtick and what follows it are read out as text.
 */

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "eci.h"

namespace openevv {

// The mark appended to every utterance, so the end of it is recognised.
constexpr int kEndOfUtterance = 0xFFFF;

// Samples per callback. A tenth of a second at 11,025, which gets the first
// sound to SAPI soonest without waking it needlessly often.
constexpr int kBufferSamples = 1100;

// What the engine makes. Eloquence is an eleven kilohertz voice, and SAPI
// converts for a caller that wants something else.
constexpr int kSampleRate = 11025;

// One instance of the engine, driven from the thread that made it.
class Engine {
 public:
	// What the engine hands back while synthesising.
	class Sink {
	 public:
		virtual ~Sink() = default;
		// Sixteen-bit mono samples. False to throw away the rest.
		virtual bool OnAudio(const short* samples, int count) = 0;
		// An index mark reached, with the audio in front of it handed over.
		virtual void OnMark(int mark) {}
	};

	Engine();
	~Engine();

	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;

	// Whether the library is there and has every name the voice calls.
	static bool Available();

	// Where it was looked for, for saying so when it is not there.
	static std::wstring LibraryPath();

	// What the engine says its version is, or nothing.
	static std::string Version();

	// The languages the library carries, by engine number.
	static std::vector<int> Languages();

	// Make an instance speaking *language*. False if the library is missing
	// or the language is not one it has.
	bool Open(int language);
	bool IsOpen() const { return handle_ != nullptr; }
	// Let the instance go, so that Open can make another.
	void Close();

	int GetParam(int which) const;
	void SetParam(int which, int value);
	int GetVoiceParam(int voice, int which) const;
	// Copy one of the eight presets into the voice that gets spoken.
	bool LoadPreset(int preset);
	// A preset's name, as the bytes the engine answers with.
	std::string PresetName(int preset) const;

	// Text must already be in the language's code set.
	void AddText(const std::string& text);
	void InsertMark(int mark);
	// Say what has been added, handing every sample to *sink*, and return
	// when the engine has finished with it. False if the sink gave up.
	bool Speak(Sink* sink);

 private:
	static int ECICALL Trampoline(ECIHand handle, ECIMessage message,
	                              int param, void* data);
	int OnMessage(ECIMessage message, int param);

	ECIHand handle_ = nullptr;
	Sink* sink_ = nullptr;
	bool abandoned_ = false;
	std::vector<short> buffer_;
};

}  // namespace openevv

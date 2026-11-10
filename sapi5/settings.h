/* What the voice reads and the dialog writes.
 *
 * They live in HKEY_CURRENT_USER rather than in a token or in HKLM, for two
 * reasons. The dialog can change them without asking for administrator,
 * which matters because these are what somebody adjusts while listening. And
 * they are shared by every voice of every language and by both word sizes,
 * because how the engine is driven is a matter of taste rather than of which
 * voice is speaking.
 *
 * They are the switches the NVDA add-on offers, with its defaults, so the
 * engine sounds the same through either.
 */

#pragma once

#include <windows.h>

namespace openevv {

struct Settings {
	// Whether the engine's abbreviation dictionary expands "Dr." to
	// "doctor". Off is what Eloquence has always done.
	bool abbreviations = false;

	// Which pauses the engine shortens: eciPauseMode, whose nought is none,
	// one -- the default -- the pause at the end of text that does not end in
	// punctuation, and two that and the one at every mark.
	int pauseMode = 1;

	// Whether a backtick in the text starts a command to the engine rather
	// than being read out. Off, so that text which happens to hold one is
	// never taken as an instruction.
	bool voiceTags = false;

	// The engine's phrase prediction.
	bool phrasePrediction = false;

	// Read what is stored, leaving anything unset or out of range at its
	// default.
	static Settings Load();

	// Write them all back. False if the key could not be written.
	bool Save() const;
};

// Where they live, so the dialog and the voice cannot disagree.
extern const wchar_t* const kSettingsKey;

}  // namespace openevv

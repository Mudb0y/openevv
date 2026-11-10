/* Getting SAPI's text to the engine.
 *
 * SAPI hands over UTF-16 and the engine reads bytes in the code set of the
 * language it is speaking, so the text is converted. It is composed first,
 * so that a u followed by a combining diaeresis becomes the one character
 * the code set has. What the code set has not got is left to Windows to
 * best-fit, and what cannot be fitted becomes the question mark IBM's own
 * drivers have always sent.
 *
 * A backtick starts a command to the engine, so unless the person has said
 * otherwise one in the text is turned into a space before it gets there.
 */

#pragma once

#include <string>

#include <windows.h>

#include "settings.h"

namespace openevv {

// Text as the bytes the engine reads in *codePage*.
std::string Encode(const wchar_t* text, size_t length, UINT codePage);

// Bytes in *codePage* as text, for what the engine says about itself.
std::wstring Decode(const std::string& bytes, UINT codePage);

// What a fragment of SAPI's text becomes on its way to the engine.
std::string ProcessText(const wchar_t* text, size_t length, UINT codePage,
                        const Settings& settings);

// The annotations that put the engine into a known state at the start of an
// utterance. An annotation changes the instance and the change stays, so an
// utterance cut off before its own reset would otherwise leave spelling on,
// or the pitch moved, for whatever is said next.
std::string Preamble(const Settings& settings, int pitch, int speed,
                     int volume);

}  // namespace openevv

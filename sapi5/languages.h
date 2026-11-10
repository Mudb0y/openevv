/* What SAPI has to be told about each of the engine's languages.
 *
 * The engine names a language by a number -- the family in the third byte,
 * the dialect in the low one -- and answers eciGetAvailableLanguages with
 * whichever of them the build carries. SAPI wants other things: a locale
 * identifier for the token's Language attribute, which is what a program
 * choosing a voice by language filters on, and a name to show. And the text
 * has to reach the engine in the code set that language reads, which is not
 * one code set for all of them.
 *
 * A language the engine has and this table does not is left unregistered
 * rather than registered without a locale: a SAPI voice with no language is
 * one most programs will not offer at all, and one or two will offer it for
 * everything.
 */

#pragma once

#include <windows.h>

namespace openevv {

struct Language {
	int id;                // what eciNewEx takes
	const wchar_t* tag;    // the locale, which also names the token
	LANGID lcid;           // what SAPI's Language attribute holds
	const wchar_t* name;   // what a voice list shows
	UINT codePage;         // what the text is converted to
};

// The language with this engine number, or nothing.
const Language* FindLanguage(int id);

// US English, which is what a token that does not say speaks.
const Language& DefaultLanguage();

}  // namespace openevv

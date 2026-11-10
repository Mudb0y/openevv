/* The table, which is the same twelve the NVDA add-on knows, with the same
 * code sets. IBM's eight Western languages and the two from ETI's 6.1 read
 * the Windows Western set. Japanese reads Shift-JIS, which is what its
 * romanizer expects. Polish declares letters of its own that the Western set
 * has not got, and is the one language the engine reads as UTF-8.
 */

#include "languages.h"

namespace openevv {
namespace {

const Language kLanguages[] = {
    {0x00010000, L"en-US", 0x0409, L"US English", 1252},
    {0x00010001, L"en-GB", 0x0809, L"British English", 1252},
    {0x00020000, L"es-ES", 0x0c0a, L"Castilian Spanish", 1252},
    {0x00020001, L"es-MX", 0x080a, L"Latin American Spanish", 1252},
    {0x00030000, L"fr-FR", 0x040c, L"French", 1252},
    {0x00030001, L"fr-CA", 0x0c0c, L"Canadian French", 1252},
    {0x00040000, L"de-DE", 0x0407, L"German", 1252},
    {0x00050000, L"it-IT", 0x0410, L"Italian", 1252},
    {0x00070000, L"pt-BR", 0x0416, L"Brazilian Portuguese", 1252},
    {0x00080000, L"ja-JP", 0x0411, L"Japanese", 932},
    {0x00090000, L"fi-FI", 0x040b, L"Finnish", 1252},
    {0x00110000, L"pl-PL", 0x0415, L"Polish", CP_UTF8},
};

}  // namespace

const Language* FindLanguage(int id) {
	for (const Language& language : kLanguages)
		if (language.id == id)
			return &language;
	return nullptr;
}

const Language& DefaultLanguage() {
	return kLanguages[0];
}

}  // namespace openevv

#include "settings.h"

namespace openevv {

const wchar_t* const kSettingsKey = L"Software\\OpenEVV\\SAPI5";

namespace {

// A value that is not there, or is not a number, leaves the default alone,
// so a voice nobody has configured behaves as the defaults say.
bool ReadDword(HKEY key, const wchar_t* name, DWORD* out) {
	DWORD type = 0;
	DWORD value = 0;
	DWORD size = sizeof value;
	if (RegQueryValueExW(key, name, nullptr, &type,
	                     reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS)
		return false;
	if (type != REG_DWORD)
		return false;
	*out = value;
	return true;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
	return RegSetValueExW(key, name, 0, REG_DWORD,
	                      reinterpret_cast<const BYTE*>(&value),
	                      sizeof value) == ERROR_SUCCESS;
}

}  // namespace

Settings Settings::Load() {
	Settings s;
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) !=
	    ERROR_SUCCESS)
		return s;

	DWORD value = 0;
	if (ReadDword(key, L"Abbreviations", &value))
		s.abbreviations = value != 0;
	if (ReadDword(key, L"PauseMode", &value) && value <= 2)
		s.pauseMode = static_cast<int>(value);
	if (ReadDword(key, L"VoiceTags", &value))
		s.voiceTags = value != 0;
	if (ReadDword(key, L"PhrasePrediction", &value))
		s.phrasePrediction = value != 0;

	RegCloseKey(key);
	return s;
}

bool Settings::Save() const {
	HKEY key = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr,
	                    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
	                    nullptr) != ERROR_SUCCESS)
		return false;

	bool ok = true;
	ok &= WriteDword(key, L"Abbreviations", abbreviations ? 1 : 0);
	ok &= WriteDword(key, L"PauseMode", static_cast<DWORD>(pauseMode));
	ok &= WriteDword(key, L"VoiceTags", voiceTags ? 1 : 0);
	ok &= WriteDword(key, L"PhrasePrediction", phrasePrediction ? 1 : 0);

	RegCloseKey(key);
	return ok;
}

}  // namespace openevv

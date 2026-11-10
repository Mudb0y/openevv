#include "dialog.h"

#include <string>

#include "engine.h"
#include "resource.h"
#include "settings.h"

namespace openevv {
namespace {

// The add-on's names for the same three, so somebody using both is not
// asked the same question in two ways.
const wchar_t* const kPauseChoices[] = {
    L"Never",
    L"At end of text only",
    L"Always",
};

void Check(HWND dialog, int control, bool on) {
	CheckDlgButton(dialog, control, on ? BST_CHECKED : BST_UNCHECKED);
}

bool Checked(HWND dialog, int control) {
	return IsDlgButtonChecked(dialog, control) == BST_CHECKED;
}

void FillDialog(HWND dialog, const Settings& settings) {
	Check(dialog, IDC_ABBREVIATIONS, settings.abbreviations);
	Check(dialog, IDC_VOICE_TAGS, settings.voiceTags);
	Check(dialog, IDC_PHRASE_PREDICTION, settings.phrasePrediction);

	HWND pauses = GetDlgItem(dialog, IDC_PAUSES);
	SendMessageW(pauses, CB_RESETCONTENT, 0, 0);
	for (const wchar_t* label : kPauseChoices)
		SendMessageW(pauses, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
	SendMessageW(pauses, CB_SETCURSEL, settings.pauseMode, 0);

	// What is actually loaded, which is the first thing to look at when a
	// voice says nothing.
	std::wstring about;
	if (Engine::Available()) {
		const std::string version = Engine::Version();
		about = L"Engine " + std::wstring(version.begin(), version.end()) +
		        L", " + std::to_wstring(Engine::Languages().size()) +
		        L" languages.";
	} else {
		about = L"No engine at " + Engine::LibraryPath() + L".";
	}
	SetDlgItemTextW(dialog, IDC_ABOUT, about.c_str());
}

Settings ReadDialog(HWND dialog) {
	Settings settings;
	settings.abbreviations = Checked(dialog, IDC_ABBREVIATIONS);
	settings.voiceTags = Checked(dialog, IDC_VOICE_TAGS);
	settings.phrasePrediction = Checked(dialog, IDC_PHRASE_PREDICTION);
	const LRESULT chosen =
	    SendMessageW(GetDlgItem(dialog, IDC_PAUSES), CB_GETCURSEL, 0, 0);
	if (chosen >= 0 && chosen < 3)
		settings.pauseMode = static_cast<int>(chosen);
	return settings;
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wparam,
                            LPARAM) {
	switch (message) {
		case WM_INITDIALOG:
			FillDialog(dialog, Settings::Load());
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wparam)) {
				case IDOK:
					ReadDialog(dialog).Save();
					EndDialog(dialog, IDOK);
					return TRUE;
				case IDCANCEL:
					EndDialog(dialog, IDCANCEL);
					return TRUE;
				default:
					break;
			}
			break;

		case WM_CLOSE:
			EndDialog(dialog, IDCANCEL);
			return TRUE;

		default:
			break;
	}
	return FALSE;
}

}  // namespace

bool ShowSettingsDialog(HINSTANCE instance, HWND parent) {
	return DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent,
	                       DialogProc, 0) == IDOK;
}

}  // namespace openevv

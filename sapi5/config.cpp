/* The configurator, run on its own.
 *
 * It shows the dialog the Settings button in a SAPI host shows, and writes
 * where that does, because there is one set of settings and two ways to
 * reach it. It exists as well as the button because not every host has one,
 * and somebody who wants to change how their voice sounds should not have
 * to go looking through a control panel to do it.
 */

#include <windows.h>

#include <commctrl.h>

#include "dialog.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
	INITCOMMONCONTROLSEX controls = {sizeof(INITCOMMONCONTROLSEX),
	                                 ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&controls);
	openevv::ShowSettingsDialog(instance, nullptr);
	return 0;
}

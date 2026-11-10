/* The settings dialog, opened from either place that opens it. */

#pragma once

#include <windows.h>

namespace openevv {

// Show it, modal to *parent*, reading the settings on the way in and
// writing them on the way out. True if they were accepted.
//
// *instance* is the module the template is in: the voice when a SAPI host
// asks for it, the configurator when that is run on its own.
bool ShowSettingsDialog(HINSTANCE instance, HWND parent);

}  // namespace openevv

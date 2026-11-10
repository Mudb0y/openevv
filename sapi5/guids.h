/* The two identities the voice publishes. They were made once and must not
 * change: every registered token names the first, and changing either would
 * leave each installed voice pointing at nothing.
 */

#pragma once

#include <windows.h>

// The engine -- what SAPI makes when something asks to speak with one of
// these voices.
// {7CAE4ACA-3E08-4B9B-A38E-D7911D77485E}
extern "C" const CLSID CLSID_OpenEvvVoice;

// The settings dialog, behind the Settings button the Windows speech control
// panel and some other hosts show for a voice.
// {B2AEEE40-798C-463F-80D0-52F879DF5D2D}
extern "C" const CLSID CLSID_OpenEvvTokenUI;

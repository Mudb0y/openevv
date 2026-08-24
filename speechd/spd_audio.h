#ifndef OPENEVV_SPD_AUDIO_H
#define OPENEVV_SPD_AUDIO_H

/* Speech Dispatcher installs spd_module_main.h for out-of-tree modules, but
 * that header includes spd_audio.h while only installing the public audio
 * plugin types it needs. Keep this compatibility header deliberately small. */
#include <speech-dispatcher/spd_audio_plugin.h>

#endif

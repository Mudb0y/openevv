#ifndef OPENEVV_SPD_AUDIO_H
#define OPENEVV_SPD_AUDIO_H

/* The audio types the module protocol beside this file sends samples back
 * to Speech Dispatcher in, under the name spd_module_main.h and
 * module_process.c include them by. Ours, for the same reason as
 * speechd_types.h. The format's value goes over the wire as big_endian=, so
 * little-endian has to be nought and big-endian one. */

typedef enum {
    SPD_AUDIO_LE = 0,
    SPD_AUDIO_BE = 1
} AudioFormat;

typedef struct {
    int bits;
    int num_channels;
    int sample_rate;
    int num_samples;
    signed short *samples;
} AudioTrack;

#endif

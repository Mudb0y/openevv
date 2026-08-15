/* The static initialisers Microsoft's runtime used to run on the way in.

   Its compiler left a pointer to each in a .CRT$XCU section and its linker
   walked them at startup. Nothing here walks that, and the sections cannot
   simply be walked either: every one of the six initialisers is a local
   symbol called $E1 living in a COMDAT called .text$yc, so the GNU linker
   sees five copies of the same thing and keeps one.

   They are all trivial, though, and every one of them is written out below.
   Without them no output format registers itself and the mutexes guarding
   the filter list, the SSML lexer, the synthesis thread and the engine's own
   first-time flag are never constructed, so the first worker thread reaches
   a null format and dies.

   The Microsoft names for two constructors and three objects cannot be
   spelled as C identifiers. They are declared here under names that can be,
   and the link is told to answer the real names with these; see the
   Makefile. Both constructors take their object in ecx the way that compiler
   passed it. */

extern void __attribute__((thiscall)) evv_mutex_ctor(void *self, int recursive);
extern void __attribute__((thiscall)) evv_isv_ctor(void *self);

extern char evv_filter_load_mutex[];
extern char evv_ssml_lexer_mutex[];
extern char evv_synth_init_mutex[];
extern char evv_protect_first_time[];
extern char evv_standard_voices[];

int initializeSoundFormats(void);

void evvRunStaticInitialisers(void)
{
    initializeSoundFormats();
    evv_mutex_ctor(evv_filter_load_mutex, 0);
    evv_mutex_ctor(evv_ssml_lexer_mutex, 0);
    evv_mutex_ctor(evv_synth_init_mutex, 0);
    evv_mutex_ctor(evv_protect_first_time, 0);
    evv_isv_ctor(evv_standard_voices);
}

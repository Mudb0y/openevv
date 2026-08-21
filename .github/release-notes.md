`eci.dll` exports the names IBM published, so a program written against IBM's
library -- a screen reader add-on, for instance -- can load ours instead. It is
sixty-four bit, wants nothing but the system's own DLLs, and `eci.ini` goes
beside it because add-ons look for one.

`evvspeak.exe` is the speak window: type something, pick one of the eight
voices, set the rate in words a minute, and hear it. `evv.exe` is the same
engine on the command line. Both are one file, sixty-four bit, and want nothing
installed.

The audio is byte for byte what IBM's own binary produces, over all 81 test
cases, from this build.

NOTICE says which parts of this are ours and which are IBM's: the engine is a
reimplementation, and the language data it speaks with is IBM's, which the MIT
licence does not cover.

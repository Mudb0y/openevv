#!/usr/bin/env python3
"""Drive the engine layer against the real library, on Windows, without NVDA.

This is the check that neither of the others can be: sequence.py never enters
the engine, and engine.py enters it with the library stood in for. Neither can
see anything that only goes wrong when ctypes is really talking to eci.dll --
and both of the faults the add-on has shipped were exactly that. A name error
in _start, and then a shutdown that raised while switching synthesiser away.

It needs a Python on the machine and the built library beside the driver, so it
does not run from the build host. It is meant to be copied to a Windows machine
along with the add-on's own directory and run there:

    python windows.py [path to the add-on's directory]

with no argument meaning the installed one under the roaming profile. NVDA is
stood in for; the wave player writes the samples down instead of playing them,
so the audio can be held to a hash without a sound card.
"""

import hashlib
import os
import struct
import sys
import types

DEFAULT_ADDON = os.path.join(
    os.environ.get("APPDATA", ""), "nvda", "addons", "openevv"
)

#: What the fixed sentence has always come to. The same number the other
#: harnesses in test/ hold the engine to.
KNOWN_SENTENCE = "The quick brown fox jumps over the lazy dog."
KNOWN_SAMPLES = 38423

FAILED = []


def check(what, got, want):
    if got == want:
        print("ok   %s" % what)
    else:
        print("FAIL %s\n     got  %r\n     want %r" % (what, got, want))
        FAILED.append(what)


def note(what):
    print("     %s" % what)


# ---- NVDA, stood in for ----------------------------------------------------


class _Log:
    def debug(self, *a, **k):
        pass

    def debugWarning(self, *a, **k):
        print("     driver warned: %s" % (a[0] if a else ""))

    def info(self, *a, **k):
        pass

    def warning(self, *a, **k):
        print("     driver warned: %s" % (a[0] if a else ""))

    def error(self, *a, **k):
        print("     driver logged an error: %s" % (a[0] if a else ""))
        if k.get("exc_info"):
            import traceback

            traceback.print_exc()


class WavePlayer:
    """Writes the samples down rather than playing them."""

    def __init__(self, **kwargs):
        self.made = kwargs
        self.chunks = []
        self.marks = []
        self.stopped = 0
        self.idled = 0
        self.closed = 0
        self.paused = []

    def feed(self, data, size=None, onDone=None):
        self.chunks.append(bytes(data))
        if onDone is not None:
            onDone()

    def idle(self):
        self.idled += 1

    def sync(self):
        pass

    def stop(self):
        self.stopped += 1

    def close(self):
        self.closed += 1

    def pause(self, switch):
        self.paused.append(switch)

    def samples(self):
        return sum(len(c) for c in self.chunks) // 2

    def digest(self):
        h = hashlib.sha256()
        for c in self.chunks:
            h.update(c)
        return h.hexdigest()


def install(addon):
    for name, contents in (
        ("config", {"conf": {"audio": {"outputDevice": "default"}}}),
        ("nvwave", {"WavePlayer": WavePlayer}),
        ("logHandler", {"log": _Log()}),
    ):
        m = types.ModuleType(name)
        for k, v in contents.items():
            setattr(m, k, v)
        sys.modules[name] = m
    sys.path.insert(0, addon)


def main():
    addon = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_ADDON
    if not os.path.isdir(os.path.join(addon, "synthDrivers")):
        raise SystemExit("windows.py: no synthDrivers under %s" % addon)
    print("driving the add-on at %s" % addon)

    install(addon)
    import synthDrivers._openevv as mod

    marks = []
    engine = mod.Engine(marks.append)
    engine.open()
    player = engine.player
    note("engine version %s, library %s" % (engine.version, mod.libraryName()))

    check("all eight voices have names", len(engine.voiceNames), 8)
    check("the voice's settings were read", len(engine.voiceParams), 8)

    # ---- one whole utterance, held to the number every other harness uses --

    engine.addText(KNOWN_SENTENCE.encode("utf-8"))
    engine.synthesize()
    check("the fixed sentence comes to what it always has",
          player.samples(), KNOWN_SAMPLES)
    check("and it said it had finished", marks, [None])
    note("sha256 of the samples: %s" % player.digest())

    # ---- an index mark lands where it should ------------------------------

    player.chunks = []
    del marks[:]
    engine.addText(b"One two three.")
    engine.index(77)
    engine.addText(b"Four five six.")
    engine.synthesize()
    check("a mark in the middle is reported, then the end", marks, [77, None])
    first = sum(len(c) for c in player.chunks[:1]) // 2
    note("%d samples in all, first chunk %d" % (player.samples(), first))

    # ---- interrupting, over and over, on the one instance ------------------

    long_text = (b"This is a long sentence which will be interrupted well "
                 b"before it has finished being spoken aloud.")
    totals = set()
    for _ in range(10):
        player.chunks = []
        del marks[:]
        engine.addText(long_text)
        engine._discarding = True      # what cancel() sets, without the queue
        engine.synthesize()
        engine._resume()

        player.chunks = []
        del marks[:]
        engine.addText(KNOWN_SENTENCE.encode("utf-8"))
        engine.synthesize()
        totals.add(player.samples())

    check("ten interruptions leave the next utterance exactly as it should be",
          totals, {KNOWN_SAMPLES})

    # ---- and shutting down, which is what switching synthesiser away does --

    try:
        engine.close()
        print("ok   closing down does not raise")
    except Exception as e:  # noqa: BLE001
        import traceback

        print("FAIL closing down raised %s: %s" % (type(e).__name__, e))
        traceback.print_exc()
        FAILED.append("close")

    check("the engine's thread has stopped", engine.alive(), False)
    check("and the player was closed", player.closed, 1)

    if FAILED:
        print("\nwindows: %d of the checks failed" % len(FAILED))
        return 1
    print("\nwindows: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

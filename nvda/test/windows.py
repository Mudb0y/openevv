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
    note("languages: %s"
         % ", ".join("0x%x (%s)" % (l, mod.nameOf(l))
                     for l in engine.languages))

    # ---- one whole utterance, held to the number every other harness uses --

    engine.addText(KNOWN_SENTENCE.encode("utf-8"))
    engine.synthesize()
    check("the fixed sentence comes to what it always has",
          player.samples(), KNOWN_SAMPLES)
    check("and it said it had finished", marks, [None])
    known = player.digest()
    note("sha256 of the samples: %s" % known)

    # ---- every language the library has, and none of them moved ----------
    #
    # A library may have more than one language in it, and a language change
    # is an engine change underneath: what has to be true is that speaking a
    # language after switching to it gives what that language gives, however
    # many others have been spoken in between. Anything less means a change
    # left something of the language before it behind, which would be heard
    # as an accent rather than as a fault.
    #
    # Lengths rather than hashes, as everywhere else here. The engine does
    # not say a sentence in the same samples twice running on one instance --
    # the same 38,423 samples come out under three different hashes -- so the
    # length is what can be held to across utterances.
    if len(engine.languages) > 1:
        passes = []
        for _ in range(2):
            said = {}
            for language in engine.languages:
                engine.setLanguage(language)
                if not passes:
                    check("speaking %s reads its own presets"
                          % mod.nameOf(language), len(engine.voiceNames), 8)
                player.chunks = []
                del marks[:]
                engine.addText(KNOWN_SENTENCE.encode("utf-8"))
                engine.synthesize()
                said[language] = player.samples()
            passes.append(said)

        note("what each language made of the fixed sentence: %s"
             % ", ".join("%s %d samples" % (mod.nameOf(l), passes[0][l])
                         for l in engine.languages))
        check("each language says the sentence at its own length",
              len(set(passes[0].values())), len(engine.languages))
        check("the first of them says it in what it took on its own",
              passes[0][engine.languages[0]], KNOWN_SAMPLES)
        check("and going round them all again gives every one back",
              passes[1], passes[0])
        engine.setLanguage(engine.languages[0])

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

    # ---- rapid fire, which is what arrowing through a list is --------------
    #
    # Both of the faults this checks were reported from use and neither was
    # visible to anything here before. Arrowing was slow because every
    # cancelled utterance was still synthesised in full, and text the engine
    # has been given cannot be taken back -- eciClearInput only empties the
    # manual queue, which this mode never fills -- so the decision has to be
    # made before an utterance is submitted at all.

    ROW = (b"2026-08-21  22:24    1,924,675  openevv-0.12.nvda-addon"
           b"  and a rather long trailing description of the row")

    def batch(*texts, words=True):
        """One utterance, as the driver builds one. `words` is what the driver
        passes: whether there was anything in it meant to make a sound."""
        return ([(engine.addText, (t,)) for t in texts]
                + [(engine.synthesize, (words,))])

    player.chunks = []
    engine.post(batch(ROW))
    engine._work.join()
    one = player.samples()

    # Cancel drops what is queued, so twenty presses in a row only ever run the
    # last -- that was true before any of this and is not what the check is
    # for. What is checked is the case cancel cannot catch: a batch already
    # taken off the queue when silence is asked for. It must not be submitted,
    # because text the engine has been given cannot be taken back.
    engine._discarding = True
    player.chunks = []
    engine.post(batch(ROW))
    engine._work.join()
    check("an utterance cancelled before it was submitted is not synthesised",
          player.samples(), 0)

    # And nothing of it is left behind to come out with the next one, which is
    # what submitting it and then abandoning it would do.
    engine._discarding = False
    player.chunks = []
    engine.post(batch(b"abc"))
    engine._work.join()
    small = player.samples()
    check("and leaves nothing behind for the next one to speak",
          small < one / 4, True)
    note("the row is %d samples, the one after the dropped one %d" % (one, small))

    # ---- and spelling does not leak out of one utterance into the next -----
    #
    # An annotation on the end of a stretch of text is not acted on -- it is
    # spoken, faithfully, as IBM's own build does -- so the closing `ts0 has to
    # go in a call of its own. Relying on the trailing form left the engine
    # spelling everything out afterwards.

    engine.cancel()
    engine._work.join()

    plain = None
    for what, texts in (
        ("plain", (b"abc",)),
        ("spelled", (b"`ts1 ", b"abc", b"`ts0 ")),
        ("plain again", (b"abc",)),
    ):
        player.chunks = []
        engine.post(batch(*texts))
        engine._work.join()
        got = player.samples()
        if what == "plain":
            plain = got
            spelledLonger = None
        elif what == "spelled":
            spelledLonger = got > plain
            check("spelling out makes more audio than not", spelledLonger, True)
        else:
            check("and the utterance after it is plain again", got, plain)

    # The engine fact the driver is built on, checked outright: an annotation on
    # the end of a stretch of text is not acted on. It is spoken instead --
    # faithfully, as IBM's own build does -- which is why the closing `ts0 goes
    # in a call of its own. If this ever stops being true the driver could be
    # simpler; while it is true, relying on the trailing form leaves the engine
    # spelling everything out afterwards.
    player.chunks = []
    engine.post(batch(b"`ts1 abc`ts0 "))
    engine._work.join()
    trailing = player.samples()
    player.chunks = []
    engine.post(batch(b"abc"))
    engine._work.join()
    check("a trailing annotation is not acted on, so spelling would leak",
          player.samples() != plain, True)
    note("the trailing form gave %d, and the utterance after it %d rather than"
         " %d" % (trailing, player.samples(), plain))

    # Put it back to plain before going on. Nothing but an annotation, so it
    # makes no sound and says so, which is what stops it being complained about.
    engine.post(batch(b"`ts0 ", words=False))
    engine._work.join()

    # The same with the spelled one cancelled part way, and with rapid fire
    # mixing the two, which is how it was found.
    engine.cancel()
    engine.post(batch(b"`ts1 ", b"abc", b"`ts0 "))
    engine.cancel()
    for i in range(10):
        engine.cancel()
        engine.post(batch(b"`ts1 ", b"x", b"`ts0 "))
        engine.cancel()
        engine.post(batch(b"item %d" % i))
    engine.cancel()
    engine._work.join()
    player.chunks = []
    engine.post(batch(b"abc"))
    engine._work.join()
    check("nor after ten cancelled ones with spelling mixed in",
          player.samples(), plain)

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

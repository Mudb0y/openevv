#!/usr/bin/env python3
"""Run the engine layer with the library and the player stood in for.

The check beside this one, sequence.py, replaces the engine wholesale and so
never enters it. That left the whole of Engine._start and the engine's callback
unrun by anything on this machine, and the first version of the add-on shipped
with a plain name error in _start: a function had been renamed and one call site
had not. NVDA found it and nothing here did. This is the answer to that.

A fake library answers the seventeen calls the layer makes, and a fake player
writes down every chunk it is fed and every callback hung off one. So this does
enter _start, does build the ctypes prototypes, and does drive the callback --
which means it also checks the thing that could otherwise only be argued for:
that an index mark is reported against the audio in front of it rather than the
audio after it.

What it still cannot check is the crossing into a real library, the calling
convention, or the sound. Those need Windows.

usage: engine.py
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON = os.path.join(os.path.dirname(HERE), "addon")

sys.path.insert(0, HERE)

import sequence  # noqa: E402  the NVDA stand-ins live there

FAILED = []


def check(what, got, want):
    if got == want:
        print("ok   %s" % what)
    else:
        print("FAIL %s\n     got  %r\n     want %r" % (what, got, want))
        FAILED.append(what)


def complain(what):
    print("FAIL %s" % what)
    FAILED.append(what)


# ---- a library that answers ------------------------------------------------

VOICE_NAMES = {
    1: b"Adult Male 1",
    2: b"Adult Female 1",
    3: b"Child 1",
    4: b"Adult Male 2",
    5: b"Adult Male 3",
    6: b"Adult Female 2",
    7: b"Elderly Female 1",
    8: b"Elderly Male 1",
}

#: What a fresh instance's voice is set to, as the engine really answers it.
VOICE_PARAMS = {0: 0, 1: 50, 2: 65, 3: 30, 4: 0, 5: 0, 6: 50, 7: 92}

HANDLE = 0x1234567890AB


class _Entry:
    """One entry point: takes the prototype the layer assigns, and answers."""

    def __init__(self, dll, name):
        self._dll = dll
        self._name = name
        self.restype = None
        self.argtypes = None

    def __call__(self, *args):
        self._dll.calls.append((self._name,) + args)
        return self._dll.answer(self._name, args)


class FakeDll:
    def __init__(self):
        self.calls = []
        self.entries = {}
        self.callback = None
        self.buffer = None
        self.params = {}
        self.voiceParams = dict(VOICE_PARAMS)

    def __getattr__(self, name):
        if not name.startswith("eci"):
            raise AttributeError(name)
        entry = self.entries.get(name)
        if entry is None:
            entry = self.entries[name] = _Entry(self, name)
        return entry

    def named(self, name):
        return [c for c in self.calls if c[0] == name]

    def answer(self, name, args):
        if name == "eciGetAvailableLanguages":
            out, count = args
            # byref() hands over a wrapper; the object behind it is what the
            # caller passed and is what has to be written.
            holder = getattr(count, "_obj", count)
            if out is None:
                holder.value = 1
            else:
                out[0] = 0x00010000
            return 0
        if name == "eciNewEx":
            return HANDLE
        if name == "eciSetOutputBuffer":
            self.buffer = args[2]
            return 1
        if name == "eciRegisterCallback":
            self.callback = args[1]
            return None
        if name == "eciVersion":
            args[0].value = b"7.0.0.0"
            return None
        if name == "eciGetVoiceName":
            args[2].value = VOICE_NAMES[args[1]]
            return 1
        if name == "eciGetVoiceParam":
            return self.voiceParams[args[2]]
        if name == "eciSetVoiceParam":
            was = self.voiceParams[args[2]]
            self.voiceParams[args[2]] = args[3]
            return was
        if name == "eciSetParam":
            was = self.params.get(args[1], 0)
            self.params[args[1]] = args[2]
            return was
        if name == "eciDelete":
            return 0
        return 1


# ---- a player that writes down what it played ------------------------------


class FakePlayer:
    def __init__(self, **kwargs):
        self.made = kwargs
        self.fed = []
        self.idled = 0
        self.stopped = 0
        self.closed = 0
        self.paused = []

    def feed(self, data, size=None, onDone=None):
        self.fed.append((bytes(data), onDone))
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


# ---- putting the layer on top of both --------------------------------------


def engine_module(dll, player_box, library_there=True):
    import ctypes

    sequence._install_stubs()
    if ADDON not in sys.path:
        sys.path.insert(0, ADDON)
    import nvwave

    import synthDrivers._openevv as mod

    def WinDLL(path):
        dll.loaded = path
        return dll

    def WavePlayer(**kwargs):
        player_box.append(FakePlayer(**kwargs))
        return player_box[-1]

    ctypes.WinDLL = WinDLL
    nvwave.WavePlayer = WavePlayer
    mod.nvwave = nvwave
    mod.ctypes.WinDLL = WinDLL
    mod.os.path.isfile = (lambda p: library_there)
    return mod


def main():
    dll = FakeDll()
    players = []
    mod = engine_module(dll, players)

    marks = []
    engine = mod.Engine(marks.append)
    engine.open()

    # Nothing below here is reached at all if _start raised, which is exactly
    # what shipped: a name error in it.
    check("the library it loads is the one for this word size",
          os.path.basename(dll.loaded), mod.libraryName())
    check("it asks the library how many languages it has, twice",
          len(dll.named("eciGetAvailableLanguages")), 2)
    check("it makes an instance", len(dll.named("eciNewEx")), 1)
    check("it registers a callback", len(dll.named("eciRegisterCallback")), 1)
    check("it hands over a sample buffer of the size it says",
          dll.named("eciSetOutputBuffer")[0][2], mod.FRAME)
    check("it turns annotations on and nothing else",
          dll.params, {mod.PARAM_INPUT_TYPE: 1})
    check("it reads the version out", engine.version, "7.0.0.0")
    check("it reads all eight voice names from the engine",
          [engine.voiceNames[n] for n in range(1, 9)],
          [VOICE_NAMES[n].decode() for n in range(1, 9)])
    check("it reads the voice's settings so the driver need not ask across threads",
          engine.voiceParams, VOICE_PARAMS)
    check("the player is made at the engine's own rate, one channel, sixteen bits",
          (players[0].made["samplesPerSec"], players[0].made["channels"],
           players[0].made["bitsPerSample"]),
          (mod.SAMPLE_RATE, 1, 16))

    if dll.callback is None:
        complain("no callback was registered, so nothing below can be checked")
        return 1

    # ---- the callback, which is where the marks are decided --------------

    player = players[0]

    def waveform(samples):
        # The library fills the buffer it was given and says how much.
        for i in range(samples):
            dll.buffer[i] = (i % 100) - 50
        return dll.callback(HANDLE, mod.MSG_WAVEFORM, samples, None)

    def index(n):
        return dll.callback(HANDLE, mod.MSG_INDEX, n, None)

    engine.synthesize()
    check("synthesising puts an end mark in after the caller's text",
          dll.named("eciInsertIndex")[-1][2], mod.END_MARK)

    player.fed = []
    marks.clear()

    # Less than the feed threshold, so nothing should go yet.
    answer = waveform(100)
    check("a short buffer is held rather than dribbled to the player",
          (len(player.fed), answer), (0, mod.DATA_PROCESSED))

    # A mark now: what is held is the audio in front of it, so it goes with
    # the mark hung off the end of it.
    index(42)
    check("a mark sends the audio in front of it, and only that",
          [len(a) for a, _ in player.fed], [200])
    check("and the mark is reported against that audio", marks, [42])

    # Enough to cross the threshold on its own.
    marks.clear()
    player.fed = []
    waveform(mod.FRAME)
    check("a buffer past the threshold goes without waiting for a mark",
          [len(a) for a, _ in player.fed], [mod.FRAME * 2])
    check("with no mark attached to it", marks, [])

    # Two marks with nothing between them: the second has no audio in front of
    # it, so it has to be reported anyway rather than waiting for ever.
    marks.clear()
    player.fed = []
    waveform(50)
    index(1)
    index(2)
    check("two marks in a row both report", marks, [1, 2])
    check("and only the audio that existed was sent",
          [len(a) for a, _ in player.fed], [100])

    # The end mark finishes the utterance: whatever is left goes, the player is
    # waited for, and done is said last.
    marks.clear()
    player.fed = []
    was = player.idled
    waveform(70)
    index(mod.END_MARK)
    check("the end mark sends what is left", [len(a) for a, _ in player.fed], [140])
    check("waits for the audio to finish", player.idled - was, 1)
    check("and says it is done, by reporting nothing", marks, [None])

    # An utterance ending exactly on a mark leaves nothing in hand, and must
    # still wait before saying it is done.
    marks.clear()
    player.fed = []
    was = player.idled
    waveform(30)
    index(9)
    index(mod.END_MARK)
    check("an utterance ending on a mark still waits before saying done",
          player.idled - was, 1)
    check("and reports the mark before the done", marks, [9, None])

    # Cancelling: the callback gives up at its next crossing.
    engine.cancel()
    check("cancelling stops the player", player.stopped, 1)
    check("and the callback then refuses to take more",
          waveform(100), mod.DATA_ABORT)

    if FAILED:
        print("\nengine: %d of the checks failed" % len(FAILED))
        return 1
    print("\nengine: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

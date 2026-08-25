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
import time

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
        if name == "eciSynchronize":
            hook = getattr(self, "answer_synchronize", None)
            return hook(*args) if hook else 1
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


def control_checks():
    """A setting is not speech and is not thrown away with it."""
    dll = FakeDll()
    players = []
    mod = engine_module(dll, players)

    engine = mod.Engine(lambda index: None)
    engine.open()

    engine.post([(engine.addText, (b"spoken",))])
    engine.control([(engine.setParam, (mod.PARAM_DICTIONARY, 0))])
    engine.post([(engine.addText, (b"also spoken",))])
    engine._drain()

    left = []
    while True:
        try:
            left.append(engine._work.get_nowait())
        except Exception:  # noqa: BLE001
            break
    check("draining for silence drops the utterances",
          [k for k, _ in left if k == mod.SPEECH], [])
    check("and keeps the settings",
          len([k for k, _ in left if k == mod.CONTROL]), 1)
    engine.close()


def stall_checks():
    """A player that stops taking audio must not silence the synthesiser.

    Blocking in the player is the design: a full one is what keeps the engine
    from running ahead of the speech. But there is one thread, the player has
    no timeout, and a player that has stopped draining is indistinguishable
    from a full one -- so it holds that thread, and every utterance behind it,
    with nothing said and nothing written down.
    """
    import threading
    import time

    class DeadPlayer:
        """Drains until told not to, and then never again."""

        def __init__(self, **kwargs):
            self.made = kwargs
            self.lock = threading.Condition()
            self.queued = []
            self.dead = False
            self.stopped = 0
            self.closed = 0
            self.paused = []
            threading.Thread(target=self._drain, daemon=True).start()

        def _drain(self):
            while True:
                with self.lock:
                    if self.queued and not self.dead:
                        _, onDone = self.queued.pop(0)
                        self.lock.notify_all()
                    else:
                        onDone = None
                if onDone:
                    onDone()
                time.sleep(0.002)

        def feed(self, data, size=None, onDone=None):
            with self.lock:
                while len(self.queued) >= 3:
                    self.lock.wait(timeout=30)
                self.queued.append((bytes(data), onDone))

        def idle(self):
            with self.lock:
                while self.queued:
                    self.lock.wait(timeout=30)

        def stop(self):
            with self.lock:
                self.queued = []
                self.dead = False
                self.stopped += 1
                self.lock.notify_all()

        def pause(self, switch):
            self.paused.append(switch)

        def close(self):
            self.closed += 1

        def sync(self):
            pass

    dll = FakeDll()
    players = []
    mod = engine_module(dll, players)
    import nvwave

    nvwave.WavePlayer = lambda **kw: players.append(DeadPlayer(**kw)) or players[-1]
    mod.nvwave = nvwave
    # The same logic on a shorter fuse, so the check is quick.
    mod.STUCK_AFTER = 1.0
    mod.WATCH_EVERY = 0.2

    def synchronize(handle):
        for _ in range(30):
            dll.callback(handle, mod.MSG_WAVEFORM, 1024, None)
        return 1

    dll.answer_synchronize = synchronize

    done = []
    engine = mod.Engine(lambda index: done.append(index))
    engine.open()
    player = players[0]

    def utterance():
        return [(engine.addText, (b"x",)), (engine.synthesize, (True,))]

    engine.post(utterance())
    time.sleep(0.8)
    check("speech reaches the player to begin with", done.count(None), 1)

    del done[:]
    with player.lock:
        player.dead = True
    for _ in range(4):
        engine.post(utterance())
    time.sleep(0.8)
    check("a player that stops draining holds the thread and the queue",
          (done.count(None), engine._work.qsize() > 0), (0, True))

    time.sleep(2.5)
    check("the stall is broken rather than waited out", player.stopped >= 1, True)
    check("and whatever waited on the utterance is told it finished",
          done.count(None) >= 1, True)
    check("and the utterances behind it are not left queued",
          engine._work.qsize(), 0)

    del done[:]
    engine.post(utterance())
    time.sleep(1.2)
    check("speech works again without the reader having to interrupt",
          done.count(None), 1)

    # A pause is meant to stop the player draining, so it is not a stall and
    # must not be broken: doing so would resume speech the reader paused.
    del done[:]
    engine.pause(True)
    with player.lock:
        player.dead = True
    before = player.stopped
    engine.post(utterance())
    time.sleep(2.5)
    check("but a pause the reader asked for is left alone",
          (player.stopped, done.count(None)), (before, 0))
    engine.pause(False)
    engine.cancel()


def idle_stall_checks():
    """One utterance is reported finished once, whichever thread gets there.

    A stall in the last player.idle() is not the same as one in feed(). The
    watchdog abandons the utterance and reports it finished, and the engine's
    thread then comes out of the player anyway -- past the point where it
    checks whether it was cancelled -- and reports it finished as well. Two
    reports of one utterance leave NVDA's speech manager a step ahead of
    itself: it takes the second for the end of the utterance after this one.
    """
    import threading
    import time

    class IdleStaller:
        """Takes everything it is fed and never runs dry until it is stopped."""

        def __init__(self, **kwargs):
            self.made = kwargs
            self.gate = threading.Event()
            self.stopped = 0
            self.closed = 0
            self.fed = 0

        def feed(self, data, size=None, onDone=None):
            self.fed += 1
            if onDone:
                onDone()

        def idle(self):
            self.gate.wait(timeout=30)
            self.gate.clear()

        def stop(self):
            self.stopped += 1
            self.gate.set()

        def pause(self, switch):
            pass

        def close(self):
            self.closed += 1

        def sync(self):
            pass

    dll = FakeDll()
    players = []
    mod = engine_module(dll, players)
    import nvwave

    nvwave.WavePlayer = lambda **kw: players.append(IdleStaller(**kw)) or players[-1]
    mod.nvwave = nvwave
    # The same logic on a shorter fuse, so the check is quick.
    mod.STUCK_AFTER = 1.0
    mod.WATCH_EVERY = 0.2

    def synchronize(handle):
        for _ in range(4):
            dll.callback(handle, mod.MSG_WAVEFORM, 1024, None)
        return 1

    dll.answer_synchronize = synchronize

    done = []
    engine = mod.Engine(lambda index: done.append(index))
    engine.open()

    engine.post([(engine.addText, (b"one sentence",)),
                 (engine.synthesize, (True,))])
    time.sleep(2.5)
    check("a stall in the last idle is broken as well", players[0].stopped >= 1,
          True)
    check("and one utterance is reported finished once, not twice",
          done.count(None), 1)
    engine.close()


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

    # eciSynchronize is what drives the utterance, so a real one would produce
    # every buffer before it returned. Here it is the hook the buffers are fed
    # through, so that the callback runs while synthesize is inside it, exactly
    # as it does against the real library.
    pending = []

    def synchronize(*args):
        for fn in pending:
            fn()
        del pending[:]
        return 1

    dll.answer_synchronize = synchronize

    engine.synthesize()
    check("synthesising starts the utterance and then waits for it",
          [c[0] for c in dll.calls if c[0] in ("eciSynthesize", "eciSynchronize")],
          ["eciSynthesize", "eciSynchronize"])
    check("and says it is done once, having had nothing to play", marks, [None])
    check("no mark of its own is inserted; the end is a fact, not a mark",
          dll.named("eciInsertIndex"), [])

    # ---- a whole utterance, driven the way the real library drives one ----

    def utterance(steps):
        """Run one synthesize whose buffers and marks are `steps`."""
        player.fed = []
        marks.clear()
        was = player.idled
        pending[:] = steps
        engine.synthesize()
        return player.idled - was

    idled = utterance([lambda: waveform(100), lambda: index(42),
                       lambda: waveform(70)])
    check("a short buffer is held until there is a reason to send it",
          [len(a) for a, _ in player.fed], [200, 140])
    check("a mark goes with the audio in front of it, and it is reported",
          marks, [42, None])
    check("the utterance waits for its audio before saying it is done", idled, 1)

    idled = utterance([lambda: waveform(mod.FRAME), lambda: waveform(10)])
    check("a buffer past the threshold goes at once",
          [len(a) for a, _ in player.fed], [mod.FRAME * 2, 20])
    check("with no mark on it, and done at the end", marks, [None])

    utterance([lambda: waveform(50), lambda: index(1), lambda: index(2)])
    check("two marks in a row both report", marks, [1, 2, None])
    check("and only the audio that existed was sent",
          [len(a) for a, _ in player.fed], [100])

    idled = utterance([lambda: waveform(30), lambda: index(9)])
    check("an utterance ending on a mark still waits before saying done", idled, 1)
    check("and reports the mark before the done", marks, [9, None])

    # ---- cancelling, which is what crashed NVDA when it was done properly --

    # cancel() is the one method meant to be called from another thread, and it
    # posts the resume to the engine's own thread. That thread is really running
    # here, so the wait below is not politeness: without it the resume lands in
    # the middle of the checks.
    stopped = player.stopped
    engine.cancel()
    check("cancelling stops the player", player.stopped - stopped, 1)
    check("eciStop is never called, at any point", dll.named("eciStop"), [])

    for _ in range(200):
        if not engine._discarding:
            break
        time.sleep(0.01)
    check("and it clears itself on the engine's thread, not the caller's",
          engine._discarding, False)

    # Now the discarding itself, with the flag set directly so that the engine's
    # thread has nothing queued and cannot race these checks.
    engine._discarding = True
    player.fed = []
    marks.clear()
    check("the callback keeps answering normally rather than aborting",
          waveform(100), mod.DATA_PROCESSED)
    check("but nothing reaches the player", player.fed, [])

    pending[:] = [lambda: waveform(200), lambda: index(5), lambda: waveform(200)]
    engine.synthesize()
    check("an utterance synthesised while cancelled plays nothing", player.fed, [])
    check("and reports no marks, nor claims to have finished", marks, [])

    engine._discarding = False
    idled = utterance([lambda: waveform(64), lambda: index(7)])
    check("once no longer cancelled the next utterance plays and reports again",
          ([len(a) for a, _ in player.fed], marks, idled), ([128], [7, None], 1))

    # ---- pausing, which must not reach the engine at all -------------------
    #
    # Pausing is the player's business. Telling the engine would be another
    # interrupt, and interrupting is what the whole design avoids.

    before = len(dll.calls)
    engine.pause(True)
    engine.pause(False)
    check("pausing is passed to the player", player.paused, [True, False])
    check("and the engine is told nothing about it",
          dll.calls[before:], [])

    # ---- an utterance the engine took and made nothing of ------------------
    #
    # The engine answers success for text it had no room for, deliberately and
    # as IBM's own build did, so a dropped stretch cannot be seen in a return
    # code. Silence that says nothing is the one failure a screen reader cannot
    # be diagnosed from, so the driver has to notice it.

    del sequence.LOGGED["warning"][:]
    utterance([])
    check("an utterance that produced no audio at all is complained about",
          len(sequence.LOGGED["warning"]), 1)

    del sequence.LOGGED["warning"][:]
    utterance([lambda: waveform(16)])
    check("and one that produced some is not",
          sequence.LOGGED["warning"], [])

    # ---- shutting down, which is what switching synthesiser away does -----
    #
    # Nothing below was checked at all until the add-on raised here in NVDA:
    # Engine used to subclass threading.Thread and keep the ECI handle in
    # _handle, which is the name Python 3.13's Thread keeps its own handle in.
    # Joining the thread therefore looked up join on an integer.

    try:
        engine.close()
        print("ok   closing down does not raise")
    except Exception as e:  # noqa: BLE001
        print("FAIL closing down raised %s: %s" % (type(e).__name__, e))
        FAILED.append("close")

    check("the thread has stopped", engine.alive(), False)
    check("the instance was given back", dll.named("eciDelete") != [], True)
    check("and the player was closed", player.closed, 1)

    control_checks()
    stall_checks()
    idle_stall_checks()

    # A Thread subclass shares Thread's namespace, so this holds the engine to
    # not using any name the standard library's Thread does.
    import threading

    theirs = set(dir(threading.Thread))
    ours = set(vars(mod.Engine)) | set(vars(engine))
    shared = sorted(
        n for n in ours & theirs if not (n.startswith("__") and n.endswith("__"))
    )
    check("no attribute of the engine collides with one of Thread's", shared, [])

    if FAILED:
        print("\nengine: %d of the checks failed" % len(FAILED))
        return 1
    print("\nengine: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

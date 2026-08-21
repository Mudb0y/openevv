#!/usr/bin/env python3
"""Check what the driver makes of a speech sequence, without NVDA.

The driver's one interesting decision is how a sequence of strings and commands
becomes a list of calls into the engine, and that decision can be checked
anywhere: it is text in and calls out, with no library and no audio involved.
So NVDA's modules are stood in for here and the driver is asked what it would
do.

What this cannot check is the sound, the timing or the crossing into the
library. Those need Windows and the suite; this is the part that can be held to
account on any machine, and it is the part where a misspelled annotation or an
index inside a stretch of text instead of between two would otherwise go
unnoticed until somebody listened.

usage: sequence.py
"""

import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ADDON = os.path.join(os.path.dirname(HERE), "addon")


# ---- NVDA, as much of it as the driver touches -----------------------------


class _Command:
    pass


class IndexCommand(_Command):
    def __init__(self, index):
        self.index = index


class CharacterModeCommand(_Command):
    def __init__(self, state):
        self.state = state


class LangChangeCommand(_Command):
    def __init__(self, lang):
        self.lang = lang


class BreakCommand(_Command):
    def __init__(self, time=0):
        self.time = time


class _Prosody(_Command):
    def __init__(self, newValue):
        self.newValue = newValue


class PitchCommand(_Prosody):
    pass


class RateCommand(_Prosody):
    pass


class VolumeCommand(_Prosody):
    pass


class _Setting:
    def __init__(self, *args, **kwargs):
        self.id = args[0] if args else None


class _Notifier:
    def __init__(self):
        self.seen = []

    def notify(self, **kwargs):
        self.seen.append(kwargs)


class _SynthDriver:
    """The setting factories, the two conversions, and the properties.

    NVDA builds a property for every _get_x and _set_x a driver defines, which
    is how `synth.rate` reaches `_get_rate`. That machinery is NVDA's, but the
    driver depends on it -- it reads self.rate itself when scaling a break --
    so it is reproduced here rather than worked around.
    """

    def __getattr__(self, name):
        getter = self.__class__.__dict__.get("_get_" + name)
        if getter is None:
            for base in self.__class__.__mro__:
                getter = base.__dict__.get("_get_" + name)
                if getter is not None:
                    break
        if getter is None:
            raise AttributeError(name)
        return getter(self)

    def __setattr__(self, name, value):
        if not name.startswith("_"):
            for base in self.__class__.__mro__:
                setter = base.__dict__.get("_set_" + name)
                if setter is not None:
                    setter(self, value)
                    return
        object.__setattr__(self, name, value)

    @classmethod
    def VoiceSetting(cls):
        return _Setting("voice")

    @classmethod
    def RateSetting(cls, minStep=1):
        return _Setting("rate")

    @classmethod
    def RateBoostSetting(cls):
        return _Setting("rateBoost")

    @classmethod
    def PitchSetting(cls, minStep=1):
        return _Setting("pitch")

    @classmethod
    def InflectionSetting(cls, minStep=1):
        return _Setting("inflection")

    @classmethod
    def VolumeSetting(cls, minStep=1):
        return _Setting("volume")

    # These two are NVDA's own arithmetic, copied so the numbers this test
    # sees are the numbers NVDA would produce.
    def _percentToParam(self, percent, min, max):
        return int(round(min + (max - min) * (percent / 100.0)))

    def _paramToPercent(self, current, min, max):
        return int(round((current - min) * 100.0 / (max - min)))


class VoiceInfo:
    def __init__(self, id, name, language=None):
        self.id = id
        self.name = name
        self.language = language


def _install_stubs():
    log = types.SimpleNamespace(
        debug=lambda *a, **k: None,
        debugWarning=lambda *a, **k: None,
        error=lambda *a, **k: sys.stderr.write("driver logged an error: %s\n" % (a,)),
        info=lambda *a, **k: None,
    )

    def module(name, **contents):
        m = types.ModuleType(name)
        for k, v in contents.items():
            setattr(m, k, v)
        sys.modules[name] = m
        return m

    module("config", conf={"audio": {"outputDevice": "default"}})
    module("nvwave", WavePlayer=object)
    module("logHandler", log=log)

    module("autoSettingsUtils")
    module(
        "autoSettingsUtils.driverSetting",
        DriverSetting=_Setting,
        BooleanDriverSetting=_Setting,
        NumericDriverSetting=_Setting,
    )

    module("speech")
    module(
        "speech.commands",
        IndexCommand=IndexCommand,
        CharacterModeCommand=CharacterModeCommand,
        LangChangeCommand=LangChangeCommand,
        BreakCommand=BreakCommand,
        PitchCommand=PitchCommand,
        RateCommand=RateCommand,
        VolumeCommand=VolumeCommand,
    )
    module("speech.types", SpeechSequence=list)
    module(
        "synthDriverHandler",
        SynthDriver=_SynthDriver,
        VoiceInfo=VoiceInfo,
        synthIndexReached=_Notifier(),
        synthDoneSpeaking=_Notifier(),
    )

    # NVDA puts gettext's _ in the builtins before any driver is imported.
    import builtins

    if not hasattr(builtins, "_"):
        builtins._ = lambda s: s

    # The two names ctypes only has on Windows. The engine layer has to use
    # them there -- a stdcall callback made any other way is a fault on the
    # crossing -- so they are stood in for here rather than avoided in it.
    import ctypes

    if not hasattr(ctypes, "WINFUNCTYPE"):
        ctypes.WINFUNCTYPE = ctypes.CFUNCTYPE
    if not hasattr(ctypes, "WinDLL"):
        ctypes.WinDLL = ctypes.CDLL


# ---- an engine that only writes down what it was asked ---------------------


class FakeEngine:
    def __init__(self, onIndex):
        self.onIndex = onIndex
        self.calls = []
        self.voiceNames = {n: "Voice %d" % n for n in range(1, 9)}
        self.voiceParams = {0: 0, 1: 50, 2: 65, 3: 30, 4: 0, 5: 0, 6: 50, 7: 92}
        self.version = "test"
        self.player = None

    def open(self):
        pass

    def close(self):
        self.calls.append(("close",))

    def post(self, batch):
        for fn, args in batch:
            self.calls.append((fn.__name__ if hasattr(fn, "__name__") else str(fn),) + args)

    def cancel(self):
        self.calls.append(("cancel",))

    def pause(self, switch):
        self.calls.append(("pause", switch))

    # The names the driver posts. They are never run; posting records them.
    def addText(self, text):
        pass

    def index(self, n):
        pass

    def synthesize(self):
        pass

    def setParam(self, which, value):
        pass

    def setVoiceParam(self, which, value):
        pass

    def copyVoice(self, number):
        pass


def driver():
    _install_stubs()
    if ADDON not in sys.path:
        sys.path.insert(0, ADDON)
    import synthDrivers._openevv as engine_module
    import synthDrivers.openevv as driver_module

    engine_module.Engine = FakeEngine
    return driver_module.SynthDriver()


# ---- the checks ------------------------------------------------------------

FAILED = []


def check(what, got, want):
    if got == want:
        print("ok   %s" % what)
    else:
        print("FAIL %s\n     got  %r\n     want %r" % (what, got, want))
        FAILED.append(what)


def spoken(d, sequence):
    d._engine.calls = []
    d.speak(sequence)
    return d._engine.calls


def main():
    d = driver()

    check(
        "one plain string is one stretch of text and a synthesise",
        spoken(d, ["Hello."]),
        [("addText", b"Hello."), ("synthesize",)],
    )

    check(
        "an index goes between the stretches, not inside one",
        spoken(d, ["One.", IndexCommand(7), "Two."]),
        [
            ("addText", b"One."),
            ("index", 7),
            ("addText", b"Two."),
            ("synthesize",),
        ],
    )

    check(
        "an index at the end still comes before the synthesise",
        spoken(d, ["Only.", IndexCommand(3)]),
        [("addText", b"Only."), ("index", 3), ("synthesize",)],
    )

    check(
        "character mode is turned on and off again in the text",
        spoken(d, [CharacterModeCommand(True), "abc", CharacterModeCommand(False)]),
        [("addText", "`ts1 abc`ts0 ".encode()), ("synthesize",)],
    )

    check(
        "character mode left on is closed anyway",
        spoken(d, [CharacterModeCommand(True), "abc"]),
        [("addText", "`ts1 abc`ts0 ".encode()), ("synthesize",)],
    )

    check(
        "pitch, rate and volume become annotations in the text",
        spoken(d, [PitchCommand(20), "low", VolumeCommand(30), "quiet"]),
        [("addText", "`vb20 low`vv30 quiet".encode()), ("synthesize",)],
    )

    check(
        "a backtick in ordinary text cannot start an annotation",
        spoken(d, ["a `vs10 b"]),
        [("addText", b"a  vs10 b"), ("synthesize",)],
    )

    d.voiceTags = True
    check(
        "unless the reader has asked for tags to go through",
        spoken(d, ["a `vs10 b"]),
        [("addText", b"a `vs10 b"), ("synthesize",)],
    )
    d.voiceTags = False

    check(
        "text goes in as UTF-8",
        spoken(d, ["café"]),
        [("addText", "café".encode("utf-8")), ("synthesize",)],
    )

    check(
        "a language change is dropped, there being one language",
        spoken(d, [LangChangeCommand("de_DE"), "Hallo."]),
        [("addText", b"Hallo."), ("synthesize",)],
    )

    # A break is scaled by the rate, so it is checked at a known rate.
    d._engine.voiceParams[6] = 40   # speed 40, which is nought per cent
    check("the rate reads back as the bottom of the range", d.rate, 0)
    calls = spoken(d, ["one", BreakCommand(100), "two"])
    check(
        "a break becomes a pause annotation between the words",
        calls,
        [("addText", b"one `p100 two"), ("synthesize",)],
    )

    check(
        "a break of nothing is still a well formed annotation",
        spoken(d, ["one", BreakCommand(0), "two"]),
        [("addText", b"one `p0 two"), ("synthesize",)],
    )

    # The rate mapping, at both ends and in the middle.
    check("nought per cent is the bottom of the useful range", d._rateToParam(0), 40)
    check("a hundred per cent is the top of it", d._rateToParam(100), 156)
    check("fifty per cent is the middle", d._rateToParam(50), 98)

    d._rateBoost = True
    check("the boost multiplies, and is clamped to what the engine takes",
          d._rateToParam(100), 250)
    d._rateBoost = False

    check(
        "the eight voices are offered under the engine's own names",
        sorted(d.availableVoices),
        [str(n) for n in range(1, 9)],
    )

    if FAILED:
        print("\nsequence: %d of the checks failed" % len(FAILED))
        return 1
    print("\nsequence: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

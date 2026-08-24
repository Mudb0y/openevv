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
    # Recording rather than discarding, so a check can assert that the driver
    # said something when it should have.
    said = {"warning": [], "error": [], "debugWarning": []}
    log = types.SimpleNamespace(
        debug=lambda *a, **k: None,
        info=lambda *a, **k: None,
        debugWarning=lambda *a, **k: said["debugWarning"].append(a[0] if a else ""),
        warning=lambda *a, **k: said["warning"].append(a[0] if a else ""),
        error=lambda *a, **k: (
            said["error"].append(a[0] if a else ""),
            sys.stderr.write("driver logged an error: %s\n" % (a,)),
        ),
    )
    LOGGED.clear()
    LOGGED.update(said)

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
    #: Which languages this one has in it. A check that wants more than one
    #: sets it before making the driver.
    languages = [0x10000]

    def __init__(self, onIndex):
        self.onIndex = onIndex
        self.calls = []
        self.languages = list(FakeEngine.languages)
        self.language = self.languages[0]
        self.voiceNamesByLanguage = {
            language: {n: "Voice %d" % n for n in range(1, 9)}
            for language in self.languages
        }
        self.voiceNames = self.voiceNamesByLanguage[self.language]
        self.voiceParams = {0: 0, 1: 50, 2: 65, 3: 30, 4: 0, 5: 0, 6: 50, 7: 92}
        self.version = "test"
        self.player = None
        #: Whether each batch arrived as speech or as a setting.
        self.kinds = []

    def open(self):
        pass

    def close(self):
        self.calls.append(("close",))

    def post(self, batch):
        for fn, args in batch:
            name = fn.__name__ if hasattr(fn, "__name__") else str(fn)
            self.calls.append((name,) + args)
            # Recording is enough for every call but this one: the driver
            # reads the language back to decide whether the next thing it
            # is asked for needs a change at all, so the one piece of state
            # it depends on is kept here as well.
            if name == "setLanguage":
                self.language = args[0]
                self.voiceNames = self.voiceNamesByLanguage[args[0]]

    def control(self, batch):
        # A control step runs the same way as speech here; what the driver is
        # being held to is that it sends settings as control and utterances as
        # speech, which the check below reads off self.kinds.
        self.kinds.append(("control", len(batch)))
        self.post(batch)

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

    def setLanguage(self, language):
        pass

    def voiceNamesFor(self, language):
        return self.voiceNamesByLanguage.get(language, self.voiceNames)


def driver():
    _install_stubs()
    if ADDON not in sys.path:
        sys.path.insert(0, ADDON)
    import synthDrivers._openevv as engine_module
    import synthDrivers.openevv as driver_module

    global _openevv
    _openevv = engine_module

    engine_module.Engine = FakeEngine
    return driver_module.SynthDriver()


# ---- the checks ------------------------------------------------------------

FAILED = []

#: What the driver logged, filled in by the stubs so checks can read it.
LOGGED = {}


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
        [("addText", b"Hello."), ("synthesize", True)],
    )

    check(
        "an index goes between the stretches, not inside one",
        spoken(d, ["One.", IndexCommand(7), "Two."]),
        [
            ("addText", b"One."),
            ("index", 7),
            ("addText", b"Two."),
            ("synthesize", True),
        ],
    )

    check(
        "an index at the end still comes before the synthesise",
        spoken(d, ["Only.", IndexCommand(3)]),
        [("addText", b"Only."), ("index", 3), ("synthesize", True)],
    )

    # Each annotation goes in a call of its own. An annotation on the end of a
    # stretch of text does not take effect, which is how spelling used to leak
    # into every utterance after the one that asked for it.
    spelled = [
        ("addText", b"`ts1 "),
        ("addText", b"abc"),
        ("addText", b"`ts0 "),
        ("synthesize", True),
    ]
    check(
        "character mode is turned on and off in calls of their own",
        spoken(d, [CharacterModeCommand(True), "abc", CharacterModeCommand(False)]),
        spelled,
    )

    check(
        "character mode left on is closed anyway, and alone",
        spoken(d, [CharacterModeCommand(True), "abc"]),
        spelled,
    )

    check(
        "and nothing is closed that was never opened",
        spoken(d, ["abc"]),
        [("addText", b"abc"), ("synthesize", True)],
    )

    # A sequence of nothing but commands is silent because it should be, and
    # the engine layer is told so rather than complaining that it made no
    # sound.
    check(
        "a sequence with no words in it says so",
        spoken(d, [CharacterModeCommand(True), CharacterModeCommand(False)]),
        [("addText", b"`ts1 "), ("addText", b"`ts0 "), ("synthesize", False)],
    )

    check(
        "pitch, rate and volume become annotations in the text",
        spoken(d, [PitchCommand(20), "low", VolumeCommand(30), "quiet"]),
        [("addText", "`vb20 low`vv30 quiet".encode()), ("synthesize", True)],
    )

    check(
        "a backtick in ordinary text cannot start an annotation",
        spoken(d, ["a `vs10 b"]),
        [("addText", b"a  vs10 b"), ("synthesize", True)],
    )

    d.voiceTags = True
    check(
        "unless the reader has asked for tags to go through",
        spoken(d, ["a `vs10 b"]),
        [("addText", b"a `vs10 b"), ("synthesize", True)],
    )
    d.voiceTags = False

    check(
        "text goes in as UTF-8",
        spoken(d, ["café"]),
        [("addText", "café".encode("utf-8")), ("synthesize", True)],
    )

    check(
        "a language change is dropped, there being one language",
        spoken(d, [LangChangeCommand("de_DE"), "Hallo."]),
        [("addText", b"Hallo."), ("synthesize", True)],
    )

    # A break is scaled by the rate, so it is checked at a known rate.
    d._engine.voiceParams[6] = 40   # speed 40, which is nought per cent
    check("the rate reads back as the bottom of the range", d.rate, 0)
    calls = spoken(d, ["one", BreakCommand(100), "two"])
    check(
        "a break becomes a pause annotation between the words",
        calls,
        [("addText", b"one `p100 two"), ("synthesize", True)],
    )

    check(
        "a break of nothing is still a well formed annotation",
        spoken(d, ["one", BreakCommand(0), "two"]),
        [("addText", b"one `p0 two"), ("synthesize", True)],
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

    # ---- the settings, which reach the engine as posted calls -------------

    d._engine.calls = []
    d.pitch = 30
    d.volume = 40
    d.inflection = 55
    d.headSize = 60
    d.roughness = 5
    d.breathiness = 15
    check(
        "each setting posts one call, with the engine's own number for it",
        d._engine.calls,
        [
            ("setVoiceParam", _openevv.VOICE_PITCH, 30),
            ("setVoiceParam", _openevv.VOICE_VOLUME, 40),
            ("setVoiceParam", _openevv.VOICE_FLUCTUATION, 55),
            ("setVoiceParam", _openevv.VOICE_HEAD_SIZE, 60),
            ("setVoiceParam", _openevv.VOICE_ROUGHNESS, 5),
            ("setVoiceParam", _openevv.VOICE_BREATHINESS, 15),
        ],
    )

    d._engine.calls = []
    d.voice = "4"
    check("choosing a voice copies that preset over the one in force",
          d._engine.calls, [("copyVoice", 4)])
    check("and the driver remembers which it is", d.voice, "4")

    d._engine.calls = []
    d.voice = "99"
    check("a voice the engine does not have is refused rather than passed on",
          (d._engine.calls, d.voice), ([], "4"))

    d._engine.calls = []
    d.abbreviations = True
    check("expanding abbreviations turns the dictionary on, which is nought",
          d._engine.calls, [("setParam", _openevv.PARAM_DICTIONARY, 0)])
    d._engine.calls = []
    d.abbreviations = False
    check("and off again is one", d._engine.calls,
          [("setParam", _openevv.PARAM_DICTIONARY, 1)])

    d._engine.calls = []
    d.pause(True)
    d.cancel()
    check("pausing and cancelling go straight through",
          d._engine.calls, [("pause", True), ("cancel",)])

    # Every setting goes as a control step and not as speech. Sent as speech
    # it is dropped by any cancel that overtakes it, and since every keystroke
    # cancels, a rate or a voice chosen at the wrong moment silently did not
    # happen -- with the dialog still showing what was asked for.
    d._engine.kinds = []
    d.rate = 60
    d.pitch = 40
    d.volume = 80
    d.inflection = 55
    d.headSize = 45
    d.roughness = 10
    d.breathiness = 20
    d.abbreviations = True
    d.voice = "3"
    check("every setting is sent as a control step, never as speech",
          [kind for kind, _ in d._engine.kinds],
          ["control"] * 9)

    d._engine.kinds = []
    d.speak(["a sentence"])
    check("and an utterance still goes as speech", d._engine.kinds, [])

    # ---- and the same driver over a library with two languages in it ----
    #
    # Everything above is a build with one language, which is what the driver
    # has always had and what these checks were written against. What follows
    # is the other shape: the same driver, told the library has German too.
    FakeEngine.languages = [0x10000, 0x40000]
    try:
        d = driver()

        voices = d.availableVoices
        check("every language and preset is offered as a voice",
              len(voices), 16)
        check("and each says which language it is",
              [voices["65536:1"].language, voices["262144:1"].language],
              ["en_US", "de_DE"])
        check("under a name with the language in it",
              voices["262144:3"].name, "German - Voice 3")
        check("the driver starts in the first language",
              (d.voice, d.language), ("65536:1", "en_US"))

        d._engine.calls = []
        d.voice = "262144:5"
        check("choosing a voice of another language changes both",
              d._engine.calls,
              [("setLanguage", 0x40000), ("copyVoice", 5)])

        d._engine.calls = []
        d.voice = "262144:2"
        check("and staying in that language changes only the preset",
              d._engine.calls, [("copyVoice", 2)])

        check("a voice of a language the library has not is refused",
              (d.voice, spoken(d, [])) and d.voice, "262144:2")
        d.voice = "196608:1"
        check("and leaves the one in force alone", d.voice, "262144:2")

        # The engine is stood in for, so its language does not really move;
        # what is being checked is what the driver asks for and in what
        # order, which is what a document with two languages in it needs.
        d._engine.language = 0x10000
        check(
            "a language change in a sequence switches and copies the preset",
            spoken(d, ["This is ", LangChangeCommand("de_DE"), "Hallo."]),
            [
                ("addText", b"This is "),
                ("setLanguage", 0x40000),
                ("copyVoice", 2),
                ("addText", b"Hallo."),
                ("synthesize", True),
            ],
        )

        d._engine.language = 0x10000
        check(
            "a change to the language already being spoken is dropped",
            spoken(d, [LangChangeCommand("en_US"), "Hello."]),
            [("addText", b"Hello."), ("synthesize", True)],
        )

        d._engine.language = 0x10000
        check(
            "a bare language matches the dialect the library has",
            spoken(d, [LangChangeCommand("de"), "Hallo."]),
            [
                ("setLanguage", 0x40000),
                ("copyVoice", 2),
                ("addText", b"Hallo."),
                ("synthesize", True),
            ],
        )

        d._engine.language = 0x10000
        check(
            "a language the library has not leaves the voice where it was",
            spoken(d, [LangChangeCommand("fr_FR"), "Bonjour."]),
            [("addText", b"Bonjour."), ("synthesize", True)],
        )
    finally:
        FakeEngine.languages = [0x10000]

    if FAILED:
        print("\nsequence: %d of the checks failed" % len(FAILED))
        return 1
    print("\nsequence: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

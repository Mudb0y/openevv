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

import atexit
import os
import shutil
import sys
import tempfile
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
    def __init__(self, newValue, isDefault=False):
        self.newValue = newValue
        self.isDefault = isDefault


class PitchCommand(_Prosody):
    pass


class RateCommand(_Prosody):
    pass


class VolumeCommand(_Prosody):
    pass


class _Setting:
    def __init__(self, *args, **kwargs):
        self.id = args[0] if args else None


class _ParameterInfo:
    """What NVDA offers as one choice of a setting that is a list."""

    def __init__(self, id, displayName=None):
        self.id = id
        self.displayName = displayName if displayName is not None else id


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


#: Where NVDA keeps the reader's configuration, here: made once, and where the
#: dictionary checks lay out add-ons and files.
CONFIG = tempfile.mkdtemp(prefix="openevv-nvda-config-")
atexit.register(shutil.rmtree, CONFIG, True)

#: The add-ons NVDA says are installed. A check that wants a dictionary
#: provider puts one here.
ADDONS = []


class FakeAddon:
    def __init__(self, path, pendingInstall=False):
        self.path = path
        self.isPendingInstall = pendingInstall


def _install_stubs():
    # Recording rather than discarding, so a check can assert that the driver
    # said something when it should have. The same lists every time, emptied:
    # a module imported under an earlier call keeps the log it was given, and
    # what it says has to land where a later check reads.
    said = {k: LOGGED.get(k, []) for k in ("warning", "error", "debugWarning")}
    for kept in said.values():
        del kept[:]
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
    module("globalVars", appArgs=types.SimpleNamespace(configPath=CONFIG))
    module("addonHandler", getAvailableAddons=lambda: list(ADDONS))
    module("nvwave", WavePlayer=object)
    module("logHandler", log=log)

    module("autoSettingsUtils")
    module(
        "autoSettingsUtils.driverSetting",
        DriverSetting=_Setting,
        BooleanDriverSetting=_Setting,
        NumericDriverSetting=_Setting,
    )
    module("autoSettingsUtils.utils", StringParameterInfo=_ParameterInfo)

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
        self.pauseMode = 1
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
            if name in ("setLanguage", "selectLanguage"):
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

    def synthesizePart(self, expectAudio=True):
        pass

    def index(self, n):
        pass

    def synthesize(self):
        pass

    def setParam(self, which, value):
        pass

    def setVoiceParam(self, which, value):
        pass

    def restoreVoiceParams(self, which):
        pass

    def copyVoice(self, number):
        pass

    def setLanguage(self, language):
        pass

    def selectLanguage(self, language, preset):
        pass

    def setDictionaryFolders(self, folders):
        pass

    def setPauseMode(self, mode):
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


#: What every utterance opens with while phrase prediction is off.
PP0 = ("addText", b"`pp0 ")


def stretches(calls):
    """The text handed over, without the phrase prediction every utterance
    opens with, for the checks that are about where the text was cut."""
    return [
        args[0] for name, *args in calls
        if name == "addText" and args[0] not in (b"`pp0 ", b"`pp1 ")
    ]


def _endsPlainSentence(text):
    """Whether a piece ends where a sentence does, rather than after an
    abbreviation or an initial. Written out here rather than imported, so that
    the driver's own rule is held against a statement of what it should be."""
    if text.endswith(("?", "!")):
        return True
    if not text.endswith("."):
        return False
    word = text.split()[-1][:-1] if text.split() else ""
    return bool(word) and "." not in word and not (
        len(word) <= 3 and word[:1].isupper()
    ) and not word[-1:].isdigit()


def _write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


CONTRACT = "[contract]\nformat = eci-dictionary-sets\nversion = 1\n"


def provider(name, sets, contract=CONTRACT, pendingInstall=False):
    """An add-on carrying dictionary sets, laid out as NVDA installs one."""
    root = os.path.join(CONFIG, "addons", name)
    os.makedirs(os.path.join(root, "dictionaries", "sets"), exist_ok=True)
    if contract is not None:
        _write(os.path.join(root, "dictionaries", "contract.ini"), contract)
    for folder, fields in sets:
        _write(
            os.path.join(root, "dictionaries", "sets", folder, "set.ini"),
            "[set]\n" + "".join("%s = %s\n" % kv for kv in fields.items()),
        )
    return FakeAddon(root, pendingInstall)


def described(set_id, **changed):
    """A set.ini's fields, every one present unless a check takes it out."""
    fields = {
        "id": set_id,
        "name": "Community",
        "source_url": "https://example.org/dictionaries",
        "source_version": "2026.10",
        "source_revision": "0" * 40,
        "attribution": "its contributors",
        "license": "CC0-1.0",
        "license_url": "https://example.org/licence",
    }
    fields.update(changed)
    return {k: v for k, v in fields.items() if v is not None}


def dictionary_checks():
    """Where the driver tells the engine to read dictionaries from.

    The managed set the reader chose, then the personal entries, then the
    add-on's own folder in the configuration; a set comes from any add-on
    carrying the contract, and is offered only when the add-on and the set
    both describe themselves as the contract says.
    """
    personal = os.path.join(CONFIG, "eciDictionaries", "personal")
    own = os.path.join(CONFIG, "openevv")

    def folders(d):
        sent = [args[0] for name, *args in d._engine.calls
                if name == "setDictionaryFolders"]
        return sent[-1] if sent else None

    del ADDONS[:]
    d = driver()
    check("with no set chosen the engine reads the personal entries and the"
          " add-on's own folder", folders(d), (personal, own))
    check("and with nothing installed only none is offered",
          list(d.availableDictionarysets), ["none"])

    good = "org.example.community"
    ADDONS.extend([
        provider("manager", [
            (good, described(good)),
            ("org.example.other", described("org.example.elsewhere")),
            ("org.example.partial", described("org.example.partial",
                                              license=None)),
            ("Not_An_Id", described("Not_An_Id")),
        ]),
        provider("future", [("org.example.future",
                             described("org.example.future"))],
                 contract=CONTRACT.replace("version = 1", "version = 2")),
        provider("nocontract", [("org.example.bare",
                                 described("org.example.bare"))],
                 contract=None),
        provider("arriving", [("org.example.pending",
                               described("org.example.pending"))],
                 pendingInstall=True),
    ])
    offered = d.availableDictionarysets
    check("a set is offered only where its add-on carries contract version"
          " one, is installed, and the set names itself as its folder does",
          list(offered), ["none", good])
    check("and it is offered under its name and version",
          offered[good].displayName, "Community (2026.10)")

    d.dictionarySet = good
    chosen = os.path.join(CONFIG, "addons", "manager", "dictionaries", "sets",
                          good)
    check("a chosen set is read first", folders(d), (chosen, personal, own))

    d.personalDictionary = False
    check("and the personal entries can be left out", folders(d),
          (chosen, own))
    d.personalDictionary = True

    del ADDONS[:]
    offered = d.availableDictionarysets
    check("a chosen set since uninstalled is still offered, as not installed",
          offered[good].displayName, good + " (not installed)")
    LOGGED["warning"][:] = []
    d.personalDictionary = True
    check("and is skipped rather than read", folders(d), (personal, own))
    check("and the log says why",
          any("not installed" in w for w in LOGGED["warning"]), True)
    check("the choice itself is kept", d.dictionarySet, good)

    d.dictionarySet = "none"
    check("none is no set", folders(d), (personal, own))


def main():
    d = driver()

    check(
        "one plain string is one stretch of text and a synthesise",
        spoken(d, ["Hello."]),
        [PP0, ("addText", b"Hello."), ("synthesize", True)],
    )

    check(
        "an index goes between the stretches, not inside one",
        spoken(d, ["One.", IndexCommand(7), "Two."]),
        [
            PP0,
            ("addText", b"One."),
            ("index", 7),
            ("addText", b"Two."),
            ("synthesize", True),
        ],
    )

    check(
        "an index at the end still comes before the synthesise",
        spoken(d, ["Only.", IndexCommand(3)]),
        [PP0, ("addText", b"Only."), ("index", 3), ("synthesize", True)],
    )

    # Each annotation goes in a call of its own. An annotation on the end of a
    # stretch of text does not take effect, which is how spelling used to leak
    # into every utterance after the one that asked for it.
    spelled = [
        PP0,
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
        [PP0, ("addText", b"abc"), ("synthesize", True)],
    )

    # A sequence of nothing but commands is silent because it should be, and
    # the engine layer is told so rather than complaining that it made no
    # sound.
    check(
        "a sequence with no words in it says so",
        spoken(d, [CharacterModeCommand(True), CharacterModeCommand(False)]),
        [PP0, ("addText", b"`ts1 "), ("addText", b"`ts0 "), ("synthesize", False)],
    )

    check(
        "pitch, rate and volume become annotations in the text",
        spoken(d, [PitchCommand(20), "low", VolumeCommand(30), "quiet"]),
        [
            PP0,
            ("addText", "`vb20 low`vv30 quiet".encode()),
            ("synthesize", True),
            ("restoreVoiceParams", (_openevv.VOICE_PITCH, _openevv.VOICE_VOLUME)),
        ],
    )

    # NVDA ends an utterance after a spelled character, and an add-on that
    # changes the rate around one -- the typing echo add-on of issue 42 --
    # has its change back sent as an utterance of its own, which the next
    # keystroke cancels before the driver ever sees it. So this is the first
    # of the two as it arrives.
    d._engine.kinds = []
    check(
        "a rate the sequence never changed back is put back after it",
        spoken(d, [RateCommand(80), CharacterModeCommand(True), "a",
                   CharacterModeCommand(False), IndexCommand(5)])[-1],
        ("restoreVoiceParams", (_openevv.VOICE_SPEED,)),
    )
    check("as a control step, which a cancel keeps",
          d._engine.kinds, [("control", 1)])

    check(
        "and nothing is put back that the sequence changed back itself",
        [name for name, *_ in spoken(d, [RateCommand(80), "a",
                                          RateCommand(50, isDefault=True)])],
        ["addText", "addText", "synthesize"],
    )

    check("phrase prediction is off unless the reader turns it on",
          d.phrasePrediction, False)
    d.phrasePrediction = True
    check(
        "and once on, every utterance opens by saying so",
        spoken(d, ["Hello."]),
        [("addText", b"`pp1 "), ("addText", b"Hello."), ("synthesize", True)],
    )
    d.phrasePrediction = False

    check(
        "a backtick in ordinary text cannot start an annotation",
        spoken(d, ["a `vs10 b"]),
        [PP0, ("addText", b"a  vs10 b"), ("synthesize", True)],
    )

    d.voiceTags = True
    check(
        "unless the reader has asked for tags to go through",
        spoken(d, ["a `vs10 b"]),
        [PP0, ("addText", b"a `vs10 b"), ("synthesize", True)],
    )
    d.voiceTags = False

    # Text goes in as the language reads it, which for English is the
    # Windows Western set and not UTF-8: the engine converts UTF-8 only for a
    # language with letters of its own, so UTF-8 here is two characters for
    # every accented one.
    check(
        "text goes in in the Western set",
        spoken(d, ["café"]),
        [PP0, ("addText", "café".encode("cp1252")), ("synthesize", True)],
    )
    check(
        "a curly apostrophe is the Western set's own",
        spoken(d, ["don\u2019t"]),
        [PP0, ("addText", b"don\x92t"), ("synthesize", True)],
    )
    check(
        "an accent the set has not got is the only one taken off",
        spoken(d, ["\u010capek and u\u0308\u030c"]),
        [PP0, ("addText", b"Capek and \xfc"), ("synthesize", True)],
    )
    check(
        "a space the set has not got is a space, not a question mark",
        spoken(d, ["5\u202fkm"]),
        [PP0, ("addText", b"5 km"), ("synthesize", True)],
    )
    check("Polish goes in as UTF-8, which its module converts",
          _openevv.textFor(0x110000, "\u017c\u00f3\u0142w"),
          "\u017c\u00f3\u0142w".encode("utf-8"))
    check("and Japanese as Shift-JIS, which its romanizer reads",
          _openevv.textFor(0x80000, "\u65e5\u672c"),
          "\u65e5\u672c".encode("cp932"))

    check(
        "a language change is dropped, there being one language",
        spoken(d, [LangChangeCommand("de_DE"), "Hallo."]),
        [PP0, ("addText", b"Hallo."), ("synthesize", True)],
    )

    # A break is scaled by the rate, so it is checked at a known rate.
    d._engine.voiceParams[6] = 40   # speed 40, which is nought per cent
    check("the rate reads back as the bottom of the range", d.rate, 0)
    calls = spoken(d, ["one", BreakCommand(100), "two"])
    check(
        "a break becomes a pause annotation between the words",
        calls,
        [PP0, ("addText", b"one `p100 two"), ("synthesize", True)],
    )

    check(
        "a break of nothing is still a well formed annotation",
        spoken(d, ["one", BreakCommand(0), "two"]),
        [PP0, ("addText", b"one `p0 two"), ("synthesize", True)],
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

    check("the pauses are offered as both other drivers offer them",
          [(k, v.displayName) for k, v in d.availablePausemodes.items()],
          [("0", "Never"), ("1", "At end of text only"), ("2", "Always")])
    check("and start where the library starts", d.pauseMode, "1")
    d._engine.calls = []
    d.pauseMode = "2"
    check("choosing one tells the engine", d._engine.calls, [("setPauseMode", 2)])
    d._engine.calls = []
    d.pauseMode = "3"
    d.pauseMode = "often"
    check("one the engine has not is refused rather than passed on",
          d._engine.calls, [])

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
    d.pauseMode = "0"
    check("every setting is sent as a control step, never as speech",
          [kind for kind, _ in d._engine.kinds], ["control"] * 10)

    d._engine.kinds = []
    d.speak(["a sentence"])
    check("and an utterance still goes as speech", d._engine.kinds, [])

    # ---- long text is handed over in pieces ----------------------------
    #
    # The engine cannot be interrupted, so asking for silence means waiting
    # out the utterance in flight. Whole, a long chat message costs the best
    # part of a second and a half on this engine, and every item a reader
    # arrows onto inside that wait says nothing at all. In pieces the wait is
    # one piece.

    d._engine.calls = []
    d.speak(["Richard Loyie, I told him, Sent at 12:32"])
    check("a line of a list is one piece, as it always was",
          [name for name, *_ in d._engine.calls], ["addText", "addText", "synthesize"])

    long_text = "Hello everyone, TableEx 3.2.0 is here. " * 8
    d._engine.calls = []
    d.speak([long_text])
    names = [name for name, *_ in d._engine.calls]
    check("a long message is handed over in more than one piece",
          names.count("synthesize") + names.count("synthesizePart") > 1, True)
    check("every piece but the last is a part", names.count("synthesize"), 1)
    check("and the last one is the whole utterance's end", names[-1], "synthesize")
    check("phrase prediction is said once, at the head of the first piece",
          [i for i, call in enumerate(d._engine.calls) if call == PP0], [0])

    said = b"".join(stretches(d._engine.calls))
    check("the split changes no word of the text",
          said.decode("utf-8").split(), long_text.split())

    # Splitting inside a word would have the engine speak two fragments.
    # Boundaries therefore carry their following whitespace with them.
    parts = stretches(d._engine.calls)
    check("no piece begins or ends inside a word",
          [p for p in parts if p[:1].isalnum() and p[-1:].isalnum()], [])

    check("a version number is not mistaken for the end of a sentence",
          [p for p in parts if p.rstrip().endswith(b"3.2.0")], [])

    # An abbreviation and an initial end in a dot and do not end a sentence,
    # and the engine knows it: cutting there puts a full stop where it had
    # none. "Mr. Jones asked whether the header is read first, and Mrs. Adams
    # said it is." measured 0.70 s longer cut after the two titles, and "The
    # book by J. R. R. Tolkien is on the shelf by the door." measured 1.48 s
    # longer than 3.72 cut at every initial.
    for text, what in (
        ("Mr. Jones asked whether the header is read first, and Mrs. Adams"
         " said it is. " * 4, "a title"),
        ("The book by J. R. R. Tolkien is on the shelf by the door. " * 4,
         "an initial"),
        ("Use a smaller step, e.g. two, and the sorting holds. " * 6,
         "a dotted abbreviation"),
    ):
        d._engine.calls = []
        d.speak([text])
        cut = [c.decode("utf-8").rstrip() for c in stretches(d._engine.calls)]
        # And it does still cut, or there would be nothing to be right about.
        check("%s is still handed over in pieces" % what, len(cut) > 1, True)
        check("no piece is cut after %s" % what,
              [c for c in cut[:-1] if not _endsPlainSentence(c)], [])


    # Sentence ends cost no extra silence: the engine already ends its clause
    # there. A stretch without one is allowed to grow much larger before the
    # whitespace fallback keeps it bounded.
    no_sentence = "word " * 120
    d._engine.calls = []
    d.speak([no_sentence])
    no_sentence_parts = stretches(d._engine.calls)
    check("an unpunctuated stretch falls back to whitespace past the cap",
          len(no_sentence_parts) > 1, True)
    check("and it is not cut at the old eighty-character limit",
          len(no_sentence_parts[0]) > 80, True)

    # A cancel drops whole queue items. A spelling or prosody restore must
    # therefore remain in the same item as the command that opened it.
    d._engine.calls = []
    d.speak([
        CharacterModeCommand(True),
        "spelled words. " * 60,
        CharacterModeCommand(False),
    ])
    check("spelling kept open refuses every boundary",
          [name for name, *_ in d._engine.calls].count("synthesizePart"), 0)

    d._engine.calls = []
    d.speak([
        PitchCommand(80),
        "raised words. " * 60,
        PitchCommand(50, isDefault=True),
    ])
    check("prosody kept open refuses every boundary",
          [name for name, *_ in d._engine.calls].count("synthesizePart"), 0)

    # A sequence of nothing but commands still has to reach the engine, or
    # whatever waits on it waits for ever.
    d._engine.calls = []
    d.speak([IndexCommand(9)])
    check("an utterance of nothing but an index still ends in a synthesize",
          [name for name, *_ in d._engine.calls][-1], "synthesize")

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
        d.voice = "65536:2"
        d._engine.language = 0x10000
        check(
            "a language change in a sequence switches in the same voice,"
            " and the reader's own comes back after it",
            spoken(d, ["This is ", LangChangeCommand("de_DE"), "Hallo."]),
            [
                PP0,
                ("addText", b"This is "),
                ("selectLanguage", 0x40000, 2),
                ("addText", b"Hallo."),
                ("synthesize", True),
                ("selectLanguage", 0x10000, 2),
            ],
        )
        check("and that putting back is a control step, which a cancel keeps",
              d._engine.kinds[-1], ("control", 1))

        d._engine.language = 0x10000
        check(
            "a change naming no language is the reader's own",
            spoken(d, [LangChangeCommand("de"), "Hallo und ",
                       LangChangeCommand(None), "back."]),
            [
                PP0,
                ("selectLanguage", 0x40000, 2),
                ("addText", b"Hallo und "),
                ("selectLanguage", 0x10000, 2),
                ("addText", b"back."),
                ("synthesize", True),
                ("selectLanguage", 0x10000, 2),
            ],
        )

        d._engine.language = 0x10000
        check(
            "a change to the language already being spoken is dropped",
            spoken(d, [LangChangeCommand("en_US"), "Hello."]),
            [PP0, ("addText", b"Hello."), ("synthesize", True)],
        )

        d._engine.language = 0x10000
        check(
            "a bare language matches the dialect the library has",
            spoken(d, [LangChangeCommand("de"), "Hallo."]),
            [
                PP0,
                ("selectLanguage", 0x40000, 2),
                ("addText", b"Hallo."),
                ("synthesize", True),
                ("selectLanguage", 0x10000, 2),
            ],
        )

        d._engine.language = 0x10000
        check(
            "a language the library has not leaves the voice where it was",
            spoken(d, [LangChangeCommand("fr_FR"), "Bonjour."]),
            [PP0, ("addText", b"Bonjour."), ("synthesize", True)],
        )
    finally:
        FakeEngine.languages = [0x10000]

    dictionary_checks()

    if FAILED:
        print("\nsequence: %d of the checks failed" % len(FAILED))
        return 1
    print("\nsequence: every check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

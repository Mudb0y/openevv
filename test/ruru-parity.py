#!/usr/bin/env python3
"""Compare Russian speech against the old voice on a local stand.

Pass two probes for phonemes and PCM, or use --dll with two Windows libraries
for PCM through the ECI API. Set EVV_RULEX to the reference's rulex.dat.
"""

import argparse
import ctypes
import multiprocessing
import os
import re
import subprocess
import tempfile
import wave
from contextlib import nullcontext
from pathlib import Path
from typing import NamedTuple

from lib.builtin import Reader, declare as declare_reader
from lib.langs import LOADER, Voice, declare, languages


RUSSIAN = 0x120000
PAUSE_MARKER = rb"`[012] (?=`\[)"
WORD_OR_BOUNDARY = re.compile(rb"`\[[^\]]*\]|" + PAUSE_MARKER)


class Speech(NamedTuple):
    pcm: bytes
    phones: bytes | None = None
    filtered: str | None = None
    wave_format: tuple | None = None


CASES = (
    "улица",
    "холодно",
    "доллар",
    "булок",
    "мыло",
    "дело",
    "сделать",
    "школу",
    "долина",
    "белая",
    "на",
    "на улице",
    "на учёбе",
    "по утрам",
    "что сегодня?",
    "триста",
    "в 19:45",
    "к 13",
    "с 20",
    "Улица Ленина. Цена $5.",
    "22 000 рублей. 1 000 000. 0 000. 22 00.",
    "Съешь же ещё этих мягких французских булок, да выпей чаю.",
)


def speak_probe(probe, text, work, env, rate, annotations):
    source = work / "case.txt"
    source.write_text(text, encoding="utf-8")
    wav = work / "case.wav"
    wav.unlink(missing_ok=True)
    phonemes = subprocess.run(
        [probe, "@case.txt", "phonemes.out", "pa" if annotations else "p"],
        cwd=work, env=env, check=True, capture_output=True, timeout=30,
    )
    reported = []
    reading = False
    prefix = b"speak: phonemes "
    for line in phonemes.stdout.splitlines():
        if line.startswith(b"speak: "):
            reading = line.startswith(prefix)
            if reading:
                reported.append(line[len(prefix):])
        elif reading:
            reported.append(line)
    if not reported:
        raise ValueError(f"{probe} returned no phonemes for {text!r}")
    mode = "w" if rate == 16000 else ""
    if annotations:
        mode += "a"
    audio = subprocess.run(
        [probe, "@case.txt", "case.wav", mode],
        cwd=work, env=env, check=True, capture_output=True, timeout=30,
    )
    if rate == 16000 and b"speak: wideband 1" not in audio.stdout.splitlines():
        raise ValueError(f"{probe} did not use the wideband voice")
    with wave.open(str(wav), "rb") as result:
        if result.getframerate() != rate:
            raise ValueError(f"{probe} returned audio at {result.getframerate()} Hz")
        pcm = result.readframes(result.getnframes())
        if not pcm:
            raise ValueError(f"{probe} returned no audio for {text!r}")
        return Speech(pcm, phones=b"".join(reported), wave_format=result.getparams())


def open_russian_library(path):
    library = LOADER(str(path))
    declare(library)
    library.eciSetParam.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    if RUSSIAN not in languages(library):
        raise ValueError(f"{path} has no Russian voice")
    return library


def filter_ssml(library, handle, text):
    pointer = ctypes.c_void_p

    class Attributes(ctypes.Structure):
        _fields_ = [("name", ctypes.c_char * 80), ("language", ctypes.c_int)]

    library.eciRegisterFilter.argtypes = [pointer, ctypes.c_int, pointer, pointer, ctypes.c_int]
    library.eciNewFilter.argtypes = [pointer, ctypes.c_int, ctypes.c_int]
    library.eciNewFilter.restype = pointer
    library.eciGetFilteredText.argtypes = [pointer, pointer, ctypes.c_char_p,
                                            ctypes.POINTER(ctypes.c_char_p)]
    entry = ctypes.cast(library.ssmlFilterGetObject, pointer)
    attributes = Attributes()
    if library.eciRegisterFilter(handle, 0, ctypes.byref(entry), ctypes.byref(attributes), 1):
        raise ValueError(f"{library._name} refused to register the SSML filter")
    filter_handle = library.eciNewFilter(handle, 0, 1)
    if not filter_handle:
        raise ValueError(f"{library._name} refused to create the SSML filter")
    document = ctypes.create_string_buffer(text.encode("utf-8"))
    result = ctypes.c_char_p()
    if library.eciGetFilteredText(handle, filter_handle, document, ctypes.byref(result)):
        raise ValueError(f"{library._name} refused the SSML document")
    return (result.value or b"").decode("utf-8")


def speak_library(library, text, annotations, ssml, phones):
    voice = Voice(library, RUSSIAN)
    try:
        if (annotations or ssml) and library.eciSetParam(voice.h, 1, 1) < 0:
            raise ValueError(f"{library._name} refused annotation input")
        filtered = filter_ssml(library, voice.h, text) if ssml else None
        if filtered != "":
            voice.speak(text if filtered is None else filtered)
        pcm = bytes(voice.said)
    finally:
        voice.close()
    if not pcm and filtered != "":
        raise ValueError(f"{library._name} returned no audio for {text!r}")
    reported = None
    if phones and filtered != "":
        declare_reader(library)
        reader = Reader(library, lang=RUSSIAN)
        try:
            if (annotations or ssml) and library.eciSetParam(reader.h, 1, 1) < 0:
                raise ValueError(f"{library._name} refused annotation input")
            reported = reader.read((text if filtered is None else filtered).encode("utf-8"))
        finally:
            reader.close()
    return Speech(pcm, phones=reported, filtered=filtered)


def speak_libraries(reference, candidate, text, annotations, ssml, phones):
    # The old DLL retains fade state after an instance is deleted. The caller
    # runs each case in a fresh process so the references start alike.
    try:
        old = speak_library(open_russian_library(reference), text, annotations, ssml, phones)
        new = speak_library(open_russian_library(candidate), text, annotations, ssml, phones)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    return old, new


def report_boundaries(phones):
    """Positions of ECI pause markers between words in a phoneme report."""
    words = 0
    boundaries = []
    for match in WORD_OR_BOUNDARY.finditer(phones):
        token = match.group()
        if token.startswith(b"`["):
            words += 1
        else:
            boundaries.append((words, token[1:2].decode("ascii")))
    return boundaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--cases", type=Path, help="UTF-8 file with one phrase per line")
    parser.add_argument("--rate", type=int, choices=(11025, 16000))
    parser.add_argument("--dll", action="store_true", help="compare PCM through the ECI API at 11025 Hz")
    parser.add_argument("--annotations", action="store_true", help="enable ECI annotation input")
    parser.add_argument("--ssml", action="store_true", help="filter each case as an SSML document")
    parser.add_argument("--phones", action="store_true", help="compare ECI phoneme and syllable reports with --dll")
    args = parser.parse_args()
    if args.dll and (os.name != "nt" or args.rate == 16000):
        parser.error("--dll requires Windows and uses 11025 Hz")
    if args.ssml and not (args.dll and args.cases):
        parser.error("--ssml requires --dll and --cases")
    env = dict(os.environ, EVV_LANGUAGE=hex(RUSSIAN))
    reference = args.reference.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    cases = CASES
    if args.cases:
        cases = tuple(line for line in args.cases.read_text(encoding="utf-8").splitlines()
                      if line.strip())
        if not cases:
            parser.error("--cases must contain at least one phrase")
    failed = 0
    workers = (multiprocessing.get_context("spawn").Pool(1, maxtasksperchild=1)
               if args.dll else nullcontext())
    with workers as pool, tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        for case in cases:
            try:
                if args.dll:
                    old, new = pool.apply_async(
                        speak_libraries, (reference, candidate, case, args.annotations, args.ssml, args.phones)
                    ).get(timeout=70)
                else:
                    old = speak_probe(reference, case, work, env, args.rate or 16000, args.annotations)
                    new = speak_probe(candidate, case, work, env, args.rate or 16000, args.annotations)
            except (OSError, ValueError, EOFError, wave.Error,
                    subprocess.SubprocessError, multiprocessing.TimeoutError) as error:
                print(f"error: {case} ({error})")
                failed += 1
                continue
            same = old == new
            failed += not same
            if same:
                print(f"match: {case}")
            else:
                print(f"difference: {case}")
                if old.filtered != new.filtered:
                    print("  filtered text differs")
                if old.phones != new.phones:
                    if old.phones is None or new.phones is None:
                        print("  phoneme report missing on one side")
                    else:
                        if re.sub(PAUSE_MARKER, b"", old.phones) == re.sub(PAUSE_MARKER, b"", new.phones):
                            print("  only pause markers differ in the phoneme reports")
                        else:
                            print("  phoneme or syllable report differs")
                        old_boundaries = report_boundaries(old.phones)
                        new_boundaries = report_boundaries(new.phones)
                        if old_boundaries != new_boundaries:
                            print(f"  boundary positions (after word, marker): {old_boundaries} / {new_boundaries}")
                if old.wave_format != new.wave_format:
                    print("  WAV format differs")
                if old.pcm != new.pcm:
                    print(f"  audio differs ({len(old.pcm) // 2}/{len(new.pcm) // 2} samples)")

    tested = "PCM at 11025 Hz" if args.dll else "phonemes and PCM"
    if args.dll and args.phones:
        tested = "phonemes and " + tested
    if args.ssml:
        tested = "filtered text and " + tested
    print(f"{len(cases) - failed}/{len(cases)} cases matched {tested}")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())

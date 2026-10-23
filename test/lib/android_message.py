#!/usr/bin/env python3
"""Exercise the Android crash report through a real ECI library in child processes.

This checks the engine on the host, not an Android device or the Kotlin pipeline.
Use --worker only through this script so a native crash cannot kill the runner.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

MESSAGE = """, APK archivoYT Music Premium v9.41.53 @blindtechnexus.apk, 113,8 MB
📱 YouTube Music Premium
Version: 9.41.53 (Mod)
Developer: Google LLC
Size: 113 MB

@blindtechnexus
Recibido el a las 9:03 PM
Visto 60 veces"""
LANGUAGES = [0x20000, 0x20001, 0x10000, 0x10001, 0x40000, 0x30000, 0x30001, 0x50000]


def worker(args):
    if os.name == 'nt':
        C.windll.kernel32.SetErrorMode(0x8003)
    dll = (C.WinDLL if os.name == 'nt' else C.CDLL)(str(Path(args.dll).resolve()))
    signatures = {
        'eciNewEx': (C.c_void_p, [C.c_int]),
        'eciDelete': (C.c_void_p, [C.c_void_p]),
        'eciSetParam': (C.c_int, [C.c_void_p, C.c_int, C.c_int]),
        'eciSetOutputBuffer': (C.c_int, [C.c_void_p, C.c_int, C.c_void_p]),
        'eciRegisterCallback': (None, [C.c_void_p, C.c_void_p, C.c_void_p]),
        'eciAddText': (C.c_int, [C.c_void_p, C.c_char_p]),
        'eciSynthesize': (C.c_int, [C.c_void_p]),
        'eciSynchronize': (C.c_int, [C.c_void_p]),
    }
    for name, (ret, params) in signatures.items():
        fn = getattr(dll, name)
        fn.restype, fn.argtypes = ret, params
    buffer = (C.c_short * 2048)()
    samples = 0
    digest = hashlib.sha256()
    callback_type = C.WINFUNCTYPE if os.name == 'nt' else C.CFUNCTYPE

    @callback_type(C.c_int, C.c_void_p, C.c_int, C.c_int, C.c_void_p)
    def callback(handle, message, count, data):
        nonlocal samples
        if message == 0:
            samples += count
            digest.update(bytes(buffer)[:count * 2])
        return 1

    handle = dll.eciNewEx(args.language)
    assert handle, 'eciNewEx failed'
    try:
        dll.eciRegisterCallback(handle, callback, None)
        assert dll.eciSetOutputBuffer(handle, len(buffer), buffer)
        dll.eciSetParam(handle, 1, 1)
        assert dll.eciSetParam(handle, 5, args.rate) != -1
        assert dll.eciSetParam(handle, 32, args.wideband) != -1
        # Match the Western input path from the reported Android message:
        # discard unsupported emoji and collapse whitespace before ECI. This
        # checks the engine, not Android text processing or user dictionaries.
        text = MESSAGE if args.fragment < 0 else MESSAGE.splitlines()[args.fragment]
        encoded = args.text_bytes.read_bytes() if args.text_bytes else ' '.join(text.encode('cp1252', errors='ignore').decode('cp1252').split()).encode('cp1252')
        for _ in range(3):
            assert dll.eciAddText(handle, encoded)
            assert dll.eciSynthesize(handle)
            assert dll.eciSynchronize(handle)
        assert samples > 0, 'No audio'
        print(json.dumps(dict(samples=samples, pcm_sha256=digest.hexdigest())))
    finally:
        dll.eciDelete(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--language', type=lambda value: int(value, 0), default=0x20001)
    parser.add_argument('--wideband', type=int, default=0, choices=(0, 1))
    parser.add_argument('--rate', type=int, default=16000)
    parser.add_argument('--text-bytes', type=Path, help='Exact Western-encoded input bytes instead of the fixture')
    parser.add_argument('--fragment', type=int, default=-1)
    parser.add_argument('--all', action='store_true', help='All eight reported Western languages, default and wideband voice')
    args = parser.parse_args()
    if args.worker:
        worker(args)
        return
    results = []
    languages = LANGUAGES if args.all else [args.language]
    for fragment in ([-1, 0] if args.all else [args.fragment]):
        for language in languages:
            for mode in (range(2) if args.all else [args.wideband]):
                command = [sys.executable, str(Path(__file__).resolve()), args.dll, '--worker', '--language', hex(language), '--wideband', str(mode), '--rate', str(args.rate), '--fragment', str(fragment)]
                if args.text_bytes:
                    command += ['--text-bytes', str(args.text_bytes)]
                try:
                    run = subprocess.run(command, capture_output=True, timeout=60)
                    result = dict(language=hex(language), wideband=mode, fragment=fragment, exit_hex=f'0x{run.returncode & 0xffffffff:08x}', stdout=run.stdout.decode('utf8', errors='replace'), stderr=run.stderr.decode('utf8', errors='replace'))
                except subprocess.TimeoutExpired:
                    result = dict(language=hex(language), wideband=mode, fragment=fragment, timeout=True)
                results.append(result)
                print(json.dumps(result, ensure_ascii=False), flush=True)
    report = dict(validation='Host ECI; no Android device or Kotlin pipeline', dll_sha256=hashlib.sha256(Path(args.dll).read_bytes()).hexdigest(), results=results)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    if any(row.get('exit_hex') != '0x00000000' for row in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()

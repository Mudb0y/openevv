#!/usr/bin/env python3
"""Check the upstream matrix hashes without per-case MSYS fork pipelines.

Uses the same probe, input files, modes, WAV concatenation and stdout filters
as test/matrix.sh. Never records or changes baselines. Intended for Windows
hosts where MSYS fork fails under memory pressure.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

LANGUAGES = dict(enus='0x10000', engb='0x10001', dede='0x40000', eses='0x20000', esus='0x20001', frfr='0x30000', frca='0x30001', itit='0x50000', plpl='0x110000', jajp='0x80000', fifi='0x90000', ptbr='0x70000')
CATEGORIES = dict(plain=('plain',''), utf8=('utf8',''), anno=('anno',''), anno3=('anno','anno'), realworld=('anno','ar'), dict=('plain','ard'), second=('plain','t'), ssml=('ssml','as'), wide=('plain','w'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('probe', type=Path)
parser.add_argument('languages', nargs='+', choices=list(LANGUAGES))
parser.add_argument('--engine', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--jobs', type=int, default=1)
args = parser.parse_args()
root = args.engine.resolve()
binary = args.probe.resolve()
out = args.output.resolve()
work = out.parent / (out.stem + '-cases')
work.mkdir(parents=True, exist_ok=True)
script = (root/'test/matrix.sh').read_bytes().replace(b'\r\n', b'\n')
assert b'CATEGORIES="plain utf8 anno anno3 realworld dict second ssml wide"' in script
for language, identifier in LANGUAGES.items():
    assert f'{language}) echo {identifier} ;;'.encode() in script, 'Upstream language mapping changed'


def digest(*parts):
    value = hashlib.sha256()
    for part in parts:
        value.update(part)
    return value.hexdigest()[:16]


def normalize_stdout(data):
    # grep emits a newline even when its final input line lacked one.
    lines = data.replace(b'\r', b'').split(b'\n')
    if lines and lines[-1] == b'':
        lines.pop()
    return b''.join(line+b'\n' for line in lines if not re.match(rb'^speak: [0-9]+ languages$|^speak:   language 0x|^log\[', line))


cases = []
for language in args.languages:
    baseline = {}
    for line in (root/'test/samples'/f'{language}.sha256').read_bytes().splitlines():
        if not line.strip() or line.startswith(b'#'):
            continue
        category, number, audio, said, text = line.split(b' ', 4)
        key = (category.decode(), int(number))
        assert key not in baseline, (language, key, 'duplicate baseline')
        baseline[key] = (audio.decode(), said.decode(), text)
    count = 0
    for category, (stem, mode) in CATEGORIES.items():
        filename = stem + ('' if language == 'enus' else '-'+language) + '.txt'
        number = 0
        for text in (root/'test/cases'/filename).read_bytes().split(b'\n'):
            if not text:
                continue
            number += 1
            expected_audio, expected_said, expected_text = baseline[(category, number)]
            assert text.rstrip(b'\r') == expected_text, (language, category, number, 'case text changed')
            cases.append((language, category, number, mode, text, expected_audio, expected_said))
            count += 1
    assert count == len(baseline), (language, count, len(baseline), 'missing cases')


def run_case(case):
    language, category, number, mode, text, expected_audio, expected_said = case
    directory = work / f'{language}-{category}-{number}'
    directory.mkdir(exist_ok=True)
    (directory/'case.txt').write_bytes(text)
    for name in ('case.wav', 'case.wav.again.wav'):
        (directory/name).unlink(missing_ok=True)
    env = dict(os.environ, EVV_LANGUAGE=LANGUAGES[language])
    command = [str(binary), '@case.txt', 'case.wav'] + ([mode] if mode else [])
    try:
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=60)
        wav = directory/'case.wav'
        again = directory/'case.wav.again.wav'
        audio = digest(wav.read_bytes(), again.read_bytes()) if wav.is_file() and wav.stat().st_size and again.is_file() and again.stat().st_size else digest(wav.read_bytes()) if wav.is_file() and wav.stat().st_size else 'nothing'
        said = digest(normalize_stdout(result.stdout))
        (directory/'said.txt').write_bytes(result.stdout)
        row = dict(language=language, category=category, number=number, audio=audio, said=said, expected_audio=expected_audio, expected_said=expected_said, returncode=result.returncode, moved=(result.returncode != 0 or audio != expected_audio or said != expected_said))
    except subprocess.TimeoutExpired:
        row = dict(language=language, category=category, number=number, moved=True, timeout=True)
    finally:
        # These are the two exact regular output paths supplied to the probe;
        # no recursive deletion or directory traversal is needed.
        for name in ('case.wav', 'case.wav.again.wav'):
            (directory/name).unlink(missing_ok=True)
    return row


results = []
with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
    for result in executor.map(run_case, cases):
        results.append(result)
        if result['moved']:
            print('MOVED:', json.dumps(result), flush=True)
        if len(results) % 100 == 0:
            print(f'matrix progress: {len(results)}/{len(cases)}', flush=True)
moved = sum(row['moved'] for row in results)
report = dict(passed=len(results)-moved, total=len(results), moved=moved, languages=args.languages, runner='Python host runner matching test/matrix.sh audio and stdout hashes', matrix_script_sha256=hashlib.sha256(script).hexdigest(), probe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), results=results)
out.write_text(json.dumps(report, indent=2), encoding='utf8')
for language in args.languages:
    rows = [row for row in results if row['language'] == language]
    print(f"{language}: {len(rows)} cases, {sum(row['moved'] for row in rows)} moved")
print(f'matrix: {len(results)} cases, {moved} moved', flush=True)
raise SystemExit(1 if moved else 0)

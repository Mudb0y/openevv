#!/usr/bin/env python3
"""Compile the real SSML code and platform task code against safety harnesses."""
import argparse
from pathlib import Path
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='cc')
parser.add_argument('--engine', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--output', type=Path)
args = parser.parse_args()
root = args.engine.resolve()
out = (args.output or root / 'build/release-safety').resolve()
out.mkdir(parents=True, exist_ok=True)
harness = Path(__file__).resolve().parents[1] / 'test/harness'
includes = ['-I' + str(root / folder) for folder in ['src/port', 'src/delta', 'src/eci/bridge', 'src/eci/ssml', 'src/eci/lang']]
common = [args.cc, '-O1', '-g', '-std=gnu99', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections', '-Wno-ignored-attributes', *includes]
ssml = ['eci_parserxml.c', 'eci_mbconvert.c', 'eci_xmltok.c', 'eci_xmltok_tables.c', 'eci_ipatospr.c']
for name, sources in [
    ('ssml-bounds', [harness / 'release_safety.c', *(root / 'src/eci/ssml' / source for source in ssml)]),
    ('task-lifetime', [harness / 'task_lifetime.c', root / 'src/port' / ('port_win32.c' if os.name == 'nt' else 'port_posix.c')]),
    ('dict-extract', [harness / 'dict_extract.c']),
]:
    binary = out / (name + ('.exe' if os.name == 'nt' else ''))
    command = [*common, *(str(source) for source in sources), '-Wl,--gc-sections', '-o', str(binary)]
    if name == 'dict-extract':
        command += ['-DEVV_ARENA=1', '-O0', '-fno-inline']
    if os.name != 'nt':
        command += ['-pthread']
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True, timeout=120)

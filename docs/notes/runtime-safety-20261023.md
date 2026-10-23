# Runtime safety regressions found while testing Android and Windows consumers

These changes address concrete invalid accesses, lifetime issues and ownership mistakes. They do not change the ECI interface, language data, recorded sample hashes or acoustic tables. They are based on public main at `d2bc0c68a7769967bb3dfdc86701f69bc53767b4`.

## Reported Android input

The following copied message caused the installed downstream Spanish TTS service to terminate:

```text
, APK archivoYT Music Premium v9.41.53 @blindtechnexus.apk, 113,8 MB
📱 YouTube Music Premium
Version: 9.41.53 (Mod)
Developer: Google LLC
Size: 113 MB

@blindtechnexus
Recibido el a las 9:03 PM
Visto 60 veces
```

The device was a Samsung SM-A065M running Android 16, using ARM64. Its original library faulted at address `0x44` in `extract`, called by dictionary lookup. Disassembly matched a null right fence indexed at field 17. This was reproduced through the original installed service, separately from the UTF-8 converter bug.

`extract` now returns no match for missing fences, invalid widths and missing statements instead of dereferencing them or dividing by zero. A small harness includes the actual helper with a minimal arena and statement table, checks a valid extraction, and exercises the invalid spans. The original helper faults on ARM64; the corrected helper passes.

`test/lib/android_message.py` retains the full message and first-line variant. It exercises a real ECI library in child processes, with the message encoded for the Western input path, three consecutive utterances per process, eight Western language variants, and both default and wideband voices at 16 kHz. It does not emulate Android preferences, the Kotlin text pipeline or TalkBack.

In the downstream application, real device instrumentation separately exercised 48 language/mode/input combinations using JNI and the application's text pipeline, then synthesized the full message and a following sentence through the installed TTS service with its current settings. The updated universal APK was installed without uninstalling the application, its installed SHA-256 was checked against the built APK, and the service test passed again. Those are downstream integration results, not proof that every configuration of public main or every Android device has been tested.

## Other corrections

* **Landing names:** both the bytecode interpreter and generated-rule helper save the translated `EVV_AT` address used by the jump path. An arena offset and its host address cannot be interchangeable lookup keys. This does not establish the separate claim of a per-rule leak in `evv_land_forget`, which takes an address range.
* **Task lifetime:** the creator receives the OS thread identifier into local storage. Windows closes its local creation handle; the child frees only its opaque identity. POSIX receives `pthread_t` into creator-owned storage. A child returning before the creation call finishes must not free the target of that call's output write. The stress harness runs 10,000 immediately returning tasks with real OS scheduling; it supplies only frame cleanup, because the harness allocates no rule frames.
* **SSML/UTF-8:** check a whole continuation sequence before emitting it and stop before reading past a truncated string's NUL; reserve a terminator after four-byte numeric references; reject negative, inconsistent or unrepresentable input lengths before sizing conversion. Bound buffer growth and keep old text buffers intact if any replacement allocation fails. The harness uses inaccessible guard pages and the actual scanner/parser.
* **Dictionary ownership:** bound active family/dialect indices and free only the owned converted lookup key after the synchronous dictionary call, including its error result.
* **Klatt filename ownership:** allocate the replacement first, then release the old name; preserve the old name on allocation failure.
* **PCM resampling:** use a 64-bit phase intermediate and rebase it before retaining the 32-bit phase. This was already present in the downstream engine; the patch here applies it to public main's implementation.
* **Japanese English-word dictionary:** check the actual generated pointer count before indexing its buckets. The hash can reach beyond the 105 generated entries. On Windows x64 the adjacent count was interpreted as a pointer (`0x69`). This was the source of the six failures in `anno:5,7`, `anno3:5,7` and `realworld:5,7`, rather than the landing mismatch. Existing sample hashes remain unchanged.

The normal SSML entry path already holds the global lexer mutex. No second mutex is added. No blanket replacement of `abort`, change to Klatt's ceiling, file append position or language dictionaries is included. Code execution or the audit's proposed critical exploitability has not been demonstrated.

## Reproduction

The focused tests require Python and a GCC/Clang-style compiler:

```sh
python tools/check-release-safety.py --cc cc
python tools/check-resampling.py --cc cc
```

On Windows use a compatible MinGW compiler on PATH, for example `--cc x86_64-w64-mingw32-clang`. Outputs default to `build/release-safety`. `--engine PATH` selects another checkout's actual sources for before/after comparison; the harnesses themselves come from the checkout running the script.

Build a library and probe using the usual targets. To exercise all current languages in the bytecode Windows build:

```sh
make win-probe win-dlltest RULES=bytecode \
  LANGS="lang/enus lang/engb lang/dede lang/eses lang/esus lang/frfr lang/frca lang/itit lang/plpl lang/jajp lang/fifi lang/ptbr"
python test/lib/android_message.py build/eci.dll --all --output build/android-message.json
python tools/check-matrix-host.py \
  build/probe-enus-engb-dede-eses-esus-frfr-frca-itit-plpl-jajp-fifi-ptbr.exe \
  enus engb dede eses esus frfr frca itit plpl jajp fifi ptbr \
  --output build/matrix-host.json
```

The Python matrix runner is a Windows host fallback for MSYS fork failures under memory pressure. It uses the original case files, category modes, recorded audio/API hashes, and stdout normalization. It rejects nonzero exits and timeouts and cannot write baselines. It does not replace the six build configurations required by `CLAUDE.md`; coverage must identify which probe, platform and rules implementation actually ran. The PR records those results and the configurations still needing validation.

The resampler test uses the actual implementation for 50,000-sample blocks, four interpolation methods, six output rates, block continuity, PCM bounds, DC gain and wideband downsampling. The Linux CI job also runs both focused test scripts. On the Windows validation host, archiving all twelve languages exceeded the process command-line limit; an LLVM response file containing the 293 freshly compiled engine objects was used to create the archive before continuing the normal make link targets. No build failure is counted as a pass.

On this branch, the Windows x64 bytecode probe built with LLVM MinGW / Clang 23.1.2 passed all 1,291 recorded cases across twelve languages with no baseline changes and no nonzero exits. The real ECI library passed 32 message/first-line/language/voice combinations and three forms of DLL shutdown with identical PCM. The three focused safety harnesses and the large-block resampler test also passed. Both original public-main SSML and dictionary-helper regressions exited with `0xc0000005` before the corrections. Linux CI and the remaining five full build configurations are still unverified here.

Historical downstream checks passed 1,081 Windows x64 bytecode cases for ten languages and 871 Windows x64 generated-C cases for eight languages. Windows x86 DLL synthesis and delete/live/unload checks also passed. These do not establish a complete current-main, six-configuration pass. Automated downstream controller tests used a simulated NVDA API. The user subsequently reported perceiving faster execution under real NVDA; this is a qualitative user report, not a latency benchmark or an end-to-end accessibility certification. Only 32 sampled crasher cases were completed; the full 20,548-case sweep was interrupted and is not reported as passing.

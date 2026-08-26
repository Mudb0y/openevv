# What is left of IBM's code

Measured 26 August 2026, by counting symbols rather than by reading. Every object in `analysis/enus` was asked what it defines, and each name held against the 9,407 our build defines and the 924 mangled names the `ALIAS` lines in `src` claim. Sizes are the code sections those functions sit in.

Nothing here is needed to build or to speak. `make missing` answers nothing, and has since the port finished; this is the reach of the machine and the features of the interface, not the engine. What it is for is deciding what to read next, and what never to read at all.

The 208 objects fall into four kinds. Fifty-five are a language's rules and are lifted as data rather than ported -- `ea_`, `e_`, `es_`, `et_`, `u_`, `us_`, `ut_`, `glob` and `link`, which `lang/enus` and its siblings already hold. Eight more, the `ed_` set, are empty stubs of 467 bytes. Most of the rest are ported. What follows is the remainder.

## The Delta machine's unused reach

The machine's own seventeen objects -- `access`, `assign`, `ctxt`, `ddelta`, `delta`, `dttime`, `extra`, `for`, `gener`, `heap`, `mem`, `misc`, `optimize`, `pointer`, `stack`, `test` and `optbl` -- define 444 names between them, and 139 of those had no answer here. They were absent for one reason: no rule in any of the nine languages IBM shipped calls them, so the link never asked. One of the 139 turned out to be ported already under an adapted name, `vinitloc` as `vinitloc_new`, which leaves 138.

This is the only part of the remainder that can block work rather than merely limit it. A Polish rule written in the upper form that wants an operation from this list will not link, and the fix is to read that function out of IBM's object.

Sixty are now written, and `test/prims.sh` is what proves them: the same table of cases compiled twice, once against our engine and once against IBM's objects, comparing what each call leaves behind. `docs/building.md` says what that harness reaches and what it does not.

Done, by object. All seven of `assign`: `settvar_l`, `settvar_lng` and `settvar_v`, with `assok`, `noass`, `chkvars` and `chkokass`, the last two being empty in the original and their argument count therefore not recoverable from it. All seven of `stack`: `npush_l`, `ncompare`, `back`, `back_nboa`, `bsclr_pushca`, `bspush_vbot` and `bspop_vbot`. All thirteen of `pointer`, which is the right register's half of the loads and the two ends -- `rpta_loadi`, `rpta_loadl`, `rpta_loadv`, `rpta_mover`, the four `tst` forms, `lpta_loadlng`, and `leftmost` and `rightmost` for both registers. Six of `mem`, which is every arithmetic operation the machine has beyond addition: `vsub`, `vmult`, `vdiv`, `vnegate`, with `divzero` and `vcompareTypeCheck`. Nine of `optimize`: `if_testle_v_i`, the six `_lng` forms of the fused test-and-branch, `move_lng` and `testneq_tvars`. Ten of `test`, which is all of it but one: the four bare orderings, `test_eof`, `test_hasval`, the two wide string tests `test_string_l` and `test_string_lng`, and `test_time` and `test_fence`. Eight of `access`: `num_fields_in_stream`, `left_context`, `right_context`, `allow_left_ctxt`, `allow_right_ctxt`, `init_stream`, `divide_time` and `project_sync`, the last three of which the harness cannot reach and which are read rather than compared.

Three things that came out of reading rather than being expected. The immediate loads into the right pointer register ask the statement table what kind the *left* register's field is -- 0x44 where 0x54 was meant. It is a slip in the original and it cannot show, because both arms of the switch write the same thing; it is carried here deliberately, and the harness has a case where it could show. All four `settvar` entry points compile the same body: none of them looks at the width its name announces. And `allow_left_ctxt` and `allow_right_ctxt` are one body as well -- the right one follows the same word of the node as the left one does, which is what its own code says rather than a slip in reading it.

One thing the harness turned up that is not settled. `divide_time` is a guard and a call into `vsplit_time`, which was ported long ago and which every case in the suite exercises; on the one position this harness can offer it, IBM's own `vsplit_time` faults where ours answers. No case in the suite reaches that corner, so which of the two is right is not known, and a harness that cannot get past the fault cannot find out.

Seventy-eight are left, by object.

`misc` has twenty-one, and most of them are not the machine: `c_code`, `call`, `call2`, `execcmd`, `startcmd`, `startstmt` and its two variants, `tag` and its two, `goto_1`, `abort_1`, `halt`, `noop1`, `nullines`, `code_end` and `etiwinMain`, which is the program's entry where we have the library's.

`delta` and `gener` between them hold the generate family, seventeen calls: `generate`, `vgenerate`, `vgen_frame`, `vgen_params`, `vgen_time`, `vgen_copy`, `gen_copy`, `gencur_framedur`, `gencur_params`, `gencur_timestm`, `gendef_framedur`, `gendef_params`, `gendef_timestm`, `merge`, `insert_2pt`, `mark_l` and `mark_lng`.

`ddelta` has ten, among them `SETCTXL`, `SETCTXR`, `CLRNONSEQ`, `TVFLDS`, `mapsyncs`, `vclrctxt`, `visnonseq`, `vmergable` and `vredoctxt`. `access` has six left, and they are its longest: the field-value enumerator `first_fieldval` and `next_fieldval`, `valid_prefix` and `valid_prefix_char`, `unique_value`, and `merge_sync` with the static `safe_mergable` under it. `test` has one, `test_string`, whose first byte is a marker saying how wide the tokens after it are and which therefore carries all four decodes at once. `for` has five loop forms: `forall_adv_over_l`, `forall_adv_upto_l`, `forto_adv_over_l`, `forto_adv_over_r` and `for_cont_from`. `heap` has `peekDeltaStackStart`, `peekDeltaStackNext`, `resetDeltaStack` and the two heap-object allocators. `dttime` has the five of the `val_expr` chain that `src/eci_deltamisc.c` says why it left out. `optimize` has four, all of them the long or location form of an insert: `insert_2pt_l`, `insert_2pt_lng`, `ins_tokens_l` and `ins_tokens_lng`, and the last two are a real transcription rather than a twin -- a token there is two bytes, seven bits of the first shifted and the second beneath it, and the range is deleted before it is written. `ctxt` has `vctxtinit`, `extra` has `prt_tvar` and `set_saved_ptrs`, and `optbl` has `ins_rdtoks`.

## Japanese

Unchanged and written up in full in `docs/japanese.md`: about 163 KB across thirty objects, a morphological analyser rather than a table, with the 2.67 MB dictionary already lifting in one command and an oracle that builds and speaks. It is the largest genuine reverse-engineering job in the tree and the only language work outstanding.

## SSML, XML and IPA

About 76 KB across sixteen objects, and none of it runs on the path this port takes: the filter manager in `src/eci_managers.c` reports nothing present and nothing active, so annotations reach the engine directly.

`ssmlprocessor.obj` is 24 KB of tag handlers, `ssmlstate.obj` 5 KB of push, pop and validate over stacks of voice, prosody, emphasis, language and audio, `ssmlmap.obj` 7 KB mapping SSML's values onto IBM's numbers, including dates, times, currency and Roman numerals, with `ssmlsayas`, `ssmlintstack`, `ssmllangstack` and `ssmlstrstack` beside them. `filter.obj` is the base class every filter inherits, 191 bytes of it. `xmltok.obj` is 10 KB and is a lex-generated scanner, so transcribing it means reproducing generated code; writing a small parser to the same interface is the better answer if this is ever wanted. `parserxml.obj` is the 657-byte shell around it.

Two pieces separate cleanly. `win_ipatospr.obj` is 21 KB on its own, converting IPA to the SPR phoneme spelling an annotation uses, with one converter per language for US and British English, French, German, Japanese and Korean, plus UTF-8 and UCS-32 conversion. Only `ssmlprocessor` calls it, so it can be had without the rest, and it is the piece of this block most useful to a language being written. `win_languageid.obj` and `langid.obj` are the LanguageId class, 3.5 KB of bookkeeping we already do our own way.

## The public calls with no wrapper

`eci.obj` defines 130 names and we answer 68. About 14 KB of what is left is the flat C entry points: `eciDictFindFirst`, `eciDictFindNext`, `eciDictLookup` and `eciUpdateDict`, each in a plain and a wide form, with `fgetUCS2` under them; the seven filter calls, which have nothing to be a wrapper for until the filter block above exists; and `eciGeneratePhonemes`. The machinery beneath the dictionary calls is already ported -- `es_engsynDictFindFirst` in `src/eci_engsyn.c`, `eciUpdateDict` in `src/eci_api2.c` -- so those are glue rather than transcription. `win/eci_api.c` says so in its own head, and a caller asking for one of them gets nothing rather than something wrong.

## What not to transcribe

The live audio device layer. `ealaudio.obj` is 10 KB of buffer queue and callbacks, with the playback half of `soundfil.obj`, `audiocvt.obj` at 6 KB, `alaw.obj`, `mulaw.obj`, `sample.obj`, `audsize.obj` and `win_sleepcyc.obj` -- about 21 KB in all. `src/eci_pcm.c` is already the boundary where these stood, and says why: below it the original opened a Windows waveform device, and none of that survives a port. PipeWire belongs there, not in a transcription. Worth knowing that `eci.obj` and `fmtinit.obj` do reference the `eal` names, so the output-device path is where that seam shows.

The concatenative manager, 12 KB. `ConcatenationManager` loads a DLL through `LibraryLoader` and drives the Torrent engine, whose data this SDK never shipped. It belongs to the other engine; this extraction runs the formant one.

The Delta debugger. `prdelta.obj` is 11 KB drawing the spine as characters, `rectbuf.obj` 2.6 KB of the record buffer under it, with `io.obj`, and `plot.obj`, `profile.obj` and `debug.obj`, which are 8, 22 and 10 bytes. `src/delta_trace.c` already does the useful part of that job.

The skip-list translation store: `win_skipstore`, `win_skiplistnode`, `win_listnode`, `win_arraylistnode`, `win_key` and `win_translation`, with `win_crypt` and `win_cryptstdio` beside them, 11.5 KB. Nothing else in the whole object set references any of it, which was checked rather than assumed. It is IBM's compiled and encrypted dictionary format, dead in this build, and worth reading only to read or write a stock `.dct` file.

The rest is odds: `win_iniwrite` at 4 KB, wanted only by `engreg` for writing settings back; `win_vcinfo` at 4.6 KB, which asks whether ScanSoft voices are installed; `win_hz2st`, `win_stack`, the endian readers in `util.obj`, `checkExpiration` in `expire.obj` and the copyright string in `version.obj`, which nothing calls.

## The other axis

None of the above is what makes the tree IBM's. That is data, and it is already transcribed: `lang/enus` and its seven siblings, and `src/klatt_tables.c`, which `tools/gen-tables.py` writes out of `clsyn.obj` and which is what the engine sounds like. `NOTICE` says whose is whose. Replacing that is what `lang/plpl` and `make census` are for, and the day it is finished the voice stops being Eloquence, which is the point of it rather than a cost.

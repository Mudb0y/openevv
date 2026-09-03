# A language module as text

Everything a language module holds now has a form a person can write, and each form regenerates the file the build compiles, byte for byte, for all eight languages.

The rules are `lang/<tag>/rules`, in the two forms `docs/rules.md` describes. The words are `<tag>.dict`. The five beside them are `<tag>.globals` for the variables the machine declares, `<tag>.settings` for the settings the engine carries in its image, `<tag>.statements` for the statement table it is parameterised by, `<tag>.sets` for the lookup sets and the dictionary's actions, and `<tag>.consts` for the bytes the rules name by address. `make tables-check` writes the C out of each and holds it against the tree: 45 of 45 across nine languages, and it wants neither Wine nor IBM's objects.

Each of the five has one writer, shared by the lifter that reads IBM's objects and the reader that reads the text, so the two cannot drift into formatting differently. And nothing in a text is stated twice: the variables are runs of kinds and where each lands is walked from them exactly as `delta_new` walks it, the statement table's readers and writers are an offset and a width whose names follow the order of the fields, and the settings' language number is the section that names it.

Two measurements worth keeping. Every one of the eight languages declares ten statement types, with 57 fields in Italian and both Spanishes, 58 in the two Englishes, 61 in German, 63 in Canadian French and 65 in French. And the sets are where the size of a language sits: English declares 511 of them and 28 dictionary actions over 274 kilobytes of entries, where Italian declares 153 and 13 over 77.

The sets' text is written out of the C in the tree rather than out of IBM's objects, and that is deliberate: the dictionary's arrays in that file are laid down by `tools/module/dict.py` out of the words, so the objects hold what the dictionary said before anything was added. Writing the text from the tree means the text carries the words as they stand.

That work also found that `tools/module/sets.py` had not been able to write the file it generates for some time. The copy in the tree had been brought to the arena's forms during the sixty-four bit port and the tool had not, and its comment about the stores had gone stale with it. English's file had the newer forms and the other seven the older; the tool now writes English's and the seven have been brought into line, ten lines each with no data touched.

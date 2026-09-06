# Heteronyms, and where the fix has to go

A heteronym is a word spelled one way and said two, the reading decided by the grammar around it: the RECord you keep against the reCORD you make. The engine does this, and does it well -- `read` is RED after `have` and REED after `will`, `wind` is WIND after `the` and WYND after `will`, and `dove`, `bow` and `record` all turn correctly on the same cues. It was reported as a feature EVV lacks and ETI Eloquence 6.1 has. That is not what is wrong.

`tools/measure/heteronyms.py` measures it by asking the engine rather than by reading a dictionary, which was the mistake the first time round: `record` is in neither homograph table and behaves perfectly. Each word goes into two frames that force opposite readings and the two answers are compared. **71 of 88 distinguish their readings; 17 do not.** The stress pairs are 46 of 55, the `-ate` words 14 of 15, and the vowel changes the weak one at 11 of 18.

The seventeen: decrease, digest, export, impress, proceed, produce, transfer, transplant, transport; bass, house, number, resume, row, sow, wound; and initiate.

## Why the dictionary cannot fix them

The two readings live in the dictionary as an `or` pair -- `produce ... says p r o d u s or p r Xx d u s` -- but the pair is data and the *choosing* is compiled code. `tools/module/dict.py` refuses a two-reading entry in as many words, and the dictionary's own header says which of the two is taken "is decided in code the other words using that test run through".

Reading the arms says how deep that goes. A heteronym arm is nothing like the three lines an ordinary word needs. `house` in `roots2` advances tokens and matches particular string constants; `produce` in `homog_roots` runs a chain of tests and branches. The eleven arms that use the general `test_noun_verb` name no records at all: they hand it two frame addresses and identify the word by an immediate, so the readings are reached through shared code keyed by that number. Adding one means allocating an immediate and extending whatever it indexes, not copying a shape.

So this is per-word rule authoring inside a rule with 779 arms, and for seventeen words it is the wrong price.

## Where it should go instead

Above the engine, as text. `` `[ `` encloses a pronunciation in the engine's own alphabet, and with the annotation input type on the engine honours it:

    the produce is fresh          .0DX  .0prx.1dyus  .0Xz  .1frES
    the `[.1pro.0dus] is fresh    .0DX  .1pro.0Fus   .0Xz  .1frES

The first is the verb, which is wrong there; the second is the noun. So a layer that recognises the word, decides from the surrounding grammar, and writes the annotation fixes every one of the seventeen without touching a rule. The filter interface `eciRegisterFilter` publishes is where it belongs, since a filter serves every caller rather than one.

## The readings, checked against the engine

Each of these was written as an annotation and read back to confirm the engine accepts it and says what it was told. The form the word already has is on the left, the one it lacks on the right.

    decrease    .0dX.1kris    verb        .1di.0kris      noun
    digest      .1dY.2JEst    noun        .0dX.1JEst      verb
    export      .1Ek.0spcrF   noun        .0Ek.1spcrt     verb
    produce     .0prx.1dyus   verb        .1pro.0dus      noun
    transfer    .1trAns.0fR   noun        .0trAns.1fR     verb
    transplant  .1trAnz.2plAnt noun       .0trAnz.1plAnt  verb
    transport   .1trAn.0spcrF noun        .0trAn.1spcrt   verb

Two things to know before writing more of these. A final `t` comes back as `F`, which is the engine's own choice of allophone and not something an annotation should try to spell. And the cues the engine's own test uses are the immediately preceding word -- `the`, `a` and prepositions for the noun, `will`, `to`, `must` and a subject pronoun for the verb -- which is why "the world record" comes out as the verb: `world` is neither. Any layer written here will have the same blind spot, and matching the engine's behaviour is worth more than beating it.

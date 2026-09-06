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

## Off unless asked for, and why that was not the first answer

The filter was going to install itself when an instance was made, so that a
caller which had never heard of it got the right reading anyway. Measurement
killed that.

Loading any filter turns annotation reading on for the whole instance --
`eciGetParam(eciInputType)` goes from 0 to 1 the moment it loads, since a
filter that writes annotations needs them read. Every backtick in the
caller's own text is then interpreted, and nothing can protect it:

    a `` here.        vanishes entirely
    a `vs50 here.     silently changes the voice
    a `x here.        spoken as "backquote x", harmless
    a \` here.        spoken as "backslash backquote"

`docs/api.md` claimed a backslash before a backtick gives a literal one. It
does not, and that is corrected there now.

So the two failures fail differently, and that decides it. A mis-stressed
`produce` is wrong and still intelligible: the word is recognisable and at
worst it grates. A swapped voice or a swallowed character is wrong in a way a
listener cannot detect, and undetectable is the worse class for a screen
reader, whose whole contract is that what is heard is what is there. The gain
fires on nine words standing behind a determiner or a modal; the loss fires on
backticks, which are constant in code and in Markdown.

`EVV_HETERO=on` installs it, and a caller can register it itself with
`hetero_getFilterObject` as the entry. The caller is the right place for the
decision because it is the only thing in the stack that knows whether it is
reading prose or a program, and a screen reader already tells those apart.

Two things not done, deliberately. Stripping the caller's backticks would
trade a silent misreading for a silently missing character, which is not an
improvement and makes the filter destructive to text it does not understand.
And shipping it on in the hope that the backtick case is rare is not
available: a fault nobody can hear is not one that gets reported and fixed.

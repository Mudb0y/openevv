/* Where the romanizer's tables are, and nothing more.
 *
 * Twenty-six accessors over the tables lifted out of IBM's own dictman.obj by
 * tools/lift-romtables.py: the hashes that index four dictionaries, the
 * penalty and phrase vectors the path search scores with, the number and
 * reading tables, the variant-kanji tables, and the two substitution tables
 * that make romaji out of English and kana out of romaji. Six hundred bytes of
 * x86 over sixty thousand bytes of data, which is why the data is lifted and
 * only this is written.
 *
 * Every one of them is an index into a table with no bound test, which is the
 * original's arrangement: the caller knows the range because the table it came
 * out of says so. test/romprims.sh sweeps each accessor over the whole range
 * its table can answer for and holds every answer against IBM's own.
 *
 * What the tables hold is not decided here and does not need to be. The
 * strides are: three bytes to an accent entry, four to a row of the TG table,
 * six to a number-reading entry, and two to a hash slot of most kinds. Those
 * come from the original's own arithmetic rather than from a header.
 */

#include "jprom.h"

/* The two substitution rules, which are what EngRulesInit fills in. Each is
   eight tables and a count: where a match starts, what it becomes, what is
   left over, the three positions those correspond to, and the accent value
   and position. The counts are the original's own constants rather than the
   two count symbols beside the tables, which it does not read. */
DictManRules dm_EngToRomanRule;
DictManRules dm_RomanToKanaRule;

/* The supplement dictionary, which is one blob of the static dictionary and
   one index into it. */
const uint8_t *dm_paUserDict;
const uint8_t *dm_paUserDictIdx;

void dm_EngRulesInit(void)
{
    dm_EngToRomanRule.from = jajp_s_szFromStringOfEng2Roman;
    dm_EngToRomanRule.to = jajp_s_szToStringOfEng2Roman;
    dm_EngToRomanRule.remain = jajp_s_szRemainStringOfEng2Roman;
    dm_EngToRomanRule.fromPos = jajp_s_anFromPositionOfEng2Roman;
    dm_EngToRomanRule.toPos = jajp_s_anToPositionOfEng2Roman;
    dm_EngToRomanRule.remainPos = jajp_s_anRemainPositionOfEng2Roman;
    dm_EngToRomanRule.accentValue = jajp_s_anAccentValueOfEng2Roman;
    dm_EngToRomanRule.accentPos = jajp_s_anAccentPositionOfEng2Roman;
    dm_EngToRomanRule.count = 0x3ca;

    dm_RomanToKanaRule.from = jajp_s_szFromStringOfRoman2Kana;
    dm_RomanToKanaRule.to = jajp_s_szToStringOfRoman2Kana;
    dm_RomanToKanaRule.remain = jajp_s_szRemainStringOfRoman2Kana;
    dm_RomanToKanaRule.fromPos = jajp_s_anFromPositionOfRoman2Kana;
    dm_RomanToKanaRule.toPos = jajp_s_anToPositionOfRoman2Kana;
    dm_RomanToKanaRule.remainPos = jajp_s_anRemainPositionOfRoman2Kana;
    dm_RomanToKanaRule.accentValue = jajp_s_anAccentValueOfRoman2Kana;
    dm_RomanToKanaRule.accentPos = jajp_s_anAccentPositionOfRoman2Kana;
    dm_RomanToKanaRule.count = 0xd4;
}

/* The first entry of each supplement blob, which is where the whole of it
   starts. */
void dm_InitSupplementDictionary(void)
{
    dm_paUserDict = jajp_s_apszSuppD[0];
    dm_paUserDictIdx = jajp_s_apszSuppI[0];
}

/* ---- the function-word dictionaries -------------------------------- */

const uint8_t *dm_GetFuncDict(void)
{
    return jajp_s_aFuncWordDict;
}

const uint8_t *dm_GetFuncDictEx(void)
{
    return jajp_s_aFuncWordDictEx;
}

/* ---- what the path search scores with ------------------------------ */

const uint8_t *dm_GetAccentAt(uint16_t i)
{
    return jajp_s_aAccentTable + (uint32_t)i * 3;
}

uint8_t dm_GetKakariAt(uint16_t i)
{
    return jajp_s_aKakariTable[i];
}

uint8_t dm_GetPhrVectorAt(uint16_t i)
{
    return jajp_s_aPhrVectorTable[i];
}

uint8_t dm_GetPenaltyAt(uint16_t i)
{
    return jajp_s_aPenaltyTable[i];
}

uint8_t dm_GetTGAt2(uint8_t row, uint8_t col)
{
    return jajp_s_aTGTable[(uint32_t)row * 4 + col];
}

const uint8_t *dm_GetTGAt(uint8_t row)
{
    return jajp_s_aTGTable + (uint32_t)row * 4;
}

/* ---- readings, phrases and numbers --------------------------------- */

const uint8_t *dm_GetYomiDataPtr(void)
{
    return jajp_s_aYomiDataTable;
}

const uint8_t *dm_GetPhraseDataPtr(void)
{
    return jajp_s_aPhraseDataTable;
}

const uint8_t *dm_GetNumberDataPtr(void)
{
    return jajp_s_aNumberDataTable;
}

uint8_t dm_GetNumMDAt(uint16_t i)
{
    return jajp_s_aNumMDTable[i];
}

/* Six bytes to an entry. */
const uint8_t *dm_GetNumYomiPtrAt(uint8_t i)
{
    return jajp_s_aNumYomiTable + (uint32_t)i * 6;
}

uint8_t dm_GetNumJMDAt(uint16_t i)
{
    return jajp_s_aNumJMDTable[i];
}

const uint8_t *dm_GetNumJMDPtr(void)
{
    return jajp_s_aNumJMDTable;
}

uint8_t dm_GetNumJCCAt(uint16_t i)
{
    return jajp_s_aNumJCCTable[i];
}

/* ---- the dictionary hashes ----------------------------------------- */

const uint8_t *dm_GetNDictHashAt(uint16_t i, uint8_t j)
{
    return jajp_s_aHash4NDict + (uint32_t)i * 4 + j;
}

const uint8_t *dm_GetTDictHashAt(uint16_t i)
{
    return jajp_s_aHash4TDict + (uint32_t)i * 2;
}

const uint8_t *dm_GetKDictHashAt(uint16_t i)
{
    return jajp_s_aHash4KDict + (uint32_t)i * 2;
}

uint8_t dm_GetKNDictHashAt(uint16_t i, uint8_t j)
{
    return jajp_s_aHash4KNDict[(uint32_t)i * 3 + j];
}

uint8_t dm_GetKTDictHashAt(uint16_t i, uint8_t j)
{
    return jajp_s_aHash4KTDict[(uint32_t)i * 2 + j];
}

const uint8_t *dm_GetEDictHashAt(uint16_t i)
{
    return jajp_s_aHash4EDict + (uint32_t)i * 2;
}

/* ---- variant kanji ------------------------------------------------- */

uint8_t dm_GetItaijiHashAt(uint16_t i, uint8_t j)
{
    return jajp_s_aItaijiHashTable[(uint32_t)i * 2 + j];
}

/* 0x7a6 bytes to a row, which is 979 entries of two bytes. */
const uint8_t *dm_GetItaijiAt(uint8_t row, uint16_t i)
{
    return jajp_s_aItaijiTable + (uint32_t)row * 0x7a6 + (uint32_t)i * 2;
}

/* The user dictionary IBM could load from a file. Its loader is in the
   registration half this port retired, so both stay null for ever and
   DictSearch::Do's test for them never passes. */
const uint8_t *dm_s_paUserDict = NULL;
const uint8_t *dm_s_paUserDictIdx = NULL;

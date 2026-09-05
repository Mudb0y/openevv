/* The romanizer's converters, ours against IBM's, one call at a time.
 *
 * test/harness/romcan.sh cannot reach these. It proves the engine below the romanizer
 * by replaying what IBM's romanizer answered, and a class the romanizer reaches
 * for itself is never called on that path at all -- the codeset conversion is
 * exactly that. So this is the same arrangement test/harness/prims.c uses for the
 * machine's primitives: one file compiled twice, once against our romanizer and
 * once against IBM's own objects, both printing the same lines for the same
 * sweep, and test/harness/romprims.sh diffing them.
 *
 * What is swept is every input there is. Shift-JIS to UCS-2 takes every single
 * byte and every two-byte pair the converter accepts, which is about eleven and
 * a half thousand; UCS-2 to Shift-JIS takes all sixty-five thousand code
 * points, twice, because the backslash is treated differently depending on the
 * third argument. Nothing is sampled and nothing is left to a case somebody
 * thought of.
 *
 * DictMan is swept the same way: each of its twenty-six accessors over the
 * whole range of the table it reads. An accessor that answers a pointer cannot
 * be compared across two processes, so what is printed is how far that pointer
 * moved from index nought -- which is the stride -- and the bytes it points at,
 * which is the data. That means the sweep holds the lifted tables to IBM's own
 * as well as the arithmetic that indexes them.
 *
 * Four bytes are left out of the first sweep and the reason is not squeamish.
 * 0x80, 0xfe and 0xff reach the end of IBM's chain of tests without its walk
 * advancing over them, so its converter loops on the same byte for ever;
 * sweeping them would hang IBM's side rather than say anything. And a pair
 * beginning 0xfd is one IBM's converter accepts and its table does not hold,
 * so it answers with whatever the linker put after that table -- which is what
 * this sweep found, and it is not an answer that means anything. Both are
 * deliberate differences and both are written down in
 * rom/jajp/unicodeconvt.c.
 *
 * usage: romprims
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_abi.h"

#ifdef EVV_ROMPRIMS_OURS
#include "jprom.h"
#endif

void evvRunStaticInitialisers(void);
#ifdef EVV_ROMPRIMS_OURS
void evv_port_start(void);
void evv_port_finish(void);
#endif

/* How the two sides are reached. Ours are C functions over structs of our own;
   IBM's are C++ methods over objects of a size only its own code knows, so
   the room is stated here and the constructors are called by their mangled
   names. */
#ifdef EVV_ROMPRIMS_OURS

typedef RomInstParam Param;
typedef UnicodeConverter Conv;

static Param param_room;
static Conv  conv_room;

static Param *makeParam(const char *path)
{
    return rp_ctor(&param_room, path);
}

static Conv *makeConv(Param *p)
{
    return uc_ctor(&conv_room, p);
}

#define TO_UCS2(c, in, out)      uc_MBCSToUCS2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   uc_UCS2ToMBCS((c), (in), (out), (f))

#include "eci_key.h"

#define IBM_KEY_ROOM   sizeof(Key)
#define IBM_TRANS_ROOM sizeof(Translation)
#define IBM_LIST_ROOM  sizeof(SkipList)

#define ibm_keyCtor(self, b, n)     key_ctor((Key *)(self), (b), (n))
#define ibm_keyDtor(self)           key_dtor((Key *)(self))
#define ibm_keySet(self, b, n)      key_set((Key *)(self), (b), (n))
#define ibm_keyDump(self)           key_dump((Key *)(self))
#define ibm_transCtor(self, v, n, w, e, p) \
    tr_ctor((Translation *)(self), (v), (n), (w), (e), (p))
#define ibm_transDtor(self)         tr_dtor((Translation *)(self))
#define ibm_transDump(self)         tr_dump((Translation *)(self))
#define ibm_slCtor(self)            sl_ctor((SkipList *)(self))
#define ibm_slDtor(self)            sl_dtor((SkipList *)(self))
#define ibm_slInsert(self, k, t) \
    sl_insert((SkipList *)(self), (Key *)(k), (Translation *)(t))
#define ibm_slSearch(self, k)       sl_search((SkipList *)(self), (Key *)(k))
#define ibm_slMultiSearch(self, k) \
    sl_multiSearch((SkipList *)(self), (Key *)(k))
#define ibm_slRemove(self, k)       sl_remove((SkipList *)(self), (Key *)(k))
#define ibm_slGetFirst(self, k, t) \
    sl_getFirst((SkipList *)(self), (Key **)(k), (Translation **)(t))
#define ibm_slGetNext(self, k, t) \
    sl_getNext((SkipList *)(self), (Key **)(k), (Translation **)(t))
#define ibm_slSave(self, p)         sl_save((SkipList *)(self), (p))
#define ibm_slLoad(self, p)         sl_load((SkipList *)(self), (p))

#include "dictsearch.h"
#include "txtanal.h"
#include "phrasebuf.h"
#include "numread.h"
#include "jpath.h"
#include "intonphrase.h"
#include "prosctrl.h"
#include "makereadable.h"
#include "textnormalizer.h"
#include "phrasetable.h"

#define ibm_dsCheckCaseMarker(d, at)      ds_CheckCaseMarker((d), (at))
#define ibm_dsCheckCnvChoon(d, c, n)      ds_CheckCnvChoon((d), (c), (n))
#define ibm_dsGetTextBuf(d, from)         ds_GetTextBuf((d), (from))
#define ibm_dsIsOnin(c)                   ds_IsOnin((c))
#define ibm_dsGetYoonIndex(d, s)          ds_GetYoonIndex((d), (s))
#define ibm_dsSetLongWord(d, n, e, w)     ds_SetLongWord((d), (n), (e), (w))
#define ibm_dsCountHrgn(d, n)             ds_CountHrgn((d), (n))
#define ibm_dsReadGWDict(d, p, a, w)      ds_ReadGWDict((d), (p), (a), (w))
#define ibm_dsConvertYoonDict(d, b, y, f) ds_ConvertYoonDict((d), (b), (y), (f))
#define ibm_dsProcessHiragana(d, at, e)   ds_ProcessHiragana((d), (at), (e))
#define ibm_dsProcessKatakana(d, at, e)   ds_ProcessKatakana((d), (at), (e))
#define ibm_dsWriteKanaData(d, h, c, u, b) \
    ds_WriteKanaData((d), (h), (c), (u), (b))
#define ibm_dsLookupKanaDict(d, at)       ds_LookupKanaDict((d), (at))
#define ibm_dsGenerateKanaString(d)       ds_GenerateKanaString((d))
#define ibm_dsCompareKanji(d, e, w)       ds_CompareKanji((d), (e), (w))
#define ibm_dsWriteGWDict(d, w, i, b, a, m) \
    ds_WriteGWDict((d), (w), (i), (b), (a), (m))
#define ibm_dsWriteDictTableData(d, h, i, m, a, b) \
    ds_WriteDictTableData((d), (h), (i), (m), (a), (b))
#define ibm_dsGetDictEntry(d, i, a, b, h, m) \
    ds_GetDictEntry((d), (i), (a), (b), (h), (m))
#define ibm_dsSearchTankanTable(d, i, a, b) \
    ds_SearchTankanTable((d), (i), (a), (b))
#define ibm_dsGenerateWord(d, at, b)      ds_GenerateWord((d), (at), (b))
#define ibm_taAddLongWord(t, w, n)        ta_AddLongWord((t), (w), (n))

#define NORMAL_N ((long)jajp_s_apszNormal_n)
#define TANKAN_N ((long)jajp_s_apszTankan_n)

#define SUPP_D jajp_s_apszSuppD[0]
#define SUPP_I jajp_s_apszSuppI[0]

#define ibm_dsIsItaiji(d, c)        ds_IsItaiji((d), (c))
#define ibm_dsSwapKanji(d, c)       ds_SwapKanji((d), (c))
#define ibm_dsErrorDummy(d, s, a)   ds_ErrorDummy((d), (s), (a))
#define ibm_dsWriteData(d, h, c, g, b, l, a) \
    ds_WriteData((d), (h), (c), (g), (b), (l), (a))
#define ibm_dsWriteTankanData(d, h, c, b, a) \
    ds_WriteTankanData((d), (h), (c), (b), (a))
#define ibm_dsWriteUserData(d, h, s, a) \
    ds_WriteUserData((d), (h), (s), (a))
#define ibm_dsLookupUserDict(d, k, t, s, x, a, u) \
    ds_LookupUserDict((d), (k), (t), (s), (x), (a), (u))
#define ibm_dsLookupEngWordDict(d, r, s, a, w, m) \
    ds_LookupEngWordDict((d), (r), (s), (a), (w), (m))
#define ibm_dsLookupEngWordDictFromText(d, s, a) \
    ds_LookupEngWordDictFromText((d), (s), (a))
#define ibm_dsLookupTankanDict(d, b, a) ds_LookupTankanDict((d), (b), (a))
#define ibm_dsLookupNormalWordDict(d, b, a, w) \
    ds_LookupNormalWordDict((d), (b), (a), (w))
#define ibm_dsHitFuncWordDict(d, h, s, a, c, r, g, v, k, f) \
    ds_HitFuncWordDict((d), (h), (s), (a), (c), (r), (g), (v), (k), (f))
#define ibm_dsSearchFuncWordDict(d, v, a, s, k, f) \
    ds_SearchFuncWordDict((d), (v), (a), (s), (k), (f))
#define ibm_dsLookupFuncWordDict(d, b, a) \
    ds_LookupFuncWordDict((d), (b), (a))
#define FUNC_DICT() dm_GetFuncDictEx()

#define ibm_dsEngRulesUppercase(d, i, o) ds_EngRulesUppercase((d), (i), (o))
#define ibm_dsEngRulesNormalize(d, i, o) ds_EngRulesNormalize((d), (i), (o))
#define ibm_dsEngRulesApplyRule(d, i, o, r, a) \
    ds_EngRulesApplyRule((d), (i), (o), (DictManRules *)(r), (a))
#define ibm_dsEngRulesConvert(d, i, o, e, k, l, c) \
    ds_EngRulesConvert((d), (i), (o), (DictManRules *)(e), \
                       (DictManRules *)(k), (l), (c))
#define ENG_RULES  ((void *)&dm_EngToRomanRule)
#define KANA_RULES ((void *)&dm_RomanToKanaRule)

#define ibm_dsSetDummySymbol(d, a, e)   ds_SetDummySymbol((d), (a), (e))
#define ibm_dsSetDummyRomanAlphabet(d, a, e) \
    ds_SetDummyRomanAlphabet((d), (a), (e))
#define ibm_dsProcessRomanAlphabet(d, a, e) \
    ds_ProcessRomanAlphabet((d), (a), (e))
#define ibm_dsNeedKatakanaAnalysis(d, b, n) \
    ds_NeedKatakanaAnalysis((d), (b), (n))
#define ibm_dsCheckJrtTable(d, b, n)    ds_CheckJrtTable((d), (b), (n))
#define ibm_dsCompareJMD(d, p, a, n)    ds_CompareJMD((d), (p), (a), (n))
#define ibm_dsSetJCC(d, m, s)           ds_SetJCC((d), (m), (s))
#define ibm_dsJoSuusiSearch(d, a)       ds_JoSuusiSearch((d), (a))
#define ibm_dsHandleError(d, a, w, b, o) \
    ds_HandleError((d), (a), (w), (b), (o))
#define NUM_JMD() dm_GetNumJMDPtr()

#define ibm_anCtor(a, t)             an_ctor((Annotation *)(a), (t))
#define ibm_anGetRomHandAnnoType(a, s) \
    an_GetRomHandAnnoType((Annotation *)(a), (s))
#define ibm_anSave(a, t, l, w)       an_Save((Annotation *)(a), (t), (l), (w))
#define ibm_anGetLastAnno(a, b, t)   an_GetLastAnno((Annotation *)(a), (b), (t))
#define ibm_anRemove(a)              an_Remove((Annotation *)(a))
#define ibm_anRemoveAfter(a, w)      an_RemoveAfter((Annotation *)(a), (w))
#define ibm_anFlush(a, e, o, p) \
    an_Flush((Annotation *)(a), (e), (DynaBuf *)(o), (p))
#define ANNO_ROOM   sizeof(Annotation)
#define AN(name)    ibm_an##name
#define ANNO_CTOR(a, t) ibm_anCtor((a), (t))
#define BUF_NEW(n)  dynaBufNew(n)
#define BUF_DEL(b)  dynaBufDelete((DynaBuf *)(b))
#define BUF_STR(b)  dynaBufContents((DynaBuf *)(b))
#define BUF_LEN(b)  dynaBufLength((DynaBuf *)(b))

#define UD_DICT(u)  (*(void **)&((RomUserDict *)(u))->dict)

#define ibm_udCtor(u, a)            rud_ctor((RomUserDict *)(u), (a))
#define ibm_udMakeKey(u, i, n, o, l) \
    rud_makeKey((RomUserDict *)(u), (i), (n), (o), (l))
#define ibm_udMakeTransValue(u, i, a, o, r) \
    rud_makeTransValue((RomUserDict *)(u), (i), (a), (o), (r))
#define ibm_udTransKatakana2Yomi(u, k, o) \
    rud_transKatakana2Yomi((RomUserDict *)(u), (k), (o))
#define ibm_udTransKana2Yomi(u, k, o) \
    rud_transKana2Yomi((RomUserDict *)(u), (k), (o))
#define ibm_udMakeUserDictData(u, d, l, k, p) \
    rud_makeUserDictData((RomUserDict *)(u), (UserDictData *)(d), (l), (k), (p))
#define ibm_udWriteData(u, d, s, a) \
    rud_writeData((RomUserDict *)(u), (UserDictData *)(d), (s), (a))
#define ibm_udLookup(u, t, a, s)    rud_lookup((RomUserDict *)(u), (t), (a), (s))
#define ibm_udUpdateDictExt(u, l, w, b, n, k, m, p) \
    rud_updateDictExt((RomUserDict *)(u), (SkipList *)(l), (w), (b), (n), \
                     (k), (m), (p))
#define ibm_udLookupDictExt(u, l, w, b, n, v, m, p) \
    rud_lookupDictExt((RomUserDict *)(u), (SkipList *)(l), (w), (b), (n), \
                     (v), (m), (p))

#define UD(name) ibm_ud##name

#define ibm_icCtor(in, t)            ic_ctor((in), (t))
#define ibm_icInit(in)               ic_Init((in))
#define ibm_icSetText(in, s)         ic_SetText((in), (s))
#define ibm_icSetTextAt(in, s, a)    ic_SetTextAt((in), (s), (a))
#define ibm_icGetNextChar(in)        ic_GetNextChar((in))
#define ibm_icIsAnnotationsInText(in) ic_IsAnnotationsInText((in))
#define ibm_icAddSnlkTable(in, a, w, r, f) \
    ic_AddSnlkTable((in), (a), (w), (r), (f))
#define ibm_icGetSnlkTableAt(in, a)  ic_GetSnlkTableAt((in), (a))
#define ibm_icDeleteSnlkTable(in)    ic_DeleteSnlkTable((in))
#define ibm_icGetUnknownKanji(in, a, f, t) \
    ic_GetUnknownKanji((in), (a), (f), (t))

#define ibm_icIsKanjiNum(in, a)      ic_IsKanjiNum((in), (a))
#define ibm_icGetCharType(in, a)     ic_GetCharType((in), (a))
#define ibm_icCheckNextAnnotation(in) ic_CheckNextAnnotation((in))
#define ibm_icCheckCyuTen(in, a)     ic_CheckCyuTen((in), (a))
#define ibm_icCheckContextForNum(in, a) ic_CheckContextForNum((in), (a))
#define ibm_icCheckContext(in, a, p) ic_CheckContext((in), (a), (p))
#define ibm_icConvertDakuten(in, a, c, m) \
    ic_ConvertDakuten((in), (a), (c), (m))
#define ibm_icProcessASCII(in, a, x, y) ic_ProcessASCII((in), (a), (x), (y))
#define ibm_icProcessAnnotation(in, a) ic_ProcessAnnotation((in), (a))
#define ibm_icRecoverOverflow(in, a) ic_RecoverOverflow((in), (a))
#define ibm_icReadSentence(in)       ic_ReadSentence((in))

#define IC(name) ibm_ic##name


/* The field isAnnotationsInText answers with. Nothing writes it, so the sweep
   does; it is IBM's sixteenth byte and our own struct's named member, which
   are not the same place once a pointer is eight bytes wide. */
#define PARAM_ANNO(p) (((RomInstParam *)(p))->inputType)
#define UD_ROOM  sizeof(RomUserDict)

#define ibm_dsIsMember(d, p, t, n)   ds_IsMember((d), (p), (t), (n))
#define ibm_dsIsZKNum(d, p)          ds_IsZKNum((d), (p))
#define ibm_dsIsZSNum(d, p)          ds_IsZSNum((d), (p))
#define ibm_dsIsZKeta(d, p)          ds_IsZKeta((d), (p))
#define ibm_dsIsZSymb(d, p)          ds_IsZSymb((d), (p))
#define ibm_dsIsCommaPosition(d, p, n) ds_IsCommaPosition((d), (p), (n))
#define ibm_dsIsEndOfQuote(d, a)     ds_IsEndOfQuote((d), (a))
#define ibm_dsCheckKetaOrder(d, n, c, kn, kc, k, b) \
    ds_CheckKetaOrder((d), (n), (c), (kn), (kc), (k), (b))
#define ibm_dsSetSuushiWord(d, s, a) ds_SetSuushiWord((d), (s), (a))
#define ibm_dsSetDummyWord(d, s, a)  ds_SetDummyWord((d), (s), (a))
#define ibm_dsgetPtrOfUserDict(d)    ((void *)ds_getPtrOfUserDict((d)))

#define ibm_dsDo(d)                  ds_Do((d))
#define ibm_rzGetParameter(rz, p)    rz_GetParameter((rz), (p))
#define RZ(name) ibm_rz##name

#define ibm_dsHitFuncWordReverse(d, h, s, a, c, n, g, v, b) \
    ds_HitFuncWordReverse((d), (h), (s), (a), (c), (n), (g), (v), (b))
#define ibm_dsFzkSearchUnknown(d, v, a, s, t, u) \
    ds_FzkSearchUnknown((d), (v), (a), (s), (t), (u))
#define ibm_dsFzkParsing(d, v, a)    ds_FzkParsing((d), (v), (a))
#define ibm_dsFzkParsingReverse(d)   ds_FzkParsingReverse((d))

#define DS(name) ibm_ds##name

#define DM(name) dm_##name
#define JU(name) ju_##name
#define STATIC_DICT_INIT() ((void)0)



/* ---- PhraseBuf -------------------------------------------------------- */

#define ibm_pbCtor(pb, ta)         pb_ctor((pb), (ta))
#define ibm_pbCopy(pb, n)          pb_Copy((pb), (n))
#define ibm_pbModifyPos(pb, o, p)  pb_ModifyPos((pb), (o), (p))
#define ibm_pbIsBunsetsuEnd(pb, s) pb_IsBunsetsuEnd((pb), (s))
#define ibm_pbIsSokuonTankanVerb(pb, s) pb_IsSokuonTankanVerb((pb), (s))
#define ibm_pbGetSpecialPhraseType(pb, w) pb_GetSpecialPhraseType((pb), (w))
#define ibm_pbChkTTELink(pb, k, f) pb_ChkTTELink((pb), (k), (f))
#define ibm_pbSetJrt(pb, p, w, a, b) pb_SetJrt((pb), (p), (w), (a), (b))
#define ibm_pbSetPhrasePart(pb, p, n, f, k, o) \
    pb_SetPhrasePart((pb), (p), (n), (f), (k), (o))
#define ibm_pbSetPhraseBuffer(pb, o) pb_SetPhraseBuffer((pb), (o))
#define PB(name) ibm_pb##name
/* ---- JPath ------------------------------------------------------------ */

#define ibm_jpCtor(jp, ta)  jp_ctor((jp), (ta))
#define ibm_jpMake(jp, at)  jp_Make((jp), (at))
#define ibm_jpAddPath(jp, p, e, o, n, i) \
    jp_AddPath((jp), (p), (e), (o), (n), (i))
#define ibm_jpCheckType(jp, tg) jp_CheckType((jp), (tg))
#define ibm_jpCheckAdFlag(jp, lt, rt, la, ra, c) \
    jp_CheckAdFlag((jp), (lt), (rt), (la), (ra), (c))
#define ibm_jpJrtJrtCheck(jp, lt, rt, la, ra, a) \
    jp_JrtJrtCheck((jp), (lt), (rt), (la), (ra), (a))
#define ibm_jpIsHead(jp, e)        jp_IsHead((jp), (e))
#define ibm_jpIsEnd(jp, e)         jp_IsEnd((jp), (e))
#define ibm_jpIsContinuable(jp, e) jp_IsContinuable((jp), (e))
#define ibm_jpGetMoraOnPath(jp, p, x) jp_GetMoraOnPath((jp), (p), (x))
#define ibm_jpSetWordAttr(jp, s, e)   jp_SetWordAttr((jp), (s), (e))
#define ibm_jpMakeJrtSubTable(jp)     jp_MakeJrtSubTable((jp))
#define JP(name) ibm_jp##name
/* ---- NumRead --------------------------------------------------------- */

#define ibm_nrInit(nr)              nr_Init((nr))
#define ibm_nrSegmentYomiBlock(nr, w, k) nr_SegmentYomiBlock((nr), (w), (k))
#define ibm_nrSetYomiType(nr, h)    nr_SetYomiType((nr), (h))
#define ibm_nrGenerateStdForm(nr, h) nr_GenerateStdForm((nr), (h))
#define ibm_nrApplySRule(nr, h, g)  nr_ApplySRule((nr), (h), (g))
#define ibm_nrApplySRuleToKetaYomi(nr, i, n, a, g, s) \
    nr_ApplySRuleToKetaYomi((nr), (i), (n), (a), (g), (s))
#define ibm_nrApplySRuleToBouYomi(nr, n, a, g, s) \
    nr_ApplySRuleToBouYomi((nr), (n), (a), (g), (s))
#define ibm_nrApplySRuleToShosu(nr, n, a, g, p, s) \
    nr_ApplySRuleToShosu((nr), (n), (a), (g), (p), (s))
#define ibm_nrApplySRuleToBunsu(nr, n, a, g, s) \
    nr_ApplySRuleToBunsu((nr), (n), (a), (g), (s))
#define ibm_nrApplyJRule(nr, w, k, n, h, g) \
    nr_ApplyJRule((nr), (w), (k), (n), (h), (g))
#define ibm_nrDo(nr, w, pk, po)     nr_Do((nr), (w), (pk), (po))
#define NR(name) ibm_nr##name

/* ---- IntonPhrase ------------------------------------------------------ */

#define ibm_ipTableAllocBG(ip, c, l, a, k, n) \
    ip_TableAllocBG((ip), (c), (l), (a), (k), (n))
#define ibm_ipBreathGroupAlloc(ip)        ip_BreathGroupAlloc((ip))
#define ibm_ipInitPhraseTable(ip, n)      ip_InitPhraseTable((ip), (n))
#define ibm_ipCheckPhraseToPhrase(ip, st) ip_CheckPhraseToPhrase((ip), (st))
#define ibm_ipProsodyControl(ip)          ip_ProsodyControl((ip))
#define ibm_ipThreePhraseParsing(ip, t)   ip_ThreePhraseParsing((ip), (t))
#define ibm_ipModifyPType(ip, t)          ip_ModifyPType((ip), (t))
#define ibm_ipCheckBreathGroup(ip, s, r, k) \
    ip_CheckBreathGroup((ip), (s), (r), (k))
#define ibm_ipCheckChoon(ip, t, at)       ip_CheckChoon((ip), (t), (at))
#define ibm_ipSetPhraseState(ip)          ip_SetPhraseState((ip))
#define ibm_ipPhraseParsing(ip, l, r, g, o) \
    ip_PhraseParsing((ip), (l), (r), (g), (o))
#define ibm_ipRegroupPhrases(ip)          ip_RegroupPhrases((ip))
#define ibm_ipPhraseSeparate(ip, a, b, m, l, f, k) \
    ip_PhraseSeparate((ip), (a), (b), (m), (l), (f), (k))
#define ibm_ipSetPauseLength(ip)          ip_SetPauseLength((ip))
#define ibm_ipSetPitchValues(ip)          ip_SetPitchValues((ip))
#define ibm_ipSetIntonationalPhrase(ip)   ip_SetIntonationalPhrase((ip))
#define ibm_ipSetAccentualPhrase(ip, p, a) \
    ip_SetAccentualPhrase((ip), (p), (a))
#define IPM(name) ibm_ip##name

/* ---- ProsCtrl -------------------------------------------------------- */

#define ibm_pcCtor(pc)                    pc_ctor((pc))
#define ibm_pcDtor(pc)                    pc_dtor((pc))
#define ibm_pcGenerateESPR(pc, e, p, t, b, o, c) \
    pc_GenerateESPR((pc), (e), (p), (t), (b), (o), (c))
#define ibm_pcBG_T2BreathGroups(pc, b, g, n) \
    pc_BG_T2BreathGroups((pc), (b), (g), (n))
#define ibm_pcFreeBreathGroups(pc, g, n)  pc_FreeBreathGroups((pc), (g), (n))
#define ibm_pcWriteESPR2(pc, g, n, m, o, c) \
    pc_WriteESPR2((pc), (g), (n), (m), (o), (c))
#define ibm_pcWriteGokiInfo(pc, a, w, k, at, b, o, c, l) \
    pc_WriteGokiInfo((pc), (a), (w), (k), (at), (b), (o), (c), (l))
#define ibm_pcGetGokiInfoToWrite(pc, a, m, f, u, p, nm, s, k) \
    pc_GetGokiInfoToWrite((pc), (a), (m), (f), (u), (p), (nm), (s), (k))
#define ibm_pcWriteBGInfo(pc, m, k, l, o, c, n) \
    pc_WriteBGInfo((pc), (m), (k), (l), (o), (c), (n))
#define ibm_pcWriteStressLevel(pc, a, f, t, o, c, n, force) \
    pc_WriteStressLevel((pc), (a), (f), (t), (o), (c), (n), (force))
#define ibm_pcWriteUserIndex(pc, n, o, c, l) \
    pc_WriteUserIndex((pc), (n), (o), (c), (l))
#define ibm_pcWriteDummyF0Pair(pc, n, o, c, l) \
    pc_WriteDummyF0Pair((pc), (n), (o), (c), (l))
#define ibm_pcWriteToOutBuf(pc, w, o, c, l) \
    pc_WriteToOutBuf((pc), (w), (o), (c), (l))
#define ibm_pcModifyWordProminence(pc, pr, po, f) \
    pc_ModifyWordProminence((pc), (pr), (po), (f))
#define ibm_pcIsBurstCons(pc, code)       pc_IsBurstCons((pc), (code))
#define ibm_pcIsValidConsForSokuOn(pc, code) \
    pc_IsValidConsForSokuOn((pc), (code))
#define PCM(name) ibm_pc##name

/* ---- MakeReadableJP --------------------------------------------------- */

#define ibm_mrCtor(mr)                    mr_ctor((mr))
#define ibm_mrDtor(mr)                    mr_dtor((mr))
#define ibm_mrlCopyAndReturn(mr, t, n, b, c) \
    mrl_copyAndReturn((mr), (t), (n), (b), (c))
#define ibm_mrReallocateBuf(mr, b, u, w)  mr_reallocateBuf((mr), (b), (u), (w))
#define ibm_mrAppendText(mr, t, b, c, l)  mr_appendText((mr), (t), (b), (c), (l))
#define ibm_mrAppendTextN(mr, t, n, b, c, l) \
    mr_appendTextN((mr), (t), (n), (b), (c), (l))
#define ibm_mrAppendChar(mr, t, b, c, l)  mr_appendChar((mr), (t), (b), (c), (l))
#define ibm_mrAppendNumber(mr, x, b, c, l, z) \
    mr_appendMakeReadableNumber((mr), (x), (b), (c), (l), (z))
#define ibm_mrSeparate(mr, w, l, r)       mr_separateNumberByDecimalPoint((mr), (w), (l), (r))
#define ibm_mrSuppressZero(mr, p, e)      mr_suppressZero((mr), (p), (e))
#define ibm_mrIsCurrencySymbol(mr, t, n)  mr_isCurrencySymbol((mr), (t), (n))
#define ibm_mrIsBoolSymbol(mr, t, n)      mr_isBoolSymbol((mr), (t), (n))
#define ibm_mrIsCurrencyPunct(mr, t)      mr_isCurrencyPunct((mr), (t))
#define ibm_mrIsDecimalPoint(mr, t)       mr_isDecimalPoint((mr), (t))
#define ibm_mrIsParenthesis(mr, t)        mr_isParenthesis((mr), (t))
#define ibm_mrIsTimeDelimiter(mr, t)      mr_isTimeDelimiter((mr), (t))
#define ibm_mrIsPlusMinusSymbol(mr, t)    mr_isPlusMinusSymbol((mr), (t))
#define ibm_mrIsDayOfWeek(mr, t)          mr_isDayOfWeek((mr), (t))
#define ibm_mrIsRangeSymbol(mr, t)        mr_isRangeSymbol((mr), (t))
#define ibm_mrIsDateSeparator(mr, t)      mr_isDateSeparator((mr), (t))
#define ibm_mrIsTelSymbol(mr, t)          mr_isTelSymbol((mr), (t))
#define ibm_mrIsDBCSDigit(mr, t)          mr_isDBCSDigit((mr), (t))
#define ibm_mrIsDigit(mr, t)              mr_isDigit((mr), (t))
#define ibm_mrNormalizeDigits(mr, t, n, b, c, f) \
    mr_normalizeDigits((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeLiteral(mr, t, n, b, c, f) \
    mr_normalizeLiteral((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeBool(mr, t, n, b, c, f) \
    mr_normalizeBool((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeNumber(mr, t, n, b, c, f) \
    mr_normalizeNumber((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizePhone(mr, t, n, b, c, f) \
    mr_normalizePhone((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeTime(mr, t, n, b, c, f) \
    mr_normalizeTime((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeCurrency(mr, t, n, b, c, f) \
    mr_normalizeCurrency((mr), (t), (n), (b), (c), (f))
#define ibm_mrNormalizeDate(mr, t, n, b, c, f) \
    mr_normalizeDate((mr), (t), (n), (b), (c), (f))
#define ibm_mrConvertSPR(mr, t, n, b, c) \
    mr_convertSPR((mr), (t), (n), (b), (c))
#define MRM(name) ibm_mr##name

#define ibm_tnCtor(tn)                    tn_ctor((tn))
#define ibm_tnDtor(tn)                    tn_dtor((tn))
#define ibm_tnReallocateBuf(tn, b, u, w)  tn_reallocateBuf((tn), (b), (u), (w))
#define ibm_tnGetAnnoType(tn, t, a, e)    tn_getAnnoType((tn), (t), (a), (e))
#define ibm_tnMakeReadable(tn, t, n, b, c, f) \
    tn_makeReadable((tn), (t), (n), (b), (c), (f))
#define ibm_tnNormalizeText(tn, t, n, b, l) \
    tn_normalizeText((tn), (t), (n), (b), (l))
#define TNM(name) ibm_tn##name

#define ibm_ptbGetPosFromTG(pt, tg)       ptb_GetPosFromTG((pt), (tg))
#define ibm_ptbGetFzkPosFromTG(pt, tg)    ptb_GetFzkPosFromTG((pt), (tg))
#define ibm_ptbGetAffixType(pt, tg)       ptb_GetAffixType((pt), (tg))
#define ibm_ptbTableAllocPhrase(pt, f, l, s, k, n) \
    ptb_TableAllocPhrase((pt), (f), (l), (s), (k), (n))
#define ibm_ptbPhraseAlloc(pt)            ptb_PhraseAlloc((pt))
#define ibm_ptbGeneratePhraseTable(pt)    ptb_GeneratePhraseTable((pt))
#define ibm_ptbExtKKRPhrase(pt, k, t, o)  ptb_ExtKKRPhrase((pt), (k), (t), (o))
#define ibm_ptbSetSubUkeType(pt, u, t, f) ptb_SetSubUkeType((pt), (u), (t), (f))
#define ibm_ptbSetNoneFzkKKR(pt, k, w)    ptb_SetNoneFzkKKR((pt), (k), (w))
#define ibm_ptbSetUkeTypePhrase(pt, u, w) ptb_SetUkeTypePhrase((pt), (u), (w))
#define ibm_ptbFzkAccent(pt, i, o)        ptb_FzkAccent((pt), (i), (o))
#define ibm_ptbCompoundWord(pt, w, r)     ptb_CompoundWord((pt), (w), (r))
#define PTBM(name) ibm_ptb##name
#define PTB_SET(blk, which, p) (*(void **)((blk) + which##_AT) = (p))
#define AI_RULE_SET(in, i, p) (AI_RULE_OF((in), (i)) = (p))

/* Where each side keeps PhraseBuf's four pointers and JPath's three. Ours are
   parked past their records; IBM's are at the offsets the maps name. */
#define PB_SET(blk, which, p) (*(void **)((blk) + which##_AT) = (p))
#define JP_SET(blk, which, p) (*(void **)((blk) + which##_AT) = (p))


/* ---- the surface: ConverterInterface, InputManager, the codesets ------ */

#define PARAM_SET(p, w, v)  rp_setParam((p), (w), (v))
#define PARAM_ERRORS(p)     rp_getErrors((p))
#define PARAM_CLEAR(p)      rp_clearErrors((p))
#define PARAM_CODESET(p)    rp_getCodeSet((p))
#define PARAM_OWNER(p)      (*(void **)&((RomInstParam *)(p))->owner)

#define ibm_ciInitBase(c, p)              ci_initBase((c), (p))
#define ibm_ciCloseBase(c)                ci_closeBase((c))
#define ibm_ciUCS2ToMBCS(c, i, o, f)      ci_UCS2ToMBCS((c), (i), (o), (f))
#define ibm_ciMBCSToUCS2(c, i, o)         ci_MBCSToUCS2((c), (i), (o))
#define ibm_ciInsertIndex(c)              ci_insertIndex((c))
#define ibm_ciAddParam(c, t, n)           ci_addParam((c), (t), (n))
#define ibm_ciOutputIndexOrParam(c, o, a) ci_outputIndexOrParam((c), (o), (a))
#define ibm_ciAddText(c, t, n, k)         ci_addText((c), (t), (n), (k))
#define ibm_ciTrans(c, t, n, s, o) \
    ci_trans2defaultCodeset((c), (t), (n), (s), (o))
#define ibm_ciStop(c)                     ci_stop((c))
#define ibm_ciResume(c)                   ci_resume((c))
#define ibm_ciNewDict(c)                  ci_newDict((c))
#define ibm_ciDeleteDict(c, d)            ci_deleteDict((c), (d))
#define ibm_ciSetDict(c, d)               ci_setDict((c), (d))
#define ibm_ciFindDictFile(c, n, o)       ci_findDictFile((c), (n), (o))
#define ibm_ciLoadDict(c, d, w, n)        ci_loadDict((c), (d), (w), (n))
#define ibm_ciSaveDict(c, d, w, n)        ci_saveDict((c), (d), (w), (n))
#define ibm_ciLookupDictExt(c, d, w, b, l, v, vl, ps, s) \
    ci_lookupDictExt((c), (d), (w), (b), (l), (v), (vl), (ps), (s))
#define ibm_ciFindFirst(c, d, w, a, al, e, el, ps, s) \
    ci_findFirstDictEntryExt((c), (d), (w), (a), (al), (e), (el), (ps), (s))
#define ibm_ciFindNext(c, d, w, a, al, e, el, ps, s) \
    ci_findNextDictEntryExt((c), (d), (w), (a), (al), (e), (el), (ps), (s))
#define ibm_ciUpdateDictExt(c, d, w, b, l, k, kl, ps, s) \
    ci_updateDictExt((c), (d), (w), (b), (l), (k), (kl), (ps), (s))
#define CI(name) ibm_ci##name

#define ibm_imCtor(m, p)       im_ctor((InputManager *)(m), (p))
#define ibm_imDtor(m)          im_dtor((InputManager *)(m))
#define ibm_imRemove(m)        im_remove((InputManager *)(m))
#define ibm_imAddText(m, t, n, s) \
    im_addText((InputManager *)(m), (t), (n), (s))
#define ibm_imGetText(m, ot, on, t, n) \
    im_getText((InputManager *)(m), (ot), (on), (t), (n))
#define ibm_imInsertIndex(m)   im_insertIndex((InputManager *)(m))
#define ibm_imAddParam(m, t, n) im_addParam((InputManager *)(m), (t), (n))
#define ibm_imHasMore(m)       im_hasMoreElement((InputManager *)(m))
#define ibm_imGetNextOffset(m) im_getNextOffset((InputManager *)(m))
#define ibm_imGetNextData(m, o) im_getNextData((InputManager *)(m), (o))
#define ibm_imRemoveElement(m) im_removeElement((InputManager *)(m))
#define ibm_imGetNextElement(m) \
    ((void *)im_getNextElement((InputManager *)(m)))
/* What a queued element carries. Ours puts them behind a vtable pointer that
   is eight bytes wide here; IBM's are at four and eight. */
#define QE_AT(e)   (((RomQueueElement *)(e))->at)
#define QE_KIND(e) (((RomQueueElement *)(e))->kind)
#define IM(name) ibm_im##name
#define IM_ROOM  sizeof(InputManager)

/* The vtable a converter is reached through. InputManager asks it where in
   the output the next mark belongs and resume asks it to throw away what was
   collected, so the harness plants one of its own that counts both. Ours sits
   past the record and IBM's at nought, which CV_SET_VT is for. */
typedef ConverterVtbl HarnessVtbl;
typedef Converter HarnessSelf;
#define HARNESS_THIS
#define CV_SET_VT(b)  (*(const void **)((b) + RZ_VTABLE_AT) = &harness_vtbl)
#define CV_INPUT(b)   (*(void **)((b) + RZ_INPUT_AT))
#define CV_TRANSBUF(b) (*(void **)((b) + RZ_TRANSBUF_AT))
#define CV_UNICODE(b) (*(void **)((b) + RZ_UNICODE_AT))


#else

typedef struct Param Param;
typedef struct Conv Conv;

/* What IBM's two objects take up: 0x3c bytes for the parameter block and
   0x14 for the converter, which is what their own constructors are handed. */
static char param_room[0x3c];
static char conv_room[0x14];

extern THIS Param *ibm_paramCtor(void *self, const char *path)
    MANGLED("??0RomInstParam@@QAE@PBD@Z");
extern THIS Conv *ibm_convCtor(void *self, Param *p)
    MANGLED("??0UnicodeConverter@@QAE@PAVRomInstParam@@@Z");
extern THIS int32_t ibm_toUcs2(Conv *c, const char *in, uint16_t **out)
    MANGLED("?MBCSToUCS2@UnicodeConverter@@QAE?AW4RomError@@PBDPAPAG@Z");
extern THIS int32_t ibm_toMbcs(Conv *c, const uint16_t *in, char **out,
                               int32_t flag)
    MANGLED("?UCS2ToMBCS@UnicodeConverter@@QAE?AW4RomError@@PBGPAPADH@Z");

static Param *makeParam(const char *path)
{
    return ibm_paramCtor(param_room, path);
}

static Conv *makeConv(Param *p)
{
    return ibm_convCtor(conv_room, p);
}

#define TO_UCS2(c, in, out)      ibm_toUcs2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   ibm_toMbcs((c), (in), (out), (f))

/* DictMan's accessors are static members, so they are ordinary functions with
   no this pointer, and cdecl rather than thiscall. The two that fill in the
   static rules are instance methods that touch nothing of the instance, and
   are handed a pointer to nothing. */
extern const uint8_t *ibm_GetFuncDict(void)
    MANGLED("?GetFuncDict@DictMan@@SAPAEXZ");
extern const uint8_t *ibm_GetFuncDictEx(void)
    MANGLED("?GetFuncDictEx@DictMan@@SAPAEXZ");
extern const uint8_t *ibm_GetAccentAt(uint16_t i)
    MANGLED("?GetAccentAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetKakariAt(uint16_t i)
    MANGLED("?GetKakariAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetPhrVectorAt(uint16_t i)
    MANGLED("?GetPhrVectorAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetPenaltyAt(uint16_t i)
    MANGLED("?GetPenaltyAt@DictMan@@SA?BEG@Z");
extern uint8_t ibm_GetTGAt2(uint8_t row, uint8_t col)
    MANGLED("?GetTGAt@DictMan@@SA?BEEE@Z");
extern const uint8_t *ibm_GetTGAt(uint8_t row)
    MANGLED("?GetTGAt@DictMan@@SAPAEE@Z");
extern const uint8_t *ibm_GetYomiDataPtr(void)
    MANGLED("?GetYomiDataPtr@DictMan@@SAPAU_yomi_data_t@@XZ");
extern const uint8_t *ibm_GetPhraseDataPtr(void)
    MANGLED("?GetPhraseDataPtr@DictMan@@SAPAU_phrase_data_t@@XZ");
extern const uint8_t *ibm_GetNumberDataPtr(void)
    MANGLED("?GetNumberDataPtr@DictMan@@SAPAU_suushi_data_t@@XZ");
extern uint8_t ibm_GetNumMDAt(uint16_t i)
    MANGLED("?GetNumMDAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNumYomiPtrAt(uint8_t i)
    MANGLED("?GetNumYomiPtrAt@DictMan@@SAPAU_ym_tbl_t@@E@Z");
extern uint8_t ibm_GetNumJMDAt(uint16_t i)
    MANGLED("?GetNumJMDAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNumJMDPtr(void)
    MANGLED("?GetNumJMDPtr@DictMan@@SAPAEXZ");
extern uint8_t ibm_GetNumJCCAt(uint16_t i)
    MANGLED("?GetNumJCCAt@DictMan@@SAEG@Z");
extern const uint8_t *ibm_GetNDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetNDictHashAt@DictMan@@SAPAEGE@Z");
extern const uint8_t *ibm_GetTDictHashAt(uint16_t i)
    MANGLED("?GetTDictHashAt@DictMan@@SAPAEG@Z");
extern const uint8_t *ibm_GetKDictHashAt(uint16_t i)
    MANGLED("?GetKDictHashAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetKNDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetKNDictHashAt@DictMan@@SAEGE@Z");
extern uint8_t ibm_GetKTDictHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetKTDictHashAt@DictMan@@SAEGE@Z");
extern const uint8_t *ibm_GetEDictHashAt(uint16_t i)
    MANGLED("?GetEDictHashAt@DictMan@@SAPAEG@Z");
extern uint8_t ibm_GetItaijiHashAt(uint16_t i, uint8_t j)
    MANGLED("?GetItaijiHashAt@DictMan@@SAEGE@Z");
extern const uint8_t *ibm_GetItaijiAt(uint8_t row, uint16_t i)
    MANGLED("?GetItaijiAt@DictMan@@SAPAEEG@Z");
extern THIS void ibm_EngRulesInit(void *self)
    MANGLED("?EngRulesInit@DictMan@@QAEXXZ");
extern THIS void ibm_InitSupplementDictionary(void *self)
    MANGLED("?InitSupplementDictionary@DictMan@@QAEXXZ");

/* The two rule blocks and the two supplement pointers are private statics of
   DictMan, so they are asked for by their mangled names too. */
extern const uint8_t *ibm_EngToRomanRule[]
    __asm__("\"?s_EngToRomanRule@DictMan@@0U_rules@@A\"");
extern const uint8_t *ibm_RomanToKanaRule[]
    __asm__("\"?s_RomanToKanaRule@DictMan@@0U_rules@@A\"");
extern const uint8_t *ibm_paUserDict
    __asm__("\"?s_paUserDict@DictMan@@0PAEA\"");
extern const uint8_t *ibm_paUserDictIdx
    __asm__("\"?s_paUserDictIdx@DictMan@@0PAEA\"");

/* The static dictionary's pointer arrays are filled in at run time by
   StaticDict::Initialize, six and a half thousand unrolled stores. Ours are
   not: tools/rom/dictionary.py reads those stores and writes the arrays out with
   their contents already in them, so there is nothing to call. IBM's side has
   to call it or the supplement pointers come back empty -- which is what this
   sweep said before it did. */
extern void ibm_StaticDictInitialize(void)
    MANGLED("?Initialize@StaticDict@@SAXXZ");

/* JpnUtil is static members too, so the same again. */
extern int32_t ibm_IsSBCSKana(char c)
    MANGLED("?IsSBCSKana@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsAlphaNumSym(char c)
    MANGLED("?IsAlphaNumSym@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsNum(char c)
    MANGLED("?IsNum@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsAlpha(char c)
    MANGLED("?IsAlpha@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsDBCSLeadByte(char c)
    MANGLED("?IsDBCSLeadByte@JpnUtil@@SAHD@Z");
extern int32_t ibm_IsDBCSTrailByte(uint8_t c)
    MANGLED("?IsDBCSTrailByte@JpnUtil@@SAHE@Z");
extern int32_t ibm_IsValidDBCS(const char *p)
    MANGLED("?IsValidDBCS@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsKatakana(const char *p)
    MANGLED("?IsKatakana@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsHiragana(const char *p)
    MANGLED("?IsHiragana@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsLongVowel(const char *p)
    MANGLED("?IsLongVowel@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsSNLKDelim(const char *p)
    MANGLED("?IsSNLKDelim@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsDBCSNum(const char *p)
    MANGLED("?IsDBCSNum@JpnUtil@@SAHPBD@Z");
extern int32_t ibm_IsKanjiNum(const char *p)
    MANGLED("?IsKanjiNum@JpnUtil@@SAHPBD@Z");
extern uint16_t ibm_MakeUshort(char *p)
    MANGLED("?MakeUshort@JpnUtil@@SAGPAD@Z");
extern int32_t ibm_DbCmp(const char *a, const char *b)
    MANGLED("?DbCmp@JpnUtil@@SAHPBD0@Z");
extern int32_t ibm_DbCmp2(const char *a, char b0, char b1)
    MANGLED("?DbCmp2@JpnUtil@@SAHPBDDD@Z");
extern void ibm_DbCpy(char *to, const char *from)
    MANGLED("?DbCpy@JpnUtil@@SAXPAD0@Z");
extern void ibm_DbSet(char *to, char b0, char b1)
    MANGLED("?DbSet@JpnUtil@@SAXPADDD@Z");
extern int32_t ibm_TwoChCmp(char b0, char b1, char *p)
    MANGLED("?TwoChCmp@JpnUtil@@SAHDDPAD@Z");
extern void ibm_TwoChCpy(char *from, char *to0, char *to1)
    MANGLED("?TwoChCpy@JpnUtil@@SAXPAD00@Z");
extern int32_t ibm_WriteRomajiStrBuf(uint8_t code, uint8_t *out)
    MANGLED("?WriteRomajiStrBuf@JpnUtil@@SAHEPAE@Z");
extern void ibm_ConvertDakuten(char *out, uint8_t kana, uint8_t mark)
    MANGLED("?ConvertDakuten@JpnUtil@@SAXPADEE@Z");
extern void ibm_Hiragana2Katakana(const uint8_t *in, uint8_t *out)
    MANGLED("?Hiragana2Katakana@JpnUtil@@SAXPBEPAE@Z");
extern int32_t ibm_YomiCmp(uint8_t *a, uint8_t lenA, uint8_t *b, uint8_t lenB)
    MANGLED("?YomiCmp@JpnUtil@@SAHPAEE0E@Z");
extern void ibm_TableFree(uint16_t *used, uint16_t *tail, uint16_t *freeHead,
                          void *table, uint16_t nil, uint16_t which)
    MANGLED("?TableFree@JpnUtil@@SAXPAG00PAU_LINK_TBL_T@@GG@Z");

/* The stored dictionary's five classes. These are C++ objects with vtables and
   thiscall methods, so they are declared the way the port declares IBM's
   engine classes: the room they take up is stated here and the constructors
   are called by their mangled names. */
#define IBM_KEY_ROOM   0x0c
#define IBM_TRANS_ROOM 0x20
#define IBM_LIST_ROOM  0x20

extern THIS void *ibm_keyCtor(void *self, char *bytes, int32_t len)
    MANGLED("??0Key@@QAE@PADH@Z");
extern THIS void ibm_keyDtor(void *self)
    MANGLED("??1Key@@UAE@XZ");
extern THIS void ibm_keySet(void *self, char *bytes, int32_t len)
    MANGLED("?set@Key@@QAEXPADH@Z");
extern THIS void ibm_keyDump(void *self)
    MANGLED("?dump@Key@@QAEXXZ");
extern THIS void *ibm_transCtor(void *self, const char *value, int32_t len,
                                const char *word, const char *extra,
                                int32_t pos)
    MANGLED("??0Translation@@QAE@PBDH00W4ECIPartOfSpeech@@@Z");
extern THIS void ibm_transDtor(void *self)
    MANGLED("??1Translation@@QAE@XZ");
extern THIS void ibm_transDump(void *self)
    MANGLED("?dump@Translation@@QAEXXZ");
extern THIS void *ibm_slCtor(void *self)
    MANGLED("??0SkipList@@QAE@XZ");
extern THIS void ibm_slDtor(void *self)
    MANGLED("??1SkipList@@UAE@XZ");
extern THIS int32_t ibm_slInsert(void *self, void *key, void *trans)
    MANGLED("?insert@SkipList@@QAEHPAVKey@@PAVTranslation@@@Z");
extern THIS void *ibm_slSearch(void *self, void *key)
    MANGLED("?search@SkipList@@QAEPAVTranslation@@PAVKey@@@Z");
extern THIS void *ibm_slMultiSearch(void *self, void *key)
    MANGLED("?multiSearch@SkipList@@QAEPAVTranslation@@PAVKey@@@Z");
extern THIS int32_t ibm_slRemove(void *self, void *key)
    MANGLED("?remove@SkipList@@QAEHPAVKey@@@Z");
extern THIS int32_t ibm_slGetFirst(void *self, void **key, void **trans)
    MANGLED("?getFirst@SkipList@@QAEHPAPAVKey@@PAPAVTranslation@@@Z");
extern THIS int32_t ibm_slGetNext(void *self, void **key, void **trans)
    MANGLED("?getNext@SkipList@@QAEHPAPAVKey@@PAPAVTranslation@@@Z");
extern THIS int32_t ibm_slSave(void *self, const char *path)
    MANGLED("?save@SkipList@@QAEHPBD@Z");
extern THIS int32_t ibm_slLoad(void *self, const char *path)
    MANGLED("?load@SkipList@@QAEHPBD@Z");

/* And the three methods of DictSearch that are written. They are thiscall
   members over a block whose layout is IBM's on both sides, so the harness
   pokes the same offsets either way. */
#include "dictsearch.h"
#include "txtanal.h"
#include "phrasebuf.h"
#include "numread.h"
#include "jpath.h"
#include "intonphrase.h"
#include "prosctrl.h"
#include "makereadable.h"
#include "textnormalizer.h"
#include "phrasetable.h"

extern THIS int32_t ibm_dsCheckCaseMarker(void *d, int16_t at)
    MANGLED("?CheckCaseMarker@DictSearch@@QAEHF@Z");
extern THIS void ibm_dsCheckCnvChoon(void *d, uint8_t code, uint8_t *next)
    MANGLED("?CheckCnvChoon@DictSearch@@QAEXEPAE@Z");
extern THIS int32_t ibm_dsGetTextBuf(void *d, int16_t from)
    MANGLED("?GetTextBuf@DictSearch@@QAEHF@Z");
extern int32_t ibm_dsIsOnin(uint8_t code)
    MANGLED("?IsOnin@DictSearch@@SAHE@Z");
extern THIS int16_t ibm_dsGetYoonIndex(void *d, char *s)
    MANGLED("?GetYoonIndex@DictSearch@@QAEFPAD@Z");
extern THIS void ibm_dsSetLongWord(void *d, int16_t n, void *e, uint8_t *word)
    MANGLED("?SetLongWord@DictSearch@@QAEXFPAU_DICTENT_T@@PAE@Z");
extern THIS int32_t ibm_dsCountHrgn(void *d, int32_t n)
    MANGLED("?CountHrgn@DictSearch@@QAEHH@Z");
extern THIS const uint8_t *ibm_dsReadGWDict(void *d, int16_t page, int16_t at,
                                            int16_t which)
    MANGLED("?ReadGWDict@DictSearch@@QAEPAU_DICT_ENT@@FFF@Z");
extern THIS int16_t ibm_dsConvertYoonDict(void *d, int16_t base, int16_t yoon,
                                          uint8_t flag)
    MANGLED("?ConvertYoonDict@DictSearch@@QAEFFFE@Z");
extern THIS void ibm_dsProcessHiragana(void *d, int16_t at, void *e)
    MANGLED("?ProcessHiragana@DictSearch@@QAEXFPAU_DICTENT_T@@@Z");
extern THIS void ibm_dsProcessKatakana(void *d, int16_t at, void *e)
    MANGLED("?ProcessKatakana@DictSearch@@QAEXFPAU_DICTENT_T@@@Z");
extern THIS int16_t ibm_dsWriteKanaData(void *d, const uint8_t *head,
                                        int16_t chars, int16_t unused,
                                        int16_t base)
    MANGLED("?WriteKanaData@DictSearch@@QAEFPAU_TDICT_HEAD@@FFF@Z");
extern THIS int16_t ibm_dsLookupKanaDict(void *d, int16_t at)
    MANGLED("?LookupKanaDict@DictSearch@@QAEFF@Z");
extern THIS int16_t ibm_dsGenerateKanaString(void *d)
    MANGLED("?GenerateKanaString@DictSearch@@QAEFXZ");
extern THIS int16_t ibm_dsCompareKanji(void *d, const uint8_t *ent,
                                       int16_t which)
    MANGLED("?CompareKanji@DictSearch@@QAEFPAU_DCTB_ENT@@F@Z");
extern THIS int16_t ibm_dsWriteGWDict(void *d, const uint8_t *word,
                                      int16_t which, int16_t base, int16_t at,
                                      int16_t mode)
    MANGLED("?WriteGWDict@DictSearch@@QAEFPAU_DICT_ENT@@FFFF@Z");
extern THIS int16_t ibm_dsWriteDictTableData(void *d, const uint8_t *head,
                                             int16_t which, int16_t mode,
                                             int16_t at, int16_t base)
    MANGLED("?WriteDictTableData@DictSearch@@QAEFPAU_DCTB_HEAD@@FFFF@Z");
extern THIS int16_t ibm_dsGetDictEntry(void *d, int16_t which, int16_t at,
                                       int16_t base, const uint8_t *head,
                                       int16_t mode)
    MANGLED("?GetDictEntry@DictSearch@@QAEFFFFPAU_DCTB_HEAD@@F@Z");
extern THIS int16_t ibm_dsSearchTankanTable(void *d, int16_t which, int16_t at,
                                            int16_t base)
    MANGLED("?SearchTankanTable@DictSearch@@QAEFFFF@Z");
extern THIS int16_t ibm_dsGenerateWord(void *d, int16_t at, int16_t base)
    MANGLED("?GenerateWord@DictSearch@@QAEFFF@Z");
extern THIS void ibm_taAddLongWord(void *t, uint8_t *word, int16_t n)
    MANGLED("?AddLongWord@TextAnalysis@@QAEXPAEF@Z");

/* How many pages each dictionary has. Each side is asked its own -- IBM its
   constant, ours the count tools/rom/dictionary.py wrote beside the array -- so a
   lifted array of the wrong length shows up as a different number of lines
   rather than as nothing at all. */
extern const uint8_t *const ibm_s_apszSuppD[]
    MANGLED("?s_apszSuppD@StaticDict@@2PAPBEA");
extern const uint8_t *const ibm_s_apszSuppI[]
    MANGLED("?s_apszSuppI@StaticDict@@2PAPBEA");
#define SUPP_D ibm_s_apszSuppD[0]
#define SUPP_I ibm_s_apszSuppI[0]

extern THIS int32_t ibm_dsIsItaiji(void *d, uint16_t code)
    MANGLED("?IsItaiji@DictSearch@@QAEHG@Z");
extern THIS uint16_t ibm_dsSwapKanji(void *d, uint16_t code)
    MANGLED("?SwapKanji@DictSearch@@QAEGG@Z");
extern THIS int16_t ibm_dsErrorDummy(void *d, int16_t slot, int16_t at)
    MANGLED("?ErrorDummy@DictSearch@@QAEFFF@Z");
extern THIS int16_t ibm_dsWriteData(void *d, const uint8_t *head,
                                    int16_t chars, int16_t hiragana,
                                    int16_t base, int16_t last, int16_t at)
    MANGLED("?WriteData@DictSearch@@QAEFPAU_DICT_HEAD@@FFFFF@Z");
extern THIS int16_t ibm_dsWriteTankanData(void *d, const uint8_t *head,
                                          int16_t chars, int16_t base,
                                          int16_t at)
    MANGLED("?WriteTankanData@DictSearch@@QAEFPAU_TDICT_HEAD@@FFF@Z");
extern THIS int16_t ibm_dsWriteUserData(void *d, const uint8_t *head,
                                        int16_t slot, int16_t at)
    MANGLED("?WriteUserData@DictSearch@@QAEFPAU_UDICT_HEAD@@FF@Z");
extern THIS int16_t ibm_dsLookupUserDict(void *d, const uint8_t *dict,
                                         char *text, int16_t slot,
                                         const uint8_t *index, int16_t at,
                                         int16_t unused)
    MANGLED("?LookupUserDict@DictSearch@@QAEFPAE0F0FF@Z");
extern THIS int16_t ibm_dsLookupEngWordDict(void *d, uint8_t *roman,
                                            int16_t slot, int16_t at,
                                            int16_t want, int32_t mark)
    MANGLED("?LookupEngWordDict@DictSearch@@QAEFPAEFFFH@Z");
extern THIS int32_t ibm_dsLookupEngWordDictFromText(void *d, int16_t slot,
                                                    int16_t at)
    MANGLED("?LookupEngWordDict@DictSearch@@QAEHFF@Z");
extern THIS int16_t ibm_dsLookupTankanDict(void *d, int16_t base, int16_t at)
    MANGLED("?LookupTankanDict@DictSearch@@QAEFFF@Z");
extern THIS int16_t ibm_dsLookupNormalWordDict(void *d, int16_t base,
                                               int16_t at, int32_t swap)
    MANGLED("?LookupNormalWordDict@DictSearch@@QAEFFFH@Z");

extern THIS int16_t ibm_dsHitFuncWordDict(void *d, const uint8_t *head,
                                          int16_t slot, int16_t at,
                                          int16_t count, int16_t run,
                                          int16_t hiragana,
                                          const uint8_t *vec,
                                          const uint8_t *dict, int16_t flag)
    MANGLED("?HitFuncWordDict@DictSearch@@QAEFPAU_FZK_HEAD@@FFFFFPAE1F@Z");
extern THIS int16_t ibm_dsSearchFuncWordDict(void *d, const uint8_t *vec,
                                             int16_t at, int16_t slot,
                                             const uint8_t *dict,
                                             int16_t flag)
    MANGLED("?SearchFuncWordDict@DictSearch@@QAEFPAEFF0F@Z");
extern THIS int16_t ibm_dsLookupFuncWordDict(void *d, int16_t base,
                                             int16_t at)
    MANGLED("?LookupFuncWordDict@DictSearch@@QAEFFF@Z");
extern const uint8_t *ibm_dmGetFuncDictEx(void)
    MANGLED("?GetFuncDictEx@DictMan@@SAPAEXZ");
#define FUNC_DICT() ibm_dmGetFuncDictEx()

extern THIS int16_t ibm_dsEngRulesUppercase(void *d, const uint8_t *in,
                                            uint8_t *out)
    MANGLED("?EngRulesUppercase@DictSearch@@QAEFPBEPAE@Z");
extern THIS int16_t ibm_dsEngRulesNormalize(void *d, const uint8_t *in,
                                            uint8_t *out)
    MANGLED("?EngRulesNormalize@DictSearch@@QAEFPBEPAE@Z");
extern THIS int16_t ibm_dsEngRulesApplyRule(void *d, const uint8_t *in,
                                            uint8_t *out, void *r,
                                            int16_t *accent)
    MANGLED("?EngRulesApplyRule@DictSearch@@QAEFPBEPAEPAU_rules@@PAF@Z");
extern THIS int16_t ibm_dsEngRulesConvert(void *d, const uint8_t *in,
                                          uint8_t *out, void *eng, void *kana,
                                          int16_t *outLen, int16_t *count)
    MANGLED("?EngRulesConvert@DictSearch@@QAEFPBEPAEPAU_rules@@2PAF3@Z");

extern THIS void ibm_dsSetDummySymbol(void *d, int16_t at, void *e)
    MANGLED("?SetDummySymbol@DictSearch@@QAEXFPAU_DICTENT_T@@@Z");
extern THIS void ibm_dsSetDummyRomanAlphabet(void *d, int16_t at, void *e)
    MANGLED("?SetDummyRomanAlphabet@DictSearch@@QAEXFPAU_DICTENT_T@@@Z");
extern THIS void ibm_dsProcessRomanAlphabet(void *d, int16_t at, void *e)
    MANGLED("?ProcessRomanAlphabet@DictSearch@@QAEXFPAU_DICTENT_T@@@Z");
extern THIS int32_t ibm_dsNeedKatakanaAnalysis(void *d, int16_t base,
                                               int16_t n)
    MANGLED("?NeedKatakanaAnalysis@DictSearch@@QAEHFF@Z");
extern THIS int16_t ibm_dsCheckJrtTable(void *d, int16_t base, int16_t n)
    MANGLED("?CheckJrtTable@DictSearch@@QAEFFF@Z");
extern THIS int16_t ibm_dsCompareJMD(void *d, uint8_t *p, int16_t at,
                                     int16_t n)
    MANGLED("?CompareJMD@DictSearch@@QAEFPAEFF@Z");
extern THIS void ibm_dsSetJCC(void *d, const uint8_t *m, int16_t slot)
    MANGLED("?SetJCC@DictSearch@@QAEXPAU_JOS_MD@@F@Z");
extern THIS int16_t ibm_dsJoSuusiSearch(void *d, int16_t at)
    MANGLED("?JoSuusiSearch@DictSearch@@QAEFF@Z");
extern THIS int16_t ibm_dsHandleError(void *d, int16_t at, int16_t written,
                                      int16_t base, char *out)
    MANGLED("?HandleError@DictSearch@@QAEFFFFPAD@Z");
extern const uint8_t *ibm_dmGetNumJMDPtr(void)
    MANGLED("?GetNumJMDPtr@DictMan@@SAPAEXZ");
#define NUM_JMD() ibm_dmGetNumJMDPtr()

/* Annotation, whose record is IBM's 0x50c and ours a struct of named fields,
   so each side builds one with its own constructor. */
#define IBM_ANNO_ROOM 0x50c

extern THIS void *ibm_anCtor(void *a, void *analysis)
    MANGLED("??0Annotation@@QAE@AAVTextAnalysis@@@Z");
extern THIS int32_t ibm_anGetRomHandAnnoType(void *a, const char *s)
    MANGLED("?GetRomHandAnnoType@Annotation@@QAE?AW4RomHandleAnnoType@@PBD@Z");
extern THIS int32_t ibm_anSave(void *a, char *text, int16_t len, int16_t at)
    MANGLED("?Save@Annotation@@QAEHPADFF@Z");
extern THIS const char *ibm_anGetLastAnno(void *a, int16_t before,
                                          int32_t type)
    MANGLED("?GetLastAnno@Annotation@@QAEPBDFW4RomHandleAnnoType@@@Z");
extern THIS void ibm_anRemove(void *a)
    MANGLED("?Remove@Annotation@@QAEXXZ");
extern THIS void ibm_anRemoveAfter(void *a, int16_t after)
    MANGLED("?Remove@Annotation@@QAEXF@Z");
extern THIS int32_t ibm_anFlush(void *a, int32_t escape, void *out,
                                int32_t dropPause)
    MANGLED("?Flush@Annotation@@QAEHHPAUDynaBuf@@H@Z");

extern void *ibm_dynaBufNew(uint32_t n) MANGLED("_dynaBufNew");
extern int ibm_dynaBufDelete(void *b)   MANGLED("_dynaBufDelete");
extern char *ibm_dynaBufContents(void *b) MANGLED("_dynaBufContents");
extern uint32_t ibm_dynaBufLength(void *b) MANGLED("_dynaBufLength");
#define ANNO_ROOM   IBM_ANNO_ROOM
#define AN(name)    ibm_an##name
#define ANNO_CTOR(a, t) ibm_anCtor((a), (t))
#define BUF_NEW(n)  ibm_dynaBufNew(n)
#define BUF_DEL(b)  ibm_dynaBufDelete(b)
#define BUF_STR(b)  ibm_dynaBufContents(b)
#define BUF_LEN(b)  ibm_dynaBufLength(b)

/* IBM's two rule tables are declared for the sweep above already; the
   English rules want them as one struct rather than as its first field. */
#define ENG_RULES  ((void *)ibm_EngToRomanRule)
#define KANA_RULES ((void *)ibm_RomanToKanaRule)

extern const uint16_t ibm_s_nNormal MANGLED("?s_nNormal@StaticDict@@2GB");
extern const uint16_t ibm_s_nTankan MANGLED("?s_nTankan@StaticDict@@2GB");

#define NORMAL_N ((long)ibm_s_nNormal)
#define TANKAN_N ((long)ibm_s_nTankan)

/* And RomUserDict, which is written whole, so ours has named fields and only
   the harness has to know where IBM's are. */
#define IBM_UD_ROOM 0x40
#define UD_DICT(u)  (*(void **)((char *)(u) + 4))

extern THIS void *ibm_udCtor(void *u, void *analysis)
    MANGLED("??0RomUserDict@@QAE@PAVTextAnalysis@@@Z");
extern THIS int32_t ibm_udMakeKey(void *u, uint8_t *in, int32_t n, char *out,
                                  int32_t *outLen)
    MANGLED("?makeKey@RomUserDict@@QAEHPAEJPADPAJ@Z");
extern THIS int32_t ibm_udMakeTransValue(void *u, const char *in,
                                         uint8_t *accent, char *out,
                                         int16_t room)
    MANGLED("?makeTransValue@RomUserDict@@QAEHPBDPAEPADF@Z");
extern THIS uint8_t ibm_udTransKatakana2Yomi(void *u, char *kana,
                                             uint8_t *out)
    MANGLED("?transKatakana2Yomi@RomUserDict@@QAEEPADPAE@Z");
extern THIS uint8_t ibm_udTransKana2Yomi(void *u, char *kana, uint8_t *out)
    MANGLED("?transKana2Yomi@RomUserDict@@QAEEPADPAE@Z");
extern THIS int32_t ibm_udMakeUserDictData(void *u, void *d, uint8_t keyLen,
                                           char *kana, int32_t pos)
    MANGLED("?makeUserDictData@RomUserDict@@AAEHPAUUserDictData@@E"
            "PADW4ECIPartOfSpeech@@@Z");
extern THIS int32_t ibm_udWriteData(void *u, void *d, int16_t slot,
                                    int16_t at)
    MANGLED("?writeData@RomUserDict@@QAEHPAUUserDictData@@FF@Z");
extern THIS int16_t ibm_udLookup(void *u, uint8_t *text, int16_t at,
                                 int16_t slot)
    MANGLED("?lookup@RomUserDict@@QAEFPAEFF@Z");
extern THIS int32_t ibm_udUpdateDictExt(void *u, void *list, int32_t which,
                                        uint8_t *word, int32_t wordLen,
                                        char *kana, int32_t kanaLen,
                                        int32_t pos)
    MANGLED("?updateDictExt@RomUserDict@@QAEJPAXJ0J0JW4ECIPartOfSpeech@@@Z");
extern THIS int32_t ibm_udLookupDictExt(void *u, void *list, int32_t which,
                                        uint8_t *word, int32_t wordLen,
                                        void **value, int32_t *valueLen,
                                        int32_t *pos)
    MANGLED("?lookupDictExt@RomUserDict@@QAEJPAXJ0JPAPAXPAJPA"
            "W4ECIPartOfSpeech@@@Z");

#define UD(name) ibm_ud##name

/* InputChar, whose record is IBM's on both sides, so each side is handed the
   same block of bytes and only the three pointers in it sit elsewhere. */
extern THIS void *ibm_icCtor(void *in, void *analysis)
    MANGLED("??0InputChar@@QAE@AAVTextAnalysis@@@Z");
extern THIS void ibm_icInit(void *in)
    MANGLED("?Init@InputChar@@QAEXXZ");
extern THIS void ibm_icSetText(void *in, const char *text)
    MANGLED("?SetText@InputChar@@QAEXPBD@Z");
extern THIS void ibm_icSetTextAt(void *in, const char *text, uint32_t at)
    MANGLED("?SetText@InputChar@@QAEXPBDK@Z");
extern THIS uint8_t ibm_icGetNextChar(void *in)
    MANGLED("?GetNextChar@InputChar@@QAEEXZ");
extern THIS int32_t ibm_icIsAnnotationsInText(void *in)
    MANGLED("?IsAnnotationsInText@InputChar@@QAEHXZ");
extern THIS int32_t ibm_icAddSnlkTable(void *in, int16_t at,
                                       const char *written,
                                       const char *reading, int32_t flag)
    MANGLED("?AddSnlkTable@InputChar@@QAEHFPBD0H@Z");
extern THIS void *ibm_icGetSnlkTableAt(void *in, int16_t at)
    MANGLED("?GetSnlkTableAt@InputChar@@QAEPAU_SNLK_TABLE@@F@Z");
extern THIS void ibm_icDeleteSnlkTable(void *in)
    MANGLED("?DeleteSnlkTable@InputChar@@QAEXXZ");
extern THIS int16_t ibm_icGetUnknownKanji(void *in, int16_t at, int32_t from,
                                          int32_t to)
    MANGLED("?GetUnknownKanji@InputChar@@QAEFFJJ@Z");

extern THIS int32_t ibm_icIsKanjiNum(void *in, int32_t at)
    MANGLED("?IsKanjiNum@InputChar@@QAEHH@Z");
extern THIS int32_t ibm_icGetCharType(void *in, int16_t at)
    MANGLED("?GetCharType@InputChar@@QAE?AW4CharType@@F@Z");
extern THIS int32_t ibm_icCheckNextAnnotation(void *in)
    MANGLED("?CheckNextAnnotation@InputChar@@QAEJXZ");
extern THIS int16_t ibm_icCheckCyuTen(void *in, int16_t *at)
    MANGLED("?CheckCyuTen@InputChar@@QAEFPAF@Z");
extern THIS int16_t ibm_icCheckContextForNum(void *in, int16_t *at)
    MANGLED("?CheckContextForNum@InputChar@@QAEFPAF@Z");
extern THIS int16_t ibm_icCheckContext(void *in, int16_t *at, int32_t peek)
    MANGLED("?CheckContext@InputChar@@QAEFPAFH@Z");
extern THIS void ibm_icConvertDakuten(void *in, int16_t at, uint8_t c,
                                      uint8_t mark)
    MANGLED("?ConvertDakuten@InputChar@@QAEXFEE@Z");
extern THIS int16_t ibm_icProcessASCII(void *in, int16_t at, uint8_t *c0,
                                       uint8_t *c1)
    MANGLED("?ProcessASCII@InputChar@@QAEFFPAE0@Z");
extern THIS int8_t ibm_icProcessAnnotation(void *in, int16_t at)
    MANGLED("?ProcessAnnotation@InputChar@@QAECF@Z");
extern THIS void ibm_icRecoverOverflow(void *in, int16_t at)
    MANGLED("?RecoverOverflow@InputChar@@QAEXF@Z");
extern THIS int16_t ibm_icReadSentence(void *in)
    MANGLED("?ReadSentence@InputChar@@QAEFXZ");

#define IC(name) ibm_ic##name


#define PARAM_ANNO(p) (*(int32_t *)((char *)(p) + 0x10))
#define UD_ROOM  IBM_UD_ROOM

extern THIS int16_t ibm_dsIsMember(void *d, uint8_t *p, const uint8_t *table,
                                   int16_t n)
    MANGLED("?IsMember@DictSearch@@QAEFPAEQAY01EF@Z");
extern THIS int16_t ibm_dsIsZKNum(void *d, uint8_t *p)
    MANGLED("?IsZKNum@DictSearch@@QAEFPAE@Z");
extern THIS int16_t ibm_dsIsZSNum(void *d, uint8_t *p)
    MANGLED("?IsZSNum@DictSearch@@QAEFPAE@Z");
extern THIS int16_t ibm_dsIsZKeta(void *d, uint8_t *p)
    MANGLED("?IsZKeta@DictSearch@@QAEFPAE@Z");
extern THIS int16_t ibm_dsIsZSymb(void *d, uint8_t *p)
    MANGLED("?IsZSymb@DictSearch@@QAEFPAE@Z");
extern THIS int32_t ibm_dsIsCommaPosition(void *d, char *p, int32_t n)
    MANGLED("?IsCommaPosition@DictSearch@@QAEHPADH@Z");
extern THIS int32_t ibm_dsIsEndOfQuote(void *d, int16_t at)
    MANGLED("?IsEndOfQuote@DictSearch@@QAEHF@Z");
extern THIS int16_t ibm_dsCheckKetaOrder(void *d, int16_t *n, int16_t *chars,
                                         int16_t *keepN, int16_t *keepChars,
                                         int16_t keta, uint8_t *buf)
    MANGLED("?CheckKetaOrder@DictSearch@@QAEFPAF000FPAE@Z");
extern THIS int16_t ibm_dsSetSuushiWord(void *d, int16_t slot, int16_t at)
    MANGLED("?SetSuushiWord@DictSearch@@QAEFFF@Z");
extern THIS int16_t ibm_dsSetDummyWord(void *d, int16_t slot, int16_t at)
    MANGLED("?SetDummyWord@DictSearch@@QAEFFF@Z");
extern THIS void *ibm_dsgetPtrOfUserDict(void *d)
    MANGLED("?getPtrOfUserDict@DictSearch@@QAEPAVRomUserDict@@XZ");

extern THIS int16_t ibm_dsDo(void *d)
    MANGLED("?Do@DictSearch@@QAEFXZ");
extern THIS int32_t ibm_rzGetParameter(void *rz, char *p)
    MANGLED("?GetParameter@Romanizer@@AAEHPAD@Z");
#define RZ(name) ibm_rz##name

extern THIS int16_t ibm_dsHitFuncWordReverse(void *d, const uint8_t *head,
                                             int16_t slot, uint16_t at,
                                             int16_t count, uint8_t chars,
                                             uint8_t hiragana, uint8_t *vec,
                                             const uint8_t *base)
    MANGLED("?HitFuncWordReverse@DictSearch@@QAEFPAU_FZK_HEAD@@FGFEEPAE1@Z");
extern THIS int16_t ibm_dsFzkSearchUnknown(void *d, uint8_t *vec, uint16_t at,
                                           int16_t slot, const uint8_t *dict,
                                           int16_t unused)
    MANGLED("?FzkSearchUnknown@DictSearch@@QAEFPAEGF0F@Z");
extern THIS int16_t ibm_dsFzkParsing(void *d, uint8_t *vec, int16_t at)
    MANGLED("?FzkParsing@DictSearch@@QAEFPAEF@Z");
extern THIS int16_t ibm_dsFzkParsingReverse(void *d)
    MANGLED("?FzkParsingReverse@DictSearch@@QAEFXZ");

#define DS(name) ibm_ds##name

#define DM(name) ibm_##name
#define JU(name) ibm_##name
#define STATIC_DICT_INIT() ibm_StaticDictInitialize()



/* ---- PhraseBuf -------------------------------------------------------- */

extern THIS void *ibm_pbCtor(void *pb, void *ta)
    MANGLED("??0PhraseBuf@@QAE@AAVTextAnalysis@@@Z");
extern THIS void ibm_pbCopy(void *pb, int16_t which)
    MANGLED("?Copy@PhraseBuf@@QAEXF@Z");
extern THIS void ibm_pbModifyPos(void *pb, uint8_t *out, uint8_t pos)
    MANGLED("?ModifyPos@PhraseBuf@@QAEXPAEE@Z");
extern THIS int32_t ibm_pbIsBunsetsuEnd(void *pb, const uint8_t *sub)
    MANGLED("?IsBunsetsuEnd@PhraseBuf@@QAEHPAU_J_SUB_T@@@Z");
extern THIS int32_t ibm_pbIsSokuonTankanVerb(void *pb, const uint8_t *sub)
    MANGLED("?IsSokuonTankanVerb@PhraseBuf@@QAEHPAU_J_SUB_T@@@Z");
extern THIS int16_t ibm_pbGetSpecialPhraseType(void *pb, const uint8_t *w)
    MANGLED("?GetSpecialPhraseType@PhraseBuf@@QAEFPAU_W_PHRASE_T@@@Z");
extern THIS int16_t ibm_pbChkTTELink(void *pb, int32_t sokuon,
                                     const uint8_t *f)
    MANGLED("?ChkTTELink@PhraseBuf@@QAEFHPAU_P_FZK_T@@@Z");
extern THIS void ibm_pbSetJrt(void *pb, const uint8_t *path, uint8_t *w,
                              int16_t *outKana, int16_t *outAccent)
    MANGLED("?SetJrt@PhraseBuf@@QAEXPAU_J_PATH_T@@PAU_W_PHRASE_T@@PAF2@Z");
extern THIS int16_t ibm_pbSetPhrasePart(void *pb, const uint8_t *path,
                                        int16_t n, int16_t fzk,
                                        int32_t sokuon, uint8_t *out)
    MANGLED("?SetPhrasePart@PhraseBuf@@QAEFPAU_J_PATH_T@@FFHQAU_W_PHRASE_T@@@Z");
extern THIS int16_t ibm_pbSetPhraseBuffer(void *pb, uint8_t *out)
    MANGLED("?SetPhraseBuffer@PhraseBuf@@QAEFPAU_W_PHRASE_T@@@Z");
#define PB(name) ibm_pb##name
/* ---- JPath ------------------------------------------------------------ */

/* Nine of these eleven are private members in IBM's own source. Access
   control is a matter for its compiler and not for its linker: each one has
   an external symbol in the object all the same, so each can be called here
   and held to on its own. */
extern THIS void *ibm_jpCtor(void *jp, void *ta)
    MANGLED("??0JPath@@QAE@AAVTextAnalysis@@@Z");
extern THIS void ibm_jpMake(void *jp, int16_t at)
    MANGLED("?Make@JPath@@QAEXF@Z");
extern THIS int32_t ibm_jpAddPath(void *jp, const uint8_t *path,
                                  const uint8_t *entry, uint8_t *out,
                                  int16_t nPaths, int16_t entryIndex)
    MANGLED("?AddPath@JPath@@QAEHPAU_J_PATH_T@@PAU_DICTENT_T@@0FF@Z");
extern THIS int16_t ibm_jpCheckType(void *jp, const uint8_t *tg)
    MANGLED("?CheckType@JPath@@IAEFPAE@Z");
extern THIS int16_t ibm_jpCheckAdFlag(void *jp, const uint8_t *lt,
                                      const uint8_t *rt, const uint8_t *la,
                                      const uint8_t *ra, int16_t cost)
    MANGLED("?CheckAdFlag@JPath@@IAEFPAE000F@Z");
extern THIS int16_t ibm_jpJrtJrtCheck(void *jp, const uint8_t *lt,
                                      const uint8_t *rt, const uint8_t *la,
                                      const uint8_t *ra, int32_t adjust)
    MANGLED("?JrtJrtCheck@JPath@@IAEFPAE000H@Z");
extern THIS int32_t ibm_jpIsHead(void *jp, const uint8_t *e)
    MANGLED("?IsHead@JPath@@IAEHPAU_DICTENT_T@@@Z");
extern THIS int32_t ibm_jpIsEnd(void *jp, const uint8_t *e)
    MANGLED("?IsEnd@JPath@@IAEHPAU_DICTENT_T@@@Z");
extern THIS int32_t ibm_jpIsContinuable(void *jp, const uint8_t *e)
    MANGLED("?IsContinuable@JPath@@IAEHPAU_DICTENT_T@@@Z");
extern THIS int16_t ibm_jpGetMoraOnPath(void *jp, const uint8_t *path,
                                        int16_t extra)
    MANGLED("?GetMoraOnPath@JPath@@IAEFPAU_J_PATH_T@@F@Z");
extern THIS void ibm_jpSetWordAttr(void *jp, uint8_t *sub, const uint8_t *e)
    MANGLED("?SetWordAttr@JPath@@IAEXPAU_J_SUB_T@@PAU_DICTENT_T@@@Z");
extern THIS void ibm_jpMakeJrtSubTable(void *jp)
    MANGLED("?MakeJrtSubTable@JPath@@IAEXXZ");
#define JP(name) ibm_jp##name
/* ---- NumRead --------------------------------------------------------- */

extern THIS void ibm_nrInit(void *nr)
    MANGLED("?Init@NumRead@@QAEXXZ");
extern THIS int16_t ibm_nrSegmentYomiBlock(void *nr, const uint8_t *w,
                                           int16_t word)
    MANGLED("?SegmentYomiBlock@NumRead@@QAEFPAU_W_PHRASE_T@@F@Z");
extern THIS int16_t ibm_nrSetYomiType(void *nr, int16_t howmany)
    MANGLED("?SetYomiType@NumRead@@QAEFF@Z");
extern THIS void ibm_nrGenerateStdForm(void *nr, int16_t howmany)
    MANGLED("?GenerateStdForm@NumRead@@QAEXF@Z");
extern THIS int16_t ibm_nrApplySRule(void *nr, int16_t howmany, int16_t *got)
    MANGLED("?ApplySRule@NumRead@@QAEFFPAF@Z");
extern THIS int16_t ibm_nrApplySRuleToKetaYomi(void *nr, int16_t which,
                                               int16_t n, int16_t at,
                                               int16_t *got,
                                               const uint8_t *ss)
    MANGLED("?ApplySRuleToKetaYomi@NumRead@@QAEFFFFQAFPAU_substr_t@@@Z");
extern THIS int16_t ibm_nrApplySRuleToBouYomi(void *nr, int16_t n, int16_t at,
                                              int16_t *got,
                                              const uint8_t *ss)
    MANGLED("?ApplySRuleToBouYomi@NumRead@@QAEFFFQAFPAU_substr_t@@@Z");
extern THIS int16_t ibm_nrApplySRuleToShosu(void *nr, int16_t n, int16_t at,
                                            int16_t *got,
                                            const uint8_t *prev,
                                            const uint8_t *ss)
    MANGLED("?ApplySRuleToShosu@NumRead@@QAEFFFQAFPAU_substr_t@@1@Z");
extern THIS int16_t ibm_nrApplySRuleToBunsu(void *nr, int16_t n, int16_t at,
                                            int16_t *got,
                                            const uint8_t *ss)
    MANGLED("?ApplySRuleToBunsu@NumRead@@QAEFFFQAFPAU_substr_t@@@Z");
extern THIS int16_t ibm_nrApplyJRule(void *nr, const uint8_t *w, int16_t word,
                                     int16_t n, int16_t howmany,
                                     int16_t *got)
    MANGLED("?ApplyJRule@NumRead@@QAEFPAU_W_PHRASE_T@@FFFPAF@Z");
extern THIS int16_t ibm_nrDo(void *nr, const uint8_t *w, int16_t *pWord,
                             int16_t *pOut)
    MANGLED("?Do@NumRead@@QAEFPAU_W_PHRASE_T@@PAF1@Z");
#define NR(name) ibm_nr##name

/* ---- IntonPhrase ----------------------------------------------------- */

/* Fifteen of these seventeen are private members in IBM's own source, and
   each has an external symbol in the object all the same, so each is held to
   IBM's own answer here rather than only through the entry point. */
extern THIS int16_t ibm_ipTableAllocBG(void *ip, uint16_t *count,
                                       uint16_t *last, uint16_t *at,
                                       uint8_t *link, uint16_t n)
    MANGLED("?TableAllocBG@IntonPhrase@@IAEFPAG00PAU_LINK_TBL_T@@G@Z");
extern THIS void *ibm_ipBreathGroupAlloc(void *ip)
    MANGLED("?BreathGroupAlloc@IntonPhrase@@IAEPAXXZ");
extern THIS void ibm_ipInitPhraseTable(void *ip, int16_t n)
    MANGLED("?InitPhraseTable@IntonPhrase@@QAEXF@Z");
extern THIS void ibm_ipCheckPhraseToPhrase(void *ip, uint8_t *st)
    MANGLED("?CheckPhraseToPhrase@IntonPhrase@@IAEXQAE@Z");
extern THIS void ibm_ipProsodyControl(void *ip)
    MANGLED("?ProsodyControl@IntonPhrase@@IAEXXZ");
extern THIS void ibm_ipThreePhraseParsing(void *ip, void *table)
    MANGLED("?ThreePhraseParsing@IntonPhrase@@QAEXPAU_PHR_TBL_T@@@Z");
extern THIS int16_t ibm_ipModifyPType(void *ip, uint8_t type)
    MANGLED("?ModifyPType@IntonPhrase@@IAEFE@Z");
extern THIS uint8_t ibm_ipCheckBreathGroup(void *ip, uint8_t *st,
                                           uint8_t *right, uint8_t kind)
    MANGLED("?CheckBreathGroup@IntonPhrase@@IAEEPAE0E@Z");
extern THIS int16_t ibm_ipCheckChoon(void *ip, const uint8_t *t, int16_t at)
    MANGLED("?CheckChoon@IntonPhrase@@IAEFPAU_PHR_TBL_T@@F@Z");
extern THIS void ibm_ipSetPhraseState(void *ip)
    MANGLED("?SetPhraseState@IntonPhrase@@IAEXXZ");
extern THIS uint8_t ibm_ipPhraseParsing(void *ip, uint8_t *l, uint8_t *r,
                                        uint8_t group, int32_t odd)
    MANGLED("?PhraseParsing@IntonPhrase@@IAEEPAE0EH@Z");
extern THIS void ibm_ipRegroupPhrases(void *ip)
    MANGLED("?RegroupPhrases@IntonPhrase@@IAEXXZ");
extern THIS int16_t ibm_ipPhraseSeparate(void *ip, void *start, void *end,
                                         int16_t moras, int16_t limit,
                                         int16_t floor_, int16_t mark)
    MANGLED("?PhraseSeparate@IntonPhrase@@IAEFPAU_PHR_TBL_T@@0FFFF@Z");
extern THIS void ibm_ipSetPauseLength(void *ip)
    MANGLED("?SetPauseLength@IntonPhrase@@IAEXXZ");
extern THIS void ibm_ipSetPitchValues(void *ip)
    MANGLED("?SetPitchValues@IntonPhrase@@IAEXXZ");
extern THIS int16_t ibm_ipSetIntonationalPhrase(void *ip)
    MANGLED("?SetIntonationalPhrase@IntonPhrase@@IAEFXZ");
extern THIS uint8_t ibm_ipSetAccentualPhrase(void *ip, void *ph, uint8_t at)
    MANGLED("?SetAccentualPhrase@IntonPhrase@@IAEEPAU_PH1_T@@E@Z");
#define IPM(name) ibm_ip##name

/* ---- ProsCtrl -------------------------------------------------------- */

/* Fourteen of these sixteen are private members in IBM's own source and each
   has an external symbol all the same, so each is held to IBM's own answer.
   The four records this class builds have their pointers parked past them on
   our side and at IBM's own offsets on IBM's, so the sweep walks them through
   macros rather than comparing them as bytes. */
extern THIS void *ibm_pcCtor(void *pc)
    MANGLED("??0ProsCtrl@@QAE@XZ");
extern THIS void ibm_pcDtor(void *pc)
    MANGLED("??1ProsCtrl@@QAE@XZ");
extern THIS int32_t ibm_pcGenerateESPR(void *pc, const void *env,
                                       int32_t param, const char *text,
                                       const void *bgt, char *out,
                                       uint32_t cap)
    MANGLED("?GenerateESPR@ProsCtrl@@QAEHPBUENVPARAMS@@HPBDPBU_BG_T@@PADK@Z");
extern THIS int32_t ibm_pcBG_T2BreathGroups(void *pc, const void *bgt,
                                            void **outGroups,
                                            int32_t *outCount)
    MANGLED("?BG_T2BreathGroups@ProsCtrl@@AAEHPBU_BG_T@@PAPAUBREATHGROUP@@PAH@Z");
extern THIS void ibm_pcFreeBreathGroups(void *pc, void *groups, int32_t n)
    MANGLED("?FreeBreathGroups@ProsCtrl@@AAEXPAUBREATHGROUP@@H@Z");
extern THIS int32_t ibm_pcWriteESPR2(void *pc, void *groups, int32_t count,
                                     int32_t ms, char *out, uint32_t cap)
    MANGLED("?WriteESPR2@ProsCtrl@@AAEHPBUBREATHGROUP@@HHPADK@Z");
extern THIS int32_t ibm_pcWriteGokiInfo(void *pc, const uint8_t *ap,
                                        int32_t which, int32_t kind,
                                        int32_t *at, char *buf, char *out,
                                        uint32_t cap, uint32_t *len)
    MANGLED("?WriteGokiInfo@ProsCtrl@@AAEHPBUACC_PHRASE@@HHPAHPAD2KPAK@Z");
extern THIS int32_t ibm_pcGetGokiInfoToWrite(void *pc, const uint8_t *ap,
                                             int32_t *m, int32_t *first,
                                             int32_t *upto, int32_t *pos,
                                             const char **name,
                                             int32_t stress, int32_t kind)
    MANGLED("?GetGokiInfoToWrite@ProsCtrl@@AAEHPBUACC_PHRASE@@PAH111PAPBDHH@Z");
extern THIS int32_t ibm_pcWriteBGInfo(void *pc, int32_t ms, int32_t kind,
                                      int32_t last, char *out, uint32_t cap,
                                      uint32_t *len)
    MANGLED("?WriteBGInfo@ProsCtrl@@AAEHHHHPADKPAK@Z");
extern THIS int32_t ibm_pcWriteStressLevel(void *pc, int32_t at, int32_t from,
                                           int32_t to, char *out,
                                           uint32_t cap, uint32_t *len,
                                           int32_t force)
    MANGLED("?WriteStressLevel@ProsCtrl@@AAEHHHHPADKPAKH@Z");
extern THIS int32_t ibm_pcWriteUserIndex(void *pc, int32_t n, char *out,
                                         uint32_t cap, uint32_t *len)
    MANGLED("?WriteUserIndex@ProsCtrl@@AAEHHPADKPAK@Z");
extern THIS int32_t ibm_pcWriteDummyF0Pair(void *pc, int32_t n, char *out,
                                           uint32_t cap, uint32_t *len)
    MANGLED("?WriteDummyF0Pair@ProsCtrl@@AAEHHPADKPAK@Z");
extern THIS int32_t ibm_pcWriteToOutBuf(void *pc, const char *what,
                                        char *out, uint32_t cap,
                                        uint32_t *len)
    MANGLED("?WriteToOutBuf@ProsCtrl@@AAEHPBDPADKPAK@Z");
extern THIS int32_t ibm_pcModifyWordProminence(void *pc, int32_t *prom,
                                               int32_t pos, int32_t flag)
    MANGLED("?ModifyWordProminence@ProsCtrl@@AAEHPAHHH@Z");
extern THIS int32_t ibm_pcIsBurstCons(void *pc, uint8_t code)
    MANGLED("?IsBurstCons@ProsCtrl@@AAEHE@Z");
extern THIS int32_t ibm_pcIsValidConsForSokuOn(void *pc, uint8_t code)
    MANGLED("?IsValidConsForSokuOn@ProsCtrl@@AAEHE@Z");
#define PCM(name) ibm_pc##name

/* ---- MakeReadableJP --------------------------------------------------- */

/* All but the six virtual normalisers are private members in IBM's source,
   and each has an external symbol all the same. Both classes hold nothing but
   a vtable, so the harness hands in a block of its own and neither side
   dispatches through it. */
extern THIS void *ibm_mrCtor(void *mr)
    MANGLED("??0MakeReadableJP@@QAE@XZ");
extern THIS void ibm_mrDtor(void *mr)
    MANGLED("??1MakeReadableJP@@UAE@XZ");
extern THIS int32_t ibm_mrlCopyAndReturn(void *mr, const char *text,
                                         uint32_t n, char **buf,
                                         uint32_t *cap)
    MANGLED("?copyAndReturn@MakeReadableLangInt@@IAEHPBDIPAPADPAI@Z");
extern THIS int32_t ibm_mrReallocateBuf(void *mr, char **buf, uint32_t used,
                                        uint32_t want)
    MANGLED("?reallocateBuf@MakeReadableJP@@AAEHPAPADII@Z");
extern THIS int32_t ibm_mrAppendText(void *mr, const char *text, char **buf,
                                     uint32_t *cap, uint32_t *len)
    MANGLED("?appendText@MakeReadableJP@@AAEHPBDPAPADPAI2@Z");
extern THIS int32_t ibm_mrAppendTextN(void *mr, const char *text, uint32_t n,
                                      char **buf, uint32_t *cap,
                                      uint32_t *len)
    MANGLED("?appendText@MakeReadableJP@@AAEHPBDIPAPADPAI2@Z");
extern THIS int32_t ibm_mrAppendChar(void *mr, const char *c, char **buf,
                                     uint32_t *cap, uint32_t *len)
    MANGLED("?appendChar@MakeReadableJP@@AAEHPBDPAPADPAI2@Z");
extern THIS int32_t ibm_mrAppendNumber(void *mr, void *num, char **buf,
                                       uint32_t *cap, uint32_t *len,
                                       int32_t trimZeros)
    MANGLED("?appendMakeReadableNumber@MakeReadableJP@@AAEHPAUMAKEREADABLE_NUMBER@@PAPADPAI2H@Z");
extern THIS int32_t ibm_mrSeparate(void *mr, const void *whole, void *left,
                                   void *right)
    MANGLED("?separateNumberByDecimalPoint@MakeReadableJP@@AAEHPBUMAKEREADABLE_NUMBER@@PAU2@1@Z");
extern THIS const char *ibm_mrSuppressZero(void *mr, const char *p,
                                           const char *end)
    MANGLED("?suppressZero@MakeReadableJP@@AAEPBDPBD0@Z");
extern THIS int32_t ibm_mrIsCurrencySymbol(void *mr, const char *t,
                                           uint32_t *howLong)
    MANGLED("?isCurrencySymbol@MakeReadableJP@@AAEHPBDPAI@Z");
extern THIS int32_t ibm_mrIsBoolSymbol(void *mr, const char *t,
                                       uint32_t *howLong)
    MANGLED("?isBoolSymbol@MakeReadableJP@@AAEHPBDPAI@Z");
extern THIS int32_t ibm_mrIsCurrencyPunct(void *mr, const char *t)
    MANGLED("?isCurrencyPunct@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsDecimalPoint(void *mr, const char *t)
    MANGLED("?isDecimalPoint@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsParenthesis(void *mr, const char *t)
    MANGLED("?isParenthesis@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsTimeDelimiter(void *mr, const char *t)
    MANGLED("?isTimeDelimiter@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsPlusMinusSymbol(void *mr, const char *t)
    MANGLED("?isPlusMinusSymbol@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsDayOfWeek(void *mr, const char *t)
    MANGLED("?isDayOfWeek@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsRangeSymbol(void *mr, const char *t)
    MANGLED("?isRangeSymbol@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsDateSeparator(void *mr, const char *t)
    MANGLED("?isDateSeparator@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsTelSymbol(void *mr, const char *t)
    MANGLED("?isTelSymbol@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsDBCSDigit(void *mr, const char *t)
    MANGLED("?isDBCSDigit@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrIsDigit(void *mr, const char *t)
    MANGLED("?isDigit@MakeReadableJP@@AAEHPBD@Z");
extern THIS int32_t ibm_mrNormalizeDigits(void *mr, const char *t,
                                          uint32_t n, char **b, uint32_t *c,
                                          int32_t f)
    MANGLED("?normalizeDigits@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeLiteral(void *mr, const char *t,
                                           uint32_t n, char **b, uint32_t *c,
                                           int32_t f)
    MANGLED("?normalizeLiteral@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeBool(void *mr, const char *t, uint32_t n,
                                        char **b, uint32_t *c, int32_t f)
    MANGLED("?normalizeBool@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeNumber(void *mr, const char *t,
                                          uint32_t n, char **b, uint32_t *c,
                                          int32_t f)
    MANGLED("?normalizeNumber@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizePhone(void *mr, const char *t, uint32_t n,
                                         char **b, uint32_t *c, int32_t f)
    MANGLED("?normalizePhone@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeTime(void *mr, const char *t, uint32_t n,
                                        char **b, uint32_t *c, int32_t f)
    MANGLED("?normalizeTime@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeCurrency(void *mr, const char *t,
                                            uint32_t n, char **b,
                                            uint32_t *c, int32_t f)
    MANGLED("?normalizeCurrency@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrNormalizeDate(void *mr, const char *t, uint32_t n,
                                        char **b, uint32_t *c, int32_t f)
    MANGLED("?normalizeDate@MakeReadableJP@@UAEHPBDIPAPADPAIH@Z");
extern THIS int32_t ibm_mrConvertSPR(void *mr, const char *t, uint32_t n,
                                     char **b, uint32_t *c)
    MANGLED("?convertSPR@MakeReadableJP@@QAEHPBDIPAPADPAI@Z");
#define MRM(name) ibm_mr##name

extern THIS void *ibm_tnCtor(void *tn)
    MANGLED("??0TextNormalizer@@QAE@XZ");
extern THIS void ibm_tnDtor(void *tn)
    MANGLED("??1TextNormalizer@@QAE@XZ");
extern THIS int32_t ibm_tnReallocateBuf(void *tn, char **b, uint32_t used,
                                        uint32_t want)
    MANGLED("?reallocateBuf@TextNormalizer@@AAEHPAPADII@Z");
extern THIS int32_t ibm_tnGetAnnoType(void *tn, const char *t,
                                      const char **argStart,
                                      const char **after)
    MANGLED("?getAnnoType@TextNormalizer@@AAEHPBDPAPBD1@Z");
extern THIS int32_t ibm_tnMakeReadable(void *tn, const char *t, int32_t n,
                                       char **b, uint32_t *c, int32_t f)
    MANGLED("?makeReadable@TextNormalizer@@AAEHPBDHPAPADPAIH@Z");
extern THIS int32_t ibm_tnNormalizeText(void *tn, const char *t, uint32_t n,
                                        char **b, uint32_t *l)
    MANGLED("?normalizeText@TextNormalizer@@QAEHPBDIPAPADPAI@Z");
#define TNM(name) ibm_tn##name

extern THIS uint8_t ibm_ptbGetPosFromTG(void *pt, uint8_t tg)
    MANGLED("?GetPosFromTG@PhraseTable@@QAEEE@Z");
extern THIS uint8_t ibm_ptbGetFzkPosFromTG(void *pt, uint8_t tg)
    MANGLED("?GetFzkPosFromTG@PhraseTable@@QAEEE@Z");
extern THIS int16_t ibm_ptbGetAffixType(void *pt, uint8_t *tg)
    MANGLED("?GetAffixType@PhraseTable@@QAEFPAE@Z");
extern THIS int16_t ibm_ptbTableAllocPhrase(void *pt, uint16_t *first,
                                            uint16_t *last, uint16_t *spare,
                                            uint8_t *link, uint16_t count)
    MANGLED("?TableAllocPhrase@PhraseTable@@QAEFPAG00PAU_LINK_TBL_T@@G@Z");
extern THIS void *ibm_ptbPhraseAlloc(void *pt)
    MANGLED("?PhraseAlloc@PhraseTable@@QAEPAXXZ");
extern THIS void *ibm_ptbGeneratePhraseTable(void *pt)
    MANGLED("?GeneratePhraseTable@PhraseTable@@QAEPAU_PHR_TBL_T@@XZ");
extern THIS void ibm_ptbExtKKRPhrase(void *pt, uint8_t *kkr, int16_t tg,
                                     uint8_t *other)
    MANGLED("?ExtKKRPhrase@PhraseTable@@QAEXPAEF0@Z");
extern THIS void ibm_ptbSetSubUkeType(void *pt, uint8_t *uke, int16_t tg,
                                      uint8_t *flag)
    MANGLED("?SetSubUkeType@PhraseTable@@QAEXPAEF0@Z");
extern THIS void ibm_ptbSetNoneFzkKKR(void *pt, uint8_t *kkr, void *wp)
    MANGLED("?SetNoneFzkKKR@PhraseTable@@QAEXPAEPAU_W_PHRASE_T@@@Z");
extern THIS int16_t ibm_ptbSetUkeTypePhrase(void *pt, uint8_t *uke, void *wp)
    MANGLED("?SetUkeTypePhrase@PhraseTable@@QAEFPAEPAU_W_PHRASE_T@@@Z");
extern THIS void ibm_ptbFzkAccent(void *pt, uint8_t *in, uint8_t *out)
    MANGLED("?FzkAccent@PhraseTable@@QAEXPAU_ACC_IN_T@@PAU_ACC_OUT_T@@@Z");
extern THIS int16_t ibm_ptbCompoundWord(void *pt, void *wp, void *row)
    MANGLED("?CompoundWord@PhraseTable@@QAEFPAU_W_PHRASE_T@@PAU_PHR_TBL_T@@@Z");
#define PTBM(name) ibm_ptb##name
#define PTB_SET(blk, which, p) (*(void **)((blk) + which) = (p))
#define AI_RULE_SET(in, i, p) \
    (*(uint8_t **)((uint8_t *)(in) + AI_RULE + (i) * 4) = (p))

#define PB_SET(blk, which, p) (*(void **)((blk) + which) = (p))
#define JP_SET(blk, which, p) (*(void **)((blk) + which) = (p))


/* ---- the surface: ConverterInterface, InputManager, the codesets ------ */

extern THIS int32_t ibm_paramSetParam(Param *p, int32_t which, int32_t v)
    MANGLED("?setParam@RomInstParam@@QAEHJH@Z");
extern THIS uint32_t ibm_paramGetErrors(Param *p)
    MANGLED("?getErrors@RomInstParam@@QAEKXZ");
extern THIS void ibm_paramClearErrors(Param *p)
    MANGLED("?clearErrors@RomInstParam@@QAEXXZ");
extern THIS int32_t ibm_paramGetCodeSet(Param *p)
    MANGLED("?getCodeSet@RomInstParam@@QAE?AW4ECILanguageDialect@@XZ");
#define PARAM_SET(p, w, v)  ibm_paramSetParam((p), (w), (v))
#define PARAM_ERRORS(p)     ibm_paramGetErrors((p))
#define PARAM_CLEAR(p)      ibm_paramClearErrors((p))
#define PARAM_CODESET(p)    ibm_paramGetCodeSet((p))
#define PARAM_OWNER(p)      (*(void **)(p))

extern THIS void ibm_ciInitBase(void *c, Param *p)
    MANGLED("?initBase@ConverterInterface@@QAEXPAVRomInstParam@@@Z");
extern THIS void ibm_ciCloseBase(void *c)
    MANGLED("?closeBase@ConverterInterface@@QAEXXZ");
extern THIS int32_t ibm_ciUCS2ToMBCS(void *c, const uint16_t *in, char **out,
                                     int32_t yen)
    MANGLED("?UCS2ToMBCS@ConverterInterface@@QAEHPBGPAPADH@Z");
extern THIS int32_t ibm_ciMBCSToUCS2(void *c, const char *in, uint16_t **out)
    MANGLED("?MBCSToUCS2@ConverterInterface@@QAEHPBDPAPAG@Z");
extern THIS int32_t ibm_ciInsertIndex(void *c)
    MANGLED("?insertIndex@ConverterInterface@@QAEHXZ");
extern THIS int32_t ibm_ciAddParam(void *c, const char *text, int32_t len)
    MANGLED("?addParam@ConverterInterface@@QAEHPBDH@Z");
extern THIS int32_t ibm_ciOutputIndexOrParam(void *c, char *out, int32_t at)
    MANGLED("?outputIndexOrParam@ConverterInterface@@QAEHPADH@Z");
extern THIS int32_t ibm_ciAddText(void *c, const char *text, int32_t len,
                                  int32_t inputType)
    MANGLED("?addText@ConverterInterface@@QAEHPBDHH@Z");
extern THIS uint32_t ibm_ciTrans(void *c, void *text, int32_t len,
                                 int32_t codeset, const char **out)
    MANGLED("?trans2defaultCodeset@ConverterInterface@@QAEKPAXHW4"
            "ECILanguageDialect@@PAPAD@Z");
extern THIS int32_t ibm_ciStop(void *c)
    MANGLED("?stop@ConverterInterface@@QAEHXZ");
extern THIS int32_t ibm_ciResume(void *c)
    MANGLED("?resume@ConverterInterface@@QAEHXZ");
extern THIS void *ibm_ciNewDict(void *c)
    MANGLED("?newDict@ConverterInterface@@QAEPAXXZ");
extern THIS void ibm_ciDeleteDict(void *c, void *dict)
    MANGLED("?deleteDict@ConverterInterface@@QAEXPAX@Z");
extern THIS void ibm_ciSetDict(void *c, void *dict)
    MANGLED("?setDict@ConverterInterface@@QAEXPAX@Z");
extern THIS long ibm_ciFindDictFile(void *c, const char *name, char *out)
    MANGLED("?findDictFile@ConverterInterface@@AAEJPBDPAD@Z");
extern THIS int32_t ibm_ciLoadDict(void *c, void *dict, int32_t which,
                                   const char *name)
    MANGLED("?loadDict@ConverterInterface@@QAEJPAXJPBD@Z");
extern THIS int32_t ibm_ciSaveDict(void *c, void *dict, int32_t which,
                                   const char *name)
    MANGLED("?saveDict@ConverterInterface@@QAEJPAXJPBD@Z");
extern THIS int32_t ibm_ciLookupDictExt(void *c, void *dict, int32_t which,
                                        uint8_t *word, int32_t wordLen,
                                        void **value, int32_t *valueLen,
                                        int32_t *pos, int32_t codeset)
    MANGLED("?lookupDictExt@ConverterInterface@@QAEJPAXJ0JPAPAXPAJPAW4"
            "ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ibm_ciFindFirst(void *c, void *dict, int32_t which,
                                    void **word, int32_t *wordLen,
                                    void **extra, int32_t *extraLen,
                                    int32_t *pos, int32_t codeset)
    MANGLED("?findFirstDictEntryExt@ConverterInterface@@QAEJPAXJPAPAXPAJ12PAW4"
            "ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ibm_ciFindNext(void *c, void *dict, int32_t which,
                                   void **word, int32_t *wordLen,
                                   void **extra, int32_t *extraLen,
                                   int32_t *pos, int32_t codeset)
    MANGLED("?findNextDictEntryExt@ConverterInterface@@QAEJPAXJPAPAXPAJ12PAW4"
            "ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ibm_ciUpdateDictExt(void *c, void *dict, int32_t which,
                                        uint8_t *word, int32_t wordLen,
                                        char *kana, int32_t kanaLen,
                                        int32_t pos, int32_t codeset)
    MANGLED("?updateDictExt@ConverterInterface@@QAEJPAXJ0J0JW4"
            "ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
#define CI(name) ibm_ci##name

extern THIS void *ibm_imCtor(void *m, Param *p)
    MANGLED("??0InputManager@@QAE@PAVRomInstParam@@@Z");
extern THIS void ibm_imDtor(void *m)
    MANGLED("??1InputManager@@QAE@XZ");
extern THIS void ibm_imRemove(void *m)
    MANGLED("?remove@InputManager@@QAEXXZ");
extern THIS int32_t ibm_imAddText(void *m, const char *text, uint32_t len,
                                  int32_t codeset)
    MANGLED("?addText@InputManager@@QAEHPBDHW4ECILanguageDialect@@@Z");
extern THIS int32_t ibm_imGetText(void *m, const char **outText,
                                  uint32_t *outLen, const char *text,
                                  uint32_t len)
    MANGLED("?getText@InputManager@@QAE?AW4GetDataType@@PAPADPAKPBDK@Z");
extern THIS int32_t ibm_imInsertIndex(void *m)
    MANGLED("?insertIndex@InputManager@@QAEHXZ");
extern THIS int32_t ibm_imAddParam(void *m, const char *text, int32_t len)
    MANGLED("?addParam@InputManager@@QAEHPBDH@Z");
extern THIS int32_t ibm_imHasMore(void *m)
    MANGLED("?hasMoreElement@InputManager@@QAEHXZ");
extern THIS int32_t ibm_imGetNextOffset(void *m)
    MANGLED("?getNextOffset@InputManager@@QAEHXZ");
extern THIS int32_t ibm_imGetNextData(void *m, const char **out)
    MANGLED("?getNextData@InputManager@@QAEHPAPBD@Z");
extern THIS void ibm_imRemoveElement(void *m)
    MANGLED("?removeElement@InputManager@@QAEXXZ");
extern THIS void *ibm_imGetNextElement(void *m)
    MANGLED("?getNextElement@InputManager@@QAEPAVRomQueueElement@@XZ");
#define QE_AT(e)   (*(int32_t *)((char *)(e) + 4))
#define QE_KIND(e) (*(int32_t *)((char *)(e) + 8))
#define IM(name) ibm_im##name
#define IM_ROOM  0x1c

extern int32_t ibm_SkipESCSeq(const char *text, long *at, int32_t *twoByte)
    MANGLED("?SkipESCSeq@JpnUtil@@SAHPBDPAJPAH@Z");
extern void ibm_jis2sjis(uint8_t *lead, uint8_t *trail)
    MANGLED("?jis2sjis@JpnUtil@@SAXPAE0@Z");
extern void ibm_han2zen(const char *text, long *at, uint8_t *lead,
                        uint8_t *trail, int32_t kind)
    MANGLED("?han2zen@JpnUtil@@SAXPBDPAJPAE2H@Z");
extern long ibm_euc2shift(const char *in, long len, char *out, int32_t zen)
    MANGLED("?euc2shift@JpnUtil@@SAJPBDJPADH@Z");
extern long ibm_seven2shift(const char *in, long len, char *out)
    MANGLED("?seven2shift@JpnUtil@@SAJPBDJPAD@Z");

/* The vtable a converter is reached through, which IBM keeps at nought.
   Slots four and up are never called here and have no signature yet. */
typedef struct HarnessVtbl {
    THIS void   *(*destroy)(void *c, int32_t freeIt);
    void         (*processSentence)(void);
    THIS int32_t (*getOffset)(void *c);
    THIS void    (*ResetBuffer)(void *c);
    void         (*isValidUserDictEntry)(void);
    void         (*mbcs2Rom)(void);
    void         (*rom2Mbcs)(void);
} HarnessVtbl;
typedef void HarnessSelf;
#define HARNESS_THIS THIS
#define CV_SET_VT(b)  (*(const void **)(b) = &harness_vtbl)
#define CV_INPUT(b)   (*(void **)((b) + RZ_INPUT))
#define CV_TRANSBUF(b) (*(void **)((b) + RZ_TRANSBUF))
#define CV_UNICODE(b) (*(void **)((b) + RZ_UNICODE))


#endif

/* ---- printing ------------------------------------------------------- */

static void putBytes(const char *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%02x", (unsigned char)*s);
}

static void putWide(const uint16_t *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%04x", (unsigned)*s);
}

/* ---- the sweeps ----------------------------------------------------- */

/* Which lead bytes take a second byte, and which single bytes stand alone.
   The trailing byte range is the whole of what Shift-JIS uses, and the
   converter is offered all of it whether the pair means anything or not:
   what a table holds for a pair nobody writes still has to be the same on
   both sides. */
static void sweepToUcs2(Conv *c)
{
    static char in[3];
    uint16_t *out;
    int lead, trail, one;

    for (one = 0x01; one <= 0x7f; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (one = 0xa0; one <= 0xdf; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (lead = 0x81; lead <= 0xfc; lead++) {
        if (lead > 0x9f && lead < 0xe0)
            continue;
        for (trail = 0x40; trail <= 0xfc; trail++) {
            in[0] = (char)lead;
            in[1] = (char)trail;
            in[2] = 0;
            out = NULL;
            printf("MBCS %02x%02x -> %d ", lead, trail,
                   (int)TO_UCS2(c, in, &out));
            putWide(out);
            putchar('\n');
        }
    }
}

/* Every code point, with the flag off and on. Nought is the terminator and
   cannot be offered. */
static void sweepToMbcs(Conv *c)
{
    static uint16_t in[2];
    char *out;
    long u;
    int flag;

    for (flag = 0; flag <= 1; flag++)
        for (u = 1; u <= 0xffff; u++) {
            in[0] = (uint16_t)u;
            in[1] = 0;
            out = NULL;
            printf("UCS2 %04lx %d -> %d ", u, flag,
                   (int)TO_MBCS(c, in, &out, flag));
            putBytes(out);
            putchar('\n');
        }
}

/* A string rather than one character at a time, so that the walk and the
   growing of the buffers are exercised as well as the tables. */
static void sweepStrings(Conv *c)
{
    static const char *const mbcs[] = {
        "", "a", "abc", "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd",
        "\x93\xfa\x96\x7b\x8c\xea", "A\x82\xa0" "1\x83\x41",
        "\xb1\xb2\xb3", "\x82\xa0\xb1" "z",
        NULL
    };
    static const uint16_t wide1[] = { 0x3053, 0x3093, 0 };
    static const uint16_t wide2[] = { 'a', 0x5c, 'b', 0xa000, 0xe000, 0 };
    static const uint16_t wide3[] = { 0xff21, 0x30a2, 0x4e00, 0 };
    const uint16_t *const wides[] = { wide1, wide2, wide3, NULL };
    int i;

    for (i = 0; mbcs[i] != NULL; i++) {
        uint16_t *out = NULL;

        printf("STR ");
        putBytes(mbcs[i]);
        printf(" -> %d ", (int)TO_UCS2(c, mbcs[i], &out));
        putWide(out);
        putchar('\n');
    }

    for (i = 0; wides[i] != NULL; i++) {
        char *out = NULL;

        printf("WSTR ");
        putWide(wides[i]);
        printf(" -> %d ", (int)TO_MBCS(c, wides[i], &out, 1));
        putBytes(out);
        putchar('\n');
    }

    /* And a long one, to make the byte buffer grow past its first kilobyte
       and be freed and made again. */
    {
        static uint16_t big[2000];
        char *out = NULL;
        int k;

        for (k = 0; k < 1999; k++)
            big[k] = (uint16_t)(0x3041 + (k % 80));
        big[1999] = 0;
        printf("LONG -> %d ", (int)TO_MBCS(c, big, &out, 1));
        printf("%d bytes\n", out ? (int)strlen(out) : -1);
    }
}

/* ---- DictMan ---------------------------------------------------------- */

/* How far each table goes, worked out from its length and the stride the
   accessor uses. The lengths are what tools/rom/tables.py measured, which
   is the distance to the next table, so these are the indices the original
   could have answered for.
 *
 * A pointer cannot be compared between two processes, so what is printed for
 * an accessor that answers one is how far it moved from index nought -- which
 * is the stride, exactly -- and the bytes it points at, which is the data. */
enum {
    N_ACCENT     = 264 / 3,
    N_KAKARI     = 280,
    N_PHRVECTOR  = 2624,
    N_PENALTY    = 536,
    N_TG         = 720 / 4,
    N_NUMMD      = 120,
    N_NUMYOMI    = 360 / 6,
    N_NUMJMD     = 5808,
    N_NUMJCC     = 2080,
    N_NDICT      = 4096 / 4,
    N_TDICT      = 1024 / 2,
    N_KDICT      = 1024 / 2,
    N_KNDICT     = 1536 / 3,
    N_KTDICT     = 1024 / 2,
    N_EDICT      = 240 / 2,
    N_ITAIJIHASH = 80 / 2,
    N_ITAIJI_ROWS = 2,
    N_ITAIJI     = 0x7a6 / 2 - 1
};

static void putAt(const uint8_t *p, int n)
{
    int i;

    if (p == NULL) {
        fputs("-", stdout);
        return;
    }
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
}

static void sweepDictMan(void)
{
    const uint8_t *base;
    long i;
    int j;

    printf("DM funcdict ");
    putAt(DM(GetFuncDict)(), 16);
    printf(" ex ");
    putAt(DM(GetFuncDictEx)(), 16);
    putchar('\n');

    printf("DM yomi ");
    putAt(DM(GetYomiDataPtr)(), 16);
    printf(" phrase ");
    putAt(DM(GetPhraseDataPtr)(), 16);
    printf(" number ");
    putAt(DM(GetNumberDataPtr)(), 16);
    printf(" jmd ");
    putAt(DM(GetNumJMDPtr)(), 16);
    putchar('\n');

    base = DM(GetAccentAt)(0);
    for (i = 0; i < N_ACCENT; i++) {
        const uint8_t *at = DM(GetAccentAt)((uint16_t)i);

        printf("DM accent %ld %ld ", i, (long)(at - base));
        putAt(at, 3);
        putchar('\n');
    }

    for (i = 0; i < N_KAKARI; i++)
        printf("DM kakari %ld %02x\n", i, DM(GetKakariAt)((uint16_t)i));
    for (i = 0; i < N_PHRVECTOR; i++)
        printf("DM phrvector %ld %02x\n", i,
               DM(GetPhrVectorAt)((uint16_t)i));
    for (i = 0; i < N_PENALTY; i++)
        printf("DM penalty %ld %02x\n", i, DM(GetPenaltyAt)((uint16_t)i));

    base = DM(GetTGAt)(0);
    for (i = 0; i < N_TG; i++) {
        const uint8_t *at = DM(GetTGAt)((uint8_t)i);

        printf("DM tg %ld %ld ", i, (long)(at - base));
        putAt(at, 4);
        for (j = 0; j < 4; j++)
            printf(" %02x", DM(GetTGAt2)((uint8_t)i, (uint8_t)j));
        putchar('\n');
    }

    for (i = 0; i < N_NUMMD; i++)
        printf("DM nummd %ld %02x\n", i, DM(GetNumMDAt)((uint16_t)i));
    for (i = 0; i < N_NUMJMD; i++)
        printf("DM numjmd %ld %02x\n", i, DM(GetNumJMDAt)((uint16_t)i));
    for (i = 0; i < N_NUMJCC; i++)
        printf("DM numjcc %ld %02x\n", i, DM(GetNumJCCAt)((uint16_t)i));

    base = DM(GetNumYomiPtrAt)(0);
    for (i = 0; i < N_NUMYOMI; i++) {
        const uint8_t *at = DM(GetNumYomiPtrAt)((uint8_t)i);

        printf("DM numyomi %ld %ld ", i, (long)(at - base));
        putAt(at, 6);
        putchar('\n');
    }

    base = DM(GetNDictHashAt)(0, 0);
    for (i = 0; i < N_NDICT; i++)
        for (j = 0; j < 4; j++) {
            const uint8_t *at = DM(GetNDictHashAt)((uint16_t)i, (uint8_t)j);

            printf("DM ndict %ld %d %ld %02x\n", i, j, (long)(at - base),
                   at[0]);
        }

    base = DM(GetTDictHashAt)(0);
    for (i = 0; i < N_TDICT; i++) {
        const uint8_t *at = DM(GetTDictHashAt)((uint16_t)i);

        printf("DM tdict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    base = DM(GetKDictHashAt)(0);
    for (i = 0; i < N_KDICT; i++) {
        const uint8_t *at = DM(GetKDictHashAt)((uint16_t)i);

        printf("DM kdict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    base = DM(GetEDictHashAt)(0);
    for (i = 0; i < N_EDICT; i++) {
        const uint8_t *at = DM(GetEDictHashAt)((uint16_t)i);

        printf("DM edict %ld %ld ", i, (long)(at - base));
        putAt(at, 2);
        putchar('\n');
    }

    for (i = 0; i < N_KNDICT; i++)
        for (j = 0; j < 3; j++)
            printf("DM kndict %ld %d %02x\n", i, j,
                   DM(GetKNDictHashAt)((uint16_t)i, (uint8_t)j));
    for (i = 0; i < N_KTDICT; i++)
        for (j = 0; j < 2; j++)
            printf("DM ktdict %ld %d %02x\n", i, j,
                   DM(GetKTDictHashAt)((uint16_t)i, (uint8_t)j));
    for (i = 0; i < N_ITAIJIHASH; i++)
        for (j = 0; j < 2; j++)
            printf("DM itaijihash %ld %d %02x\n", i, j,
                   DM(GetItaijiHashAt)((uint16_t)i, (uint8_t)j));

    base = DM(GetItaijiAt)(0, 0);
    for (j = 0; j < N_ITAIJI_ROWS; j++)
        for (i = 0; i < N_ITAIJI; i++) {
            const uint8_t *at = DM(GetItaijiAt)((uint8_t)j, (uint16_t)i);

            printf("DM itaiji %d %ld %ld ", j, i, (long)(at - base));
            putAt(at, 2);
            putchar('\n');
        }
}

/* The two rule blocks, after the call that fills them in, and the two
   supplement pointers after theirs. What is printed is the count and the
   first bytes of each of the eight tables, which says both that the right
   table went into the right slot and that it holds what IBM's holds. */
static void sweepRules(void)
{
#ifdef EVV_ROMPRIMS_OURS
    const DictManRules *r[2];

    dm_EngRulesInit();
    dm_InitSupplementDictionary();
    r[0] = &dm_EngToRomanRule;
    r[1] = &dm_RomanToKanaRule;
#else
    const uint8_t **r[2];
    char room[0x40];

    ibm_EngRulesInit(room);
    ibm_InitSupplementDictionary(room);
    r[0] = ibm_EngToRomanRule;
    r[1] = ibm_RomanToKanaRule;
#endif

    {
        int k;

        for (k = 0; k < 2; k++) {
#ifdef EVV_ROMPRIMS_OURS
            const uint8_t *t[8];
            unsigned count = r[k]->count;

            t[0] = r[k]->from;    t[1] = r[k]->to;
            t[2] = r[k]->remain;  t[3] = r[k]->fromPos;
            t[4] = r[k]->toPos;   t[5] = r[k]->remainPos;
            t[6] = r[k]->accentValue; t[7] = r[k]->accentPos;
#else
            const uint8_t *t[8];
            unsigned count = *(const unsigned short *)((const char *)r[k]
                                                       + 0x20);
            int q;

            for (q = 0; q < 8; q++)
                t[q] = r[k][q];
#endif
            printf("DM rule %d count %u", k, count);
            {
                int q;

                for (q = 0; q < 8; q++) {
                    printf(" ");
                    putAt(t[q], 12);
                }
            }
            putchar('\n');
        }
    }

    printf("DM supp ");
#ifdef EVV_ROMPRIMS_OURS
    putAt(dm_paUserDict, 24);
    printf(" idx ");
    putAt(dm_paUserDictIdx, 24);
#else
    putAt(ibm_paUserDict, 24);
    printf(" idx ");
    putAt(ibm_paUserDictIdx, 24);
#endif
    putchar('\n');
}

/* ---- JpnUtil ---------------------------------------------------------- */

/* Every byte through the six predicates that take one, as one line each. */
static void sweepBytes(void)
{
    int b;

    for (b = 0; b <= 0xff; b++)
        printf("JU byte %02x %d%d%d%d%d%d\n", b,
               JU(IsSBCSKana)((char)b) ? 1 : 0,
               JU(IsAlphaNumSym)((char)b) ? 1 : 0,
               JU(IsNum)((char)b) ? 1 : 0,
               JU(IsAlpha)((char)b) ? 1 : 0,
               JU(IsDBCSLeadByte)((char)b) ? 1 : 0,
               JU(IsDBCSTrailByte)((uint8_t)b) ? 1 : 0);
}

/* Every two-byte character through the seven predicates that take a pointer,
   and through MakeUshort, as one line each. Sixty-five thousand lines is the
   whole space and there is no reason to sample it. */
static void sweepPairs(void)
{
    static char pair[3];
    long hi, lo;

    for (hi = 0; hi <= 0xff; hi++)
        for (lo = 1; lo <= 0xff; lo++) {
            pair[0] = (char)hi;
            pair[1] = (char)lo;
            pair[2] = 0;
            printf("JU pair %02lx%02lx %04x %d%d%d%d%d%d%d\n", hi, lo,
                   (unsigned)JU(MakeUshort)(pair),
                   JU(IsKatakana)(pair) ? 1 : 0,
                   JU(IsHiragana)(pair) ? 1 : 0,
                   JU(IsLongVowel)(pair) ? 1 : 0,
                   JU(IsSNLKDelim)(pair) ? 1 : 0,
                   JU(IsDBCSNum)(pair) ? 1 : 0,
                   JU(IsKanjiNum)(pair) ? 1 : 0,
                   JU(IsValidDBCS)(pair) ? 1 : 0);
        }
}

/* The two-byte copies and comparisons, over a handful of pairs each way. */
static void sweepTwoByte(void)
{
    static const char *const some[] = {
        "\x82\xa0", "\x82\xa1", "\x83\x41", "\x81\x42", "ab", "a\x01",
        NULL
    };
    int i, j;

    for (i = 0; some[i] != NULL; i++)
        for (j = 0; some[j] != NULL; j++) {
            char to[3];
            char c0 = 0, c1 = 0;

            printf("JU db %d %d cmp %d cmp2 %d twoch %d", i, j,
                   JU(DbCmp)(some[i], some[j]) ? 1 : 0,
                   JU(DbCmp2)(some[i], some[j][0], some[j][1]) ? 1 : 0,
                   JU(TwoChCmp)(some[i][0], some[i][1], (char *)some[j]) ? 1
                                                                        : 0);
            to[0] = 0; to[1] = 0; to[2] = 0;
            JU(DbCpy)(to, some[j]);
            printf(" cpy %02x%02x", (unsigned char)to[0],
                   (unsigned char)to[1]);
            JU(DbSet)(to, some[i][0], some[j][1]);
            printf(" set %02x%02x", (unsigned char)to[0],
                   (unsigned char)to[1]);
            JU(TwoChCpy)((char *)some[i], &c0, &c1);
            printf(" twocpy %02x%02x\n", (unsigned char)c0,
                   (unsigned char)c1);
        }
}

/* Every kana code into letters, four times over: into an empty buffer, into
   one already ending in the geminate marker, into one ending in that marker
   and an apostrophe, and into one ending in an ordinary syllable, which is
   what the capitalising row acts on. */
static void sweepRomaji(void)
{
    static const char *const before[] = { "", "Q", "Q'", "ka", "kaN", NULL };
    int b;
    long code;

    for (b = 0; before[b] != NULL; b++)
        for (code = 0; code <= 0xff; code++) {
            static uint8_t out[128];
            int32_t rc;

            strcpy((char *)out, before[b]);
            rc = JU(WriteRomajiStrBuf)((uint8_t)code, out);
            printf("JU romaji %d %02lx %d ", b, code, (int)rc);
            putBytes((const char *)out);
            putchar('\n');
        }
}

/* Every half-width kana with each of the two voicing marks. */
static void sweepDakuten(void)
{
    long kana;
    int  mark;

    for (mark = 0xde; mark <= 0xdf; mark++)
        for (kana = 0xa1; kana <= 0xdf; kana++) {
            char out[4];

            out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
            JU(ConvertDakuten)(out, (uint8_t)kana, (uint8_t)mark);
            printf("JU dakuten %02lx %02x %02x%02x\n", kana, mark,
                   (unsigned char)out[0], (unsigned char)out[1]);
        }
}

/* Every hiragana into katakana, one at a time and then in strings. 0x82ec is
   left out: IsHiragana accepts it and the table has not got it, so IBM's walks
   it for ever. rom/jajp/jpnutil.c says what ours does instead. */
static void sweepKatakana(void)
{
    static const uint8_t *const strings[] = {
        (const uint8_t *)"\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd",
        (const uint8_t *)"a\x82\xa0" "1\x83\x41\xb1",
        (const uint8_t *)"\x93\xfa\x96\x7b\x8c\xea",
        (const uint8_t *)"",
        NULL
    };
    long lo;
    int  i;

    for (lo = 0x9f; lo <= 0xf1; lo++) {
        uint8_t in[3];
        uint8_t out[16];

        if (lo == 0xec)
            continue;
        in[0] = 0x82;
        in[1] = (uint8_t)lo;
        in[2] = 0;
        memset(out, 0, sizeof out);
        JU(Hiragana2Katakana)(in, out);
        printf("JU h2k 82%02lx ", lo);
        putBytes((const char *)out);
        putchar('\n');
    }

    for (i = 0; strings[i] != NULL; i++) {
        uint8_t out[64];

        memset(out, 0, sizeof out);
        JU(Hiragana2Katakana)(strings[i], out);
        printf("JU h2kstr %d ", i);
        putBytes((const char *)out);
        putchar('\n');
    }
}

static void sweepYomi(void)
{
    static uint8_t a[] = { 1, 2, 3, 4 };
    static uint8_t b[] = { 1, 2, 3, 5 };
    int i, j;

    for (i = 0; i <= 4; i++)
        for (j = 0; j <= 4; j++)
            printf("JU yomi %d %d %d %d\n", i, j,
                   (int)JU(YomiCmp)(a, (uint8_t)i, b, (uint8_t)j),
                   (int)JU(YomiCmp)(a, (uint8_t)i, a, (uint8_t)j));
}

/* A chain of eight, unlinked one at a time, printed after each. The nil is
   0xffff and the first two indices stand for the head and the tail. */
static void sweepTableFree(void)
{
    enum { N = 8, NIL = 0xffff };
    uint16_t table[N * 2];
    uint16_t used = 0, tail = NIL, freeHead = NIL;
    int i, k;

    for (i = 0; i < N; i++) {
        table[i * 2] = (uint16_t)(i == 0 ? NIL : i - 1);
        table[i * 2 + 1] = (uint16_t)(i == N - 1 ? NIL : i + 1);
    }
    used = N - 1;

    for (k = 0; k < N; k++) {
        JU(TableFree)(&used, &tail, &freeHead, (void *)table, NIL,
                      (uint16_t)k);
        printf("JU tablefree %d used %04x tail %04x free %04x", k, used, tail,
               freeHead);
        for (i = 0; i < N; i++)
            printf(" %04x/%04x", table[i * 2], table[i * 2 + 1]);
        putchar('\n');
    }
}

/* ---- the stored dictionary -------------------------------------------- */

/* What is compared and what cannot be. A skip list draws each entry's tower
   height at random, and IBM's constructor seeds that from the clock, so the
   towers differ between two runs of the same program and a saved file is not
   the same file twice. What does not depend on the draw is every answer the
   list gives: order decides those, and the towers only decide how quickly they
   are reached. So this prints answers -- what a search found, what a walk
   yields, what a remove said -- and never a level or a file byte. A save and a
   load are checked by walking the loaded list and finding the same entries in
   the same order. */

/* The keys, chosen to exercise what the comparisons do: the same first bytes
   with different lengths, so a prefix search has something to find; keys that
   sort before and after each other; and one inserted twice, so the replace
   path is walked. */
static const char *const SL_KEYS[] = {
    "\x82\xa0", "\x82\xa0\x82\xa2", "\x82\xa0\x82\xa2\x82\xa4",
    "\x82\xa2", "\x82\xa4", "\x83\x41", "\x83\x41\x83\x43",
    "\x93\xfa\x96\x7b", "\x93\xfa\x96\x7b\x8c\xea",
    "ab", "abcd", "abcdef", "b", "zz", "\x82\xa0", "\x81\x40",
    "\xfc\xfc", "m", "\x82\xf1", "\x83\x93",
    NULL
};

static void slWalk(void *list, const char *what)
{
    void *key = 0;
    void *trans = 0;
    int   n = 0;
    int32_t more;

    printf("SL walk %s\n", what);
    for (more = ibm_slGetFirst(list, &key, &trans); more != 0;
         more = ibm_slGetNext(list, &key, &trans)) {
        printf("SL  %d ", n++);
        ibm_keyDump(key);
        ibm_transDump(trans);
        if (n > 200)
            break;
    }
    printf("SL walked %d\n", n);
}

static void sweepSkipList(void)
{
    static char list[IBM_LIST_ROOM];
    static char list2[IBM_LIST_ROOM];
    static char key[IBM_KEY_ROOM];
    static char trans[IBM_TRANS_ROOM];
    const char *path = "romprims-skiplist.tmp";
    int i;

    ibm_slCtor(list);

    for (i = 0; SL_KEYS[i] != NULL; i++) {
        char what[64];
        int32_t rc;

        sprintf(what, "value-%d", i);
        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        ibm_transCtor(trans, what, (int32_t)strlen(what), "word", "extra",
                      i % 5);
        rc = ibm_slInsert(list, key, trans);
        printf("SL insert %d rc %d\n", i, (int)rc);
        ibm_transDtor(trans);
        ibm_keyDtor(key);
    }

    slWalk(list, "after inserting");

    /* Every key that went in, and a few that did not. */
    for (i = 0; SL_KEYS[i] != NULL; i++) {
        void *found;

        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        found = ibm_slSearch(list, key);
        printf("SL search %d %s", i, found ? "found " : "none\n");
        if (found)
            ibm_transDump(found);
        ibm_keyDtor(key);
    }
    {
        static const char *const absent[] = { "q", "\x82\xa1", "zzz", NULL };

        for (i = 0; absent[i] != NULL; i++) {
            void *found;

            ibm_keyCtor(key, (char *)absent[i],
                        (int32_t)strlen(absent[i]));
            found = ibm_slSearch(list, key);
            printf("SL absent %d %s\n", i, found ? "found" : "none");
            ibm_keyDtor(key);
        }
    }

    /* Every prefix of a long key at once, which is what a Japanese lookup
       wants: the count comes back in the four bytes before the answer. */
    {
        static const char *const multi[] = {
            "\x82\xa0\x82\xa2\x82\xa4", "\x93\xfa\x96\x7b\x8c\xea",
            "abcdef", NULL
        };

        for (i = 0; multi[i] != NULL; i++) {
            void *out;

            ibm_keyCtor(key, (char *)multi[i], (int32_t)strlen(multi[i]));
            out = ibm_slMultiSearch(list, key);
            if (out == 0) {
                printf("SL multi %d none\n", i);
            } else {
                int32_t n = *(const int32_t *)((const char *)out - 4);
                int32_t j;

                printf("SL multi %d count %d\n", i, (int)n);
                for (j = 0; j < n; j++) {
                    printf("SL  multi %d %d ", i, (int)j);
                    ibm_transDump((char *)out + j * IBM_TRANS_ROOM);
                }
            }
            ibm_keyDtor(key);
        }
    }

    /* Out again, every other one. */
    for (i = 0; SL_KEYS[i] != NULL; i += 2) {
        int32_t rc;

        ibm_keyCtor(key, (char *)SL_KEYS[i], (int32_t)strlen(SL_KEYS[i]));
        rc = ibm_slRemove(list, key);
        printf("SL remove %d rc %d\n", i, (int)rc);
        ibm_keyDtor(key);
    }

    slWalk(list, "after removing");

    /* And round through a file. The file's own bytes cannot be compared --
       the towers in it are random -- but what comes back has to be what went
       in. */
    printf("SL save %d\n", (int)ibm_slSave(list, path));
    ibm_slCtor(list2);
    printf("SL load %d\n", (int)ibm_slLoad(list2, path));
    slWalk(list2, "after loading");

    ibm_slDtor(list2);
    ibm_slDtor(list);
    remove(path);
}

/* ---- the dictionary search ------------------------------------------- */

/* How this one is driven. DictSearch is a third of the way through being
   written, so there is no way to build one the way the engine does -- that
   wants a TextAnalysis, which wants the rest of the analyser. Instead both
   sides are handed a block of the right size with nothing in it, and the
   harness writes the few fields the methods under test read, at IBM's own
   offsets, which our side keeps for exactly this reason. Two sides starting
   from bytes they were both given cannot disagree about the starting state,
   which is the whole difficulty with a class this size.
 *
 * rom/jajp/dictsearch.h is where the offsets come from. */
static char ds_block[DS_ROOM];

/* Where each side keeps DictSearch's owner. IBM's is four bytes at
   DS_OWNER; ours cannot be, because a pointer is eight here and the candidate
   array begins at the next word -- rom/jajp/dictsearch.h says why. */
#ifdef EVV_ROMPRIMS_OURS
#define DS_SET_OWNER(blk, ta) (*(void **)((blk) + DS_OWNER_AT) = (ta))
#else
#define DS_SET_OWNER(blk, ta) (*(void **)((blk) + DS_OWNER) = (ta))
#endif

/* And the same for InputChar's chain and for one node of it. Our side parks
   every pointer past the record it belongs to; IBM has the chain head at
   0x27ac of the reader and a node's two at four and eight. */
#ifdef EVV_ROMPRIMS_OURS
#define SN_SET_KEY(n, p)      (*(char **)((char *)(n) + SN_KEY_AT) = (p))
#define SN_KEY_OF(n)          (*(char **)((char *)(n) + SN_KEY_AT))
#define SN_VALUE_OF(n)        (*(char **)((char *)(n) + SN_VALUE_AT))
#define IC_SNLK_HEAD(blk)     (*(void **)((blk) + IC_SNLK_AT))
#define IC_TEXTP_SET(blk, p)  (*(const char **)((blk) + IC_TEXTP_AT) = (p))
#define SN_NEXT_OF(n)         (*(void **)((char *)(n) + SN_NEXT_AT))
#define RZ_SET_PARAM(b, p)    (*(void **)((b) + RZ_PARAM_AT) = (p))
#define RZ_SET_USERDICT(b, p) (*(void **)((b) + RZ_USERDICT_AT) = (p))
#define TA_SET(blk, which, p) (*(void **)((blk) + which##_AT) = (p))
#else
#define SN_SET_KEY(n, p)      (*(char **)((char *)(n) + 4) = (p))
#define SN_KEY_OF(n)          (*(char **)((char *)(n) + 4))
#define SN_VALUE_OF(n)        (*(char **)((char *)(n) + 8))
#define IC_SNLK_HEAD(blk)     (*(void **)((blk) + IC_SNLK_TABLE))
#define IC_TEXTP_SET(blk, p)  (*(const char **)((blk) + IC_TEXTP) = (p))
#define SN_NEXT_OF(n)         (*(void **)((char *)(n) + 0))
#define RZ_SET_PARAM(b, p)    (*(void **)((b) + RZ_PARAM) = (p))
#define RZ_SET_USERDICT(b, p) (*(void **)((b) + RZ_USERDICT) = (p))
#define TA_SET(blk, which, p) (*(void **)((blk) + which) = (p))
#endif

static char ic_block[IC_ROOM];

static void dsPut(int32_t off, const char *bytes, int32_t n)
{
    memcpy(ds_block + off, bytes, (size_t)n);
}

/* One text laid into the input reader: the characters two bytes at a time,
   what each of them is, and how many there are. */
static void icSet(const char *text, const int32_t *kinds, int n)
{
    int i;

    memset(ic_block, 0, sizeof ic_block);
    for (i = 0; i < n; i++) {
        ic_block[IC_TEXT + i * 2] = text[i * 2];
        ic_block[IC_TEXT + i * 2 + 1] = text[i * 2 + 1];
        *(int32_t *)(ic_block + IC_KIND + i * 4) = kinds[i];
    }
    *(int16_t *)(ic_block + IC_COUNT) = (int16_t)n;
}

static void sweepDictSearch(void)
{
    /* One character of each kind that matters, plus the case marker and a
       long-vowel mark, so that every road through GetTextBuf is walked. */
    static const char TEXT[] =
        "\x93\xfa"      /* a kanji */
        "\x82\xa0"      /* hiragana */
        "\x81\x5b"      /* the long-vowel mark */
        "\x82\xa2"      /* hiragana */
        "\x81\x5b"      /* a second long-vowel mark */
        "\x82\xf0"      /* the particle wo, which ends a run */
        "\x83\x41"      /* katakana */
        "\x82\xa4"      /* hiragana */
        "\x82\xa6"      /* hiragana */
        "\x82\xa8";     /* hiragana */
    static const int32_t KINDS[] = { 1, 4, 9, 4, 9, 4, 8, 4, 4, 4 };
    static const int N = 10;
    long from;
    long code;
    int  i;

    memset(ds_block, 0, sizeof ds_block);
    *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
    icSet(TEXT, KINDS, N);

    for (from = 0; from < N; from++)
        printf("DS marker %ld %d\n", from,
               (int)DS(CheckCaseMarker)(ds_block, (int16_t)from));

    for (from = 0; from < N; from++) {
        int32_t rc;

        /* The buffer is left as the last call made it, on purpose: what it
           holds when a call answers nought is part of the answer. */
        rc = DS(GetTextBuf)(ds_block, (int16_t)from);
        printf("DS text %ld rc %d copied %d from %d to %d buf ", from,
               (int)rc, (int)*(int16_t *)(ds_block + DS_COPIED),
               (int)*(int16_t *)(ds_block + DS_FROM),
               (int)*(int16_t *)(ds_block + DS_TO));
        putBytes(ds_block + DS_TEXT);
        putchar('\n');
    }

    /* And the same over a text of one character, and of none, since the
       bounds are where this sort of walk goes wrong. */
    for (i = 0; i <= 2; i++) {
        icSet(TEXT, KINDS, i);
        memset(ds_block + DS_TEXT, 0, 16);
        printf("DS short %d rc %d copied %d\n", i,
               (int)DS(GetTextBuf)(ds_block, 0),
               (int)*(int16_t *)(ds_block + DS_COPIED));
    }

    /* Every pair of codes through the long-vowel conversion. The second is
       passed by address and may be changed, so both are printed. */
    for (code = 0; code <= 0xff; code++) {
        long next;

        for (next = 0xf0; next <= 0xff; next++) {
            uint8_t was = (uint8_t)next;

            DS(CheckCnvChoon)(ds_block, (uint8_t)code, &was);
            if (was != (uint8_t)next)
                printf("DS choon %02lx %02lx -> %02x\n", code, next, was);
        }
    }
    printf("DS choon done\n");
}

/* ---- and the rest of it ----------------------------------------------- */

/* The owner, which the two Process methods and SetLongWord reach for. It is
   nearly a megabyte and only three bytes of it matter here, but the offsets
   are TextAnalysis's own and a smaller block would have to invent them. */
static char ta_block[TA_ROOM];

/* The parameter block main made, which InputChar reaches through the romanizer
   above it. */
static Param *the_param;

/* The character classes InputChar would give this text.
 *
 * InputChar is not written, and its classifier is a method on an object this
 * harness has not got, so GetCharType's ranges are copied here. This decides
 * only what is fed in, and both sides are fed the same thing, so a mistake
 * here narrows the sweep and cannot hide a difference. The one rule left out
 * is IsKanjiNum, which wants a table; a kanji numeral therefore arrives as an
 * ordinary kanji, which is a case worth sweeping anyway. */
static int32_t harnessKind(const unsigned char *c)
{
    if (c[0] == 0x83) {
        if (c[1] >= 0x40 && c[1] <= 0x96) return KIND_KATAKANA;
        if (c[1] >= 0xa0 && c[1] <= 0xd6) return KIND_GREEK;
        return KIND_OTHER;
    }
    if (c[0] == 0x81) {
        if (c[1] == 0x5a) return KIND_DIGIT;
        if (c[1] == 0x5b) return KIND_CHOON;
        if (c[1] == 0x45) return KIND_NAKAGURO;
        if (c[1] == 0x6d) return KIND_BRACKET;
        if (c[1] >= 0x43 && c[1] <= 0xac) return KIND_PUNCT;
        return KIND_OTHER;
    }
    if (c[0] == 0x82) {
        if ((c[1] >= 0x60 && c[1] <= 0x79) || (c[1] >= 0x81 && c[1] <= 0x9a))
            return KIND_LATIN;
        if (c[1] >= 0x9f && c[1] <= 0xf1) return KIND_HIRAGANA;
        if (c[1] >= 0x4f && c[1] <= 0x58) return KIND_DIGIT;
        return KIND_OTHER;
    }
    if (c[0] == 0xfa && c[1] >= 0x40 && c[1] <= 0x5a) return KIND_ROMAN;
    return KIND_KANJI;
}

/* Real Japanese, which is what the walks above have to be swept over: nothing
   short of a sentence reaches the arms that copy a candidate for every reading
   a kanji has. These are test/cases/plain-jajp.txt, the seven the differential
   suite already speaks, in the Shift-JIS the romanizer is handed. */
static const char *const TEXTS[] = {
    "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd\x81\x42\x82\xa8\x8c\xb3\x8b\x43"
    "\x82\xc5\x82\xb7\x82\xa9\x81\x42",
    "\x93\xfa\x96\x7b\x8c\xea\x82\xcc\x89\xb9\x90\xba\x8d\x87\x90\xac\x82\xf0"
    "\x83\x65\x83\x58\x83\x67\x82\xb5\x82\xc4\x82\xa2\x82\xdc\x82\xb7\x81\x42",
    "\x8d\xa1\x93\xfa\x82\xcd\x31\x39\x39\x39\x94\x4e\x38\x8c\x8e\x32\x37\x93"
    "\xfa\x81\x41\x8c\xdf\x8c\xe3\x37\x8e\x9e\x34\x35\x95\xaa\x82\xc5\x82\xb7"
    "\x81\x42",
    "\x83\x52\x83\x93\x83\x73\x83\x85\x81\x5b\x83\x5e\x82\xc6\x83\x43\x83\x93"
    "\x83\x5e\x81\x5b\x83\x6c\x83\x62\x83\x67\x82\xcc\x8b\x5a\x8f\x70\x81\x42",
    "\x49\x42\x4d\x20\x56\x69\x61\x56\x6f\x69\x63\x65\x20\x82\xcd\x89\x70\x8c"
    "\xea\x82\xcc\x96\xbc\x91\x4f\x82\xc5\x82\xb7\x81\x42",
    "\x8e\x52\x93\x63\x82\xb3\x82\xf1\x82\xcd\x93\x8c\x8b\x9e\x93\x73\x90\x56"
    "\x8f\x68\x8b\xe6\x82\xc9\x8f\x5a\x82\xf1\x82\xc5\x82\xa2\x82\xdc\x82\xb7"
    "\x81\x42",
    "\x6b\x6f\x6e\x6e\x69\x63\x68\x69\x77\x61\x2e\x20\x6b\x6f\x72\x65\x20\x77"
    "\x61\x20\x72\x6f\x6f\x6d\x61\x6a\x69\x20\x64\x65\x73\x75\x2e",
    /* And a few by hand for the roads a sentence does not take: a small tsu
       before each kind of thing, a doubled long-vowel bar, a middle dot at
       the end of a katakana run, and a yoon of each script. */
    "\x82\xa2\x82\xc1\x82\xbd\x82\xf1\x82\xc1\x81\x42\x82\xc1",
    "\x83\x41\x81\x5b\x81\x5b\x83\x43\x81\x45",
    "\x83\x41\x83\x8a\x81\x45\x83\x58\x81\x45",
    "\x82\xab\x82\xe1\x82\xad\x82\xe5\x82\xa4\x82\xb5\x82\xe1",
    "\x83\x4c\x83\x83\x83\x93\x83\x76\x83\x74\x83\x40\x83\x43\x83\x8b",
    /* And what the seven above never reach. The English dictionary is keyed
       by full-width lowercase letters, so a text of half-width ASCII walks
       past it entirely -- which is what the sabotage matrix noticed. The
       supplement dictionary holds symbols and compounds beginning with a
       full-width digit, and nothing else in this file has either. */
    "\x82\x81\x82\x82\x82\x8c\x82\x85",                  /* able */
    "\x82\x60\x82\x61\x82\x6b\x82\x64",                  /* ABLE */
    "\x82\x60\x82\x82\x82\x8c\x82\x85\x82\x62\x82\x81\x82\x94",  /* AbleCat */
    "\x82\x81\x82\x82\x82\x82\x82\x85\x82\x99",        /* abbey */
    "\x81\x59\x81\x60\x81\x7d\x81\x87\x81\x95\x81\x97\x81\xa7",
    "\x82\x50\x93\xfa\x95\xbd\x8b\xcf",
    "\x82\x50\x82\x51\x82\x52\x93\xfa",
    /* Twenty full-width letters, which is past the seventeen the English
       fallback will take, and long enough that its reading reaches the room
       a candidate has. */
    "\x82\x81\x82\x82\x82\x83\x82\x84\x82\x85\x82\x86\x82\x87"
    "\x82\x88\x82\x89\x82\x8a\x82\x8b\x82\x8c\x82\x8d\x82\x8e"
    "\x82\x8f\x82\x90\x82\x91\x82\x92\x82\x93\x82\x94",
    /* Three kanji the variant table rewrites, so that the retry a run with
       nothing found makes -- the one that turns the table on -- is a road the
       sweep actually walks. */
    "\x88\xaa\x89\x6f\x89\x92\x93\xfa",
    /* A particle with a long bar after it, which is the one road through the
       function-word search that continues a word rather than ending it. */
    "\x93\xfa\x82\xcc\x81\x5b\x82\xcd\x81\x5b\x82\xc5\x82\xb7",
    NULL
};

/* A kanji followed by every small kana there is, in the order GetYoonIndex
   numbers them. The first writer refuses a word that ends where one of these
   would join it, and which of them count is a range with two ends; laying
   them out like this is what puts both ends of it under the sweep. */
static const char KANA_EDGES[] =
    "\x93\xfa"
    "\x82\x9f" "\x82\xa1" "\x82\xa3" "\x82\xa5" "\x82\xa7"
    "\x82\xe1" "\x82\xe3" "\x82\xe5" "\x81\x5b" "\x82\xc1"
    "\x83\x40" "\x83\x42" "\x83\x44" "\x83\x46" "\x83\x48"
    "\x83\x83" "\x83\x85" "\x83\x87" "\x83\x62"
    "\x93\xfa";

/* One text laid into the input reader, split into slots the way InputChar
   would: a two-byte character to a slot, and anything else on its own. */
static int icSetText(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;

    memset(ic_block, 0, sizeof ic_block);
    while (*p != 0) {
        unsigned char pair[2];

        if ((*p >= 0x81 && *p <= 0x9f) || (*p >= 0xe0 && *p <= 0xfc)) {
            if (p[1] == 0)
                break;
            pair[0] = p[0];
            pair[1] = p[1];
            p += 2;
        } else {
            pair[0] = p[0];
            pair[1] = 0;
            p += 1;
        }
        ic_block[IC_TEXT + n * 2] = (char)pair[0];
        ic_block[IC_TEXT + n * 2 + 1] = (char)pair[1];
        *(int32_t *)(ic_block + IC_KIND + n * 4) = harnessKind(pair);
        *(int16_t *)(ic_block + IC_OFFSET + n * 2) = (int16_t)(n * 2);
        *(int32_t *)(ic_block + IC_MARK + n * 4) = 0x1000 + n;
        n++;
    }
    *(int16_t *)(ic_block + IC_COUNT) = (int16_t)n;
    return n;
}

/* A record, printed as the bytes it is, because naming its fields twice is
   what the shared layout exists to avoid. */
static void putRecord(const char *what, long which, const uint8_t *r, int n)
{
    int i;

    printf("DS %s %ld", what, which);
    for (i = 0; i < n; i++)
        printf(" %02x", r[i]);
    putchar('\n');
}

/* Every candidate the last walk left behind. */
static void putCandidates(const char *what, long which)
{
    int16_t n = *(int16_t *)(ds_block + DS_NCAND);
    int16_t j;

    printf("DS %s %ld cand %d total %d runs %d\n", what, which, (int)n,
           (int)*(int16_t *)(ds_block + DS_TOTAL),
           (int)*(int16_t *)(ds_block + DS_RUNS));
    for (j = 0; j < n && j < DS_CAND_N; j++) {
        int k;

        printf("DS %s %ld  %d len %d chars %d mark %d taken %d kana", what,
               which, (int)j,
               (int)*(int16_t *)(ds_block + DS_LEN + j * 2),
               (int)*(int16_t *)(ds_block + DS_CHARS + j * 2),
               (int)*(uint8_t *)(ds_block + DS_MARK + j),
               (int)*(int16_t *)(ds_block + DS_TAKEN + j * 2));
        for (k = 0; k < DS_READING_SIZE; k++)
            printf(" %02x",
                   (unsigned)(uint8_t)ds_block[DS_READING
                                               + j * DS_READING_SIZE + k]);
        putchar('\n');
    }
}

static void sweepDictSearchRest(void)
{
    long code;
    long lead;
    long page;
    int  t;
    int  i;
    int  j;

    /* Which codes stand for a sound rather than a kana. */
    for (code = 0; code <= 0xff; code++)
        printf("DS onin %02lx %d\n", code, (int)DS(IsOnin)((uint8_t)code));

    /* And which characters are small kana. Every pair in the three lead
       bytes any of them can have. */
    for (lead = 0x81; lead <= 0x83; lead++)
        for (code = 0; code <= 0xff; code++) {
            char pair[3];

            pair[0] = (char)lead;
            pair[1] = (char)code;
            pair[2] = 0;
            printf("DS yoon %02lx%02lx %d\n", lead, code,
                   (int)DS(GetYoonIndex)(ds_block, pair));
        }

    /* The yoon table, over every row a kana index can be and every small kana
       there is, both ways round on the flag. */
    for (code = 0; code <= 0x60; code++) {
        long y;

        for (y = 0; y <= 19; y++) {
            printf("DS yoondict %02lx %ld %d %d\n", code, y,
                   (int)DS(ConvertYoonDict)(ds_block, (int16_t)code,
                                            (int16_t)y, 0),
                   (int)DS(ConvertYoonDict)(ds_block, (int16_t)code,
                                            (int16_t)y, 1));
        }
    }

    /* Where a word sits in its page, over every page of both dictionaries and
       over the offsets on either side of each bound. The answer is a pointer,
       so what is printed is whether there was one and the bytes it reaches. */
    for (t = 0; t < 2; t++) {
        static const int16_t OFFS[] = { 0, 1, 0xc7, 0xc8, 0xfff, 0x1000 };
        long pages = t == 0 ? NORMAL_N : TANKAN_N;

        for (page = 0; page < pages; page++)
            for (i = 0; i < (int)(sizeof OFFS / sizeof OFFS[0]); i++) {
                const uint8_t *w = DS(ReadGWDict)(ds_block, (int16_t)page,
                                                  OFFS[i],
                                                  (int16_t)(t == 0 ? 1 : 2));

                /* The bytes only where the offset is small. The bound
                   ReadGWDict tests against is a logical one and is larger
                   than the blob a page sits in, so reading eight bytes from
                   near it would compare what happens to follow that blob in
                   each build rather than the dictionary. */
                printf("DS gw %d %ld %d", t, page, (int)OFFS[i]);
                if (w == NULL)
                    printf(" -");
                else if (OFFS[i] > 2)
                    printf(" ok");
                else
                    for (j = 0; j < 8; j++)
                        printf(" %02x", w[j]);
                putchar('\n');
            }
    }

    /* The long-reading store, filled past the thirty it holds so that the
       refusal is swept too. */
    memset(ta_block, 0, sizeof ta_block);
    DS_SET_OWNER(ds_block, ta_block);
    for (i = 0; i < 33; i++) {
        int32_t entRoom[DS_ENTRY_SIZE / 4];
        uint8_t *e = (uint8_t *)entRoom;
        uint8_t  word[26];

        memset(e, 0xee, DS_ENTRY_SIZE);
        for (j = 0; j < 26; j++)
            word[j] = (uint8_t)(i * 26 + j);
        DS(SetLongWord)(ds_block, 26, e, word);
        printf("DS long %d used %d slot %02x row", i,
               (int)(uint8_t)ta_block[TA_LONGWORDS],
               (unsigned)e[DE_KANA]);
        for (j = 0; j < TA_LONGWORD_SIZE; j++)
            printf(" %02x",
                   (unsigned)(uint8_t)ta_block[TA_LONGWORD
                                               + (i % TA_LONGWORD_N)
                                                 * TA_LONGWORD_SIZE + j]);
        putchar('\n');
    }

    /* One node's readings copied out, over a node built by hand.
     *
     * Reaching this through the dictionary sweeps only the shapes the
     * dictionary happens to hold, and no kanji in it has five readings on one
     * node, so the cap at five would go untested. A node made here does test
     * it: nought to eight readings, at both bases, of every length a nibble
     * can say. */
    for (i = 0; i <= 8; i++) {
        static const int16_t BASES[] = { 0, 5 };
        int  bi;

        for (bi = 0; bi < 2; bi++)
            for (j = 1; j <= 12; j++) {
                uint8_t node[256];
                uint8_t *w = node + TH_READING;
                int      r;
                int      k;

                memset(node, 0, sizeof node);
                node[TH_FLAGS] = (uint8_t)(i << 4);
                for (r = 0; r < i; r++) {
                    w[TR_LEN] = (uint8_t)j;
                    for (k = 0; k < j; k++)
                        w[TR_KANA + k] = (uint8_t)(0x40 + r * 16 + k);
                    w += TR_KANA + j;
                }
                memset(ds_block + DS_KANA, 0,
                       (size_t)(DS_KANA_N * DS_KANA_SIZE));
                memset(ds_block + DS_KANA_CHARS, 0, DS_KANA_N);
                memset(ds_block + DS_KANA_LEN, 0, DS_KANA_N);
                printf("DS kanadata %d %d %d rc %d", i, (int)BASES[bi], j,
                       (int)DS(WriteKanaData)(ds_block, node, (int16_t)(i + 1),
                                              0, BASES[bi]));
                for (k = 0; k < DS_KANA_N; k++)
                    printf(" %d/%d",
                           (int)(uint8_t)ds_block[DS_KANA_CHARS + k],
                           (int)(uint8_t)ds_block[DS_KANA_LEN + k]);
                for (k = 0; k < DS_KANA_N * DS_KANA_SIZE; k++)
                    printf(" %02x", (unsigned)(uint8_t)ds_block[DS_KANA + k]);
                putchar('\n');
            }
    }

    /* And the whole of the walk, over real Japanese. */
    for (t = 0; TEXTS[t] != NULL; t++) {
        int n = icSetText(TEXTS[t]);
        int at;

        for (at = 0; at < n; at++) {
            int32_t entRoom[DS_ENTRY_SIZE / 4];
            uint8_t *e = (uint8_t *)entRoom;
            int32_t kind = *(int32_t *)(ic_block + IC_KIND + at * 4);
            long which = t * 1000L + at;

            memset(ds_block, 0, sizeof ds_block);
            memset(ta_block, 0, sizeof ta_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            *(int16_t *)(ds_block + DS_FROM) = (int16_t)at;

            printf("DS hrgn %ld", which);
            for (i = 0; i <= 8; i++)
                printf(" %d", (int)DS(CountHrgn)(ds_block, i));
            putchar('\n');

            if (kind == KIND_HIRAGANA) {
                memset(e, 0, DS_ENTRY_SIZE);
                DS(ProcessHiragana)(ds_block, (int16_t)at, e);
                putRecord("hira", which, e, DS_ENTRY_SIZE);
            }
            if (kind == KIND_KATAKANA) {
                memset(e, 0, DS_ENTRY_SIZE);
                DS(ProcessKatakana)(ds_block, (int16_t)at, e);
                putRecord("kata", which, e, DS_ENTRY_SIZE);
            }
            if (kind == KIND_KANJI) {
                int16_t found = DS(LookupKanaDict)(ds_block, (int16_t)at);

                printf("DS look %ld %d\n", which, (int)found);
                for (j = 0; j < DS_KANA_N; j++) {
                    printf("DS look %ld  %d chars %d len %d kana", which, j,
                           (int)(uint8_t)ds_block[DS_KANA_CHARS + j],
                           (int)(uint8_t)ds_block[DS_KANA_LEN + j]);
                    for (i = 0; i < DS_KANA_SIZE; i++)
                        printf(" %02x",
                               (unsigned)(uint8_t)ds_block[DS_KANA
                                                    + j * DS_KANA_SIZE + i]);
                    putchar('\n');
                }
            }

            /* The readings on their own, then the whole word. */
            memset(ds_block, 0, sizeof ds_block);
            memset(ta_block, 0, sizeof ta_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            if (DS(GetTextBuf)(ds_block, (int16_t)at)) {
                printf("DS kana %ld rc %d\n", which,
                       (int)DS(GenerateKanaString)(ds_block));
                putCandidates("kana", which);

                /* And CompareKanji over every candidate, against an entry
                   built out of the text itself so that the bars match, and
                   one built out of a kanji that is not there so that they do
                   not. */
                for (j = 0; j < *(int16_t *)(ds_block + DS_NCAND)
                            && j < DS_CAND_N; j++) {
                    uint8_t ent[64];
                    int     m;

                    memset(ent, 0, sizeof ent);
                    ent[DB_COUNT] = 0x20;
                    for (m = 0; m < 2; m++) {
                        ent[DB_KANJI + m * 2] =
                            (uint8_t)ic_block[IC_TEXT + (at + m) * 2];
                        ent[DB_KANJI + m * 2 + 1] =
                            (uint8_t)ic_block[IC_TEXT + (at + m) * 2 + 1];
                    }
                    printf("DS cmp %ld %d %d", which, j,
                           (int)DS(CompareKanji)(ds_block, ent, (int16_t)j));
                    ent[DB_KANJI] = 0x93;
                    ent[DB_KANJI + 1] = 0xfa;
                    ent[DB_KANJI + 2] = 0x81;
                    ent[DB_KANJI + 3] = 0x5b;
                    printf(" %d\n",
                           (int)DS(CompareKanji)(ds_block, ent, (int16_t)j));
                }
            }

            memset(ds_block, 0, sizeof ds_block);
            memset(ta_block, 0, sizeof ta_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            printf("DS word %ld wrote %d\n", which,
                   (int)DS(GenerateWord)(ds_block, (int16_t)at, 0));
            for (j = 0; j < *(int16_t *)(ds_block + DS_CURSOR)
                        && j < DS_ENTRY_N; j++)
                putRecord("word", which * 100L + j,
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + j * DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
            printf("DS word %ld longs %d\n", which,
                   (int)(uint8_t)ta_block[TA_LONGWORDS]);
        }
    }
    printf("DS rest done\n");
}

/* ---- the user dictionary --------------------------------------------- */

/* Words a caller might teach it, as written form and reading. The reading
   carries a caret where the accent falls, which is the interface, and the
   list covers what each arm of the walk is for: hiragana, katakana, a long
   vowel, a doubled consonant, a small kana, an accent at the front, at the
   back, and none at all. */
static const char *const UD_WORDS[][2] = {
    { "\x93\xfa\x96\x7b", "\x83\x6a\x83\x7a\x83\x93\x5e" },
    { "\x8c\xea",         "\x83\x53\x5e" },
    { "\x8e\x52\x93\x63", "\x83\x84\x83\x7d\x83\x5f\x5e" },
    { "\x93\x8c\x8b\x9e", "\x5e\x83\x67\x83\x45\x83\x4c\x83\x87\x83\x45" },
    { "\x8b\x5a\x8f\x70", "\x83\x4d\x83\x85\x83\x63\x83\x5e\x5e" },
    { "\x8d\x87\x90\xac", "\x83\x53\x81\x5b\x5e\x83\x5a\x83\x43" },
    { "A",                "\x83\x47\x81\x5b\x5e" },
    { "\xb1\xb2\xb3",     "\x83\x41\x83\x43\x83\x45\x5e" },
    { "\x89\xb9\x90\xba", "\x83\x49\x83\x93\x83\x5a\x83\x43" },
    /* And two written in hiragana, so that the fold into katakana that
       transKana2Yomi does before spelling anything out is something the sweep
       can see. Every reading above is already katakana, where that fold is a
       no-op. */
    { "\x82\xa0\x82\xa2", "\x82\xa0\x82\xa2\x5e\x82\xa4" },
    { "\x8b\x9e",         "\x82\xab\x82\xe5\x82\xa4\x5e" },
    { NULL, NULL }
};

/* And the things makeKey has to refuse or fold: white space, the full-width
   space, half-width kana with and without a voicing mark, the two commas,
   letters and digits, and a key of every single byte there is. */
static const char *const UD_KEYS[] = {
    "\x93\xfa\x96\x7b\x8c\xea",
    "\x82\xa0\x82\xa2\x82\xa4",
    "\x83\x41\x81\x5b\x83\x67",
    "abcXYZ019",
    "!\"#$%&'()*+,-./",
    "a b",
    "a\tb",
    "a\nb",
    "a\rb",
    "\x81\x40",
    "\x93\xfa\x81\x40\x96\x7b",
    "\xb1\xb2\xb3\xb4\xb5",
    "\xb6\xde\xb7\xde\xb8\xde",
    "\xca\xdf\xcb\xdf\xcc\xdf",
    "\xa1\xa2\xa3\xa4\xa5",
    "\xdc\xdd\xde\xdf",
    "\x82\xa0" "a" "\xb1",
    "",
    NULL
};

static void sweepUserDict(void)
{
    static char ud_room[0x40];
    static char list_room[IBM_LIST_ROOM];
    static char key_room[IBM_KEY_ROOM];
    static char context[SN_ROOM];
    static char contextWord[16];
    char        keyBuf[256];
    uint8_t     yomi[64];
    int32_t     entRoom[0x40 / 4];
    void       *u = ud_room;
    long        i;
    long        j;

    /* The owner, with the two sub-objects the constructor reaches for. */
    memset(ta_block, 0, sizeof ta_block);
    memset(ds_block, 0, sizeof ds_block);
    memset(ic_block, 0, sizeof ic_block);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
    DS_SET_OWNER(ds_block, ta_block);

    memset(ud_room, 0, sizeof ud_room);
    UD(Ctor)(u, ta_block);

    /* The key, over everything that has to be folded or refused. */
    for (i = 0; UD_KEYS[i] != NULL; i++) {
        int32_t len = -1;
        int32_t rc;

        memset(keyBuf, 0, sizeof keyBuf);
        rc = UD(MakeKey)(u, (uint8_t *)UD_KEYS[i],
                         (int32_t)strlen(UD_KEYS[i]), keyBuf, &len);
        printf("UD key %ld rc %d len %d ", i, (int)rc, (int)len);
        putBytes(keyBuf);
        putchar('\n');
    }

    /* And over every single byte on its own, and every byte after a kana,
       since a voicing mark is only meaningful after one. */
    for (i = 0; i <= 0xff; i++) {
        char in[4];
        int32_t len = -1;
        int32_t rc;

        in[0] = (char)i;
        in[1] = 0;
        memset(keyBuf, 0, sizeof keyBuf);
        rc = UD(MakeKey)(u, (uint8_t *)in, 1, keyBuf, &len);
        printf("UD byte %02lx rc %d len %d ", i, (int)rc, (int)len);
        putBytes(keyBuf);

        in[0] = (char)0xb1;
        in[1] = (char)i;
        in[2] = 0;
        len = -1;
        memset(keyBuf, 0, sizeof keyBuf);
        rc = UD(MakeKey)(u, (uint8_t *)in, 2, keyBuf, &len);
        printf(" | after kana rc %d len %d ", (int)rc, (int)len);
        putBytes(keyBuf);
        putchar('\n');
    }

    /* The reading: where the caret lands, and what the kana become. */
    for (i = 0; UD_WORDS[i][0] != NULL; i++) {
        uint8_t accent = 0xee;
        int32_t rc;
        uint8_t n;

        memset(keyBuf, 0, sizeof keyBuf);
        rc = UD(MakeTransValue)(u, UD_WORDS[i][1], &accent, keyBuf, 0x33);
        printf("UD trans %ld rc %d accent %d ", i, (int)rc, (int)accent);
        putBytes(keyBuf);
        putchar('\n');

        memset(yomi, 0, sizeof yomi);
        n = UD(TransKatakana2Yomi)(u, keyBuf, yomi);
        printf("UD kata %ld n %d", i, (int)n);
        for (j = 0; j < 32; j++)
            printf(" %02x", (unsigned)yomi[j]);
        putchar('\n');

        memset(yomi, 0, sizeof yomi);
        n = UD(TransKana2Yomi)(u, keyBuf, yomi);
        printf("UD kana %ld n %d", i, (int)n);
        for (j = 0; j < 32; j++)
            printf(" %02x", (unsigned)yomi[j]);
        putchar('\n');
    }

    /* A caret in every position of one reading, and a reading with something
       in it that is neither kana nor a caret. */
    {
        static const char *const ODD[] = {
            "^", "^\x83\x41", "\x83\x41^", "\x83\x41^\x83\x43",
            "\x83\x62^", "\x81\x5b^", "\x83\x83^", "\x83\x41\x83\x83^",
            "\x83\x41" "x", "x", "\x82\xa0^", "\x82\xc1^",
            "\x83\x41\x83\x43\x83\x45\x83\x47\x83\x49^",
            NULL
        };

        for (i = 0; ODD[i] != NULL; i++) {
            uint8_t accent = 0xee;
            int32_t rc;

            memset(keyBuf, 0, sizeof keyBuf);
            rc = UD(MakeTransValue)(u, ODD[i], &accent, keyBuf, 0x33);
            printf("UD odd %ld rc %d accent %d ", i, (int)rc, (int)accent);
            putBytes(keyBuf);
            putchar('\n');
        }
    }

    /* The stored record, over every part of speech there is and two that are
       not, and a reading too long to store. */
    for (i = 0; UD_WORDS[i][0] != NULL; i++) {
        long p;

        for (p = -1; p <= 4; p++) {
            uint8_t d[0x40];
            int32_t rc;

            memset(d, 0xee, sizeof d);
            rc = UD(MakeUserDictData)(u, d, 4, (char *)UD_WORDS[i][1],
                                      (int32_t)p);
            printf("UD data %ld %ld rc %d", i, p, (int)rc);
            for (j = 0; j < 0x1f; j++)
                printf(" %02x", (unsigned)d[j]);
            putchar('\n');
        }
    }
    {
        char longKana[128];
        uint8_t d[0x40];
        int32_t rc;

        for (i = 0; i < 30; i++) {
            longKana[i * 2] = (char)0x83;
            longKana[i * 2 + 1] = (char)(0x41 + (i % 10) * 2);
        }
        longKana[60] = 0;
        memset(d, 0xee, sizeof d);
        rc = UD(MakeUserDictData)(u, d, 4, longKana, 1);
        printf("UD long rc %d", (int)rc);
        for (j = 0; j < 0x1f; j++)
            printf(" %02x", (unsigned)d[j]);
        putchar('\n');

        longKana[50] = 0;
        memset(d, 0xee, sizeof d);
        rc = UD(MakeUserDictData)(u, d, 4, longKana, 1);
        printf("UD long2 rc %d", (int)rc);
        for (j = 0; j < 0x1f; j++)
            printf(" %02x", (unsigned)d[j]);
        putchar('\n');
    }

    /* Writing one into a candidate entry, at the bounds of the array and
       with the long-word store both empty and full. */
    icSetText(TEXTS[1]);
    for (i = 0; i < 2; i++) {
        static const int16_t SLOTS[] = { 0, 1, 709, 710, 711 };
        int si;

        for (si = 0; si < (int)(sizeof SLOTS / sizeof SLOTS[0]); si++) {
            uint8_t d[0x40];
            int32_t rc;

            memset(ds_block, 0, sizeof ds_block);
            memset(ta_block, 0, sizeof ta_block);
            TA_SET(ta_block, TA_INPUTCHAR, ic_block);
            TA_SET(ta_block, TA_DICTSEARCH, ds_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            ta_block[TA_LONGWORDS] = (char)(i == 0 ? 0 : TA_LONGWORD_N);

            memset(d, 0, sizeof d);
            UD(MakeUserDictData)(u, d, 8, (char *)UD_WORDS[3][1], 1);
            rc = UD(WriteData)(u, d, SLOTS[si], 2);
            printf("UD write %ld %d rc %d", i, (int)SLOTS[si], (int)rc);
            if (rc && SLOTS[si] < DS_ENTRY_N)
                for (j = 0; j < DS_ENTRY_SIZE; j++)
                    printf(" %02x", (unsigned)(uint8_t)
                           ds_block[DS_ENTRY + SLOTS[si] * DS_ENTRY_SIZE + j]);
            printf(" longs %d\n", (int)(uint8_t)ta_block[TA_LONGWORDS]);
        }
    }
    (void)entRoom;

    /* And the whole of it over a real dictionary: teach it every word, read
       each back, then look a sentence up against it in both modes. */
    ibm_slCtor(list_room);
    UD_DICT(u) = list_room;

    for (i = 0; UD_WORDS[i][0] != NULL; i++) {
        int32_t rc = UD(UpdateDictExt)(u, list_room, 0,
                                       (uint8_t *)UD_WORDS[i][0],
                                       (int32_t)strlen(UD_WORDS[i][0]),
                                       (char *)UD_WORDS[i][1],
                                       (int32_t)strlen(UD_WORDS[i][1]), 1);

        printf("UD update %ld rc %d\n", i, (int)rc);
    }
    /* A word too long for a key, and one with nothing storable in it. */
    {
        char big[64];

        memset(big, 'a', 40);
        big[40] = 0;
        printf("UD update big rc %d\n",
               (int)UD(UpdateDictExt)(u, list_room, 0, (uint8_t *)big, 40,
                                      (char *)"\x83\x41", 2, 1));
        printf("UD update space rc %d\n",
               (int)UD(UpdateDictExt)(u, list_room, 0,
                                      (uint8_t *)"a b", 3,
                                      (char *)"\x83\x41", 2, 1));
        printf("UD update badpos rc %d\n",
               (int)UD(UpdateDictExt)(u, list_room, 0,
                                      (uint8_t *)"\x82\xa0", 2,
                                      (char *)"\x83\x41", 2, 9));
    }

    for (i = 0; UD_WORDS[i][0] != NULL; i++) {
        void   *value = (void *)0x1234;
        int32_t len = -1;
        int32_t pos = -1;
        int32_t rc = UD(LookupDictExt)(u, list_room, 0,
                                       (uint8_t *)UD_WORDS[i][0],
                                       (int32_t)strlen(UD_WORDS[i][0]),
                                       &value, &len, &pos);

        printf("UD read %ld rc %d len %d pos %d ", i, (int)rc, (int)len,
               (int)pos);
        putBytes(value == NULL ? NULL : (const char *)value);
        putchar('\n');
    }
    printf("UD read absent rc %d\n",
           (int)(UD(LookupDictExt)(u, list_room, 0, (uint8_t *)"\x82\xf1", 2,
                                   (void **)&keyBuf,
                                   (int32_t *)&entRoom[0],
                                   (int32_t *)&entRoom[1])));

    /* The lookup the analyser makes: the whole of what is left of the
       sentence at once, in both of DictSearch's modes. */
    memset(context, 0, sizeof context);
    strcpy(contextWord, "\x93\xfa\x96\x7b");
    SN_SET_KEY(context, contextWord);
    for (i = 0; i < 2; i++) {
        int n = icSetText(TEXTS[1]);
        int at;

        for (at = 0; at < n && at < 6; at++) {
            int16_t rc;

            memset(ds_block, 0, sizeof ds_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            memset(ta_block, 0, sizeof ta_block);
            TA_SET(ta_block, TA_INPUTCHAR, ic_block);
            TA_SET(ta_block, TA_DICTSEARCH, ds_block);
            *(int32_t *)(ds_block + DS_USERDICT_MODE) = (int32_t)i;
            *(void **)(ds_block + DS_USERDICT_WORD) = context;
            if (i == 1) {
                /* What the context names has to be what the stored record
                   holds: its first two bytes, which are how many characters
                   the written form has and how many codes its reading is --
                   not the reading itself. Setting them from the record the
                   first word would make is what gives the mode-one arm
                   something to match, and without a match the whole arm goes
                   untested. */
                uint8_t d[0x40];

                memset(d, 0, sizeof d);
                UD(MakeUserDictData)(u, d, 4, (char *)UD_WORDS[0][1], 1);
                context[0x10] = (char)d[0];
                context[0x11] = (char)d[1];
            }

            rc = UD(Lookup)(u, (uint8_t *)(ic_block + IC_TEXT),
                            (int16_t)at, 0);
            printf("UD lookup %ld %d rc %d cursor %d\n", i, at, (int)rc,
                   (int)*(int16_t *)(ds_block + DS_CURSOR));
            for (j = 0; j < rc && j < 8; j++)
                putRecord("lookup", i * 1000 + at * 10 + j,
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + j * DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
        }
    }

    /* The key-length bound, from both sides of it, and with the two kinds of
       character that make a key of a different length from the word: a
       two-byte character keeps its length and a half-width kana doubles. */
    for (i = 24; i <= 34; i++) {
        char word[80];
        long k;

        for (k = 0; k < i; k += 2) {
            word[k] = (char)0x82;
            word[k + 1] = (char)(0xa0 + (k / 2) % 8);
        }
        word[i] = 0;
        printf("UD bound wide %ld rc %d\n", i,
               (int)UD(UpdateDictExt)(u, list_room, 0, (uint8_t *)word,
                                      (int32_t)i, (char *)"\x83\x41", 2, 1));
    }

    /* Not swept: a word of twenty half-width kana or more. Each of them
       becomes two bytes, so the key is twice the length of the word, and
       IBM's own buffer for it is about thirty-six bytes on its stack while
       the bound it enforces lets a key reach sixty-four. Nineteen is the last
       one that works; at twenty its own frame goes and Wine's debugger comes
       up. Measured, not reasoned about, and ours is not swept against a side
       that has stopped running. See rom/jajp/userdict.c. */

    printf("UD done\n");
}

/* ---- the dictionaries proper ----------------------------------------- */

/* One DictSearch and one owner, laid out the way the sweeps above do it. */
static void dsFresh(void)
{
    memset(ds_block, 0, sizeof ds_block);
    memset(ta_block, 0, sizeof ta_block);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
    DS_SET_OWNER(ds_block, ta_block);
}

/* Every candidate entry a lookup wrote, as the bytes they are. */
static void putEntries(const char *what, long which, int n)
{
    int j;

    for (j = 0; j < n && j < 12; j++)
        putRecord(what, which * 100L + j,
                  (const uint8_t *)(ds_block + DS_ENTRY + j * DS_ENTRY_SIZE),
                  DS_ENTRY_SIZE);
}

static void sweepDictionaries(void)
{
    static char context[SN_ROOM];
    static char contextWord[64];
    long        code;
    int         t;
    int         j;

    /* The variant table, over every value a two-byte character can have.
       Both answers on one line, so a table read that has drifted shows up as
       a run rather than as one line. */
    dsFresh();
    for (code = 0; code <= 0xffff; code++)
        printf("DD itaiji %04lx %d %04x\n", code,
               (int)DS(IsItaiji)(ds_block, (uint16_t)code),
               (unsigned)DS(SwapKanji)(ds_block, (uint16_t)code));

    /* The placeholder, at each end of the candidate array. */
    icSetText(TEXTS[1]);
    {
        static const int16_t SLOTS[] = { 0, 1, 708, 709, 710, 711 };
        int si;

        for (si = 0; si < (int)(sizeof SLOTS / sizeof SLOTS[0]); si++) {
            int16_t rc;

            dsFresh();
            rc = DS(ErrorDummy)(ds_block, SLOTS[si], 2);
            printf("DD dummy %d rc %d\n", (int)SLOTS[si], (int)rc);
            if (rc)
                putRecord("dummy", SLOTS[si],
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + SLOTS[si] * DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
        }
    }

    /* The three writers, over records built by hand: a word count of nought
       to three, readings of every length a nibble can hold -- including the
       ones longer than a candidate entry has room for -- and the long-word
       store both empty and full. The text under them is the katakana one, so
       that every small kana the first writer refuses on is there to be
       found, one after another. */
    icSetText(KANA_EDGES);
    for (t = 0; t < 4; t++) {
        long len;

        for (len = 0; len <= 15; len++) {
            uint8_t node[512];
            uint8_t *w;
            int      r;
            int      k;
            int16_t  rc;

            memset(node, 0, sizeof node);
            node[NH_FLAGS] = (uint8_t)(t > 0 ? 0x80 : 0x00);
            node[NH_COUNT] = (uint8_t)t;
            w = node + NH_WORD;
            for (r = 0; r < t; r++) {
                w[NW_HEAD] = (uint8_t)((r << 4) | (len & 0xf));
                w[NW_POS] = (uint8_t)(r == 0 ? 0 : 0x40 + r);
                w[NW_ATTR] = (uint8_t)(0x10 + r);
                w[NW_ATTR + 1] = (uint8_t)(0x20 + r);
                for (k = 0; k < (len & 0xf); k++)
                    w[NW_KANA + k] = (uint8_t)(0x30 + r * 16 + k);
                w += NW_KANA + (len & 0xf);
            }
            /* Over several places for the word to end, because what the
               character after it is decides whether anything is written at
               all -- a small kana or a long bar there refuses the lot. */
            for (j = 0; j < 20; j++) {
                dsFresh();
                rc = DS(WriteData)(ds_block, node, (int16_t)(t + 1),
                                   (int16_t)(len & 0xf), 0, (int16_t)j, 1);
                printf("DD write %d %ld %d rc %d\n", t, len, j, (int)rc);
                putEntries("write", (t * 100L + len) * 20 + j, rc);
            }

            /* And the same node read as a single-kanji one, whose count is a
               nibble of the fourth byte rather than a byte of the seventh. */
            memset(node, 0, sizeof node);
            node[TH_FLAGS] = (uint8_t)(t << 4);
            w = node + TH_READING;
            for (r = 0; r < t; r++) {
                w[TR_LEN] = (uint8_t)((r << 4) | (len & 0xf));
                w[1] = (uint8_t)(0x50 + r);
                w[2] = (uint8_t)(0x11 + r);
                w[3] = (uint8_t)(0x22 + r);
                for (k = 0; k < (len & 0xf); k++)
                    w[TR_KANA + k] = (uint8_t)(0x60 + r * 16 + k);
                w += TR_KANA + (len & 0xf);
            }
            dsFresh();
            rc = DS(WriteTankanData)(ds_block, node, (int16_t)(t + 1), 0, 1);
            printf("DD tankan %d %ld rc %d\n", t, len, (int)rc);
            putEntries("tankan", t * 100L + len, rc);

            /* And as a supplement record, which holds one word. */
            memset(node, 0, sizeof node);
            node[UH_CHARS] = (uint8_t)(t + 1);
            node[UH_KANALEN] = (uint8_t)(len & 0xf);
            node[UH_ACCENT] = (uint8_t)(len & 7);
            for (k = 0; k < (t + 1) * 2; k++)
                node[UH_TEXT + k] = (uint8_t)(0x82 + k);
            w = node + UH_TEXT + (t + 1) * 2;
            for (k = 0; k < (len & 0xf); k++)
                w[k] = (uint8_t)(0x70 + k);
            w[len & 0xf] = 0x33;
            w[(len & 0xf) + 1] = 0x44;
            w[(len & 0xf) + 2] = 0x55;
            node[UH_LEN] = (uint8_t)(UH_TEXT + (t + 1) * 2 + (len & 0xf) + 3);

            for (j = 0; j < 2; j++) {
                dsFresh();
                ta_block[TA_LONGWORDS] = (char)(j == 0 ? 0 : TA_LONGWORD_N);
                rc = DS(WriteUserData)(ds_block, node, 0, 1);
                printf("DD user %d %ld %d rc %d longs %d\n", t, len, j,
                       (int)rc, (int)(uint8_t)ta_block[TA_LONGWORDS]);
                putEntries("user", (t * 100L + len) * 10 + j, rc);
            }
        }
    }

    /* And the five dictionaries themselves, over real Japanese at every
       position of every text, out of context and in it. */
    memset(context, 0, sizeof context);
    SN_SET_KEY(context, contextWord);

    for (t = 0; TEXTS[t] != NULL; t++) {
        int n = icSetText(TEXTS[t]);
        int at;
        int mode;

        for (mode = 0; mode < 2; mode++)
            for (at = 0; at < n; at++) {
                long which = (mode * 100L + t) * 1000L + at;
                int16_t rc;

                /* In context, the word to agree with is the one that
                   actually starts here, two characters of it, so the arm is
                   entered rather than always refused. */
                contextWord[0] = ic_block[IC_TEXT + at * 2];
                contextWord[1] = ic_block[IC_TEXT + at * 2 + 1];
                contextWord[2] = ic_block[IC_TEXT + (at + 1) * 2];
                contextWord[3] = ic_block[IC_TEXT + (at + 1) * 2 + 1];
                contextWord[4] = 0;
                context[SN_CHARS] = 2;

                dsFresh();
                if (mode) {
                    *(int32_t *)(ds_block + DS_USERDICT_MODE) = 1;
                    *(void **)(ds_block + DS_USERDICT_WORD) = context;
                }
                rc = DS(LookupNormalWordDict)(ds_block, 0, (int16_t)at, 0);
                printf("DD normal %ld rc %d\n", which, (int)rc);
                putEntries("normal", which, rc);

                dsFresh();
                if (mode) {
                    *(int32_t *)(ds_block + DS_USERDICT_MODE) = 1;
                    *(void **)(ds_block + DS_USERDICT_WORD) = context;
                }
                rc = DS(LookupTankanDict)(ds_block, 0, (int16_t)at);
                printf("DD tankandict %ld rc %d\n", which, (int)rc);
                putEntries("tankandict", which, rc);

                dsFresh();
                if (mode) {
                    *(int32_t *)(ds_block + DS_USERDICT_MODE) = 1;
                    *(void **)(ds_block + DS_USERDICT_WORD) = context;
                }
                rc = (int16_t)DS(LookupEngWordDictFromText)(ds_block, 0,
                                                            (int16_t)at);
                printf("DD eng %ld rc %d\n", which, (int)rc);
                putEntries("eng", which, rc);

                dsFresh();
                if (mode) {
                    *(int32_t *)(ds_block + DS_USERDICT_MODE) = 1;
                    *(void **)(ds_block + DS_USERDICT_WORD) = context;
                }
                rc = DS(LookupUserDict)(ds_block, SUPP_D,
                                        (char *)(ic_block + IC_TEXT + at * 2),
                                        0, SUPP_I, (int16_t)at, 0);
                printf("DD supp %ld rc %d\n", which, (int)rc);
                putEntries("supp", which, rc);
            }
    }
    /* And the function words, which are a different kind of lookup: a word is
       taken on whether the phrase before it can carry one, so the search is
       handed a bit vector of what precedes. Every position of every text, and
       the vector both wide open and narrowed to one bit at a time so that the
       agreement test is swept rather than assumed. */
    for (t = 0; TEXTS[t] != NULL; t++) {
        int n = icSetText(TEXTS[t]);
        int at;

        for (at = 0; at < n; at++) {
            long    which = t * 1000L + at;
            int16_t rc;
            int     b;

            dsFresh();
            rc = DS(LookupFuncWordDict)(ds_block, 0, (int16_t)at);
            printf("DF word %ld rc %d\n", which, (int)rc);
            putEntries("fword", which, rc);
            for (j = 0; j < rc && j < 8; j++)
                putRecord("frow", which * 100L + j,
                          (const uint8_t *)(ds_block + DS_FZK
                                            + j * DS_FZK_SIZE),
                          DS_FZK_SIZE);

            /* And the one bound the texts never reach on their own: the
               function-word array is 726 rows and no sentence here fills a
               tenth of it, so the row cursor is driven to its end by hand. */
            {
                static const int16_t SLOTS[] = { 0, 724, 725, 726, 727 };
                uint8_t wide[14];
                int     si;

                for (j = 0; j < 14; j++)
                    wide[j] = 0xff;
                for (si = 0; si < (int)(sizeof SLOTS / sizeof SLOTS[0]); si++) {
                    dsFresh();
                    printf("DF hit %ld %d rc %d\n", which, (int)SLOTS[si],
                           (int)DS(SearchFuncWordDict)(ds_block, wide,
                                                       (int16_t)at, SLOTS[si],
                                                       FUNC_DICT(), 0));
                }
            }

            for (b = 0; b < 16; b++) {
                uint8_t vec[14];
                int     k;
                int     fl;

                for (k = 0; k < 14; k++)
                    vec[k] = (uint8_t)(b == 0 ? 0xff
                                       : (b <= 14 && k == b - 1 ? 0xff : 0));
                if (b == 15)
                    for (k = 0; k < 14; k++)
                        vec[k] = 0x55;

                for (fl = 0; fl < 3; fl++) {
                    dsFresh();
                    rc = DS(SearchFuncWordDict)(ds_block, vec, (int16_t)at, 0,
                                                FUNC_DICT(), (int16_t)fl);
                    printf("DF search %ld %d %d rc %d\n", which, b, fl,
                           (int)rc);
                    for (j = 0; j < rc && j < 6; j++)
                        putRecord("srow", (which * 100L + b) * 10 + fl,
                                  (const uint8_t *)(ds_block + DS_FZK
                                                    + j * DS_FZK_SIZE),
                                  DS_FZK_SIZE);
                }
            }
        }
    }

    /* And over every hiragana there is, in pairs.
     *
     * The nineteen texts above are sentences and reach only the function
     * words those sentences happen to use; the dictionary holds twenty
     * thousand records and more than a thousand of them carry an accent past
     * the end of their own reading, which is a road worth walking. Every pair
     * of hiragana as a two-character text covers the index entirely. */
    {
        long a;
        long b;

        for (a = 0x9f; a <= 0xf1; a++)
            for (b = 0x9f; b <= 0xf1; b++) {
                char    text[8];
                int16_t rc;

                text[0] = (char)0x82;
                text[1] = (char)a;
                text[2] = (char)0x82;
                text[3] = (char)b;
                text[4] = 0;
                icSetText(text);
                dsFresh();
                rc = DS(LookupFuncWordDict)(ds_block, 0, 0);
                printf("DF pair %02lx%02lx rc %d\n", a, b, (int)rc);
                for (j = 0; j < rc && j < 4; j++)
                    putRecord("prow", a * 1000L + b * 10 + j,
                              (const uint8_t *)(ds_block + DS_ENTRY
                                                + j * DS_ENTRY_SIZE),
                              DS_ENTRY_SIZE);
            }
    }

    /* And the English rules, which are not a dictionary at all: an English
       word in the text has no entry anywhere, so it is spelled out by two
       tables of substitutions instead. Every one, two and three letter word
       there is goes through all four methods -- eighteen thousand of them,
       which is the whole of what three letters can spell -- and a list of
       real words after them for the shapes only a longer word has. */
    {
        static const char *const WORDS[] = {
            "abandon", "abbey", "computer", "internet", "voice", "viavoice",
            "Japan", "JAPAN", "japanese", "rhythm", "strength", "queue",
            "yacht", "gnome", "knight", "psalm", "xylophone", "zzz",
            "AbleCat", "aB", "A", "a", "y", "yy", "myth", "sky", "by",
            "eye", "aeiou", "bcdfg", "e", "I", "IBM", "ibm", "aBcd",
            /* Words chosen out of the rule tables rather than out of the
               head. Three roads through the rules were unreachable from
               ordinary short words, and the tables say exactly which rules
               they need: the thirteen whose replacement carries a star, the
               twenty anchored to the end of a word, and the eight that weigh
               exactly eight and so decide a tie. One word per rule. */
            "school", "president", "presume", "accuse", "bread", "create",
            "friend", "great", "swum", "quarter", "quality", "quantity",
            "identity",
            "basically", "totally", "helpless", "kindness", "woman", "women",
            "salesman", "salesmen", "carefully", "quickly", "lifelike",
            "careful", "movement", "movements", "friendship", "handsome",
            "childhood",
            "criticism", "artist", "musician", "fashion", "vision", "mission",
            "question", "nation",
            NULL
        };
        long a;
        long b;
        long c;
        int  w;

        for (w = 0; WORDS[w] != NULL; w++) {
            uint8_t in[64];
            uint8_t o1[256];
            uint8_t o2[256];
            uint8_t o3[256];
            int16_t acc = -1;
            int16_t len = -1;
            int16_t cnt = -1;
            size_t  n = strlen(WORDS[w]);

            memcpy(in, WORDS[w], n);
            in[n] = '#';
            in[n + 1] = '#';
            in[n + 2] = 0;

            memset(o1, 0, sizeof o1);
            printf("DE upper %d rc %d ", w,
                   (int)DS(EngRulesUppercase)(ds_block, in, o1));
            putBytes((const char *)o1);
            memset(o2, 0, sizeof o2);
            printf(" | norm rc %d ",
                   (int)DS(EngRulesNormalize)(ds_block, in, o2));
            putBytes((const char *)o2);
            memset(o3, 0, sizeof o3);
            acc = -1;
            printf(" | rule rc %d acc ",
                   (int)DS(EngRulesApplyRule)(ds_block, o2, o3, ENG_RULES,
                                              &acc));
            printf("%d ", (int)acc);
            putBytes((const char *)o3);
            putchar('\n');

            memset(o3, 0, sizeof o3);
            len = -1;
            cnt = -1;
            printf("DE conv %d rc %d len %d cnt %d", w,
                   (int)DS(EngRulesConvert)(ds_block, in, o3, ENG_RULES,
                                            KANA_RULES, &len, &cnt),
                   (int)len, (int)cnt);
            for (j = 0; j < 24; j++)
                printf(" %02x", (unsigned)o3[j]);
            putchar('\n');
        }

        /* Every word of one, two and three letters. */
        for (a = 'a'; a <= 'z'; a++)
            for (b = 'a' - 1; b <= 'z'; b++)
                for (c = 'a' - 1; c <= 'z'; c++) {
                    uint8_t in[8];
                    uint8_t out[256];
                    int16_t len = -1;
                    int16_t cnt = -1;
                    int     n = 0;
                    int     rc;

                    if (b < 'a' && c >= 'a')
                        continue;
                    in[n++] = (uint8_t)a;
                    if (b >= 'a')
                        in[n++] = (uint8_t)b;
                    if (c >= 'a')
                        in[n++] = (uint8_t)c;
                    in[n] = '#';
                    in[n + 1] = '#';
                    in[n + 2] = 0;

                    memset(out, 0, sizeof out);
                    rc = DS(EngRulesConvert)(ds_block, in, out, ENG_RULES,
                                             KANA_RULES, &len, &cnt);
                    printf("DE w %.*s rc %d len %d cnt %d", n, (char *)in,
                           rc, (int)len, (int)cnt);
                    for (j = 0; j < 16; j++)
                        printf(" %02x", (unsigned)out[j]);
                    putchar('\n');
                }
    }

    /* And the fallbacks: what happens to a character no dictionary knew.
     *
     * These want a Romanizer as well, because one of them reads a flag out of
     * it, so a block stands in for the one class of the analyser that is not
     * written. Everything else is laid out as the sweeps above do it. */
    {
        static char rz_block[0x100];
        int         t;
        int         sp;

        TA_SET(ta_block, TA_OWNER, rz_block);

        for (t = 0; TEXTS[t] != NULL; t++) {
            int n = icSetText(TEXTS[t]);
            int at;

            for (at = 0; at < n; at++) {
                long which = t * 1000L + at;
                int32_t entRoom[DS_ENTRY_SIZE / 4];
                uint8_t *e = (uint8_t *)entRoom;
                char     say[8];
                int16_t  rc;
                int      w;

                /* The two placeholders and the letter name, on their own. */
                dsFresh();
                TA_SET(ta_block, TA_OWNER, rz_block);
                memset(e, 0, DS_ENTRY_SIZE);
                DS(SetDummySymbol)(ds_block, (int16_t)at, e);
                putRecord("dsym", which, e, DS_ENTRY_SIZE);

                memset(e, 0, DS_ENTRY_SIZE);
                DS(SetDummyRomanAlphabet)(ds_block, (int16_t)at, e);
                putRecord("drom", which, e, DS_ENTRY_SIZE);

                /* The English rules through the analyser's own entry, with
                   the romanizer's flag both ways round. */
                for (sp = 0; sp < 4; sp++) {
                    dsFresh();
                    memset(rz_block, 0, sizeof rz_block);
                    TA_SET(ta_block, TA_OWNER, rz_block);
                    *(int32_t *)(rz_block + RZ_SPELL_ENGLISH) = sp & 1;
                    /* And with the long-word store full, which narrows the
                       room a reading has from twenty-four bytes to eight. */
                    ta_block[TA_LONGWORDS] = (char)(sp & 2 ? TA_LONGWORD_N : 0);
                    memset(e, 0, DS_ENTRY_SIZE);
                    DS(ProcessRomanAlphabet)(ds_block, (int16_t)at, e);
                    putRecord("prom", which * 10 + sp, e, DS_ENTRY_SIZE);
                }

                /* The number counters. */
                dsFresh();
                TA_SET(ta_block, TA_OWNER, rz_block);
                rc = DS(JoSuusiSearch)(ds_block, (int16_t)at);
                printf("DH josuusi %ld rc %d\n", which, (int)rc);
                for (j = 0; j < rc && j < 4; j++)
                    putRecord("jrow", which * 10 + j,
                              (const uint8_t *)(ds_block + DS_REC
                                                + j * DS_REC_SIZE),
                              DS_REC_SIZE);
                printf("DH cmp %ld %d %d %d\n", which,
                       (int)DS(CompareJMD)(ds_block,
                                           (uint8_t *)(ic_block + IC_TEXT
                                                       + at * 2),
                                           (int16_t)at, 1),
                       (int)DS(CompareJMD)(ds_block,
                                           (uint8_t *)(ic_block + IC_TEXT
                                                       + at * 2),
                                           (int16_t)at, 3),
                       (int)DS(CompareJMD)(ds_block,
                                           (uint8_t *)(ic_block + IC_TEXT),
                                           (int16_t)at, 2));

                /* The two that read the candidate array, over an array with
                   something in it: the normal-word lookup fills it first. */
                dsFresh();
                TA_SET(ta_block, TA_OWNER, rz_block);
                w = (int)DS(LookupNormalWordDict)(ds_block, 0, (int16_t)at, 0);
                printf("DH katakana %ld %d %d\n", which, w,
                       (int)DS(NeedKatakanaAnalysis)(ds_block, 0, (int16_t)w));
                printf("DH jrt %ld %d marks", which,
                       (int)DS(CheckJrtTable)(ds_block, 0, (int16_t)w));
                for (j = 0; j < 24; j++)
                    printf(" %02x", (unsigned)(uint8_t)ta_block[TA_MARKS + j]);
                putchar('\n');

                /* The mark table over entries built by hand, because what
                   sets a mark turns on a candidate's own part of speech and
                   length, and no lookup produces the whole range of either.
                   The placeholder's part of speech in particular is only ever
                   written by ErrorDummy. */
                {
                    static const uint8_t POS[] = { 0, 0x75, 0x76, 0x7a, 0x7d };
                    int pi;
                    int ch;

                    /* The two lengths vary apart from one another on
                       purpose: tying them together lets the test on the
                       reading shadow the test on the characters, and then
                       neither is really swept. */
                    for (pi = 0; pi < 5; pi++)
                        for (ch = 0; ch <= 6; ch++) {
                            int kl;

                            for (kl = 0; kl <= 4; kl++) {
                                uint8_t *ent;

                                dsFresh();
                                TA_SET(ta_block, TA_OWNER, rz_block);
                                ent = (uint8_t *)(ds_block + DS_ENTRY);
                                ent[DE_POS] = POS[pi];
                                ent[DE_CHARS] = (uint8_t)ch;
                                ent[DE_KANALEN] = (uint8_t)kl;
                                *(int16_t *)(ent + DE_AT) = (int16_t)at;
                                printf("DH jrt2 %ld %d %d %d rc %d marks",
                                       which, pi, ch, kl,
                                       (int)DS(CheckJrtTable)(ds_block, 0, 1));
                                for (j = 0; j < 16; j++)
                                    printf(" %02x", (unsigned)(uint8_t)
                                           ta_block[TA_MARKS + j]);
                                putchar('\n');
                            }
                        }
                }

                /* And the whole of it, with the parse mark both plain and
                   set to the value that asks for a number counter. */
                for (sp = 0; sp < 2; sp++) {
                    dsFresh();
                    TA_SET(ta_block, TA_OWNER, rz_block);
                    ta_block[TA_MARKS + at] = (char)(sp ? 3 : 0);
                    memset(say, 0xee, sizeof say);
                    rc = DS(HandleError)(ds_block, (int16_t)at, 0, 0, say);
                    printf("DH err %ld %d rc %d say %02x%02x cursor %d\n",
                           which, sp, (int)rc, (unsigned)(uint8_t)say[0],
                           (unsigned)(uint8_t)say[1],
                           (int)(uint8_t)ta_block[TA_MARKS + at]);
                    putEntries("herr", which * 10 + sp, rc);
                }
            }
        }
    }

    /* And the annotations, which ride along with the text rather than being
       part of it. A ring of 128, so the sweep fills it past its bound as well
       as under: what a hundred and twenty-ninth does is IBM's answer and not
       an opinion of mine. */
    {
        static const char *const ANNOS[] = {
            "`ts h", "`ui 4242", "`g", "`i2", "`p100", "`vv692", "`0",
            "plain", "", "`t", "`u", "`ui", "`ts", "`gg", "`ii", "`pp",
            NULL
        };
        static char an_room[0x600];
        void  *an = an_room;
        long   k;
        int    esc;
        int    drop;

        /* One at a time, and what each is taken for. */
        for (k = 0; ANNOS[k] != NULL; k++) {
            memset(an_room, 0, sizeof an_room);
            ANNO_CTOR(an, ta_block);
            printf("DA kind %ld %d\n", k,
                   (int)AN(GetRomHandAnnoType)(an, ANNOS[k]));
        }

        /* Saved in order, read back, and given up from each end. */
        for (k = 0; k <= 4; k++) {
            long m;

            memset(an_room, 0, sizeof an_room);
            ANNO_CTOR(an, ta_block);
            for (m = 0; ANNOS[m] != NULL; m++)
                printf("DA save %ld %ld rc %d\n", k, m,
                       (int)AN(Save)(an, (char *)ANNOS[m],
                                     (int16_t)strlen(ANNOS[m]),
                                     (int16_t)(m * 2)));
            for (m = 0; m < 3; m++)
                for (j = 0; j < 40; j += 7) {
                    const char *got = AN(GetLastAnno)(an, (int16_t)j,
                                                      (int32_t)m);

                    printf("DA last %ld %ld %d ", k, m, j);
                    putBytes(got);
                    putchar('\n');
                }
            for (m = 0; m < k; m++)
                AN(Remove)(an);
            printf("DA after remove %ld ", k);
            for (j = 0; j < 6; j++) {
                const char *got = AN(GetLastAnno)(an, 99, 2);

                putBytes(got);
                putchar(' ');
                if (got == NULL)
                    break;
                AN(Remove)(an);
            }
            putchar('\n');
        }

        /* Giving up everything past a position. */
        for (k = 0; k <= 40; k += 4) {
            long m;

            memset(an_room, 0, sizeof an_room);
            ANNO_CTOR(an, ta_block);
            for (m = 0; ANNOS[m] != NULL; m++)
                AN(Save)(an, (char *)ANNOS[m], (int16_t)strlen(ANNOS[m]),
                         (int16_t)(m * 2));
            AN(RemoveAfter)(an, (int16_t)k);
            printf("DA cut %ld ", k);
            for (m = 0; m < 20; m++) {
                const char *got = AN(GetLastAnno)(an, 99, 2);

                if (got == NULL)
                    break;
                putBytes(got);
                putchar(' ');
                AN(Remove)(an);
            }
            putchar('\n');
        }

        /* And written out, with each of the two flags both ways. */
        for (esc = 0; esc < 2; esc++)
            for (drop = 0; drop < 2; drop++) {
                void *buf;
                long  m;

                memset(an_room, 0, sizeof an_room);
                ANNO_CTOR(an, ta_block);
                for (m = 0; ANNOS[m] != NULL; m++)
                    AN(Save)(an, (char *)ANNOS[m],
                             (int16_t)strlen(ANNOS[m]), (int16_t)(m * 2));
                buf = BUF_NEW(4096);
                printf("DA flush %d %d rc %d len %d ", esc, drop,
                       (int)AN(Flush)(an, (int32_t)esc, buf, (int32_t)drop),
                       (int)BUF_LEN(buf));
                putBytes(BUF_STR(buf));
                putchar('\n');
                BUF_DEL(buf);
            }

        /* Past the ring's bound, which nothing checks. */
        {
            long m;

            memset(an_room, 0, sizeof an_room);
            ANNO_CTOR(an, ta_block);
            for (m = 0; m < 140; m++) {
                char one[16];

                sprintf(one, "`u%ld", m);
                AN(Save)(an, one, (int16_t)strlen(one), (int16_t)m);
            }
            printf("DA over ");
            for (m = 0; m < 8; m++) {
                const char *got = AN(GetLastAnno)(an, 999, 2);

                if (got == NULL)
                    break;
                putBytes(got);
                putchar(' ');
                AN(Remove)(an);
            }
            putchar('\n');
        }
    }

    printf("DD done\n");
}

/* ---- InputChar --------------------------------------------------------- */

/* The reader's record after a call, printed as runs of equal bytes so that ten
   thousand of them fit on one line. The three fields IBM keeps a pointer in
   are skipped rather than printed: on its side they hold an address and on
   ours nothing at all, since our own three sit past the record. What is in
   them is proved by what the calls do instead. */
static void putReader(const char *what)
{
    long at = 0;

    printf("IC %s", what);
    while (at < IC_BYTES) {
        long          first = at;
        unsigned char v;

        if (at == IC_OWNER || at == IC_TEXTP || at == IC_SNLK_TABLE) {
            at += 4;
            fputs(" ptr", stdout);
            continue;
        }
        v = (unsigned char)ic_block[at];
        do {
            at++;
        } while (at < IC_BYTES && at != IC_TEXTP && at != IC_SNLK_TABLE
                 && (unsigned char)ic_block[at] == v);
        printf(" %lx:%lx=%02x", first, at - first, v);
    }
    putchar('\n');
}

/* And one node of the chain: everything on it that is not a pointer, and then
   what the two pointers point at. */
static void putSnlk(const char *what, long i, void *node)
{
    unsigned char *n = (unsigned char *)node;
    int            k;

    printf("IC %s %ld ", what, i);
    if (node == NULL) {
        printf("-\n");
        return;
    }
    printf("at %d trans %02x chars %d yomi %d ",
           (int)*(int16_t *)(n + SN_AT), n[SN_TRANS],
           (int)n[SN_CHARS], (int)n[SN_YOMI_N]);
    for (k = 0; k < (int)n[SN_YOMI_N] && k < SN_BYTES - SN_YOMI; k++)
        printf("%02x", n[SN_YOMI + k]);
    putchar(' ');
    putBytes(SN_KEY_OF(node));
    putchar(' ');
    putBytes(SN_VALUE_OF(node));
    putchar('\n');
}

/* What is handed to ic_AddSnlkTable. The positions are deliberately out of
   order: the chain is kept in the order it was given and the lookup stops as
   soon as it has gone past what it wants, so an unsorted chain is what proves
   that line rather than the one below it. The last six are the six ways the
   call is refused. */
static const struct {
    int16_t     at;
    const char *written;
    const char *reading;
    int32_t     flag;
} SNLK[] = {
    { 0, "\x93\xfa\x96\x7b", "\x83\x6a\x83\x7a\x83\x93\x5e",         0 },
    { 2, "\x8c\xea",         "\x83\x53\x5e",                         1 },
    { 5, "\x8e\x52\x93\x63", "\x83\x84\x83\x7d\x83\x5f\x5e",         0 },
    { 1, "\x82\xa0\x82\xa2", "\x82\xa0\x82\xa2\x5e\x82\xa4",         7 },
    { 9, "A",                "\x83\x47\x81\x5b\x5e",                 0 },
    { 3, "\xb1\xb2\xb3",     "\x83\x41\x83\x43\x83\x45\x5e",         0 },
    { 4, "\x20\x20",         "\x83\x41\x5e",                         0 },
    /* Half-width kana with a voicing mark, so that the fold is walked, and a
       byte in the middle that makeKey drops outright, which is the only shape
       that tells the count of the key apart from the count of what the caller
       wrote. */
    { 10, "\xb6\xde\x01\xb7\xde", "\x83\x4b\x83\x4e\x5e",        0 },
    { 6, "\x8b\x9e",         "\x82\xab\x82\xe5\x82\xa4\x5e",         0 },
    { 7, "\x89\xb9\x90\xba", "abcdef",                               0 },
    { 8, "\x93\x8c\x8b\x9e",
         "\x5e\x83\x67\x83\x45\x83\x4c\x83\x87\x83\x45\x83\x67\x83\x45\x83\x4c"
         "\x83\x87\x83\x45\x83\x67\x83\x45\x83\x4c\x83\x87\x83\x45\x83\x67\x83"
         "\x45\x83\x4c\x83\x87\x83\x45",                             0 },
    { -1, "\x8c\xea",        "\x83\x53\x5e",                         0 },
    { 0, NULL,               "\x83\x53\x5e",                         0 },
    { 0, "",                 "\x83\x53\x5e",                         0 },
    { 0, "\x8c\xea",         NULL,                                   0 },
    { 0, "\x8c\xea",         "",                                     0 },
    { 0, "\x8c\xea",         "\x83\x53\x5e",                        -1 }
};

/* Texts for the unknown-kanji walk that the shared list has not got: all seven
   of the marks it steps over, a mark between characters it keeps, newlines
   between them, single bytes between double ones, and marks before a kanji. */
static const char *const IC_TEXTS[] = {
    "\x81\x40\x81\x41\x81\x42\x81\x48\x81\x49\x81\x43\x81\x44",
    "\x93\xfa\x81\x40\x96\x7b\x81\x41\x8c\xea\x81\x42\x8e\x52\x81\x43\x93\x63"
    "\x81\x44\x93\x8c\x81\x48\x8b\x9e\x81\x49\x89\xb9",
    "\x93\xfa\x0a\x96\x7b\x0a\x0a\x8c\xea",
    "a\x93\xfa" "b\x96\x7b" "c",
    "\x81\x48\x81\x49\x81\x43\x81\x44\x93\xfa"
};

static void sweepInputChar(void)
{
    static char rom_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    long        i;
    long        j;
    long        nTexts;
    for (nTexts = 0; TEXTS[nTexts] != NULL; nTexts++)
        ;


    /* The chain the reader goes up and comes back down: TextAnalysis above
       it, the romanizer above that, and hanging off the romanizer the
       parameter block this file made at the start and a user dictionary built
       on the same TextAnalysis. */
    memset(ta_block, 0, sizeof ta_block);
    memset(rom_room, 0, sizeof rom_room);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_OWNER, rom_room);
    RZ_SET_PARAM(rom_room, the_param);
    RZ_SET_USERDICT(rom_room, ud_room);
    UD(Ctor)(ud_room, ta_block);

    /* Made, and made again over a record that is not empty, which is the only
       way to see how much of each array the two of them actually clear. */
    memset(ic_block, 0xa5, sizeof ic_block);
    IC(Ctor)(ic_block, ta_block);
    putReader("ctor");
    memset(ic_block + IC_TEXT, 0x5a, IC_BYTES - IC_TEXT);
    IC(Init)(ic_block);
    putReader("init");

    /* The question two objects up. Nothing in the romanizer writes the field
       it comes from, so the sweep writes it and puts it back afterwards. */
    {
        int32_t was = PARAM_ANNO(the_param);

        for (i = 0; i < 4; i++) {
            PARAM_ANNO(the_param) = (int32_t)i;
            printf("IC anno %ld %d\n", i,
                   (int)IC(IsAnnotationsInText)(ic_block));
        }
        PARAM_ANNO(the_param) = was;
    }

    /* Setting text, both ways. The second counts the characters of whatever
       was there up to where the reader had got to, so the byte it is told to
       carry on from is swept over the whole of each text. */
    for (i = 0; TEXTS[i] != NULL; i++) {
        long len = (long)strlen(TEXTS[i]);

        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);

        /* Once on a reader that has never been given text, which is the arm
           that skips the counting altogether. */
        *(int32_t *)(ic_block + IC_POS) = 5;
        IC(SetTextAt)(ic_block, TEXTS[i], (uint32_t)len);
        printf("IC settextat fresh %ld pos %ld len %d\n", i,
               (long)*(int32_t *)(ic_block + IC_POS),
               (int)*(int16_t *)(ic_block + IC_LENGTH));

        IC(SetText)(ic_block, TEXTS[i]);
        printf("IC settext %ld pos %ld ended %ld end %ld len %d\n", i,
               (long)*(int32_t *)(ic_block + IC_POS),
               (long)*(int32_t *)(ic_block + IC_ENDED),
               (long)*(int32_t *)(ic_block + IC_AT_END),
               (int)*(int16_t *)(ic_block + IC_LENGTH));

        for (j = 0; j <= len; j++) {
            *(int32_t *)(ic_block + IC_POS) = (int32_t)j;
            IC(SetTextAt)(ic_block, TEXTS[i], (uint32_t)j);
            printf("IC settextat %ld %ld pos %ld len %d\n", i, j,
                   (long)*(int32_t *)(ic_block + IC_POS),
                   (int)*(int16_t *)(ic_block + IC_LENGTH));
        }

        /* And the byte the reader is on, over every byte of the text and one
           past the end of it. */
        printf("IC nextchar %ld ", i);
        for (j = 0; j <= len; j++) {
            *(int32_t *)(ic_block + IC_POS) = (int32_t)j;
            printf("%02x", IC(GetNextChar)(ic_block));
        }
        putchar('\n');
    }

    /* The unknown-kanji pass, over every text and six spans of each, from two
       starting characters. The texts are the shared ones plus five of its own,
       since the shared list has none of the seven marks the walk steps over
       and no newline. */
    for (i = 0; i < nTexts + (long)(sizeof IC_TEXTS / sizeof *IC_TEXTS); i++) {
        const char *text = i < nTexts ? TEXTS[i] : IC_TEXTS[i - nTexts];
        long        len = (long)strlen(text);
        long        spans[6][2];
        long        s;
        int         a;

        spans[0][0] = 0;     spans[0][1] = len;
        spans[1][0] = 0;     spans[1][1] = 1;
        spans[2][0] = 1;     spans[2][1] = len;
        spans[3][0] = 0;     spans[3][1] = 0;
        spans[4][0] = 3;     spans[4][1] = 2;
        spans[5][0] = 2;     spans[5][1] = len - 1;

        for (a = 0; a < 2; a++) {
            for (s = 0; s < 6; s++) {
                int16_t rc;
                long    n;

                memset(ic_block, 0xa5, sizeof ic_block);
                IC(Ctor)(ic_block, ta_block);
                IC(SetText)(ic_block, text);
                rc = IC(GetUnknownKanji)(ic_block, (int16_t)(a * 7),
                                         (int32_t)spans[s][0],
                                         (int32_t)spans[s][1]);
                n = (long)*(int16_t *)(ic_block + IC_COUNT);
                printf("IC unknown %ld %d %ld rc %d count %ld pos %ld\n", i, a,
                       s, (int)rc, n,
                       (long)*(int32_t *)(ic_block + IC_POS));
                for (j = 0; j < n && j < 64; j++)
                    printf("IC unknown %ld %d %ld at %ld %02x%02x kind %ld"
                           " off %d mark %ld\n", i, a, s, j,
                           (unsigned char)ic_block[IC_TEXT + j * 2],
                           (unsigned char)ic_block[IC_TEXT + j * 2 + 1],
                           (long)*(int32_t *)(ic_block + IC_KIND + j * 4),
                           (int)*(int16_t *)(ic_block + IC_OFFSET + j * 2),
                           (long)*(int32_t *)(ic_block + IC_MARK + j * 4));
            }
        }
    }

    /* And one text long enough to reach the guard, which is a road no sentence
       the analyser will accept can take. It lets the six hundred and
       ninety-fifth character through and writes it one entry past the scratch
       it has, over the first two bytes of the kinds; both sides do it, so what
       IC_KIND holds afterwards is the thing to watch. */
    {
        static char many[1500];
        int16_t     rc;

        for (j = 0; j < 700; j++) {
            many[j * 2] = '\x93';
            many[j * 2 + 1] = (char)(0xfa - (j & 3));
        }
        many[1400] = 0;
        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        IC(SetText)(ic_block, many);
        rc = IC(GetUnknownKanji)(ic_block, 0, 0, 1400);
        printf("IC many rc %d count %d kind0 %ld pos %ld\n", (int)rc,
               (int)*(int16_t *)(ic_block + IC_COUNT),
               (long)*(int32_t *)(ic_block + IC_KIND),
               (long)*(int32_t *)(ic_block + IC_POS));
        for (j = 690; j < 700; j++)
            printf("IC many at %ld %02x%02x off %d mark %ld\n", j,
                   (unsigned char)ic_block[IC_TEXT + j * 2],
                   (unsigned char)ic_block[IC_TEXT + j * 2 + 1],
                   (int)*(int16_t *)(ic_block + IC_OFFSET + j * 2),
                   (long)*(int32_t *)(ic_block + IC_MARK + j * 4));
    }


    /* The chain: everything added, then looked up at every position it could
       be at, then thrown away and looked up again. The lookup is done with
       three values of the count of characters already consumed, since that is
       added to what it is asked for. */
    memset(ic_block, 0xa5, sizeof ic_block);
    IC(Ctor)(ic_block, ta_block);
    for (i = 0; i < (long)(sizeof SNLK / sizeof *SNLK); i++)
        printf("IC add %ld rc %d\n", i,
               (int)IC(AddSnlkTable)(ic_block, SNLK[i].at, SNLK[i].written,
                                     SNLK[i].reading, SNLK[i].flag));

    /* Then the chain itself, from the head, which is the only way to see a
       node the lookup above cannot reach -- the chain is in the order it was
       given and the lookup stops as soon as it has gone past what it wants,
       so a node behind a later position is never answered with. This is also
       what says the new one went on the end rather than anywhere else. */
    {
        void *node = IC_SNLK_HEAD(ic_block);

        for (j = 0; node != NULL && j < 32; j++) {
            putSnlk("chain", j, node);
            node = SN_NEXT_OF(node);
        }
    }
    for (i = 0; i < 3; i++) {
        *(int16_t *)(ic_block + IC_LENGTH) = (int16_t)(i * 2);
        for (j = -1; j < 12; j++)
            putSnlk("at", i * 100 + j,
                    IC(GetSnlkTableAt)(ic_block, (int16_t)j));
    }
    *(int16_t *)(ic_block + IC_LENGTH) = 0;
    IC(DeleteSnlkTable)(ic_block);
    for (j = 0; j < 12; j++)
        putSnlk("gone", j, IC(GetSnlkTableAt)(ic_block, (int16_t)j));
    printf("IC head %d\n", IC(GetSnlkTableAt)(ic_block, 0) == NULL);

    /* And a chain built in position order, so that the road the unsorted one
       above never takes -- walking on past a node -- is taken too. */
    for (i = 0; i < 6; i++)
        (void)IC(AddSnlkTable)(ic_block, (int16_t)i, UD_WORDS[i][0],
                               UD_WORDS[i][1], 0);
    for (j = -1; j < 12; j++)
        putSnlk("sorted", j, IC(GetSnlkTableAt)(ic_block, (int16_t)j));
    IC(DeleteSnlkTable)(ic_block);

    /* And readings long enough to straddle the twenty-five the count is
       clamped to, each on a chain of its own so that the walk above cannot
       stop short of them. Half of them end in a long-vowel bar, which is the
       only way the count comes back at twenty-six: transKatakana2Yomi stops
       itself at twenty-five and then writes one more for the bar. */
    for (i = 0; i < 10; i++) {
        char reading[128];
        long k;
        int  n = 0;

        reading[n++] = '\x5e';
        for (k = 0; k < 20 + i; k++) {
            reading[n++] = '\x83';
            reading[n++] = '\x67';
        }
        if (i & 1) {
            reading[n++] = '\x81';
            reading[n++] = '\x5b';
        }
        reading[n] = 0;
        (void)IC(AddSnlkTable)(ic_block, 0, "\x93\x8c\x8b\x9e", reading, 0);
        putSnlk("long", i, IC(GetSnlkTableAt)(ic_block, 0));
        IC(DeleteSnlkTable)(ic_block);
    }

    printf("IC done\n");
}


/* Everything the reader put in the record, after one call. The counts are
   printed first so that a difference in how far it got is one line rather than
   a hundred. */
static char *anno_of;

static void putSentence(const char *what, long i, long k, int rc)
{
    long n = (long)*(int16_t *)(ic_block + IC_COUNT);
    long j;

    printf("IC %s %ld %ld rc %d count %ld ended %ld pos %ld raw %d end %ld"
           " more %ld pause %ld eng %ld num %ld join %ld brk %d mark ",
           what, i, k, rc, n,
           (long)*(int32_t *)(ic_block + IC_ENDED),
           (long)*(int32_t *)(ic_block + IC_POS),
           (int)*(int16_t *)(ic_block + IC_RAWPOS),
           (long)*(int32_t *)(ic_block + IC_AT_END),
           (long)*(int32_t *)(ic_block + IC_MORE),
           (long)*(int32_t *)(ic_block + IC_PAUSE),
           (long)*(int32_t *)(ic_block + IC_ENGRUN),
           (long)*(int32_t *)(ic_block + IC_NUMRUN),
           (long)*(int32_t *)(ic_block + IC_NUMJOIN),
           (int)*(int16_t *)(ic_block + IC_BRACKET_AT));
    putBytes(ic_block + IC_ENDMARK);
    putchar('\n');
    /* One past the count as well, because the comma the recovery walk writes
       goes there and nothing else would ever print it. */
    for (j = 0; j <= n && j < 70; j++)
        printf("IC %s %ld %ld at %ld %02x%02x kind %ld off %d mark %ld\n",
               what, i, k, j,
               (unsigned char)ic_block[IC_TEXT + j * 2],
               (unsigned char)ic_block[IC_TEXT + j * 2 + 1],
               (long)*(int32_t *)(ic_block + IC_KIND + j * 4),
               (int)*(int16_t *)(ic_block + IC_OFFSET + j * 2),
               (long)*(int32_t *)(ic_block + IC_MARK + j * 4));

    /* And whatever the reader lifted out into the annotations, which is the
       only way the position it saved them at is observed. */
    if (anno_of != NULL) {
        long t;

        for (t = 0; t < 6; t++) {
            long b;

            for (b = 0; b < 9; b++) {
                printf("IC %s %ld %ld anno %ld %ld ", what, i, k, t, b);
                putBytes(AN(GetLastAnno)(anno_of,
                                         (int16_t)(b < 7 ? b : (b - 6) * 20),
                                         (int32_t)t));
                putchar('\n');
            }
        }
    }
}

/* Texts for the reader: the shared ones, then ends of every kind, spaces and
   tabs in and out of numbers and English, annotations of each shape, middle
   dots, half-width kana with and without a voicing mark, a full-width space
   where a break belongs, brackets, and one long enough to be cut back. */
static const char *const IC_SENTENCES[] = {
    "\x93\xfa\x96\x7b\x8c\xea\x81\x42\x8e\x52\x93\x63\x81\x41\x93\x8c\x8b\x9e"
    "\x81\x48\x89\xb9\x90\xba\x81\x49",
    "\x93\xfa\x96\x7b\x81\x44\x8e\x52\x93\x63\x81\x43\x93\x8c\x8b\x9e",
    "1.5\x82\xcd\x8e\x52\x93\x63\x81\x42" "3,000\x89\x7e\x81\x42",
    "\x82\xa0\x81\x40\x82\xa2\x81\x40\x82\xa4\x81\x42",
    "abc def\x81\x42" "ghi\tjkl\n" "mno",
    "\x93\xfa\x81\x45\x96\x7b\x81\x45\x8c\xea\x81\x42",
    "\x93\xfa\x81\x45\x81\x45\x81\x45\x96\x7b\x81\x42",
    "\x60p200\x93\xfa\x96\x7b\x81\x42",
    "\x60pau\x93\xfa\x81\x42\x60" "0\x96\x7b",
    "\x60\x93\xfa\x96\x7b\x81\x42",
    "\xb6\xde\xb7\xde\xb8\x93\xfa\x81\x42",
    "\xb1\xb2\xb3\xa4\xb4\xa1\xb5",
    "\x81\x6d\x93\xfa\x96\x7b\x81\x6e\x8c\xea\x81\x42",
    "\x82\xa0\x82\xa2\x82\xa4\n\n\x82\xa6\x82\xa8\x81\x42",
    "\x82\xcd\x93\xfa\x96\x7b\x82\xcd\x8e\x52\x81\x42",
    "\x81\x40\x93\xfa\x96\x7b",
    "\x81\x41\x93\xfa\x96\x7b",
    "\x81\x42\x93\xfa\x96\x7b",
    ".\x93\xfa\x96\x7b",
    ",\x93\xfa\x96\x7b",
    "\xa4\x93\xfa\x96\x7b",
    "\xa1\x93\xfa\x96\x7b",
    "\x93\xfa\x96\x7b\r\n\x8c\xea\x81\x42",
    "\x93\xfa\x96\x7b 12 \x8c\xea\x81\x42",
    "IBM \x93\xfa\x96\x7b Corp\x81\x42",
    "\x93\xfa",
    "abc,def\x81\x42",
    "\x93\xfa\x96\x7b,\x8c\xea\x81\x42",
    "\x93\xfa\x96\x7b.\x8c\xea\x81\x42",
    "\x82\x60\x82\x61\x82\x62\x81\x44\x82\x63\x82\x64",
    "\x82\x50\x82\x51\r\n\x82\x52\x82\x53\x81\x42",
    "\x82\x60\x82\x61\r\n\x82\x62\x82\x63\x81\x42",
    "\x82\x60\x82\x61 \x82\x62\x82\x63\x81\x42",
    "\x93\xfa\x96\x7b   \x8c\xea\x81\x42"
};

/* Texts for the number reader: kanji digits with and without place words,
   full-width digits, the counters that may follow a number, thousands marks in
   the right places and in the wrong ones, and a place word standing alone. */
static const char *const NUM_TEXTS[] = {
    "\x88\xea\x93\xf1\x8e\x4f",
    "\x88\xea\x8f\x5c",
    "\x88\xea\x8f\x5c\x93\xf1",
    "\x8e\x4f\x90\xe7\x8c\xdc\x95\x53",
    "\x88\xea\x96\x9c\x93\xf1\x90\xe7",
    "\x8f\x5c",
    "\x95\x53",
    "\x90\xe7",
    "\x96\x9c",
    "\x82\x50\x82\x51\x82\x52",
    "\x82\x50\x81\x43\x82\x50\x82\x50\x82\x50",
    "\x82\x50\x82\x50\x81\x43\x82\x50\x82\x50",
    "\x82\x50\x82\x4f\x82\x4f\x89\x7e",
    "\x88\xea\x8c\x8e",
    "\x88\xea\x93\xfa",
    "\x88\xea\x94\x4e",
    "\x88\xea\x8e\x9e",
    "\x88\xea\x95\xaa",
    "\x88\xea\x95\x53\x93\xf1\x8f\x5c\x8e\x4f",
    "\x82\x50\x82\x51\x82\x52\x82\x53\x82\x54\x82\x55\x82\x56"
    "\x82\x57\x82\x58\x82\x50\x82\x51\x82\x52\x82\x53\x82\x54"
    "\x82\x55\x82\x56\x82\x57",
    "\x97\xe9",
    "\x93\xfa\x96\x7b"
};

static void sweepReader(void)
{
    static char rom_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    static char anno_room[ANNO_ROOM];
    static char raw_room[4096];
    static char work[4096];
    long        i;
    long        j;
    long        k;

    memset(ta_block, 0, sizeof ta_block);
    memset(rom_room, 0, sizeof rom_room);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_OWNER, rom_room);
    TA_SET(ta_block, TA_ANNOTATION, anno_room);
    TA_SET(ta_block, TA_RAW, raw_room);
    RZ_SET_PARAM(rom_room, the_param);
    RZ_SET_USERDICT(rom_room, ud_room);
    UD(Ctor)(ud_room, ta_block);

    /* What every one of the sixty-five thousand two-byte characters is, which
       is the whole of the classifier and the whole of the kanji-numeral test
       in one sweep. */
    memset(ic_block, 0xa5, sizeof ic_block);
    IC(Ctor)(ic_block, ta_block);
    for (i = 0; i < 0x10000; i++) {
        ic_block[IC_TEXT] = (char)(i >> 8);
        ic_block[IC_TEXT + 1] = (char)i;
        printf("IC kind %04lx %ld %ld\n", i,
               (long)IC(GetCharType)(ic_block, 0),
               (long)IC(IsKanjiNum)(ic_block, 0));
    }
    printf("IC kind before %ld\n", (long)IC(GetCharType)(ic_block, -1));

    /* Every half-width kana against both voicing marks. */
    for (i = 0xa1; i <= 0xdf; i++) {
        for (j = 0; j < 2; j++) {
            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            *(int32_t *)(ic_block + IC_POS) = 9;
            *(int16_t *)(ic_block + IC_RAWPOS) = 7;
            IC(ConvertDakuten)(ic_block, 1, (uint8_t)i,
                               (uint8_t)(0xde + j));
            printf("IC dakuten %02lx %ld %02x%02x off %d mark %ld\n", i, j,
                   (unsigned char)ic_block[IC_TEXT + 2],
                   (unsigned char)ic_block[IC_TEXT + 3],
                   (int)*(int16_t *)(ic_block + IC_OFFSET + 2),
                   (long)*(int32_t *)(ic_block + IC_MARK + 4));
        }
    }

    /* Every single byte through the ASCII writer, at the start of a sentence
       and inside one, which are the two roads it has. */
    for (i = 0; i < 256; i++) {
        for (j = 0; j < 2; j++) {
            uint8_t c0 = (uint8_t)i;
            uint8_t c1 = 0x5a;
            int16_t rc;

            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            *(int32_t *)(ic_block + IC_POS) = 5;
            *(int16_t *)(ic_block + IC_RAWPOS) = 3;
            rc = IC(ProcessASCII)(ic_block, (int16_t)j, &c0, &c1);
            printf("IC ascii %02lx %ld rc %d %02x%02x off %d mark %ld end ",
                   i, j, (int)rc,
                   (unsigned char)ic_block[IC_TEXT + j * 2],
                   (unsigned char)ic_block[IC_TEXT + j * 2 + 1],
                   (int)*(int16_t *)(ic_block + IC_OFFSET + j * 2),
                   (long)*(int32_t *)(ic_block + IC_MARK + j * 4));
            putBytes(ic_block + IC_ENDMARK);
            putchar('\n');
        }
    }

    /* The two look-ahead questions, over every byte as the next one. */
    for (i = 0; i < 256; i++) {
        int16_t at;

        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        memset(work, 0, sizeof work);
        work[0] = (char)i;
        work[1] = (char)0x20;
        work[2] = (char)0x40;
        work[3] = 0x60;
        IC(SetText)(ic_block, work);
        printf("IC next %02lx %ld\n", i,
               (long)IC(CheckNextAnnotation)(ic_block));

        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        memset(work, 0, sizeof work);
        work[0] = (char)i;
        work[1] = 0x20;
        work[2] = 0x60;
        IC(SetText)(ic_block, work);
        printf("IC next2 %02lx %ld\n", i,
               (long)IC(CheckNextAnnotation)(ic_block));

        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        memset(work, 0, sizeof work);
        work[0] = (char)i;
        work[1] = (char)0x50;
        IC(SetText)(ic_block, work);
        *(int32_t *)(ic_block + IC_KIND) = KIND_DIGIT;
        at = 1;
        {
            /* The call first and the flag afterwards, in two statements: in
               one printf the order is the compiler's and the flag was being
               read before the call that sets it. */
            int rc = (int)IC(CheckContextForNum)(ic_block, &at);

            printf("IC ctxnum %02lx rc %d join %ld at %d\n", i, rc,
                   (long)*(int32_t *)(ic_block + IC_NUMJOIN), (int)at);
        }
    }

    /* The context walk on its own, over both values of its flag and every
       kind before it, so that the two run flags, the English promotion and the
       look-ahead write are all reached without having to find a sentence that
       reaches them. */
    for (i = 0; i < 14; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 6; k++) {
                static const char *const AFTER[] = {
                    "\x93\xfa", "\x82\x50", "1", " ", "", "\r\n\x82\x60"
                };
                int16_t at = 1;
                int16_t rc;

                memset(ic_block, 0xa5, sizeof ic_block);
                IC(Ctor)(ic_block, ta_block);
                memset(work, 0, sizeof work);
                strcpy(work, "  ");
                strcpy(work + 2, AFTER[k]);
                IC(SetText)(ic_block, work);
                *(int32_t *)(ic_block + IC_POS) = 2;
                *(int16_t *)(ic_block + IC_RAWPOS) = 5;
                *(int32_t *)(ic_block + IC_KIND) = (int32_t)i;
                memcpy(ic_block + IC_TEXT, "\x82\x60", 2);
                memcpy(ic_block + IC_TEXT + 2, "\x82\x61", 2);
                *(int32_t *)(ic_block + IC_MARK + 4) = 1;
                rc = IC(CheckContext)(ic_block, &at, (int32_t)j);
                printf("IC ctx %ld %ld %ld rc %d at %d kind0 %ld kind1 %ld"
                       " kind2 %ld eng %ld num %ld brk %d next %02x%02x"
                       " off2 %d mark2 %ld\n", i, j, k, (int)rc, (int)at,
                       (long)*(int32_t *)(ic_block + IC_KIND),
                       (long)*(int32_t *)(ic_block + IC_KIND + 4),
                       (long)*(int32_t *)(ic_block + IC_KIND + 8),
                       (long)*(int32_t *)(ic_block + IC_ENGRUN),
                       (long)*(int32_t *)(ic_block + IC_NUMRUN),
                       (int)*(int16_t *)(ic_block + IC_BRACKET_AT),
                       (unsigned char)ic_block[IC_TEXT + 4],
                       (unsigned char)ic_block[IC_TEXT + 5],
                       (int)*(int16_t *)(ic_block + IC_OFFSET + 4),
                       (long)*(int32_t *)(ic_block + IC_MARK + 8));
            }
        }
    }

    /* And at the bound itself, with and without a space before the character,
       which is what decides whether one is given back before the sentence is
       cut. */
    for (i = 0; i < 2; i++) {
        int16_t at = 0x3d;
        int16_t rc;

        memset(ic_block, 0, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        anno_of = NULL;
        memset(raw_room, 0, sizeof raw_room);
        memset(work, i ? ' ' : 'x', 200);
        work[200] = 0;
        for (k = 0; k < 64; k++) {
            memcpy(ic_block + IC_TEXT + k * 2, "\x82\xa0", 2);
            *(int32_t *)(ic_block + IC_KIND + k * 4) =
                (k % 2 == 0) ? KIND_HIRAGANA : KIND_KANJI;
            *(int32_t *)(ic_block + IC_MARK + k * 4) = (int32_t)(k * 2);
            *(int16_t *)(ic_block + IC_OFFSET + k * 2) = (int16_t)k;
        }
        memset(anno_room, 0, sizeof anno_room);
        ANNO_CTOR(anno_room, ta_block);
        IC(SetText)(ic_block, work);
        *(int32_t *)(ic_block + IC_POS) = 100;
        rc = IC(CheckContext)(ic_block, &at, 0);
        printf("IC ctxbound %ld rc %d at %d count %d pos %ld raw %d\n", i,
               (int)rc, (int)at, (int)*(int16_t *)(ic_block + IC_COUNT),
               (long)*(int32_t *)(ic_block + IC_POS),
               (int)*(int16_t *)(ic_block + IC_RAWPOS));
    }

    /* And once with a closing bracket laid down, which is the only character
       that walk remembers where it was. */
    {
        int16_t at = 1;

        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        memset(work, 0, sizeof work);
        IC(SetText)(ic_block, work);
        memcpy(ic_block + IC_TEXT + 2, "\x81\x6e", 2);
        printf("IC ctxbrk rc %d brk %d\n",
               (int)IC(CheckContext)(ic_block, &at, 0),
               (int)*(int16_t *)(ic_block + IC_BRACKET_AT));
    }

    /* Runs of middle dots of every length up to five, and the same with
       something else after each. */
    for (i = 0; i <= 5; i++) {
        for (j = 0; j < 2; j++) {
            int16_t at = (int16_t)j;

            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            memset(work, 0, sizeof work);
            for (k = 0; k < i; k++) {
                work[k * 2] = (char)0x81;
                work[k * 2 + 1] = (char)0x45;
            }
            work[i * 2] = (char)0x93;
            work[i * 2 + 1] = (char)0xfa;
            IC(SetText)(ic_block, work);
            printf("IC cyuten %ld %ld %d\n", i, j,
                   (int)IC(CheckCyuTen)(ic_block, &at));
        }
    }

    /* The reader itself, over every text, called until it says the text is
       done or ten times, whichever comes first. The raw text the annotations
       are placed against carries its own marks in the second pass, which is
       the only way the recovery walk reaches them. */
    for (j = 0; j < 2; j++) {
        for (i = 0; i < (long)(sizeof IC_SENTENCES / sizeof *IC_SENTENCES);
             i++) {
            memset(anno_room, 0, sizeof anno_room);
            ANNO_CTOR(anno_room, ta_block);
            anno_of = anno_room;
            memset(raw_room, (int)(j ? 1 : 0), sizeof raw_room);
            memset(work, 0, sizeof work);
            strcpy(work, IC_SENTENCES[i]);
            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            IC(SetText)(ic_block, work);

            for (k = 0; k < 10; k++) {
                int rc = (int)IC(ReadSentence)(ic_block);

                putSentence(j ? "marked" : "read", i, k, rc);
                if (*(int32_t *)(ic_block + IC_AT_END))
                    break;
                *(int32_t *)(ic_block + IC_RESUME) = 1;
            }
            printf("IC after %ld %ld ", j, i);
            putBytes(work);
            putchar('\n');
        }
    }

    /* Again without ever setting the resume flag, so that every call starts a
       sentence afresh and clears the arrays. */
    for (i = 0; i < (long)(sizeof IC_SENTENCES / sizeof *IC_SENTENCES); i++) {
        memset(anno_room, 0, sizeof anno_room);
        ANNO_CTOR(anno_room, ta_block);
        anno_of = anno_room;
        memset(raw_room, 0, sizeof raw_room);
        memset(work, 0, sizeof work);
        strcpy(work, IC_SENTENCES[i]);
        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        IC(SetText)(ic_block, work);
        for (k = 0; k < 4; k++) {
            int rc = (int)IC(ReadSentence)(ic_block);

            putSentence("fresh", i, k, rc);
            if (*(int32_t *)(ic_block + IC_AT_END))
                break;
        }
    }

    /* And a buffer that runs out in the middle of a sentence, with a second
       one handed over after it, which is the only way the step back over the
       mark the reader had already counted is reached. */
    for (i = 0; i < (long)(sizeof IC_SENTENCES / sizeof *IC_SENTENCES); i++) {
        static char more[4096];

        memset(anno_room, 0, sizeof anno_room);
        ANNO_CTOR(anno_room, ta_block);
        anno_of = anno_room;
        memset(raw_room, 0, sizeof raw_room);
        memset(work, 0, sizeof work);
        memcpy(work, IC_SENTENCES[i], 6);
        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        IC(SetText)(ic_block, work);
        putSentence("cut", i, 0, (int)IC(ReadSentence)(ic_block));
        memset(more, 0, sizeof more);
        strcpy(more, IC_SENTENCES[i]);
        IC(SetTextAt)(ic_block, more, 6);
        *(int32_t *)(ic_block + IC_RESUME) = 1;
        putSentence("cut", i, 1, (int)IC(ReadSentence)(ic_block));
    }

    /* And one sentence far too long, which is the only way to the walk that
       cuts it back. */
    for (j = 0; j < 3; j++) {
        memset(anno_room, 0, sizeof anno_room);
        ANNO_CTOR(anno_room, ta_block);
        anno_of = anno_room;
        memset(raw_room, j == 2 ? 1 : 0, sizeof raw_room);
        memset(work, 0, sizeof work);
        for (k = 0; k < 90; k++) {
            static const char *const RUN[] = {
                "\x82\xcd", "\x93\xfa", "\x81\x41", "\x8e\x52", "\x82\xa0"
            };
            const char *p = j == 0 ? RUN[k % 5] : RUN[k % 2];

            work[k * 2] = p[0];
            work[k * 2 + 1] = p[1];
        }
        memset(ic_block, 0xa5, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        IC(SetText)(ic_block, work);
        putSentence("long", j, 0, (int)IC(ReadSentence)(ic_block));
    }

    /* The recovery walk on its own, over a record laid out by hand, so that
       the roads the reader does not reach are reached. */
    for (j = 0; j < 8; j++) {
        memset(ic_block, 0, sizeof ic_block);
        IC(Ctor)(ic_block, ta_block);
        memset(work, ' ', 200);
        work[200] = 0;
        for (k = 0; k < 64; k++) {
            ic_block[IC_TEXT + k * 2] = (char)0x81;
            ic_block[IC_TEXT + k * 2 + 1] = (char)(0x43 + (k % 3));
            /* j nought has punctuation in it and so never reaches the two
               walks before it; j one and up have none, so the hiragana walk
               and then the topic particle are what decide. */
            *(int32_t *)(ic_block + IC_KIND + k * 4) =
                (j == 0)
                    ? ((k % 4 == 0) ? KIND_HIRAGANA
                                    : (k % 4 == 1) ? KIND_KANJI
                                                   : (k % 4 == 2) ? KIND_PUNCT
                                                                  : KIND_DIGIT)
                    : ((k % 2 == 0) ? KIND_HIRAGANA : KIND_KANJI);
            *(int32_t *)(ic_block + IC_MARK + k * 4) = (int32_t)(k * 2);
            *(int16_t *)(ic_block + IC_OFFSET + k * 2) = (int16_t)k;
        }
        if (j & 1)
            memcpy(ic_block + IC_TEXT + 8, "\x82\xcd", 2);
        /* Four, five and six put one punctuation high up and nothing else of
           that kind, so that each of the three things the third walk steps
           over is the one deciding rather than one of many. */
        if (j >= 4 && j <= 6) {
            *(int32_t *)(ic_block + IC_KIND + 50 * 4) = KIND_PUNCT;
            if (j == 4)
                *(int32_t *)(ic_block + IC_KIND + 49 * 4) = KIND_DIGIT;
            if (j == 5)
                memcpy(ic_block + IC_TEXT + 50 * 2, "\x81\x58", 2);
            if (j == 6)
                memcpy(ic_block + IC_TEXT + 50 * 2, "\x81\x5b", 2);
        }
        /* Two leaves only one place for the first walk to find, low down, so
           that the bracket is past it and wins. */
        if (j == 2) {
            for (k = 4; k < 64; k++)
                *(int32_t *)(ic_block + IC_KIND + k * 4) = KIND_KATAKANA;
            *(int16_t *)(ic_block + IC_BRACKET_AT) = 40;
        }
        memset(raw_room, (j == 3 || j == 7) ? 1 : 0, sizeof raw_room);
        if (j == 7)
            raw_room[9] = 2;
        IC(SetText)(ic_block, work);
        *(int32_t *)(ic_block + IC_POS) = 100;
        memset(anno_room, 0, sizeof anno_room);
        ANNO_CTOR(anno_room, ta_block);
        anno_of = anno_room;
        AN(Save)(anno_room, (char *)"`p100", 5, 4);
        AN(Save)(anno_room, (char *)"`p200", 5, 12);
        AN(Save)(anno_room, (char *)"`p300", 5, 40);
        IC(RecoverOverflow)(ic_block, 40);
        putSentence("recover", j, 0, 0);
    }

    printf("IC reader done\n");
}

/* The number reader: the four tables it looks characters up in, the two tests
   over a run of codes, and the two that write an entry. */
/* Every shape of voice annotation rz_GetParameter takes, and a few it does
   not, so that each of the six roads through it is walked. */
static const char *const RZ_ANNOS[] = {
    "`vs50", "`vb40", "`vf30", "`vv60", "`vx10",
    "`vs%+10", "`vs%-10", "`vb%+50", "`vb%-90", "`vf%+20", "`vv%+30",
    "`vv%-99", "`vx%+10",
    "`vswpm+20", "`vswpm-20", "`vbhz+50", "`vbhz-50",
    "`vsmed", "`vbmed", "`vfmed", "`vxmed",
    "`v1", "`v2", "`v3", "`v0", "`v01", "`v02", "`v03", "`v001",
    "`ts1", "`ts0", "`tx1", "`t", "`v", "`vs", "`x99", "ab"
};

static void sweepNumbers(void)
{
    static char rom_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    static char work[4096];
    long        i;
    long        j;
    long        k;

    memset(ta_block, 0, sizeof ta_block);
    memset(rom_room, 0, sizeof rom_room);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_OWNER, rom_room);
    RZ_SET_PARAM(rom_room, the_param);
    RZ_SET_USERDICT(rom_room, ud_room);

    /* Every two-byte character against each of the four tables, which settles
       all four of them and IsMember with them. */
    memset(ds_block, 0, sizeof ds_block);
    DS_SET_OWNER(ds_block, ta_block);
    for (i = 0; i < 0x10000; i++) {
        uint8_t c[2];

        c[0] = (uint8_t)(i >> 8);
        c[1] = (uint8_t)i;
        printf("DS num %04lx %d %d %d %d\n", i,
               (int)DS(IsZKNum)(ds_block, c), (int)DS(IsZSNum)(ds_block, c),
               (int)DS(IsZKeta)(ds_block, c), (int)DS(IsZSymb)(ds_block, c));
    }

    /* And IsMember over a table of its own, at every length from nothing to
       past the end of what it is given. */
    for (i = 0; i < 12; i++) {
        uint8_t c[2];

        c[0] = 0x82;
        c[1] = (uint8_t)(0x4f + i);
        for (j = 0; j <= 12; j++)
            printf("DS member %ld %ld %d\n", i, j,
                   (int)DS(IsMember)(ds_block, c,
                                     DM(GetNumberDataPtr)() + 0x14,
                                     (int16_t)j));
    }

    /* The thousands-mark test over every arrangement of five codes drawn from
       a digit, the two marks and something that is neither. */
    for (i = 0; i < 1024; i++) {
        char p[8];
        long v = i;

        for (j = 0; j < 5; j++) {
            static const char PICK[] = { 9, 0x18, 0x1b, 0x11 };

            p[j] = PICK[v & 3];
            v >>= 2;
        }
        for (j = 0; j <= 5; j++)
            printf("DS comma %ld %ld %d\n", i, j,
                   (int)DS(IsCommaPosition)(ds_block, p, (int32_t)j));
    }

    /* The closing-quote test over every character, at a kind that lets it
       through and one that does not. */
    for (i = 0; i < 0x10000; i++) {
        for (j = 0; j < 2; j++) {
            memset(ic_block, 0, sizeof ic_block);
            ic_block[IC_TEXT] = (char)(i >> 8);
            ic_block[IC_TEXT + 1] = (char)i;
            *(int32_t *)(ic_block + IC_KIND) = (int32_t)(j ? KIND_PUNCT
                                                            : KIND_KANJI);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            if (DS(IsEndOfQuote)(ds_block, 0))
                printf("DS quote %04lx %ld\n", i, j);
        }
    }
    printf("DS quote done\n");

    /* The place-order check, over every level it takes and over runs built to
       walk each road: a run of digits, one with a small place word in it, one
       with a large one, one with the marks in the right places and one with
       them wrong. */
    for (i = -1; i <= 6; i++) {
        for (j = 0; j < 24; j++) {
            static const uint8_t RUNS[24][8] = {
                { 0 }, { 1 }, { 1, 2 }, { 1, 0xa }, { 1, 0xb }, { 1, 0xc },
                { 1, 0xd }, { 1, 0xe }, { 1, 0xf }, { 1, 0xd, 2 },
                { 1, 0xd, 2, 0xa }, { 1, 2, 3 }, { 1, 2, 3, 4 },
                { 1, 0x18, 2, 3, 4 }, { 1, 0x1b, 2, 3, 4 },
                { 1, 2, 0x18, 3, 4 }, { 0xa }, { 0xd },
                { 1, 2, 3, 4, 5, 6, 7, 8 }, { 1, 0xf, 2, 0xd, 3, 0xa },
                { 0, 0xa }, { 0, 0 }, { 1, 0x1a }, { 9, 9, 9, 9, 9, 9, 9 }
            };
            static const int LEN[24] = {
                1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 4, 3, 4, 5, 5, 5, 1, 1, 8, 6,
                2, 2, 2, 7
            };
            uint8_t buf[24];
            int16_t n = (int16_t)LEN[j];
            int16_t chars = (int16_t)(LEN[j] + 1);
            int16_t keepN = 1;
            int16_t keepChars = 1;
            int16_t rc;

            memset(buf, 0x10, sizeof buf);
            memcpy(buf, RUNS[j], (size_t)LEN[j]);
            rc = DS(CheckKetaOrder)(ds_block, &n, &chars, &keepN, &keepChars,
                                    (int16_t)i, buf);
            printf("DS keta %ld %ld rc %d n %d chars %d keep %d %d ", i, j,
                   (int)rc, (int)n, (int)chars, (int)keepN, (int)keepChars);
            for (k = 0; k < 24; k++)
                printf("%02x", buf[k]);
            putchar('\n');
        }
    }

    /* Texts built out of the four tables themselves: every counter after a
       one-digit number and after a two-digit one, every place word after each
       digit, and a run long enough to fill the buffer. Written texts alone
       left nine of the sabotages standing, all of them in the machinery that
       reads a counter, because the hand-written ones reached only two of the
       nine counters there are. */
    {
        const uint8_t *num = DM(GetNumberDataPtr)();
        long           g;

        for (g = 0; g < 9 + 9 + 10 + 9 + 4 + 9 + 4 + 8; g++) {
            for (j = 0; j < 6; j++) {
                int at;
                int n;

                memset(work, 0, sizeof work);
                if (g < 9) {
                    memcpy(work, num + 0x00 + 1 * 2, 2);
                    memcpy(work + 2, num + 0x3c + g * 2, 2);
                } else if (g < 18) {
                    memcpy(work, num + 0x00 + 1 * 2, 2);
                    memcpy(work + 2, num + 0x28 + 0 * 2, 2);
                    memcpy(work + 4, num + 0x00 + 2 * 2, 2);
                    memcpy(work + 6, num + 0x3c + (g - 9) * 2, 2);
                } else if (g < 28) {
                    memcpy(work, num + 0x00 + (g - 18) * 2, 2);
                    memcpy(work + 2, num + 0x28 + 4 * 2, 2);
                } else if (g < 37) {
                    memcpy(work, num + 0x00 + 3 * 2, 2);
                    memcpy(work + 2, num + 0x28 + (g - 28) * 2, 2);
                    memcpy(work + 4, num + 0x00 + 5 * 2, 2);
                } else if (g < 41) {
                    long q;

                    for (q = 0; q < 20; q++)
                        memcpy(work + q * 2,
                               num + (g == 37 ? 0x00 : 0x14)
                               + ((q + (g == 39 ? 1 : 0)) % 10) * 2, 2);
                } else if (g < 50) {
                    /* A digit, one of the nine counters, a place word and a
                       digit: the only shape that reaches the arms which look
                       at what the last counter was. */
                    memcpy(work, num + 0x00 + 1 * 2, 2);
                    memcpy(work + 2, num + 0x3c + (g - 41) * 2, 2);
                    memcpy(work + 4, num + 0x28 + 4 * 2, 2);
                    memcpy(work + 6, num + 0x00 + 2 * 2, 2);
                } else {
                    /* A run long enough to fill the buffer with a large place
                       word inside it, which is what the cut-back wants, and a
                       place word followed by two digits, which is what the
                       step back over a digit wants. */
                    long q;

                    if (g == 50 || g == 51) {
                        for (q = 0; q < 20; q++)
                            memcpy(work + q * 2,
                                   num + (q % 5 == 4
                                          ? 0x28 + (5 + (g - 50) * 2) * 2
                                          : 0x00 + (q % 10) * 2), 2);
                    } else if (g < 54) {
                        memcpy(work, num + 0x00 + 3 * 2, 2);
                        memcpy(work + 2, num + 0x28 + (g == 52 ? 2 : 5) * 2,
                               2);
                        memcpy(work + 4, num + 0x00 + 9 * 2, 2);
                        memcpy(work + 6, num + 0x00 + 5 * 2, 2);
                        memcpy(work + 8, num + 0x00 + 7 * 2, 2);
                    } else {
                        /* Runs of exactly the length the buffer holds, with
                           and without a large place word in them and with a
                           counter at the last character, which is what the
                           cut-back and the bound on the character count
                           want. */
                        long len = (g & 1) ? 9 : 16;
                        long big = ((g - 54) / 2) % 4;

                        for (q = 0; q < len; q++)
                            memcpy(work + q * 2,
                                   num + 0x00 + (q % 9 + 1) * 2, 2);
                        /* A place word switches the reader into the mode
                           that stops two characters later, so one early in
                           the run keeps the buffer from filling. It has to be
                           at the very end for the run to reach the length the
                           cut-back wants. */
                        if (big == 1)
                            memcpy(work + 5 * 2, num + 0x28 + 4 * 2, 2);
                        if (big == 2)
                            memcpy(work + (len - 1) * 2,
                                   num + 0x28 + 4 * 2, 2);
                        if (big == 3)
                            memcpy(work + (len - 2) * 2,
                                   num + 0x28 + 4 * 2, 2);
                    }
                }
                memset(ic_block, 0xa5, sizeof ic_block);
                IC(Ctor)(ic_block, ta_block);
                IC(SetText)(ic_block, work);
                n = icSetText(work);
                TA_SET(ta_block, TA_INPUTCHAR, ic_block);
                *(uint16_t *)(rom_room + RZ_NUMBER_MODE) = (uint16_t)(j % 3);
                *(int32_t *)(rom_room + RZ_SPELL_ENGLISH) = (int32_t)(j / 3);
                ta_block[TA_LONGWORDS] = (char)(j & 1 ? 0x28 : 0);
                for (at = 0; at < n && at < 22; at++) {
                    int16_t rc;
                    long    slot = (g == 38 && at == 0) ? DS_ENTRY_N - 1 : 0;

                    memset(ds_block, 0, sizeof ds_block);
                    *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
                    DS_SET_OWNER(ds_block, ta_block);
                    memset(ta_block + TA_MARKS, 0, 128);
                    rc = DS(SetSuushiWord)(ds_block, (int16_t)slot,
                                           (int16_t)at);
                    printf("DS gen %ld %ld %d rc %d\n", g, j, at, (int)rc);
                    putRecord("gen", g * 1000 + j * 100 + at,
                              (const uint8_t *)(ds_block + DS_ENTRY
                                                + slot * DS_ENTRY_SIZE),
                              DS_ENTRY_SIZE);
                    printf("DS gen %ld %ld %d marks ", g, j, at);
                    for (k = 0; k < 32; k++)
                        printf("%02x", (unsigned char)ta_block[TA_MARKS + k]);
                    putchar('\n');
                }
            }
        }
    }

    /* And the two writers, over texts built out of the four tables at every
       position, in each of the number modes and with the long-reading store
       both empty and nearly full. */
    for (i = 0; i < (long)(sizeof NUM_TEXTS / sizeof *NUM_TEXTS); i++) {
        for (j = 0; j < 6; j++) {
            int n;
            int at;

            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            memset(work, 0, sizeof work);
            strcpy(work, NUM_TEXTS[i]);
            IC(SetText)(ic_block, work);
            n = icSetText(NUM_TEXTS[i]);
            TA_SET(ta_block, TA_INPUTCHAR, ic_block);
            *(uint16_t *)(rom_room + RZ_NUMBER_MODE) = (uint16_t)(j % 3);
            *(int32_t *)(rom_room + RZ_SPELL_ENGLISH) = (int32_t)(j / 3);
            ta_block[TA_LONGWORDS] = (char)(j & 1 ? 0x28 : 0);
            for (at = 0; at < n && at < 12; at++) {
                int16_t rc;

                memset(ds_block, 0, sizeof ds_block);
                *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
                DS_SET_OWNER(ds_block, ta_block);
                memset(ta_block + TA_MARKS, 0, 64);
                rc = DS(SetSuushiWord)(ds_block, 0, (int16_t)at);
                printf("DS suushi %ld %ld %d rc %d\n", i, j, at, (int)rc);
                putRecord("suushi", i * 100 + j * 10 + at,
                          (const uint8_t *)(ds_block + DS_ENTRY),
                          DS_ENTRY_SIZE);
                printf("DS suushi %ld %ld %d marks ", i, j, at);
                for (k = 0; k < 24; k++)
                    printf("%02x", (unsigned char)ta_block[TA_MARKS + k]);
                putchar('\n');

                memset(ds_block, 0, sizeof ds_block);
                *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
                DS_SET_OWNER(ds_block, ta_block);
                printf("DS dummy %ld %ld %d rc %d\n", i, j, at,
                       (int)DS(SetDummyWord)(ds_block, 1, (int16_t)at));
                putRecord("dummy", i * 100 + j * 10 + at,
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
            }
        }
    }

    /* The user dictionary the search reaches for through the romanizer. */
    printf("DS userdict %d\n",
           DS(getPtrOfUserDict)(ds_block) == (void *)ud_room);

    printf("DS numbers done\n");
}

/* The parameter annotations, and then the whole search. */
/* Texts for the function-word walk: runs of hiragana that are function words,
   a kanji then a run, a character below the first hiragana, one above the last
   the table indexes, and the small kana that stop a word beginning a phrase. */
static const char *const FZK_TEXTS[] = {
    "\x82\xcd\x82\xb1\x82\xea\x82\xf0\x82\xc5\x82\xb7",
    "\x93\xfa\x96\x7b\x82\xcc\x82\xbd\x82\xdf\x82\xc9",
    "\x82\xc5\x82\xb7\x81\x42",
    "\x82\xc1\x82\xe1\x82\xe3\x82\xe5\x82\xf1",
    "\x81\x40\x82\xcd\x82\xc5",
    "\x83\x41\x83\x43\x82\xcd",
    "\x82\xa0\x82\xa2\x82\xa4\x82\xa6\x82\xa8",
    "\x82\xf0",
    /* Longer runs, so that the walk goes several nodes deep into one chain
       and takes the step that passes a node by. */
    "\x82\xc5\x82\xcd\x82\xc8\x82\xa2\x82\xc5\x82\xb7\x82\xa9",
    "\x82\xc9\x82\xc2\x82\xa2\x82\xc4\x82\xcd",
    "\x82\xc6\x82\xa2\x82\xa4\x82\xb1\x82\xc6\x82\xc5\x82\xb7",
    /* A function word before each of the five that cannot begin a phrase. */
    "\x82\xcd\x82\xc1\x82\xbd",
    "\x82\xcd\x82\xe1\x82\xa0",
    "\x82\xcd\x82\xe3\x82\xa0",
    "\x82\xcd\x82\xe5\x82\xa0",
    "\x82\xcd\x82\xf1\x82\xa0",
    /* And a character above every one the table indexes by name. */
    "\x82\xfa\x82\xcd\x82\xc5"
};

static void sweepDo(void)
{
    static char rom_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    static char anno_room[ANNO_ROOM];
    static char raw_room[4096];
    static char work[4096];
    long        i;
    long        j;
    long        k;

    /* Every shape of voice annotation there is, at every setting, from four
       starting states, so that both clamps and the reset are walked. */
    for (i = 0; i < (long)(sizeof RZ_ANNOS / sizeof *RZ_ANNOS); i++) {
        for (j = 0; j < 4; j++) {
            memset(rom_room, 0, sizeof rom_room);
            RZ_SET_PARAM(rom_room, the_param);
            *(int32_t *)(rom_room + RZ_VOICE)    = (int32_t)(j + 1);
            *(int32_t *)(rom_room + RZ_BASELINE) = (int32_t)(j * 40);
            *(int32_t *)(rom_room + RZ_FLUENCY)  = (int32_t)(j * 30 + 1);
            *(int32_t *)(rom_room + RZ_SPEED)    = (int32_t)(j * 80);
            *(int32_t *)(rom_room + RZ_VOLUME)   = (int32_t)(j * 25);
            *(int32_t *)(rom_room + RZ_SPELL_ENGLISH) = 0;
            {
                /* The call first, then the fields: in one printf the order is
                   the compiler's and the fields were being read before the
                   call that sets them. */
                int rc = (int)RZ(GetParameter)(rom_room,
                                               (char *)(uintptr_t)
                                               (const void *)RZ_ANNOS[i]);

                printf("RZ %ld %ld rc %d voice %ld base %ld fluc %ld"
                       " speed %ld vol %ld eng %ld\n", i, j, rc,
                   (long)*(int32_t *)(rom_room + RZ_VOICE),
                   (long)*(int32_t *)(rom_room + RZ_BASELINE),
                   (long)*(int32_t *)(rom_room + RZ_FLUENCY),
                   (long)*(int32_t *)(rom_room + RZ_SPEED),
                   (long)*(int32_t *)(rom_room + RZ_VOLUME),
                   (long)*(int32_t *)(rom_room + RZ_SPELL_ENGLISH));
            }
        }
    }

    /* And the search, over every text the reader is driven over, with the
       caller's own marks absent, saying a reading follows, and saying leave
       this alone. The parameter block is told annotations are in the text so
       that the road from an annotation to a setting is walked, and the user
       dictionary is taught a few words first. */
    for (j = 0; j < 3; j++) {
        for (i = 0; i < (long)(sizeof IC_SENTENCES / sizeof *IC_SENTENCES);
             i++) {
            int16_t rc;
            long    n;

            memset(ta_block, 0, sizeof ta_block);
            memset(rom_room, 0, sizeof rom_room);
            memset(anno_room, 0, sizeof anno_room);
            memset(raw_room, (int)j, sizeof raw_room);
            memset(work, 0, sizeof work);
            strcpy(work, IC_SENTENCES[i]);
            ANNO_CTOR(anno_room, ta_block);
            TA_SET(ta_block, TA_INPUTCHAR, ic_block);
            TA_SET(ta_block, TA_ANNOTATION, anno_room);
            TA_SET(ta_block, TA_OWNER, rom_room);
            TA_SET(ta_block, TA_RAW, raw_room);
            TA_SET(ta_block, TA_DICTSEARCH, ds_block);
            RZ_SET_PARAM(rom_room, the_param);
            RZ_SET_USERDICT(rom_room, ud_room);
            UD(Ctor)(ud_room, ta_block);
            PARAM_ANNO(the_param) = 1;

            /* A dictionary with words in it, so that the arm which asks it is
               walked rather than stepped over. */
            {
                static char list_room2[IBM_LIST_ROOM];

                ibm_slCtor(list_room2);
                UD_DICT(ud_room) = list_room2;
                for (k = 0; UD_WORDS[k][0] != NULL && k < 6; k++)
                    (void)UD(UpdateDictExt)(ud_room, list_room2, 0,
                                            (uint8_t *)UD_WORDS[k][0],
                                            (int32_t)strlen(UD_WORDS[k][0]),
                                            (char *)UD_WORDS[k][1],
                                            (int32_t)strlen(UD_WORDS[k][1]),
                                            1);
            }

            memset(ic_block, 0xa5, sizeof ic_block);
            IC(Ctor)(ic_block, ta_block);
            IC(SetText)(ic_block, work);
            (void)IC(ReadSentence)(ic_block);
            if (j == 1) {
                void *node;

                for (k = 0; k < 6; k++)
                    (void)IC(AddSnlkTable)(ic_block, (int16_t)k,
                                           UD_WORDS[k][0], UD_WORDS[k][1], 0);
                /* Every other node has its accent left unset, which is the
                   only state in which the search still asks the dictionaries
                   and the two filters afterwards have anything to filter. */
                node = IC_SNLK_HEAD(ic_block);
                for (k = 0; node != NULL; k++) {
                    *((unsigned char *)node + SN_TRANS) = 0xff;
                    node = SN_NEXT_OF(node);
                }
            }

            /* TextAnalysis is what seeds the parse marks, and it is not
               written, so the harness does it: every character may begin a
               word until the walk says otherwise. */
            for (k = 0; k < 128; k++)
                ta_block[TA_MARKS + k] = (char)(k == 0 ? 1
                                                : (k % 7 == 3 ? 0
                                                   : (k % 3 == 0 ? 2 : 1)));
            memset(ds_block, 0, sizeof ds_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            /* A sentence that read as nothing is left out: Do returns a
               local it never assigned when the loop runs no turns, and stack
               left over from one process cannot be held against another's. */
            if (*(int16_t *)(ic_block + IC_COUNT) == 0) {
                printf("DO %ld %ld empty\n", j, i);
                continue;
            }
            rc = DS(Do)(ds_block);
            n = (long)*(int16_t *)(ds_block + DS_COUNT);
            printf("DO %ld %ld rc %d count %ld\n", j, i, (int)rc, n);
            for (k = 0; k < n && k < 60; k++)
                putRecord("do", j * 10000 + i * 100 + k,
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + k * DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
            for (k = 705; k < 710; k++)
                putRecord("dotail", j * 10000 + i * 100 + k,
                          (const uint8_t *)(ds_block + DS_ENTRY
                                            + k * DS_ENTRY_SIZE),
                          DS_ENTRY_SIZE);
            printf("DO %ld %ld marks ", j, i);
            for (k = 0; k < 40; k++)
                printf("%02x", (unsigned char)ta_block[TA_MARKS + k]);
            putchar('\n');
        }
    }

    printf("DO done\n");
}

/* The function words read backwards, which is the last of DictSearch. */
static void sweepFzk(void)
{
    static char rom_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    static char work[4096];
    long        i;
    long        j;
    long        k;

    memset(ta_block, 0, sizeof ta_block);
    memset(rom_room, 0, sizeof rom_room);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_OWNER, rom_room);
    RZ_SET_PARAM(rom_room, the_param);
    RZ_SET_USERDICT(rom_room, ud_room);

    /* One real node of the dictionary against every vector that could select
       it. The node has to be one the walk would actually reach and the count
       its own: handed a made-up head the walk steps off the end of the table,
       and what lies past it is not the same on the two sides. */
    for (i = 0; i < (long)(sizeof FZK_TEXTS / sizeof *FZK_TEXTS); i++) {
        const uint8_t *dict = DM(GetFuncDict)();
        int            n = icSetText(FZK_TEXTS[i]);
        int            at;

        for (at = 0; at < n && at < 6; at++) {
            uint16_t key = JU(MakeUshort)(ic_block + IC_TEXT + at * 2);
            uint16_t base = JU(MakeUshort)((char *)"\x82\xa0");
            uint16_t off;
            const uint8_t *node;

            if (key < base)
                continue;
            off = key < 0x82ff
                  ? JU(MakeUshort)((char *)(dict + (key - base + 1) * 2))
                  : JU(MakeUshort)((char *)(dict + 0xa6));
            if (off == 0xffff || off == 0)
                continue;
            node = dict + 0xa8 + off;

            for (j = 0; j < 18; j++) {
                uint8_t vec[14];
                int16_t rc;

                memset(ds_block, 0, sizeof ds_block);
                *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
                DS_SET_OWNER(ds_block, ta_block);
                for (k = 0; k < 14; k++)
                    vec[k] = (uint8_t)(j == 0 ? 0xff
                                       : (j == 1 ? 0
                                          : (k == (long)((j - 2) / 8)
                                             ? 0x80 >> ((j - 2) % 8) : 0)));
                rc = DS(HitFuncWordReverse)(ds_block, node,
                                            (int16_t)(j % 5), (uint16_t)at,
                                            (int16_t)(node[3] >> 4),
                                            (uint8_t)(1 + j % 3),
                                            (uint8_t)(j % 2), vec, dict);
                printf("FZ hit %ld %d %ld rc %d ", i, at, j, (int)rc);
                for (k = 0; k < 8 * DS_FZK_SIZE; k++)
                    printf("%02x", (unsigned char)ds_block[DS_FZK + k]);
                putchar('\n');
            }
        }
    }

    /* The chain walk from every character of every text, with the vector
       all-ones so that nothing is filtered out, and again with one bit. */
    for (i = 0; i < (long)(sizeof FZK_TEXTS / sizeof *FZK_TEXTS); i++) {
        int n = icSetText(FZK_TEXTS[i]);
        int at;

        for (at = 0; at < n && at < 10; at++) {
            for (j = 0; j < 3; j++) {
                uint8_t vec[14];
                int16_t rc;

                memset(ds_block, 0, sizeof ds_block);
                *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
                DS_SET_OWNER(ds_block, ta_block);
                for (k = 0; k < 14; k++)
                    vec[k] = (uint8_t)(j == 0 ? 0xff : (j == 1 ? 0 : 0x0f));
                rc = DS(FzkSearchUnknown)(ds_block, vec, (uint16_t)at,
                                          (int16_t)(j * 3),
                                          DM(GetFuncDict)(), 0);
                printf("FZ unk %ld %d %ld rc %d ", i, at, j, (int)rc);
                for (k = 0; k < 8 * DS_FZK_SIZE; k++)
                    printf("%02x", (unsigned char)ds_block[DS_FZK + k]);
                putchar('\n');
            }
        }
    }

    /* And the two round-after-round walks, over every text. */
    for (i = 0; i < (long)(sizeof FZK_TEXTS / sizeof *FZK_TEXTS); i++) {
        int n = icSetText(FZK_TEXTS[i]);
        int at;

        for (at = 0; at < n && at < 6; at++) {
            uint8_t vec[14];
            int16_t rc;

            memset(ds_block, 0, sizeof ds_block);
            *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
            DS_SET_OWNER(ds_block, ta_block);
            for (k = 0; k < 14; k++)
                vec[k] = 0xff;
            rc = DS(FzkParsing)(ds_block, vec, (int16_t)at);
            printf("FZ fwd %ld %d rc %d ", i, at, (int)rc);
            for (k = 0; k < 12 * DS_FZK_SIZE; k++)
                printf("%02x", (unsigned char)ds_block[DS_FZK + k]);
            putchar('\n');
        }

        memset(ds_block, 0, sizeof ds_block);
        *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
        DS_SET_OWNER(ds_block, ta_block);
        printf("FZ rev %ld rc %d ", i, (int)DS(FzkParsingReverse)(ds_block));
        for (k = 0; k < 12 * DS_FZK_SIZE; k++)
            printf("%02x", (unsigned char)ds_block[DS_FZK + k]);
        putchar('\n');
        (void)n;
    }

    printf("FZ done\n");
}
/* ---- the surface -------------------------------------------------------- */

/* What the harness's converter answers when InputManager asks where the next
   mark belongs, and how often each of the two slots was reached. Romanizer is
   not written, so the vtable a converter is reached through is the harness's
   own; counting the calls is how a sabotage inside insertIndex or resume is
   made visible. */
static int32_t cv_offset;
static long    cv_offsetCalls;
static long    cv_resetCalls;

static HARNESS_THIS int32_t harnessGetOffset(HarnessSelf *c)
{
    (void)c;
    cv_offsetCalls++;
    return cv_offset;
}

static HARNESS_THIS void harnessResetBuffer(HarnessSelf *c)
{
    (void)c;
    cv_resetCalls++;
}

static const HarnessVtbl harness_vtbl = {
    NULL, NULL, harnessGetOffset, harnessResetBuffer, NULL, NULL, NULL
};

/* The codesets trans2defaultCodeset knows, and three it does not. */
static const int32_t CC_CODESETS[] = {
    0, 0x80000, 0x80800, 0x80100, 0x80200, 0x80300, 0x80400,
    0x80500, 0x80900, 0x1234
};

/* One text, its bytes, and how many of them there are. Held as a length
   rather than as a string, because a codeset conversion is given a count and
   several of the texts below have a nought in the middle. */
typedef struct CcText {
    const char *bytes;
    int32_t     len;
} CcText;

/* Every byte in every context that decides a road through the two whole-text
   conversions: on its own, in front of and behind a byte the EUC test
   accepts, behind the single shift with and without a voicing mark after it,
   behind an escape, and inside a two-byte run. */
static int ccTexts(int b, CcText *out, char store[8][12])
{
    static const char shapes[8][12] = {
        { 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0x00, (char)0xa1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { (char)0xa1, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { (char)0x8e, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { (char)0x8e, 0x00, (char)0xde, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0x1b, 0x00, 'B', '0', '1', 0, 0, 0, 0, 0, 0, 0 },
        { 0x1b, '$', 'B', 0x00, '0', 0x1b, '(', 'B', 0, 0, 0, 0 },
        { 0x00, 0x1b, 'K', 0x00, '0', 0, 0, 0, 0, 0, 0, 0 },
    };
    static const int lens[8] = { 1, 2, 2, 2, 3, 5, 8, 5 };
    static const int where[8][2] = {
        { 0, -1 }, { 0, -1 }, { 1, -1 }, { 1, -1 },
        { 1, -1 }, { 1, -1 }, { 3, -1 }, { 0, 3 },
    };
    int i;

    for (i = 0; i < 8; i++) {
        memcpy(store[i], shapes[i], 12);
        store[i][where[i][0]] = (char)b;
        if (where[i][1] >= 0)
            store[i][where[i][1]] = (char)b;
        out[i].bytes = store[i];
        out[i].len   = lens[i];
    }
    return 8;
}

/* The five codeset conversions, over every input each of them has.
 *
 * jis2sjis and han2zen are swept over every byte there is, because both are
 * pure arithmetic on a pair and there is no reason to sample. The two
 * whole-text ones are swept over the eight shapes above with every byte in
 * each, which walks every arm either of them has. */
static void sweepCodeconv(void)
{
    long i;
    long j;
    long k;

    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++) {
            uint8_t lead  = (uint8_t)i;
            uint8_t trail = (uint8_t)j;

            JU(jis2sjis)(&lead, &trail);
            printf("CC j2s %02x %02x %02x %02x\n", (unsigned)i, (unsigned)j,
                   (unsigned)lead, (unsigned)trail);
        }

    for (i = 0; i < 256; i++)
        for (j = 0; j < 2; j++) {
            char    text[4];
            long    at   = 0;
            int32_t mode = (int32_t)j;
            int     rc;

            text[0] = (char)i;
            text[1] = 'B';
            text[2] = 0;
            text[3] = 0;
            rc = (int)JU(SkipESCSeq)(text, &at, &mode);
            printf("CC esc %02x %ld rc %d at %ld mode %ld\n", (unsigned)i, j,
                   rc, at, (long)mode);
        }

    /* Every half-width byte against every byte that could follow it, in the
       Shift-JIS walk; then the EUC walk, whose own test is the one IBM wrote
       against a sign-extended byte and which therefore never joins a mark. */
    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++) {
            char    text[4];
            long    at    = 0;
            uint8_t lead  = (uint8_t)i;
            uint8_t trail = 0x5a;

            text[0] = (char)j;
            text[1] = (char)0xde;
            text[2] = 0;
            text[3] = 0;
            JU(han2zen)(text, &at, &lead, &trail, 5);
            printf("CC h2z5 %02x %02x at %ld %02x %02x\n", (unsigned)i,
                   (unsigned)j, at, (unsigned)lead, (unsigned)trail);

            at    = 0;
            lead  = (uint8_t)i;
            trail = 0x5a;
            JU(han2zen)(text, &at, &lead, &trail, 4);
            printf("CC h2z4 %02x %02x at %ld %02x %02x\n", (unsigned)i,
                   (unsigned)j, at, (unsigned)lead, (unsigned)trail);
        }

    for (i = 0; i < 256; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 2; k++) {
                static const char after[3] = { (char)0xde, (char)0xdf, 'A' };
                char    text[4];
                long    at    = 0;
                uint8_t lead  = (uint8_t)i;
                uint8_t trail = 0x5a;

                text[0] = k ? (char)0x8e : 'Z';
                text[1] = after[j];
                text[2] = 0;
                text[3] = 0;
                JU(han2zen)(text, &at, &lead, &trail, 4);
                printf("CC h2ze %02x %ld %ld at %ld %02x %02x\n", (unsigned)i,
                       j, k, at, (unsigned)lead, (unsigned)trail);
            }

    for (i = 0; i < 256; i++) {
        CcText texts[8];
        char   store[8][12];
        int    n = ccTexts((int)i, texts, store);

        for (j = 0; j < n; j++) {
            char out[64];
            long got;

            memset(out, 0x5a, sizeof out);
            got = JU(euc2shift)(texts[j].bytes, texts[j].len, out, 0);
            printf("CC euc0 %02x %ld n %ld ", (unsigned)i, j, got);
            for (k = 0; k < got + 1 && k < 40; k++)
                printf("%02x", (unsigned)(uint8_t)out[k]);
            putchar('\n');

            memset(out, 0x5a, sizeof out);
            got = JU(euc2shift)(texts[j].bytes, texts[j].len, out, 1);
            printf("CC euc1 %02x %ld n %ld ", (unsigned)i, j, got);
            for (k = 0; k < got + 1 && k < 40; k++)
                printf("%02x", (unsigned)(uint8_t)out[k]);
            putchar('\n');

            memset(out, 0x5a, sizeof out);
            got = JU(seven2shift)(texts[j].bytes, texts[j].len, out);
            printf("CC jis %02x %ld n %ld ", (unsigned)i, j, got);
            for (k = 0; k < got + 1 && k < 40; k++)
                printf("%02x", (unsigned)(uint8_t)out[k]);
            putchar('\n');
        }
    }

    printf("CC done\n");
}

/* What getText handed back, printed as bytes rather than as a pointer: the
   two sides cannot agree about an address and both can agree about what is
   at it. */
static void putGot(const char *what, long a, long b, long c, long d, int rc,
                   const char *text, uint32_t len)
{
    uint32_t i;

    printf("%s %ld %ld %ld %ld rc %d len %lu ", what, a, b, c, d, rc,
           (unsigned long)len);
    if (text == NULL) {
        printf("none");
    } else {
        /* One byte past the length as well, which is where the join writes
           its nul and is the only place that nul can be seen. Every text
           handed in here is a literal, so the byte past a caller's own text
           is its own terminator and both sides read the same thing. */
        for (i = 0; i < len + 1 && i < 33; i++)
            printf("%02x", (unsigned)(uint8_t)text[i]);
    }
    putchar('\n');
}

/* The manager on its own: text waiting, text joined to it, and the queue.
 *
 * The whole matrix of what may be waiting against what has just arrived,
 * across a codeset that agrees with the block and one that does not, which is
 * every road through getText there is. The queue is then filled, walked and
 * emptied twice over, so that the destructor has something to free the second
 * time and nothing the third. */
static void sweepInputManager(void)
{
    static char im_room[IM_ROOM];
    static char cv_stub[RZ_ROOM];
    static const char *const TEXTS[3] = { "", "abc", "\x82\xa0\x82\xa2" };
    static const int32_t LENS[3] = { 0, 3, 4 };
    long i;
    long j;
    long k;
    long m;

    /* A converter for the manager to ask where a mark belongs. Nothing but
       the vtable in it is read, and the parameter block has to point at it
       because that is how InputManager finds its way back up. It is the
       block's own field rather than initBase's doing, so that this sweep
       leans on nothing above the class it is testing. */
    memset(cv_stub, 0, sizeof cv_stub);
    CV_SET_VT(cv_stub);
    PARAM_OWNER(the_param) = cv_stub;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 2; k++)
                for (m = 0; m < 2; m++) {
                    const char *got  = (const char *)0x1;
                    uint32_t    n    = 0xffffffffu;
                    int         rc;

                    PARAM_CLEAR(the_param);
                    PARAM_SET(the_param, 2, 0x80000);
                    memset(im_room, 0, sizeof im_room);
                    IM(Ctor)(im_room, the_param);

                    if (k)
                        (void)IM(InsertIndex)(im_room);

                    (void)IM(AddText)(im_room, TEXTS[i],
                                      (uint32_t)LENS[i], 0x80000);
                    PARAM_SET(the_param, 2, m ? 0x80100 : 0x80000);

                    rc = (int)IM(GetText)(im_room, &got, &n, TEXTS[j],
                                          (uint32_t)LENS[j]);
                    putGot("IM get", i, j, k, m, rc, got, n);

                    /* And again with nothing, which is what says whether the
                       first call left anything behind. */
                    got = (const char *)0x1;
                    n   = 0xffffffffu;
                    rc  = (int)IM(GetText)(im_room, &got, &n, NULL, 0);
                    putGot("IM again", i, j, k, m, rc, got, n);

                    printf("IM err %ld %ld %ld %ld %lu codeset %ld\n", i, j,
                           k, m, (unsigned long)PARAM_ERRORS(the_param),
                           (long)PARAM_CODESET(the_param));

                    /* And a second join on the same manager, so that the
                       buffer the first one made is there to be kept or
                       thrown away -- which is the only way the test on how
                       long the string in it is can be seen at all. Twice
                       more, once shorter than the first join and once
                       longer. */
                    PARAM_SET(the_param, 2, 0x80000);
                    {
                        long r;

                        for (r = 0; r < 3; r++) {
                            static const char *const WAIT[3] = {
                                "wxyz", "0123456789", "wxy"
                            };
                            static const uint32_t WAITN[3] = { 4, 10, 3 };

                            (void)IM(AddText)(im_room, WAIT[r], WAITN[r],
                                              0x80000);
                            got = (const char *)0x1;
                            n   = 0xffffffffu;
                            rc  = (int)IM(GetText)(im_room, &got, &n, "0", 1u);
                            putGot("IM rejoin", i, j, r, m, rc, got, n);
                        }
                    }

                    IM(Dtor)(im_room);
                }

    /* The queue: marks and parameters at rising offsets, read out and taken
       off one at a time, then thrown away wholesale. */
    for (i = 0; i < 2; i++) {
        static const char *const PARAMS[4] = {
            "`vs50", "`vb-10", "", "`vv%+5"
        };

        PARAM_CLEAR(the_param);
        memset(im_room, 0, sizeof im_room);
        IM(Ctor)(im_room, the_param);
        cv_offsetCalls = 0;

        for (j = 0; j < 4; j++) {
            int32_t n = (int32_t)strlen(PARAMS[j]);

            cv_offset = (int32_t)(j * 7);
            printf("IM ix %ld %ld rc %d\n", i, j,
                   (int)IM(InsertIndex)(im_room));
            cv_offset = (int32_t)(j * 7 + 3);
            /* Every other one is given a length shorter than the string it
               is given, which is the only way the nul the constructor writes
               is visible at all. */
            if (j & 1)
                n = n ? n - 1 : 0;
            printf("IM par %ld %ld rc %d\n", i, j,
                   (int)IM(AddParam)(im_room, PARAMS[j], n));
        }
        printf("IM calls %ld %ld\n", i, cv_offsetCalls);

        for (j = 0; j < 10; j++) {
            const char *data = (const char *)0x1;
            int         len;
            int         more = (int)IM(HasMore)(im_room);
            int         at   = (int)IM(GetNextOffset)(im_room);
            void       *e    = IM(GetNextElement)(im_room);

            len = (int)IM(GetNextData)(im_room, &data);
            printf("IM next %ld %ld more %d at %d len %d elem %ld %ld ", i, j,
                   more, at, len,
                   e ? (long)QE_AT(e) : -1, e ? (long)QE_KIND(e) : -1);
            if (data == NULL)
                printf("none");
            else
                /* One past the length as well: that is where the element's
                   constructor puts its nul, and the room it allocated is
                   long enough to hold it. */
                for (k = 0; k < len + 1 && k < 33; k++)
                    printf("%02x", (unsigned)(uint8_t)data[k]);
            putchar('\n');

            if (!more)
                break;
            if (i)
                IM(RemoveElement)(im_room);
        }

        /* Text left waiting, and then everything forgotten, which is what
           says whether remove drops the text as well as the queue. */
        (void)IM(AddText)(im_room, "abc", 3, 0x80000);
        /* And a second helping of nothing, which is refused and must leave
           the first alone. */
        (void)IM(AddText)(im_room, "zz", 0, 0x80100);
        if (i == 0)
            IM(Remove)(im_room);
        printf("IM left %ld more %d\n", i, (int)IM(HasMore)(im_room));
        {
            const char *got = (const char *)0x1;
            uint32_t    n   = 0xffffffffu;
            int         rc  = (int)IM(GetText)(im_room, &got, &n, NULL, 0);

            putGot("IM after remove", i, 0, 0, 0, rc, got, n);
        }
        IM(Dtor)(im_room);
    }

    printf("IM done\n");
}

/* The converter itself: everything the engine asks a Japanese instance, over
 * every codeset and every text the two conversions above are swept with.
 *
 * The block is a Romanizer's, because ConverterInterface is its base and the
 * two share one record, and the vtable in it is the harness's. The one thing
 * not reached is the spin in resume: it waits on a flag no second thread here
 * ever lowers, so the sweep sets the flag clear and walks what follows it.
 */
static void sweepConverter(void)
{
    static char cv_room[RZ_ROOM];
    static char ud_room[UD_ROOM];
    static char list_room[IBM_LIST_ROOM];
    const char *path = "romprims-convt.tmp";
    long i;
    long j;
    long k;

    /* Made, then unmade, and what the block says at each point. */
    memset(cv_room, 0x5a, sizeof cv_room);
    CV_SET_VT(cv_room);
    PARAM_CLEAR(the_param);
    CI(InitBase)(cv_room, the_param);
    printf("CV init stop %ld busy %ld input %d trans %d uni %d owner %d"
           " err %lu\n",
           (long)*(int32_t *)(cv_room + RZ_STOPPED),
           (long)*(int32_t *)(cv_room + RZ_BUSY),
           CV_INPUT(cv_room) != NULL, CV_TRANSBUF(cv_room) != NULL,
           CV_UNICODE(cv_room) != NULL,
           PARAM_OWNER(the_param) == (void *)cv_room,
           (unsigned long)PARAM_ERRORS(the_param));

    /* Every codeset over every shape of text, which is what decides whether
       anything is recoded at all and into what. */
    for (i = 0; i < (long)(sizeof CC_CODESETS / sizeof *CC_CODESETS); i++)
        for (j = 0; j < 256; j += 17) {
            CcText texts[8];
            char   store[8][12];
            int    n = ccTexts((int)j, texts, store);
            long   t;

            for (t = 0; t < n; t++) {
                const char *got = (const char *)0x1;
                uint32_t    rc;

                PARAM_CLEAR(the_param);
                rc = CI(Trans)(cv_room, (void *)(uintptr_t)
                               (const void *)texts[t].bytes,
                               texts[t].len, CC_CODESETS[i], &got);
                printf("CV tr %ld %ld %ld n %lu same %d ", i, j, t,
                       (unsigned long)rc, got == texts[t].bytes);
                if (got == (const char *)0x1)
                    printf("untouched");
                else if (got == NULL)
                    printf("none");
                else
                    for (k = 0; k < (long)rc + 1 && k < 40; k++)
                        printf("%02x", (unsigned)(uint8_t)got[k]);
                printf(" err %lu\n", (unsigned long)PARAM_ERRORS(the_param));
            }
        }

    /* Text handed over, which recodes and then leaves it with the manager;
       what waited is read back through the manager itself. */
    for (i = 0; i < (long)(sizeof CC_CODESETS / sizeof *CC_CODESETS); i++)
        for (j = 0; j < 3; j++) {
            static const char *const TEXTS[3] = { "", "abc", "\xa4\xa2\xa4\xa4" };
            static const int32_t LENS[3] = { 0, 3, 4 };
            const char *got = (const char *)0x1;
            uint32_t    n   = 0xffffffffu;
            int         rc;

            PARAM_CLEAR(the_param);
            PARAM_SET(the_param, 2, CC_CODESETS[i]);
            IM(Remove)(CV_INPUT(cv_room));

            PARAM_ANNO(the_param) = -1;
            rc = (int)CI(AddText)(cv_room, TEXTS[j], LENS[j], (int32_t)j);
            /* The input type as well, which addText is the only caller of
               setInputType and therefore the only thing that writes. */
            printf("CV add %ld %ld rc %d codeset %ld type %ld\n", i, j, rc,
                   (long)PARAM_CODESET(the_param),
                   (long)PARAM_ANNO(the_param));

            rc = (int)IM(GetText)(CV_INPUT(cv_room), &got, &n, NULL, 0);
            putGot("CV waited", i, j, 0, 0, rc, got, n);
        }
    PARAM_SET(the_param, 2, 0x80000);

    /* Marks and parameters written out at a rising cut-off, which is the one
       method that walks the queue rather than adding to it. */
    for (i = 0; i < 6; i++) {
        char out[512];

        IM(Remove)(CV_INPUT(cv_room));
        for (j = 0; j < 3; j++) {
            cv_offset = (int32_t)(j * 4);
            (void)CI(InsertIndex)(cv_room);
            cv_offset = (int32_t)(j * 4 + 2);
            (void)CI(AddParam)(cv_room, "`vs50", 5);
        }

        {
            void *e = IM(GetNextElement)(CV_INPUT(cv_room));

            printf("CV head %ld at %ld kind %ld\n", i,
                   e ? (long)QE_AT(e) : -1, e ? (long)QE_KIND(e) : -1);
        }

        memset(out, 0, sizeof out);
        strcpy(out, "head");
        {
            int rc = (int)CI(OutputIndexOrParam)(cv_room, out,
                                                 (int32_t)(i * 3));

            printf("CV out %ld rc %d [%s] more %d\n", i, rc, out,
                   (int)IM(HasMore)(CV_INPUT(cv_room)));
        }
    }
    IM(Remove)(CV_INPUT(cv_room));

    /* And once more with nothing on the queue at all, which is the only way
       the answer for an empty queue can be told from the answer for one that
       still has something further along. */
    {
        char out[64];
        int  rc;

        memset(out, 0, sizeof out);
        strcpy(out, "head");
        rc = (int)CI(OutputIndexOrParam)(cv_room, out, 0);
        printf("CV out empty rc %d [%s]\n", rc, out);
    }

    /* Stopped and started again. The flag resume waits on is clear, so what
       is swept is the reset and the clearing after it. */
    /* The call first and the fields after it, every time. In one printf the
       order is the compiler's, and it read the counter before the call that
       raises it. */
    cv_resetCalls = 0;
    {
        int rc = (int)CI(Stop)(cv_room);

        printf("CV stop rc %d flag %ld\n", rc,
               (long)*(int32_t *)(cv_room + RZ_STOPPED));
        rc = (int)CI(Resume)(cv_room);
        printf("CV resume rc %d flag %ld resets %ld\n", rc,
               (long)*(int32_t *)(cv_room + RZ_STOPPED), cv_resetCalls);
    }

    /* Unicode, which is a wrapper over the converter the sweep above already
       holds to IBM's answer; what is swept here is the making of it on first
       use and the answer when there is none. */
    for (i = 0; i < 4; i++) {
        static const char *const IN[4] = { "", "A", "\x82\xa0", "\x81\x40" };
        static const uint16_t WIDE[4][3] = {
            { 0 }, { 'A', 0 }, { 0x3042, 0 }, { 0x3042, 'z', 0 }
        };
        char     *mb = (char *)0x1;
        uint16_t *uc = (uint16_t *)0x1;
        int       rc;

        PARAM_CLEAR(the_param);
        rc = (int)CI(MBCSToUCS2)(cv_room, IN[i], &uc);
        printf("CV to16 %ld rc %d ", i, rc);
        if (uc == NULL)
            printf("none");
        else
            for (j = 0; uc[j] != 0 && j < 8; j++)
                printf("%04x", (unsigned)uc[j]);
        putchar('\n');

        rc = (int)CI(UCS2ToMBCS)(cv_room, WIDE[i], &mb, (int32_t)(i & 1));
        printf("CV to8 %ld rc %d ", i, rc);
        if (mb == NULL)
            printf("none");
        else
            putBytes(mb);
        putchar('\n');
    }

    /* The user dictionary, which is where every one of these calls ends up.
       The store is this converter's own rather than the one DictSearch holds,
       and setDict is what puts it in force. */
    memset(ta_block, 0, sizeof ta_block);
    memset(ud_room, 0, sizeof ud_room);
    memset(ic_block, 0, sizeof ic_block);
    memset(ds_block, 0, sizeof ds_block);
    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    TA_SET(ta_block, TA_OWNER, cv_room);
    DS_SET_OWNER(ds_block, ta_block);
    UD(Ctor)(ud_room, ta_block);
    RZ_SET_USERDICT(cv_room, ud_room);

    {
        void *made = CI(NewDict)(cv_room);

        printf("CV new %d\n", made != NULL);
        CI(SetDict)(cv_room, made);
        printf("CV set %d\n", UD_DICT(ud_room) == made);

        for (i = 0; UD_WORDS[i][0] != NULL && i < 8; i++)
            for (j = 0; j < 2; j++) {
                int rc = (int)CI(UpdateDictExt)(cv_room, made, 0,
                             (uint8_t *)(uintptr_t)(const void *)
                                 UD_WORDS[i][0],
                             (int32_t)strlen(UD_WORDS[i][0]),
                             (char *)(uintptr_t)(const void *)UD_WORDS[i][1],
                             (int32_t)strlen(UD_WORDS[i][1]),
                             (int32_t)(j + 1), j ? 0x80000 : 0x80100);

                printf("CV upd %ld %ld rc %d err %lu\n", i, j, rc,
                       (unsigned long)PARAM_ERRORS(the_param));
            }

        for (i = 0; UD_WORDS[i][0] != NULL && i < 8; i++)
            for (j = 0; j < 2; j++) {
                void   *value = (void *)0x1;
                int32_t vlen  = -1;
                int32_t pos   = -1;
                int     rc    = (int)CI(LookupDictExt)(cv_room, made, 0,
                             (uint8_t *)(uintptr_t)(const void *)
                                 UD_WORDS[i][0],
                             (int32_t)strlen(UD_WORDS[i][0]),
                             &value, &vlen, &pos, j ? 0x80000 : 0x80100);

                printf("CV look %ld %ld rc %d len %ld pos %ld ", i, j, rc,
                       (long)vlen, (long)pos);
                if (value == NULL || value == (void *)0x1)
                    printf("none");
                else
                    for (k = 0; k < vlen && k < 40; k++)
                        printf("%02x",
                               (unsigned)((uint8_t *)value)[k]);
                putchar('\n');
            }

        /* The whole store read out entry by entry, then past the end. */
        for (i = 0; i < 12; i++) {
            void   *word  = (void *)0x1;
            int32_t wlen  = -1;
            void   *extra = (void *)0x1;
            int32_t elen  = -1;
            int32_t pos   = -1;
            int     rc;

            if (i == 0)
                rc = (int)CI(FindFirst)(cv_room, made, 0, &word, &wlen,
                                        &extra, &elen, &pos, 0x80000);
            else
                rc = (int)CI(FindNext)(cv_room, made, 0, &word, &wlen,
                                       &extra, &elen, &pos, 0x80000);

            printf("CV walk %ld rc %d wlen %ld elen %ld pos %ld extra %d ",
                   i, rc, (long)wlen, (long)elen, (long)pos,
                   extra == (void *)0x1 ? 2 : extra != NULL);
            if (word == NULL || word == (void *)0x1)
                printf("none");
            else
                for (k = 0; k < wlen && k < 40; k++)
                    printf("%02x", (unsigned)((uint8_t *)word)[k]);
            putchar('\n');
            if (rc != 0)
                break;
        }

        /* Written out, read back into a store of its own, and read out again
           so that both halves are held to the same bytes. */
        remove(path);
        printf("CV save rc %d\n",
               (int)CI(SaveDict)(cv_room, made, 0, path));
        /* And into a directory that is not there, which is the only way the
           store's own no-file answer reaches either of these two. */
        printf("CV save nowhere rc %d\n",
               (int)CI(SaveDict)(cv_room, made, 0,
                                 "romprims-no-such-dir/x.tmp"));
        printf("CV load missing rc %d\n",
               (int)CI(LoadDict)(cv_room, made, 0, "romprims-nowhere.tmp"));

        ibm_slCtor(list_room);
        printf("CV load rc %d\n",
               (int)CI(LoadDict)(cv_room, list_room, 0, path));
        for (i = 0; i < 12; i++) {
            void   *word  = (void *)0x1;
            int32_t wlen  = -1;
            void   *extra = (void *)0x1;
            int32_t elen  = -1;
            int32_t pos   = -1;
            int     rc;

            if (i == 0)
                rc = (int)CI(FindFirst)(cv_room, list_room, 0, &word, &wlen,
                                        &extra, &elen, &pos, 0x80000);
            else
                rc = (int)CI(FindNext)(cv_room, list_room, 0, &word, &wlen,
                                       &extra, &elen, &pos, 0x80000);

            printf("CV reread %ld rc %d wlen %ld pos %ld extra %d ", i, rc,
                   (long)wlen, (long)pos,
                   extra == (void *)0x1 ? 2 : extra != NULL);
            if (word == NULL || word == (void *)0x1)
                printf("none");
            else
                for (k = 0; k < wlen && k < 40; k++)
                    printf("%02x", (unsigned)((uint8_t *)word)[k]);
            putchar('\n');
            if (rc != 0)
                break;
        }
        ibm_slDtor(list_room);
        remove(path);

        /* And thrown away, which also clears whatever was in force. */
        CI(DeleteDict)(cv_room, made);
        printf("CV del %d\n", UD_DICT(ud_room) == NULL);
    }

    CI(CloseBase)(cv_room);
    printf("CV close input %d trans %d uni %d\n",
           CV_INPUT(cv_room) != NULL, CV_TRANSBUF(cv_room) != NULL,
           CV_UNICODE(cv_room) != NULL);
    CI(CloseBase)(cv_room);
    printf("CV close twice input %d trans %d uni %d\n",
           CV_INPUT(cv_room) != NULL, CV_TRANSBUF(cv_room) != NULL,
           CV_UNICODE(cv_room) != NULL);

    printf("CV done\n");
}

/* ---- the phrase buffer ------------------------------------------------ */

/* How this one is driven. PhraseBuf reads three records it does not own -- a
 * path and a sub-word out of JPath, and a function word out of DictSearch --
 * and writes a fourth into the caller's buffer. None of the three is made by
 * anything written yet, so the harness builds all of them by hand at IBM's
 * own offsets, which both sides keep for exactly this reason.
 *
 * What is swept. Every part of speech through the two that only look at one,
 * every phrase head through the vector builder, a path of one word and of
 * several through the two that fill a phrase, and the whole of
 * SetPhraseBuffer over a set of paths with function words hung off them. The
 * phrase is printed whole after every call that writes one, so a field set
 * wrongly shows even where no answer changes.
 */
static char pb_room[PB_ROOM];
static char jp_room[JP_ROOM];
static char wp_room[8 * PB_SLOT_SIZE];

/* Where in the function-word dictionary a word of an odd length sits.
   The chain's total steps by that, so it lands on every value rather
   than every other one, which is what the bound on it needs. */
static long pb_odd = -1;

/* The reader's text, which IsSokuonTankanVerb indexes with a sub-word's own
   mark. It is the harness's, so both sides read the same bytes. */
static char PB_TEXT[16];

/* The first verb of PhraseBuf's own table, put where the sweep's sub-words
   point, so that the compare matches for one place and not for the others.
   Taken from the table rather than written out, so the two sides cannot
   disagree about it. */
static void pbMakeText(void)
{
    /* The first two bytes are the first verb of PhraseBuf's own table, so
       that the compare matches for one place in this text and not for the
       others. They are written out rather than read from the table because
       that table is a file-static of IBM's object and does not link; the
       bytes are the third entry of it, and the third rather than the first
       because the first is at offset nought whatever stride the walk uses,
       so a sabotage of that stride would not show. The sweep proves the pair
       is really in the table: a wrong one would make both sides answer no
       everywhere and every sabotage of the compare would go quiet. */
    memcpy(PB_TEXT, "\x8e\xa1", 2);
    memcpy(PB_TEXT + 2, "\x8c\xa9\x8d\x73\x82\xa2\x82\xa4", 8);
    PB_TEXT[10] = 0;
}

/* One sub-word laid into JPath, and the entry index that names it. */
static void jpSub(int s, int e, int at, int32_t mark, int accent,
                  int kanalen, int chars, int hiragana, int pos, int attr,
                  int offset, const uint8_t *kana)
{
    uint8_t *sub = JP_SUB_AT(jp_room, s);
    int      i;

    memset(sub, 0, JP_SUB_SIZE);
    *(int16_t *)(sub + JS_ENTRY)  = (int16_t)e;
    *(int16_t *)(sub + JS_AT)     = (int16_t)at;
    *(int32_t *)(sub + JS_MARK)   = mark;
    *(int16_t *)(sub + JS_ACCENT) = (int16_t)accent;
    sub[JS_KANALEN]  = (uint8_t)kanalen;
    sub[JS_CHARS]    = (uint8_t)chars;
    sub[JS_HIRAGANA] = (uint8_t)hiragana;
    sub[JS_POS]      = (uint8_t)pos;
    sub[JS_ATTR]     = (uint8_t)attr;
    *(int16_t *)(sub + JS_OFFSET) = (int16_t)offset;
    for (i = 0; i < JS_KANA_N; i++)
        sub[JS_KANA + i] = kana ? kana[i] : (uint8_t)(0x40 + i);
    JP_INDEX_OF(jp_room, e) = (int16_t)s;
}

/* And one path over a run of entry indices. */
static void jpPath(int p, int cost, const int *ents, int n)
{
    uint8_t *path = JP_PATH_AT(jp_room, p);
    int      i;

    memset(path, 0, JP_PATH_SIZE);
    path[JPT_COUNT] = (uint8_t)n;
    path[JPT_COST]  = (uint8_t)cost;
    for (i = 0; i < n && i < JPT_AT_N; i++)
        path[JPT_AT + i] = (uint8_t)ents[i];
}

/* One function word in the dictionary search's own table. */
static void dsFzk(int i, int link, int kanalen, int moras, int code, int at,
                  int accent, int flags, int offset)
{
    uint8_t *f = (uint8_t *)ds_block + DS_FZK + i * DS_FZK_SIZE;

    memset(f, 0, DS_FZK_SIZE);
    f[PF_LINK]    = (uint8_t)link;
    f[PF_KANALEN] = (uint8_t)kanalen;
    f[PF_MORAS]   = (uint8_t)moras;
    f[PF_CODE]    = (uint8_t)code;
    *(int16_t *)(f + PF_AT)     = (int16_t)at;
    *(int16_t *)(f + PF_ACCENT) = (int16_t)accent;
    f[PF_FLAGS]   = (uint8_t)flags;
    *(int16_t *)(f + PF_OFFSET) = (int16_t)offset;
}

/* A phrase printed whole, which is the only way a field written wrongly by a
   method that answers nothing is seen at all. */
static void putPhrase(const char *what, long a, long b, const uint8_t *w)
{
    int i;

    printf("%s %ld %ld ", what, a, b);
    for (i = 0; i < PB_SLOT_SIZE; i++)
        printf("%02x", (unsigned)w[i]);
    putchar('\n');
}

/* Everything the three records need, laid out the same on both sides. */
static void pbSetUp(void)
{
    memset(ta_block, 0, sizeof ta_block);
    memset(ic_block, 0, sizeof ic_block);
    memset(ds_block, 0, sizeof ds_block);
    memset(jp_room, 0, sizeof jp_room);
    memset(pb_room, 0, sizeof pb_room);

    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    TA_SET(ta_block, TA_JPATH, jp_room);
    IC_TEXTP_SET(ic_block, PB_TEXT);
    DS_SET_OWNER(ds_block, ta_block);
    JP_SET(jp_room, JP_OWNER, ta_block);
    JP_SET(jp_room, JP_SEARCH, ds_block);
    PB(Ctor)(pb_room, ta_block);
}

static void sweepPhraseBuf(void)
{
    static const uint8_t KANA[JS_KANA_N] = {
        0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49
    };
    long i;
    long j;

    pbMakeText();
    pbSetUp();

    /* Every part of speech through the vector builder and the two tests that
       read nothing but a table row. */
    for (i = 0; i < 256; i++) {
        uint8_t pos[16];
        uint8_t sub[JP_SUB_SIZE];
        int     k;

        memset(pos, 0xa5, sizeof pos);
        PB(ModifyPos)(pb_room, pos, (uint8_t)i);
        printf("PB pos %ld ", i);
        for (k = 0; k < 14; k++)
            printf("%02x", (unsigned)pos[k]);
        putchar('\n');

        memset(sub, 0, sizeof sub);
        sub[JS_POS] = (uint8_t)i;
        printf("PB end %ld %d\n", i, (int)PB(IsBunsetsuEnd)(pb_room, sub));
    }

    /* The single-kanji verb test, over every text the two bytes could be.
       The reader's text is the harness's own, so both sides read the same
       bytes at the same place. */
    {
        for (i = 0; i < 8; i += 2)
            for (j = 0; j < 3; j++) {
                uint8_t sub[JP_SUB_SIZE];

                memset(sub, 0, sizeof sub);
                sub[JS_CHARS]    = (uint8_t)(j == 1 ? 2 : 1);
                sub[JS_HIRAGANA] = (uint8_t)(j == 2 ? 1 : 0);
                *(int32_t *)(sub + JS_MARK) = (int32_t)i;
                printf("PB verb %ld %ld %d\n", i, j,
                       (int)PB(IsSokuonTankanVerb)(pb_room, sub));
            }
    }

    /* The phrase kind, over every part of speech and every shape of phrase
       that decides it. */
    for (i = 0; i < 256; i++)
        for (j = 0; j < 4; j++) {
            uint8_t w[PB_SLOT_SIZE];

            memset(w, 0, sizeof w);
            w[WP_TYPE]  = 7;
            w[WP_WORDS] = (uint8_t)(j & 1 ? 1 : 2);
            *(uint16_t *)(w + WP_MORAS) = (uint16_t)(j & 2 ? 1 : 4);
            w[WP_ACCENT] = (uint8_t)(j & 2 ? 1 : 4);
            w[WP_WORD + WW_POS] = (uint8_t)i;
            printf("PB kind %ld %ld %d\n", i, j,
                   (int)PB(GetSpecialPhraseType)(pb_room, w));
        }

    /* The one pair the analysis refuses. The dictionary is searched for a
       word two codes long, and for the one word whose two codes are the pair
       that is refused, so that both answers of every test are reached; the
       search is over the same table on both sides, so what it finds cannot
       differ. */
    {
        const uint8_t *dict = DM(GetFuncDictEx)();
        long           two  = -1;
        long           hit  = -1;
        long           at;

        for (at = 0; at < 20000; at++) {
            if (pb_odd < 0 && dict[at] == 7)
                pb_odd = at;
            if (dict[at] != 8)
                continue;
            if (two < 0)
                two = at;
            if (dict[at + 6] == 0xfd && dict[at + 7] == 0x23) {
                hit = at;
                break;
            }
        }
        printf("PB tte found %ld %ld %ld\n", two, hit, pb_odd);

        for (i = 0; i < 256; i++)
            for (j = 0; j < 8; j++) {
                uint8_t f[PB_SLOT_SIZE];
                long    where = (j & 4) ? hit : ((j & 2) ? two : 0);

                if (where < 0)
                    where = 0;
                memset(f, 0, sizeof f);
                f[WF_CODE]    = (uint8_t)i;
                f[WF_KANALEN] = (uint8_t)(j & 1 ? 2 : 3);
                *(int16_t *)(f + WF_AT) = (int16_t)where;
                printf("PB tte %ld %ld %d\n", i, j,
                       (int)PB(ChkTTELink)(pb_room, (int32_t)(j & 1), f));
            }
    }

    /* Every part of speech through the one that fills a word, which is what
       reaches the three stand-ins a phrase head is written as. */
    for (i = 0; i < 256; i++) {
        static const int ONE[1] = { 5 };
        uint8_t *w = (uint8_t *)wp_room;
        int16_t  a = -1;
        int16_t  b = -1;

        pbSetUp();
        jpSub(0, 5, 3, 4, 7, 5, 2, 1, (int)i, 0x11, 9, KANA);
        jpPath(0, 2, ONE, 1);
        JP_S16(jp_room, JP_PATH_COUNT) = 1;
        memset(wp_room, 0, sizeof wp_room);
        w[WP_TYPE] = 7;
        PB(SetJrt)(pb_room, JP_PATH_AT(jp_room, 0), w, &a, &b);
        printf("PB jrtpos %ld kana %d accent %d\n", i, (int)a, (int)b);
        putPhrase("PB jrtposw", i, 0, w);
    }

    /* And with no paths at all, which is the one road out of it that writes
       nothing. */
    {
        static const int ONE[1] = { 5 };
        uint8_t *w = (uint8_t *)wp_room;
        int16_t  a = -1;
        int16_t  b = -1;

        pbSetUp();
        jpSub(0, 5, 3, 4, 7, 5, 2, 1, 1, 0x11, 9, KANA);
        jpPath(0, 2, ONE, 1);
        JP_S16(jp_room, JP_PATH_COUNT) = 0;
        memset(wp_room, 0, sizeof wp_room);
        PB(SetJrt)(pb_room, JP_PATH_AT(jp_room, 0), w, &a, &b);
        printf("PB jrtnone kana %d accent %d\n", (int)a, (int)b);
        putPhrase("PB jrtnonew", 0, 0, w);
    }

    /* A path of one word and of three, through the two that fill a phrase.
       The sub-words differ in every field the phrase carries, so a field
       taken from the wrong place shows. */
    for (i = 0; i < 4; i++) {
        static const int ENTS[3] = { 5, 9, 2 };
        uint8_t *w = (uint8_t *)wp_room;
        int16_t  a = -1;
        int16_t  b = -1;
        int      n = (int)(i % 3) + 1;

        pbSetUp();
        jpSub(0, 5, 3, 4, 7, 5, 2, 1, (int)(i * 37 + 1), 0x11, 9, KANA);
        jpSub(1, 9, 6, 8, 2, 12, 3, 0, (int)(i * 53 + 2), 0x22, 4, KANA);
        jpSub(2, 2, 1, 2, 5, 1, 1, 1, (int)(i * 71 + 3), 0x44, 6, KANA);
        jpPath(0, (int)(i + 1), ENTS, n);
        JP_S16(jp_room, JP_PATH_COUNT) = 1;

        memset(wp_room, 0, sizeof wp_room);
        PB(SetJrt)(pb_room, JP_PATH_AT(jp_room, 0), w, &a, &b);
        printf("PB jrt %ld kana %d accent %d\n", i, (int)a, (int)b);
        putPhrase("PB jrtw", i, 0, w);
    }

    /* And the whole of it: paths with a chain of function words hung off
       each, over the two roads out of SetPhrasePart and the bound on how long
       a chain may get. */
    for (i = 0; i < 24; i++) {
        static const int ENTS[2] = { 5, 9 };
        int16_t rc;
        int     k;

        pbSetUp();
        /* The reader says the text is already used up, which is what keeps
           SetPhraseBuffer out of DictSearch::FzkParsing. That method wants a
           parse state neither side can be handed by hand -- driven over a
           built-up one both engines walk off their own tables -- so the road
           through it is left to the day TextAnalysis can make one. */
        *(int16_t *)(ic_block + IC_COUNT) = 0;
        /* The two words differ in every field the buffer reads, and one
           of them has no reading at all, which is the case the road out
           of the bottom refuses. */
        jpSub(0, 5, 3, 4, (int)(i % 7 == 2 ? 5 : 7),
              (int)(i % 4 ? 5 : 0), (int)(i % 8 == 2 ? 0 : 2), 1,
              (int)(i * 11 + 1), 0x11, 9, KANA);
        jpSub(1, 9, 6, 8, (int)(i % 5 == 4 ? 1 : 2),
              (int)(i % 5 == 4 ? 1 : 12), 3, (int)(i % 5 == 4 ? 1 : 0),
              (int)(i * 7 + 2), 0x22, 4, KANA);
        /* Two words on the first path, so that the road out of the bottom
           looks at a different entry from the one at the head. */
        jpPath(0, (int)(i % 5) + 1, ENTS, 2);
        jpPath(1, (int)(i % 3) + 4, ENTS, (int)(i % 2) + 1);
        JP_S16(jp_room, JP_PATH_COUNT) = 2;

        /* A chain of three, and for the last two cases one long enough to be
           refused -- by its length for one and by the moras it comes to for
           the other. The flags differ down the chain so that a word which may
           not start one is stepped over. */
        {
            int n = (i >= 4) ? 20 : 3;

            for (k = 0; k < n; k++)
                dsFzk(k, k + 1 < n ? k + 1 : -1,
                      (int)(i == 5 ? 9 : i + 1), 2, 0x49 + k,
                      /* Where the word is looked up decides how many moras
                         the chain comes to, and a long one is what makes the
                         moras bound fire before the count bound does. */
                      (i % 6 == 3) ? 50 : ((i % 6 == 1) ? pb_odd : 0), k,
                      (k % 3 == 2) ? 0x02 : 0x03, k);
        }

        memset(wp_room, 0, sizeof wp_room);
        rc = PB(SetPhrasePart)(pb_room, JP_PATH_AT(jp_room, 0), 0,
                               (int16_t)(i % 4), (int32_t)(i & 1),
                               (uint8_t *)wp_room);
        printf("PB part %ld rc %d\n", i, (int)rc);
        /* And once with the buffer already full, which is the one road out
           of it that writes nothing at all. */
        printf("PB part full %ld rc %d\n", i,
               (int)PB(SetPhrasePart)(pb_room, JP_PATH_AT(jp_room, 0),
                                      PB_SLOT_N, (int16_t)(i % 4),
                                      (int32_t)(i & 1), (uint8_t *)wp_room));
        for (k = 0; k < 3; k++)
            putPhrase("PB partw", i, k, WP_SLOT(wp_room, k));

        memset(wp_room, 0, sizeof wp_room);
        rc = PB(SetPhraseBuffer)(pb_room, (uint8_t *)wp_room);
        printf("PB buf %ld rc %d\n", i, (int)rc);
        for (k = 0; k < 3; k++)
            putPhrase("PB bufw", i, k, WP_SLOT(wp_room, k));
    }

    /* And the copy, which is the one method that touches the buffer itself. */
    for (i = 0; i < 3; i++) {
        int k;

        pbSetUp();
        memset(ta_block + TA_BUFFERS + i * TA_BUFFER_SIZE,
               (int)(0x30 + i), TA_BUFFER_SIZE);
        PB(Copy)(pb_room, (int16_t)i);
        printf("PB copy %ld ", i);
        for (k = 0; k < 16; k++)
            printf("%02x", (unsigned)(uint8_t)pb_room[PB_BUFFER + k]);
        /* And the last bytes of it, which is the only place a copy one byte
           short of the whole shows. */
        for (k = PB_BUFFER_SIZE - 8; k < PB_BUFFER_SIZE; k++)
            printf("%02x", (unsigned)(uint8_t)pb_room[PB_BUFFER + k]);
        putchar('\n');
    }

    printf("PB done\n");
}


/* ---- the path search -------------------------------------------------- */

/* How this one is driven.
 *
 * JPath's eleven methods split three ways. Four read nothing but a part of
 * speech and one or two attribute bytes, so they are swept exhaustively over
 * every value of each: 256 parts of speech is the whole of that input, and
 * the attribute bytes are read one bit at a time, so 256 values of each is
 * the whole of theirs too.
 *
 * Two -- the pair that decides what it costs to put one word after another --
 * take two four-byte type groups and two attribute pairs between them, which
 * is eighty bits of input and cannot be swept whole. CheckAdFlag reduces each
 * type group to one of eleven classes and then reads the attribute bytes, so
 * it is swept over all 121 pairs of classes with each attribute byte taken
 * through all 256 of its values, and again with the four bits it reads out of
 * the type groups themselves turned on and off in every combination.
 * JrtJrtCheck's guards read all four bytes of the left-hand type group
 * directly, so it gets a byte-at-a-time sweep instead: each of the eight
 * bytes through all 256 of its values against a spread of the other seven.
 *
 * Those two run to millions of calls, and a line a call would be gigabytes.
 * Their answers go into a rolling digest printed every 1024 calls, which
 * still says which window of calls a difference is in; the structured part of
 * each sweep is printed in full besides, so the common cases are legible.
 *
 * The last five need a lattice of candidate entries, which nothing written
 * yet builds. The harness lays one into DictSearch by hand at IBM's own
 * offsets -- 64 lattices over the same eight characters, each with different
 * parts of speech, attributes and dictionary costs -- and then drives Make
 * over it, printing every path and every sub-word it leaves behind. That is
 * what covers Make, AddPath, GetMoraOnPath and MakeJrtSubTable together, and
 * what would catch a search that finds the wrong paths rather than one that
 * merely scores them wrongly.
 */

/* One type group per class CheckType names. Each is exactly what CheckType
   reads and nothing more; the four bits the cost functions read out of a type
   group themselves are in the other two bytes and the sweep adds them. */
static const uint8_t JP_TG[11][4] = {
    { 0, 0, 0x00, 0x80 },   /*  0, the sentence's edge */
    { 0, 0, 0x00, 0x00 },   /*  1 */
    { 0, 0, 0x02, 0x00 },   /*  2 */
    { 0, 0, 0x01, 0x00 },   /*  3 */
    { 0, 0, 0x00, 0x01 },   /*  4 */
    { 0, 0, 0x02, 0x01 },   /*  5 */
    { 0, 0, 0x01, 0x01 },   /*  6 */
    { 0, 0, 0x10, 0x00 },   /*  7 */
    { 0, 0, 0x00, 0x08 },   /*  8 */
    { 0, 0, 0x01, 0x10 },   /*  9 */
    { 0, 0, 0x00, 0x10 },   /* 10 */
};

static uint32_t jp_roll;
static long     jp_rolled;

static void jpRoll(long v)
{
    jp_roll = jp_roll * 1000003u + (uint32_t)v;
    jp_rolled++;
    if ((jp_rolled & 0x3ff) == 0)
        printf("JP roll %ld %08lx\n", jp_rolled, (unsigned long)jp_roll);
}

/* The eight bytes of one call to either cost function, so that a sweep can
   name one byte and leave the rest alone. */
static void jpBytes(uint8_t *lt, uint8_t *rt, uint8_t *la, uint8_t *ra,
                    uint32_t seed)
{
    int i;

    for (i = 0; i < 4; i++) {
        seed = seed * 1103515245u + 12345u;
        lt[i] = (uint8_t)(seed >> 16);
        seed = seed * 1103515245u + 12345u;
        rt[i] = (uint8_t)(seed >> 16);
    }
    for (i = 0; i < 2; i++) {
        seed = seed * 1103515245u + 12345u;
        la[i] = (uint8_t)(seed >> 16);
        seed = seed * 1103515245u + 12345u;
        ra[i] = (uint8_t)(seed >> 16);
    }
}

/* One candidate entry of the search, which is thirty-two bytes at IBM's own
   offsets whichever side is reading it. */
#define JP_ENT_AT(i) ((uint8_t *)ds_block + DS_ENTRY + (i) * DS_ENTRY_SIZE)

/* One entry of the lattice, at IBM's own offsets within a candidate. */
static void jpEntry(int i, int at, int chars, int pos, int attr, int attr2,
                    int32_t cost, int kanalen)
{
    uint8_t *e = JP_ENT_AT(i);
    int      k;

    memset(e, 0, DS_ENTRY_SIZE);
    *(int16_t *)(e + DE_ACCENT) = (int16_t)(1 + i % 4);
    e[DE_KANALEN]  = (uint8_t)kanalen;
    e[DE_CHARS]    = (uint8_t)chars;
    e[DE_HIRAGANA] = (uint8_t)(i % 3);
    e[DE_POS]      = (uint8_t)pos;
    e[DE_ATTR]     = (uint8_t)attr;
    e[DE_ATTR2]    = (uint8_t)attr2;
    for (k = 0; k < 10; k++)
        e[DE_KANA + k] = (uint8_t)(0x30 + i + k);
    *(int16_t *)(e + DE_AT)     = (int16_t)at;
    *(int32_t *)(e + DE_MARK)   = (int32_t)(at * 2);
    *(int16_t *)(e + DE_OFFSET) = (int16_t)(i * 3);
    *(int32_t *)(e + DE_COST)   = cost;
}

/* The lattice: how many characters each entry covers and where it starts.
   The entries are in rising order of where they start, because Make looks for
   the first entry at a character and then walks the run of them, which is
   what DictSearch's own writers leave behind. */
static const signed char JP_LAT[][2] = {
    /* at, chars */
    { 0, 1 }, { 0, 2 }, { 0, 1 },
    { 1, 1 }, { 1, 3 },
    { 2, 2 }, { 2, 1 },
    { 3, 1 }, { 3, 2 },
    { 4, 1 }, { 4, 2 },
    { 5, 1 },
    { 6, 2 },
};

/* And a second shape, of one-character words only, so that a path can grow to
   the ten entries the record holds and to more moras than a phrase may. The
   first shape cannot reach either bound: its words are long enough that four
   of them cover the text. */
static const signed char JP_LONG[][2] = {
    { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 }, { 5, 1 },
    { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 10, 1 }, { 11, 1 },
    { 12, 1 },
};
#define JP_LAT_N ((int)(sizeof JP_LAT / sizeof JP_LAT[0]))

/* Everything JPath needs, laid out the same on both sides. The reader's text
   carries a case marker at one character and not at the others, so the one
   branch of Make that asks about the character before a word is taken for
   some words and not for the rest. */
static void jpSetUp(void)
{
    int i;

    memset(ta_block, 0, sizeof ta_block);
    memset(ic_block, 0, sizeof ic_block);
    memset(ds_block, 0, sizeof ds_block);
    memset(jp_room, 0, sizeof jp_room);

    /* A case marker at the first character as well as at the third: the
       first is what makes the bound on that test observable, since a word
       starting at the second character is the only one whose neighbour is
       character nought. */
    for (i = 0; i < 16; i++) {
        ic_block[IC_TEXT + i * 2]     = (char)0x82;
        ic_block[IC_TEXT + i * 2 + 1] =
            (char)((i == 0 || i == 2) ? 0xf0 : 0xa0 + i);
    }
    *(int16_t *)(ic_block + IC_COUNT) = 16;

    TA_SET(ta_block, TA_INPUTCHAR, ic_block);
    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    TA_SET(ta_block, TA_JPATH, jp_room);
    DS_SET_OWNER(ds_block, ta_block);
    *(void **)(ds_block + DS_INPUTCHAR) = ic_block;
    JP(Ctor)(jp_room, ta_block);
}

/* What one run of Make left behind: the paths, the cheapest way to each
   character, and the sub-words. Printed whole, so a path found in the wrong
   order or a sub-word field copied from the wrong place both show. */
static void jpReport(long lat, int at)
{
    long n = (long)*(uint16_t *)(jp_room + JP_PATH_COUNT);
    long i;
    int  k;

    printf("JP make %ld %d paths %ld\n", lat, at, n);
    for (i = 0; i < n; i++) {
        const uint8_t *path = JP_PATH_AT(jp_room, i);

        printf("JP path %ld %d %ld %d %d %d %d ", lat, at, i,
               (int)path[JPT_COUNT], (int)path[JPT_COST],
               (int)path[JPT_END], (int)path[JPT_CONT]);
        for (k = 0; k < JPT_AT_N; k++)
            printf("%02x", (unsigned)path[JPT_AT + k]);
        putchar('\n');
    }
    printf("JP cost %ld %d ", lat, at);
    for (k = 0; k < 20; k++)
        printf("%02x", (unsigned)(uint8_t)jp_room[JP_COST + k]);
    putchar('\n');
    for (i = 0; i < JP_LAT_N; i++) {
        long   sub = (long)JP_INDEX_OF(jp_room, i);
        printf("JP index %ld %d %ld %ld", lat, at, i, sub);
        if (sub >= 0) {
            const uint8_t *w = JP_SUB_AT(jp_room, sub);

            putchar(' ');
            for (k = 0; k < JP_SUB_SIZE; k++)
                printf("%02x", (unsigned)w[k]);
        }
        putchar('\n');
    }
}

static void sweepJPath(void)
{
    long i, j, b, v, c;

    jpSetUp();

    /* The eleven classes, over every value of the two type-group bytes
       CheckType reads. This is the whole of its input. */
    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++) {
            uint8_t tg[4];

            tg[0] = 0xa5;
            tg[1] = 0x5a;
            tg[2] = (uint8_t)i;
            tg[3] = (uint8_t)j;
            printf("JP type %ld %ld %d\n", i, j,
                   (int)JP(CheckType)(jp_room, tg));
        }

    /* And the type group the sweep below relies on being each class. A wrong
       one here would leave whole branches of the cost functions unreached
       without anything saying so. */
    for (i = 0; i < 11; i++)
        printf("JP class %ld %d\n", i,
               (int)JP(CheckType)(jp_room, JP_TG[i]));

    /* The three that read nothing but a part of speech and one bit. */
    for (i = 0; i < 256; i++) {
        uint8_t e[DS_ENTRY_SIZE];

        memset(e, 0, sizeof e);
        e[DE_POS] = (uint8_t)i;
        printf("JP head %ld %d\n", i, (int)JP(IsHead)(jp_room, e));
        printf("JP cont %ld %d\n", i, (int)JP(IsContinuable)(jp_room, e));
        printf("JP end %ld 0 %d\n", i, (int)JP(IsEnd)(jp_room, e));
        e[DE_ATTR2] = 0x02;
        printf("JP end %ld 1 %d\n", i, (int)JP(IsEnd)(jp_room, e));
    }

    /* The attribute byte of a sub-word, over the whole of what it reads: 256
       parts of speech, 256 second attribute bytes, and the one bit of the
       first that it looks at. */
    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++)
            for (c = 0; c < 2; c++) {
                uint8_t e[DS_ENTRY_SIZE];
                uint8_t sub[JP_SUB_SIZE];

                memset(e, 0, sizeof e);
                memset(sub, 0xa5, sizeof sub);
                e[DE_POS]   = (uint8_t)i;
                e[DE_ATTR2] = (uint8_t)j;
                e[DE_ATTR]  = (uint8_t)(c ? 0x80 : 0x00);
                JP(SetWordAttr)(jp_room, sub, e);
                printf("JP attr %ld %ld %ld %02x\n", i, j, c,
                       (unsigned)sub[JS_ATTR]);
            }

    /* The cost of putting one word after another, printed in full over every
       pair of classes and the corners of the attribute bytes. */
    for (i = 0; i < 11; i++)
        for (j = 0; j < 11; j++)
            for (v = 0; v < 4; v++)
                for (c = 0; c < 3; c++) {
                    uint8_t la[2], ra[2];
                    int16_t cost = (int16_t)(c == 0 ? 0 : c == 1 ? 5 : 40);

                    la[0] = (uint8_t)((v & 1) ? 0xff : 0x00);
                    la[1] = (uint8_t)((v & 1) ? 0xff : 0x00);
                    ra[0] = (uint8_t)((v & 2) ? 0xff : 0x00);
                    ra[1] = (uint8_t)((v & 2) ? 0xff : 0x00);
                    printf("JP ad %ld %ld %ld %ld %d\n", i, j, v, c,
                           (int)JP(CheckAdFlag)(jp_room, JP_TG[i], JP_TG[j],
                                                la, ra, cost));
                }

    /* And over all 121 pairs of classes with each attribute byte taken
       through all 256 of its values, and the four bits the function reads out
       of the type groups themselves in every combination. */
    for (i = 0; i < 11; i++)
        for (j = 0; j < 11; j++)
            for (v = 0; v < 16; v++) {
                uint8_t lt[4], rt[4];

                memcpy(lt, JP_TG[i], 4);
                memcpy(rt, JP_TG[j], 4);
                if (v & 1)
                    lt[1] |= 0x08;
                if (v & 2)
                    lt[2] |= 0x40;
                if (v & 4)
                    rt[1] |= 0x08;
                if (v & 8)
                    rt[2] |= 0x40;

                for (b = 0; b < 4; b++)
                    for (c = 0; c < 256; c++) {
                        uint8_t la[2], ra[2];

                        la[0] = la[1] = ra[0] = ra[1] = 0;
                        if (b == 0)
                            la[0] = (uint8_t)c;
                        else if (b == 1)
                            la[1] = (uint8_t)c;
                        else if (b == 2)
                            ra[0] = (uint8_t)c;
                        else
                            ra[1] = (uint8_t)c;
                        jpRoll(JP(CheckAdFlag)(jp_room, lt, rt, la, ra,
                                               (int16_t)(c & 0x1f)));
                    }
            }
    /* One byte at a time leaves the tests that read a bit of one attribute
       byte and a bit of another only ever seeing both bits clear, so the same
       classes and variants are swept again over pairs of bytes: the two first
       attribute bytes together over every pair of the values whose bits the
       function reads, and then the two second ones. */
    {
        static const uint8_t FIRST[8] = {
            0x00, 0x07, 0x08, 0x10, 0x20, 0x40, 0x80, 0xff
        };
        static const uint8_t SECOND[8] = {
            0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40
        };
        long x, y;

        for (i = 0; i < 11; i++)
            for (j = 0; j < 11; j++)
                for (v = 0; v < 16; v++) {
                    uint8_t lt[4], rt[4];

                    memcpy(lt, JP_TG[i], 4);
                    memcpy(rt, JP_TG[j], 4);
                    if (v & 1)
                        lt[1] |= 0x08;
                    if (v & 2)
                        lt[2] |= 0x40;
                    if (v & 4)
                        rt[1] |= 0x08;
                    if (v & 8)
                        rt[2] |= 0x40;

                    for (x = 0; x < 8; x++)
                        for (y = 0; y < 8; y++) {
                            uint8_t la[2], ra[2];

                            la[0] = FIRST[x];
                            ra[0] = FIRST[y];
                            la[1] = 0;
                            ra[1] = 0;
                            jpRoll(JP(CheckAdFlag)(jp_room, lt, rt, la, ra,
                                                   (int16_t)(x * 8 + y)));
                            la[0] = 0;
                            ra[0] = 0;
                            la[1] = SECOND[x];
                            ra[1] = SECOND[y];
                            jpRoll(JP(CheckAdFlag)(jp_room, lt, rt, la, ra,
                                                   (int16_t)(x * 8 + y)));
                            la[0] = FIRST[y];
                            ra[0] = FIRST[x];
                            la[1] = SECOND[x];
                            ra[1] = SECOND[y];
                            jpRoll(JP(CheckAdFlag)(jp_room, lt, rt, la, ra,
                                                   (int16_t)(x * 8 + y)));
                        }
                }
    }
    printf("JP ad done %ld %08lx\n", jp_rolled, (unsigned long)jp_roll);

    /* The table lookup, printed in full over the classes and both roads
       through the adjustment. */
    for (i = 0; i < 11; i++)
        for (j = 0; j < 11; j++)
            for (v = 0; v < 2; v++) {
                uint8_t la[2], ra[2];

                la[0] = 0x11;
                la[1] = 0x22;
                ra[0] = 0x44;
                ra[1] = 0x88;
                printf("JP jrt %ld %ld %ld %d\n", i, j, v,
                       (int)JP(JrtJrtCheck)(jp_room, JP_TG[i], JP_TG[j],
                                            la, ra, (int32_t)v));
            }

    /* And byte at a time over all eight bytes of its input, each through all
       256 of its values against a spread of the other seven. */
    for (b = 0; b < 8; b++)
        for (v = 0; v < 256; v++)
            for (i = 0; i < 64; i++) {
                uint8_t lt[4], rt[4], la[2], ra[2];

                jpBytes(lt, rt, la, ra, (uint32_t)(i * 7919 + 1));
                if (b < 4)
                    lt[b] = (uint8_t)v;
                else
                    rt[b - 4] = (uint8_t)v;
                jpRoll(JP(JrtJrtCheck)(jp_room, lt, rt, la, ra,
                                       (int32_t)(i & 1)));
            }
    /* Two places in JrtJrtCheck that a sweep over byte patterns does not
       reach, because both want a left-hand type group that passes all nine
       of the guards before the fall-through chain -- which a pattern picked
       at random almost never does.
     *
     * The first is the tail of the chain, which takes a cost of exactly
       nought where every block above it wanted more: reaching it needs a
       left-hand word whose only set bit is one of the two that the first
       block of the chain reads, and a right-hand word whose table entry is
       nought. The second is the one place two of the tables leave alone once
       a cost has been found, which wants a right-hand word with that bit set
       and a dearer bit set before it. */
    {
        static const uint8_t TAIL_L[6][4] = {
            { 0, 0x10, 0x00, 0x00 },   /* the first block of the chain */
            { 0, 0x20, 0x00, 0x00 },
            { 0, 0x80, 0x00, 0x00 },   /* and the second */
            { 0, 0x00, 0x20, 0x00 },
            { 0, 0x00, 0x10, 0x00 },   /* the kanji block */
            { 0, 0x00, 0x04, 0x00 },   /* and the one that takes the cheaper */
        };
        long x;

        for (x = 0; x < 6; x++)
            for (b = 0; b < 4; b++)
                for (v = 0; v < 256; v++)
                    for (i = 0; i < 2; i++) {
                        uint8_t rt[4], la[2], ra[2];

                        memset(rt, 0, sizeof rt);
                        rt[b] = (uint8_t)v;
                        la[0] = la[1] = ra[0] = ra[1] = 0;
                        printf("JP tail %ld %ld %ld %ld %d\n", x, b, v, i,
                               (int)JP(JrtJrtCheck)(jp_room, TAIL_L[x], rt,
                                                    la, ra, (int32_t)i));
                    }

        /* And the kept place: the function-word table and the second of the
           plain ones, with that bit always set on the right-hand word and
           every other byte of it swept, so that something dearer is found
           before the sweep reaches it. */
        for (x = 0; x < 2; x++)
            for (b = 0; b < 3; b++)
                for (v = 0; v < 256; v++) {
                    uint8_t lt[4], rt[4], la[2], ra[2];

                    memset(lt, 0, sizeof lt);
                    if (x == 0)
                        lt[3] = 0x08;
                    else
                        lt[1] = 0x80;
                    memset(rt, 0, sizeof rt);
                    rt[2] = 0x08;
                    if (b < 2)
                        rt[b] = (uint8_t)v;
                    else
                        rt[2] = (uint8_t)(0x08 | v);
                    la[0] = la[1] = ra[0] = ra[1] = 0;
                    printf("JP keep %ld %ld %ld %d\n", x, b, v,
                           (int)JP(JrtJrtCheck)(jp_room, lt, rt, la, ra, 0));
                }
    }
    printf("JP jrt done %ld %08lx\n", jp_rolled, (unsigned long)jp_roll);

    /* How many moras a path runs to, over paths of every length the record
       allows and every entry the lattice holds. */
    for (i = 0; i < JP_LAT_N; i++)
        jpEntry((int)i, JP_LAT[i][0], JP_LAT[i][1], (int)(i * 17) & 0xff,
                0, 0, 0, (int)(1 + i % 7));
    *(int16_t *)(ds_block + DS_COUNT) = (int16_t)JP_LAT_N;

    for (i = 1; i <= JPT_AT_N; i++)
        for (j = 0; j < JP_LAT_N; j++) {
            uint8_t path[JP_PATH_SIZE];
            int     k;

            memset(path, 0, sizeof path);
            path[JPT_COUNT] = (uint8_t)i;
            for (k = 0; k < (int)i; k++)
                path[JPT_AT + k] = (uint8_t)((j + k) % JP_LAT_N);
            printf("JP mora %ld %ld %d\n", i, j,
                   (int)JP(GetMoraOnPath)(jp_room, path, (int16_t)j));
        }

    /* How many entries a path may hold, asked of AddPath directly. No
       lattice reaches it: most pairs of word classes cannot stand together
       at all, so the longest path any of the 128 lattices grows is four
       entries, and the bound of ten is never seen. Handing it a path that is
       already that long is the only way to ask. */
    for (i = 6; i <= 11; i++)
        for (j = 0; j < JP_LAT_N; j++)
            for (c = 0; c < JP_LAT_N; c++) {
                uint8_t path[JP_PATH_SIZE], out[JP_PATH_SIZE];
                int     k;

                /* Nothing may have been left in the way of a cheaper path,
                   or the cost test refuses the join before the bound is
                   reached and the two sides agree for the wrong reason. */
                for (k = 0; k < JP_COST_N; k++)
                    JP_COST_AT(jp_room, k) = 0xff;
                memset(path, 0, sizeof path);
                memset(out, 0xa5, sizeof out);
                path[JPT_COUNT] = (uint8_t)i;
                path[JPT_COST]  = 0;
                for (k = 0; k < (int)i && k < JPT_AT_N; k++)
                    path[JPT_AT + k] = (uint8_t)j;
                printf("JP grow %ld %ld %ld %d ", i, j, c,
                       (int)JP(AddPath)(jp_room, path, JP_ENT_AT(c),
                                        out, 5, (int16_t)c));
                for (k = 0; k < JP_PATH_SIZE; k++)
                    printf("%02x", (unsigned)out[k]);
                putchar('\n');
            }

    /* The whole search, over sixty-four lattices of each of the two shapes
       with different parts of speech, attributes and dictionary costs on the
       entries, from each of the first four characters. */
    for (i = 0; i < 128; i++) {
        const signed char (*lat)[2] = (i < 64) ? JP_LAT : JP_LONG;

        for (j = 0; j < JP_LAT_N; j++)
            jpEntry((int)j, lat[j][0], lat[j][1],
                    (int)((i * 7 + j * 31) & 0xff),
                    (int)((i * 13 + j * 5) & 0xff),
                    (int)((i * 29 + j * 11) & 0xff),
                    (int32_t)((i + j) % 10),
                    /* Past nine for some of the first shape's words, which
                       is where a sub-word's reading is cut off; one mora
                       each for the second shape's, so that a path there can
                       reach the ten entries the record holds before the
                       bound on moras stops it. */
                    (i < 64) ? (int)(1 + (i + j) % 12) : 1);
        *(int16_t *)(ds_block + DS_COUNT) = (int16_t)JP_LAT_N;

        for (j = 0; j < 4; j++) {
            /* The whole block, then the constructor again: IBM keeps its
               three pointers inside the record and ours past it, so only
               remaking it leaves both sides in the same state. */
            memset(jp_room, 0, sizeof jp_room);
            JP(Ctor)(jp_room, ta_block);
            JP(Make)(jp_room, (int16_t)j);
            jpReport(i, (int)j);
        }
    }

    printf("JP done\n");
}


/* ---- the number reader ----------------------------------------------- */

/* How this one is driven.
 *
 * NumRead reads three things it does not own: the phrase word it is handed,
 * the spine's long-reading store when a reading is too long for that word,
 * and two of the romanizer's settings. All three are laid out by hand here
 * at IBM's own offsets, and the record itself is ours to fill.
 *
 * What is swept. Init over a record filled with a pattern, so that every
 * byte it clears and every byte it leaves shows. Then each of the four
 * reading roads over a substring built by hand: every kind, counts from one
 * to the thirty-two a substring holds, and codes drawn from the whole of
 * m_sanTCodes. Then SegmentYomiBlock, SetYomiType and GenerateStdForm over
 * numbers built as digit strings, printing the substring array whole after
 * each. And last Do, which drives all of it, over every number the harness
 * can write: each length from one digit to twenty, each of the operators in
 * each position, and a counter word after it.
 */
static char nr_room[NR_ROOM];
static char nr_rom[RZ_ROOM];
static char nr_word[PB_SLOT_SIZE];

/* Where each side keeps NumRead's owner: ours past the record, IBM's at
   nought with the readings starting at four. */
#ifdef EVV_ROMPRIMS_OURS
#define NR_SET_OWNER(blk, ta) (*(void **)((blk) + NR_OWNER_AT) = (ta))
#else
#define NR_SET_OWNER(blk, ta) (*(void **)((blk) + NR_OWNER) = (ta))
#endif

/* The digits of a number, as the codes NumRead reads rather than as
   characters: nought to nine are the digits, ten to eighteen the scales and
   the punctuation. */
static void nrDigits(const int *codes, int n)
{
    int i;

    for (i = 0; i < n; i++)
        nr_room[NR_DIGITS + i] = (char)codes[i];
    nr_room[NR_COUNT] = (char)n;
}

/* One word of a phrase, which is what Do and the two that need a word are
   handed. The reading goes in the word itself when it fits and in the
   spine's long-reading store when it does not, which is the road
   SegmentYomiBlock takes on a reading of more than nine. */
static void nrWord(int kanalen, int chars, int pos, int accent,
                   const int *kana, int slot)
{
    uint8_t *ww = WW_SLOT(nr_word, 0);
    int      i;

    memset(nr_word, 0, sizeof nr_word);
    nr_word[WP_WORDS] = 2;
    ww[WW_KANALEN] = (uint8_t)kanalen;
    ww[WW_CHARS]   = (uint8_t)chars;
    ww[WW_POS]     = (uint8_t)pos;
    *(int16_t *)(ww + WW_ACCENT) = (int16_t)accent;
    if (kanalen > 9) {
        ww[WW_KANA] = (uint8_t)slot;
        for (i = 0; i < kanalen && i < TA_LONGWORD_SIZE; i++)
            ta_block[TA_LONGWORD + slot * TA_LONGWORD_SIZE + i] =
                (char)kana[i];
    } else {
        for (i = 0; i < kanalen && i < WW_KANA_N; i++)
            ww[WW_KANA + i] = (uint8_t)kana[i];
    }
}

/* Everything the record needs, laid out the same on both sides. */
static void nrSetUp(int spell, int mode)
{
    memset(ta_block, 0, sizeof ta_block);
    memset(ds_block, 0, sizeof ds_block);
    memset(nr_room, 0, sizeof nr_room);
    memset(nr_rom, 0, sizeof nr_rom);

    TA_SET(ta_block, TA_DICTSEARCH, ds_block);
    TA_SET(ta_block, TA_OWNER, nr_rom);
    DS_SET_OWNER(ds_block, ta_block);
    NR_SET_OWNER(nr_room, ta_block);
    *(int32_t *)(nr_rom + RZ_SPELL_ENGLISH) = spell;
    *(uint16_t *)(nr_rom + RZ_NUMBER_MODE) = (uint16_t)mode;
}

/* One of a reading's five pairs, which the fixtures set before a call so
   that a rule adjusting one shows. */
static void rd_pair_a(uint8_t *rd, int i, int v)
{
    *(int16_t *)(rd + RD_A + i * RD_PAIR_SIZE) = (int16_t)v;
}

/* One reading and one substring printed whole, which is how a field written
   to the wrong place shows even where no answer changes. */
static void nrShow(const char *what, long a, long b, long c)
{
    int i, k;

    printf("NR %s %ld %ld %ld", what, a, b, c);
    for (i = 0; i < 3; i++) {
        const uint8_t *rd = NR_READ_AT(nr_room, i);

        putchar(' ');
        for (k = 0; k < NR_READ_SIZE; k++)
            printf("%02x", (unsigned)rd[k]);
    }
    putchar('\n');
}

static void nrShowSubstr(const char *what, long a, long b, int howmany)
{
    int i, k;

    printf("NR %s %ld %ld %d", what, a, b, howmany);
    for (i = 0; i < 4 && i < NR_SUBSTR_HALF; i++) {
        const uint8_t *ss = NR_SUBSTR_AT(nr_room, NR_SUBSTR_HALF + i);

        putchar(' ');
        for (k = 0; k < NR_SUBSTR_SIZE; k++)
            printf("%02x", (unsigned)ss[k]);
    }
    printf(" %02x", (unsigned)(uint8_t)nr_room[NR_COUNT]);
    putchar('\n');
}

static void sweepNumRead(void)
{
    long i, j, c;
    int  k;

    nrSetUp(0, 0);

    /* Init over a record filled with a pattern: what it clears and what it
       leaves both show, including the last byte of each run that IBM does
       not reach. */
    for (i = 0; i < NR_BYTES; i++)
        nr_room[i] = (char)(0xa5 ^ (i & 0xff));
    NR_SET_OWNER(nr_room, ta_block);
    NR(Init)(nr_room);
    printf("NR init");
    /* From the readings on: the four bytes in front of them are the owner
       on IBM's side and pattern on ours, so printing them would compare an
       address with a fill byte. */
    for (i = NR_READ; i < NR_BYTES; i++)
        printf("%s%02x", ((i - NR_READ) % 32) ? "" : "\n  ",
               (unsigned)(uint8_t)nr_room[i]);
    putchar('\n');

    /* The four reading roads. A substring is built by hand for each: every
       kind, every count the record holds, and every code GenerateStdForm can
       put in one.
     *
     * Nineteen codes and not the twenty-six m_sanTCodes names, because SINDX
     * is twenty long and KetaYomi reads it at the code and at the code plus
     * one. A code of twenty or more indexes past that table on both sides,
     * and what each finds after it is its own linker's business rather than
     * the engine's -- so the sweep stays inside what the standard form can
     * actually hold, which is nought to eighteen. */
    for (i = 0; i < 4; i++)
        for (j = 1; j <= SS_CODES_N; j++)
            for (c = 0; c < 0x13; c++) {
                int16_t  got[NR_ANSWER_N];
                uint8_t *ss;
                uint8_t *two;
                int16_t  n;

                nrSetUp(0, 0);
                NR(Init)(nr_room);
                for (k = 0; k < NR_ANSWER_N; k++)
                    got[k] = 1;

                ss  = NR_SUBSTR_AT(nr_room, NR_SUBSTR_HALF);
                two = NR_SUBSTR_AT(nr_room, NR_SUBSTR_HALF + 1);
                SS_B8(ss, SS_COUNT) = (uint8_t)j;
                for (k = 0; k < (int)j; k++) {
                    SS_B8(ss, SS_CODES + k) = (uint8_t)((c + k) % 0x13);
                    SS_B8(ss, SS_MORE + k)  = (uint8_t)(k + 1);
                }
                SS_S16(ss, SS_FROM) = 0;
                SS_S16(ss, SS_TO)   = (int16_t)j;
                SS_B8(two, SS_COUNT) = (uint8_t)(j % 3);
                for (k = 0; k < NR_READ_N; k++) {
                    rd_pair_a(NR_READ_AT(nr_room, k), 0, 5);
                    RD_B8(NR_READ_AT(nr_room, k), RD_COUNT) = 4;
                }

                if (i == 0)
                    n = NR(ApplySRuleToKetaYomi)(nr_room, 1, 0, 0, got, ss);
                else if (i == 1)
                    n = NR(ApplySRuleToBouYomi)(nr_room, 0, 0, got, ss);
                else if (i == 2)
                    n = NR(ApplySRuleToShosu)(nr_room, 1, 0, got, ss, two);
                else
                    n = NR(ApplySRuleToBunsu)(nr_room, 0, 0, got, ss);
                printf("NR road %ld %ld %ld %d %d %d %d\n", i, j, c,
                       (int)n, (int)got[0], (int)got[1], (int)got[2]);
                nrShow("after", i, j, c);
            }


    /* Every adjacent pair of codes, which the sweep above cannot reach: its
       codes run consecutively, so a pair like "eighteen then sixteen" never
       occurs, and several of the tests in the by-place road read the code
       before the one they are on. */
    for (i = 0; i < 4; i++)
        for (j = 0; j < 0x13; j++)
            for (c = 0; c < 0x13; c++) {
                int16_t  got[NR_ANSWER_N];
                uint8_t *ss;
                uint8_t *two;
                int16_t  n;
                long     len;

                for (len = 2; len <= 6; len += 2) {
                    nrSetUp(0, 0);
                    NR(Init)(nr_room);
                    for (k = 0; k < NR_ANSWER_N; k++)
                        got[k] = 1;

                    ss  = NR_SUBSTR_AT(nr_room, NR_SUBSTR_HALF);
                    two = NR_SUBSTR_AT(nr_room, NR_SUBSTR_HALF + 1);
                    SS_B8(ss, SS_COUNT) = (uint8_t)len;
                    for (k = 0; k < (int)len; k++) {
                        SS_B8(ss, SS_CODES + k) =
                            (uint8_t)((k & 1) ? c : j);
                        SS_B8(ss, SS_MORE + k) = (uint8_t)(k + 1);
                    }
                    SS_S16(ss, SS_FROM) = 0;
                    SS_S16(ss, SS_TO)   = (int16_t)len;
                    SS_B8(two, SS_COUNT) = (uint8_t)(len % 3);
                    for (k = 0; k < NR_READ_N; k++) {
                        rd_pair_a(NR_READ_AT(nr_room, k), 0, 5);
                        RD_B8(NR_READ_AT(nr_room, k), RD_COUNT) = 4;
                    }

                    if (i == 0)
                        n = NR(ApplySRuleToKetaYomi)(nr_room, 1, 0, 0,
                                                     got, ss);
                    else if (i == 1)
                        n = NR(ApplySRuleToBouYomi)(nr_room, 0, 0, got, ss);
                    else if (i == 2)
                        n = NR(ApplySRuleToShosu)(nr_room, 1, 0, got, ss,
                                                  two);
                    else
                        n = NR(ApplySRuleToBunsu)(nr_room, 0, 0, got, ss);
                    printf("NR pair %ld %ld %ld %ld %d %d\n", i, j, c, len,
                           (int)n, (int)got[0]);
                    nrShow("pairs", i * 100 + j, c, len);
                }
            }

    /* And over the whole of the JCC table, which is what the counter's own
       accent field indexes: forty of the five hundred and nineteen rows it
       holds left three of the rules in the second and third pass unreached,
       so a handful of the parts of speech that are counters at all get every
       row instead. Which parts of speech those are was measured rather than
       guessed: of 256, forty-six carry the bit. */
    {
        static const int COUNTERS[6] = { 124, 137, 150, 161, 174, 188 };
        long x;

        for (x = 0; x < 6; x++)
            for (j = 1; j < 519; j++) {
                int      codes[8];
                int16_t  got[NR_ANSWER_N];
                int16_t  n;

                nrSetUp(0, 0);
                NR(Init)(nr_room);
                for (k = 0; k < NR_ANSWER_N; k++)
                    got[k] = 1;
                /* The number itself has to vary as well as the counter: the
                   rule is indexed by the last code of the standard form as
                   well as by the counter's own row, and a number of one
                   fixed shape reaches one column of the table. */
                for (k = 0; k < 5; k++)
                    codes[k] = (int)((j * 3 + k * 7) % 0x13);
                codes[0] = (int)(j % 10);
                nrDigits(codes, (int)(1 + j % 5));
                nrWord((int)(1 + j % 5), 2, COUNTERS[x], (int)j, codes, 2);
                for (k = 0; k < NR_READ_N; k++) {
                    rd_pair_a(NR_READ_AT(nr_room, k), 0, 6);
                    RD_B8(NR_READ_AT(nr_room, k), RD_COUNT) = 5;
                    RD_B8(NR_READ_AT(nr_room, k), RD_CODES + 3) = 0x31;
                    RD_B8(NR_READ_AT(nr_room, k), RD_CODES + 4) = 0x42;
                }
                n = NR(SegmentYomiBlock)(nr_room, nr_word, 0);
                n = NR(SetYomiType)(nr_room, n);
                NR(GenerateStdForm)(nr_room, n);
                printf("NR rows %ld %ld %d\n", x, j,
                       (int)NR(ApplyJRule)(nr_room, nr_word, 0, 0, n, got));
                nrShow("rowed", x, j, 0);
            }
    }

    /* The counter word, over every part of speech: the sweep above never
       reached ApplyJRule at all, because none of the two dozen parts of
       speech it used has the type-group bit that says a word is a counter,
       so the method refused on its first line every time and five sabotages
       of it moved nothing. */
    for (i = 0; i < 256; i++)
        for (j = 1; j <= 40; j++)
            for (c = 0; c < 2; c++) {
                int      codes[8];
                int16_t  got[NR_ANSWER_N];
                int16_t  n;

                nrSetUp(0, 0);
                NR(Init)(nr_room);
                for (k = 0; k < NR_ANSWER_N; k++)
                    got[k] = 1;
                for (k = 0; k < 4; k++)
                    codes[k] = (int)((j + k) % 10);
                nrDigits(codes, 4);
                nrWord(3, 2, (int)i, (int)j, codes, 2);
                for (k = 0; k < NR_READ_N; k++) {
                    rd_pair_a(NR_READ_AT(nr_room, k), 0, 6);
                    RD_B8(NR_READ_AT(nr_room, k), RD_COUNT) = 5;
                    RD_B8(NR_READ_AT(nr_room, k), RD_CODES + 3) =
                        (uint8_t)(0x30 + c);
                    RD_B8(NR_READ_AT(nr_room, k), RD_CODES + 4) =
                        (uint8_t)(0x40 + c);
                }
                n = NR(SegmentYomiBlock)(nr_room, nr_word, 0);
                n = NR(SetYomiType)(nr_room, n);
                NR(GenerateStdForm)(nr_room, n);
                printf("NR join %ld %ld %ld %d\n", i, j, c,
                       (int)NR(ApplyJRule)(nr_room, nr_word, 0, 0, n, got));
                nrShow("joined", i * 100 + j, c, 0);
                printf("NR rec %ld %ld %ld", i, j, c);
                for (k = 0; k < 16; k++)
                    printf("%02x", (unsigned)(uint8_t)ds_block[DS_REC + k]);
                putchar('\n');
            }

    /* The three passes that cut a number up and rewrite it, over numbers
       built as digit strings. */
    for (i = 1; i <= 20; i++)
        for (j = 0; j < 24; j++)
            for (c = 0; c < 2; c++) {
                int      codes[24];
                int16_t  howmany;

                nrSetUp((int)c, 0);
                NR(Init)(nr_room);
                for (k = 0; k < (int)i; k++)
                    codes[k] = (int)((j + k * 7) % 0x1c);
                nrDigits(codes, (int)i);
                nrWord((int)i, (int)i, 0x30, 1, codes, 0);

                howmany = NR(SegmentYomiBlock)(nr_room, nr_word, 0);
                nrShowSubstr("cut", i * 100 + j, c, (int)howmany);
                howmany = NR(SetYomiType)(nr_room, howmany);
                nrShowSubstr("kind", i * 100 + j, c, (int)howmany);
                NR(GenerateStdForm)(nr_room, howmany);
                nrShowSubstr("form", i * 100 + j, c, (int)howmany);
            }

    /* And the whole of it, over every number the harness can write and a
       counter word after each.
     *
     * The first code is always a digit, which is what a number begins with.
     * A standard form beginning with anything else asks the first reading
     * for a pair it has not got, and IBM then reads the second half of its
     * own owner pointer as how many codes that reading holds -- 131, into a
     * field of twenty-two. The record goes and so does the harness driving
     * it: the run stops with its own loop counter smashed and no final line.
     * rom/jajp/numread.c says what ours does there instead, and
     * docs/status.md lists it with the other deliberate divergences. Only
     * the first reading is affected; every later one asks in front of the
     * reading before it, which both engines have and hold the same.
     *
     * And thirteen codes rather than twenty, for the second half of the same
     * defect: a longer number asks for more than the eight readings IBM's
     * `Do' keeps on its stack, and it then reads and writes its caller's
     * frame. Fourteen codes is where the sweep first reaches nine readings
     * and where IBM's run stops. The three passes above go to twenty, since
     * none of them touches that array. */
    for (i = 1; i <= 13; i++)
        for (j = 0; j < 24; j++)
            for (c = 0; c < 4; c++) {
                int     codes[24];
                int16_t word = 0;
                int16_t out = 0;
                int16_t n;

                nrSetUp((int)(c & 1), (int)(c >> 1) * 2);
                for (k = 0; k < (int)i; k++)
                    codes[k] = (int)((j + k * 5) % 0x1c);
                codes[0] = (int)(j % 10);
                nrDigits(codes, (int)i);
                nrWord((int)i, (int)i, (int)(0x30 + j), (int)(1 + j % 8),
                       codes, 1);

                n = NR(Do)(nr_room, nr_word, &word, &out);
                printf("NR do %ld %ld %ld %d %d %d", i, j, c,
                       (int)n, (int)word, (int)out);
                for (k = 0; k < NR_ANSWER_N; k++)
                    printf(" %d", (int)NR_ANSWER_AT(nr_room, k));
                putchar('\n');
                nrShow("said", i * 100 + j, c, 0);
                nrShowSubstr("left", i * 100 + j, c, 0);
            }


    /* Six shapes written out by hand rather than swept, each aimed at a road
       the sweeps above never take. A number with one decimal marker and one
       fraction marker in it, so that the count of substrings read as more
       than a scale is two rather than one and the fraction road is refused.
       A digit either side of a myriad marker, which is what makes a scale
       stand alone and is the only thing that reaches the rewrite of the two
       codes before it. And a marker at the end, which is what makes the walk
       backwards in SegmentYomiBlock ask whether the digits group in fours. */
    {
        static const int SHAPES[6][8] = {
            { 1, 0x13, 2, 0x15, 3, -1 },
            { 4, 0x18, 5, -1 },
            { 7, 0x18, 8, -1 },
            { 1, 2, 3, 4, 0x18, 5, 6, -1 },
            { 1, 2, 3, 4, 0x1b, 5, -1 },
            { 2, 0x14, 3, 0x1a, 4, -1 },
        };
        /* One more, and it is the only shape that makes the walk backwards
           in SegmentYomiBlock decide anything. Three conditions have to hold
           at once: fewer than four digits in front of the marker, so the
           scan backwards does not stop on its own; a whole group of four
           behind it, so the length test does not either; and nothing but
           digits after it. A marker at the fourth character of seven is the
           smallest that does all three. */
        static const int FOURS[9] = { 1, 2, 3, 0x18, 4, 5, 6, -1 };
        long x;

        for (x = 0; x < 7; x++)
            for (c = 0; c < 2; c++) {
                int     codes[9];
                int16_t word = 0;
                int16_t out = 0;
                int16_t nn;
                int     len = 0;

                {
                    const int *from = (x == 6) ? FOURS : SHAPES[x];

                    while (from[len] >= 0)
                        len++;
                    for (k = 0; k < len; k++)
                        codes[k] = from[k];
                }
                nrSetUp((int)c, 0);
                nrDigits(codes, len);
                nrWord(len, len, 0x7c, 3, codes, 1);
                nn = NR(Do)(nr_room, nr_word, &word, &out);
                printf("NR shape %ld %ld %d %d %d\n", x, c,
                       (int)nn, (int)word, (int)out);
                nrShow("shaped", x, c, 0);
                nrShowSubstr("shapecut", x, c, 0);
            }
    }

    printf("NR done\n");
}

/* ---- IntonPhrase ------------------------------------------------------ */

/* The record, the rows it is handed, and one group phrase on its own.
 *
 * The rows have to be one array at IBM's own row stride. That is how
 * TextAnalysis holds them, and it is what our build's chain link needs: the
 * link cannot hold a pointer on a build where a pointer is eight bytes wide,
 * because the row's own index sits four bytes behind it, so it holds how many
 * rows forward the next one is instead. rom/jajp/intonphrase.h says so. IBM's
 * side is thirty-two bit and keeps the pointer.
 *
 * Which means no digest may include those four bytes, and none below does.
 * What the chain says is checked by walking it and printing which row or
 * group comes next, which is the same number on both sides. */
#define IP_ROW_N 14
static char ip_room[IP_ROOM];
static char ip_rows[IP_ROW_N * PT_ROW_SIZE];
static char ip_one[PT_ROW_SIZE];
static char ip_phrase[IG_PHRASE_SIZE + 8];
static char ip_rom[RZ_ROOM];

#define IP_ROW(i) (ip_rows + (long)(i) * PT_ROW_SIZE)

#ifdef EVV_ROMPRIMS_OURS
#define IP_SET(blk, which, p)  (*(void **)((blk) + which##_AT) = (p))
#define IP_GET(blk, which)     (*(void **)((blk) + which##_AT))
#define RZ_SET_TXTANAL(b, p)   (*(void **)((b) + RZ_TXTANAL_AT) = (p))
#define IP_ROW_LINK(t, p)      PT_NEXT_SET((t), (p))
#define IP_ROW_NEXT(t)         PT_NEXT_OF(t)
#define IP_GROUP_NEXT(g)       IG_NEXT_OF(g)
#else
#define IP_SET(blk, which, p)  (*(void **)((blk) + which) = (p))
#define IP_GET(blk, which)     (*(void **)((blk) + which))
#define RZ_SET_TXTANAL(b, p)   (*(void **)((b) + RZ_TXTANAL) = (p))
#define IP_ROW_LINK(t, p)      (*(void **)(t) = (p))
#define IP_ROW_NEXT(t)         (*(void **)(t))
#define IP_GROUP_NEXT(g)       (*(void **)(g))
#endif

#define IPB(p, off)   (*((uint8_t *)(p) + (off)))
#define IPS(p, off)   (*(int16_t *)((uint8_t *)(p) + (off)))

/* A region digested, so that a sweep too large to print a line a call still
   says which case moved. */
static unsigned long ipDigest(const void *p, long n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t       h = 2166136261u;
    long           i;

    for (i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return (unsigned long)h;
}

/* Every row but its link, which is the one field the two sides spell
   differently. */
static unsigned long ipRowsDigest(int n)
{
    uint32_t h = 2166136261u;
    int      i;
    long     k;

    for (i = 0; i < n; i++) {
        const uint8_t *t = (const uint8_t *)IP_ROW(i);

        for (k = 4; k < PT_ROW_SIZE; k++)
            h = (h ^ t[k]) * 16777619u;
    }
    return (unsigned long)h;
}

static uint32_t ipNext(uint32_t *s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

/* One phrase-table row, filled from a seed. Every field IntonPhrase reads is
   set, and the shapes are the ones the passes turn on: a mora run that stops
   at a nought, a reading of exactly as many codes as the moras add up to and
   with a chosen last one, and a long run terminated by minus one. The reading
   is capped at twelve codes so that three phrases of a group cross the thirty
   a group phrase holds. */
static void ipRow(int i, uint32_t seed, int lowMoras, int hiMoras,
                  int group, int kind, int tailCode)
{
    uint8_t *t = (uint8_t *)IP_ROW(i);
    int      k;
    int      n = 0;

    memset(t, 0, PT_ROW_SIZE);
    IPS(t, PT_INDEX) = (int16_t)i;
    for (k = 0; k < 3; k++)
        t[PT_STATE + k] = (uint8_t)ipNext(&seed);
    t[PT_GROUP] = (uint8_t)group;
    t[PT_KIND]  = (uint8_t)kind;
    t[PT_HOLD]  = (uint8_t)(ipNext(&seed) & 7);
    for (k = 0; k < 4; k++) {
        t[PT_LEFT + k]  = (uint8_t)ipNext(&seed);
        t[PT_RIGHT + k] = (uint8_t)ipNext(&seed);
    }
    for (k = 0; k < PT_MORA_N; k++) {
        t[PT_MORA + k] =
            (uint8_t)(k < lowMoras ? 1 + (ipNext(&seed) & 3) : 0);
        t[PT_MORA_ACC + k]   = (uint8_t)(ipNext(&seed) & 0x0f);
        t[PT_MORA_PITCH + k] = (uint8_t)(ipNext(&seed) & 7);
        t[PT_MORA_MARK + k]  = (uint8_t)(ipNext(&seed) & 3);
        IPS(t, PT_MORA_VAL + k * 2) = (int16_t)(ipNext(&seed) & 0x3f);
        t[PT_MORA_HI + k] =
            (uint8_t)(k < hiMoras ? 1 + (ipNext(&seed) & 3) : 0);
        t[PT_MORA_HI_ACC + k]   = (uint8_t)(ipNext(&seed) & 0x0f);
        t[PT_MORA_HI_PITCH + k] = (uint8_t)(ipNext(&seed) & 7);
        t[PT_MORA_HI_MARK + k]  = (uint8_t)(ipNext(&seed) & 3);
        IPS(t, PT_MORA_HI_VAL + k * 2) = (int16_t)(ipNext(&seed) & 0x3f);
    }
    for (k = 0; k < PT_MORA_N; k++)
        n += t[PT_MORA + k] + t[PT_MORA_HI + k];
    if (n > 12)
        n = 12;
    if (n < 1)
        n = 1;
    for (k = 0; k < n; k++)
        t[PT_KANA + k] = (uint8_t)(0x20 + ipNext(&seed) % 0x60);
    t[PT_KANA + n - 1] = (uint8_t)tailCode;
    t[PT_MORAS] = (uint8_t)n;
    for (k = 0; k < IH_E_N; k++) {
        int on = k < 4 && (ipNext(&seed) & 3) != 0;

        IPS(t, PT_LONG + k * 2) = (int16_t)(on ? (int)(ipNext(&seed) & 0x1f)
                                               : -1);
        t[PT_LONG_B + k] = (uint8_t)ipNext(&seed);
    }
}

/* A free list over n entries, laid out as either class's own InitPhraseTable
   leaves it. TextAnalysis's is written here rather than called, because that
   class is not transcribed and the pass gives rows back to its list. */
static void ipLinkTable(uint8_t *link, int n)
{
    int i;

    *(uint16_t *)(link + IL_PREV) = (uint16_t)n;
    *(uint16_t *)(link + IL_NEXT) = 1;
    for (i = 1; i < n - 1; i++) {
        *(uint16_t *)(link + i * IP_LINK_SIZE + IL_PREV) = (uint16_t)(i - 1);
        *(uint16_t *)(link + i * IP_LINK_SIZE + IL_NEXT) = (uint16_t)(i + 1);
    }
    *(uint16_t *)(link + (n - 1) * IP_LINK_SIZE + IL_PREV) = (uint16_t)(n - 2);
    *(uint16_t *)(link + (n - 1) * IP_LINK_SIZE + IL_NEXT) = (uint16_t)n;
}

/* Which group marks and kinds a shape gives its rows.
 *
 * Twenty is what closes a breath group and ten what closes a phrase inside
 * one, so a shape has to carry both to reach either. Three eights of the
 * pattern are what the first round of sabotages asked for and none of them
 * was a statement about the code. The middle eight close almost nothing,
 * which is the only way the second regrouping pass gets a run over its limit
 * of twenty-five moras to break; with a closer every other row no run is ever
 * long enough and three sabotages of that pass moved nothing. The last eight
 * close three phrases in a row, which is the only way a group fills all three
 * of the phrases it holds and the only way the test on that bound is reached.
 *
 * The kinds have to reach four, because `PhraseSeparate' widens its target
 * where the group's last phrase has been broken four times already, and
 * `SetPauseLength' gives a whole second of pause to a group of four or more.
 * With a highest kind of three both were unswept. */
static const int IP_GROUPS[24] = {
    0x14, 0x0a, 0, 1, 0x0b, 0x14, 4, 0x0a,
    0, 1, 2, 0x0b, 0, 1, 3, 0x14,
    0x0a, 0x0a, 0x0a, 0x14, 0, 0, 0x0a, 0x14
};
static const int IP_KINDS[24]  = {
    1, 1, 1, 2, 1, 0, 1, 3,
    1, 1, 4, 1, 5, 1, 1, 4,
    1, 1, 1, 4, 1, 1, 1, 5
};
static const int IP_TAILS[4]  = { 0x50, 0xfd, 0xf0, 0xfe };
#define IP_SHIFT_N 24

static void ipSetUp(int n, uint32_t seed, int shift)
{
    int i;

    memset(ip_room, 0, sizeof ip_room);
    memset(ip_rows, 0, sizeof ip_rows);
    memset(ip_rom, 0, sizeof ip_rom);
    memset(ta_block, 0, sizeof ta_block);

    for (i = 0; i < n; i++)
        ipRow(i, seed + (uint32_t)i * 7919u,
              1 + (int)((seed >> (i & 7)) % 9),
              (int)((seed >> (i & 5)) % 4),
              IP_GROUPS[(i + shift) % IP_SHIFT_N],
              IP_KINDS[(i + shift) % IP_SHIFT_N],
              IP_TAILS[(i + shift) & 3]);
    for (i = 0; i < n; i++)
        IP_ROW_LINK(IP_ROW(i), i + 1 < n ? IP_ROW(i + 1) : NULL);

    ipLinkTable((uint8_t *)ta_block + TA_LINK, TA_LINK_N);
    *(uint16_t *)(ta_block + TA_LAST)     = (uint16_t)TA_LINK_N;
    *(uint16_t *)(ta_block + TA_SPARE_18) = (uint16_t)TA_LINK_N;
    *(uint16_t *)(ta_block + TA_TOP)      = 0;

    RZ_SET_TXTANAL(ip_rom, ta_block);
    IP_SET(ip_room, IP_OWNER, ip_rom);
    IP_SET(ip_room, IP_TABLE, IP_ROW(0));
    IP_SET(ip_room, IP_HEAD, NULL);
    IP_SET(ip_room, IP_CUR, NULL);
    IPM(InitPhraseTable)(ip_room, IP_LINK_N);
    IPS(ip_room, IP_MORE) = (int16_t)(0x20 + (seed & 0x3f));
}

/* Which row or group a pointer names, so that a chain can be printed without
   printing an address. */
static long ipWhichRow(const void *t)
{
    if (t == NULL)
        return -1;
    return (long)(((const char *)t - ip_rows) / PT_ROW_SIZE);
}

static long ipWhichGroup(const void *g)
{
    if (g == NULL)
        return -1;
    return (long)(((const char *)g - (ip_room + IP_GROUP)) / IP_GROUP_SIZE);
}

/* Everything a pass leaves behind: the rows, the chain of them, the four
   counters, and every group with its three phrases. Printed whole for the
   shapes the sweep names and digested for the rest. */
static void ipReport(const char *tag, long a, int n)
{
    const void *g;
    int         i, k, guard;

    printf("IP rows %s %ld %d %08lx\n", tag, a, n,
           (unsigned long)ipRowsDigest(n));
    for (i = 0; i < n; i++) {
        const uint8_t *r = (const uint8_t *)IP_ROW(i);

        printf("IP row %s %ld %d %d %d %d %d %d %d %d %ld", tag, a, i,
               (int)IPS(r, PT_INDEX), (int)r[PT_STATE],
               (int)r[PT_STATE + 1], (int)r[PT_STATE + 2],
               (int)r[PT_GROUP], (int)r[PT_KIND], (int)r[PT_MORAS],
               ipWhichRow(IP_ROW_NEXT((void *)r)));
        for (k = 0; k < PT_MORA_N; k++)
            printf(" %02x%02x%02x%02x", (unsigned)r[PT_MORA + k],
                   (unsigned)r[PT_MORA_ACC + k],
                   (unsigned)r[PT_MORA_PITCH + k],
                   (unsigned)r[PT_MORA_MARK + k]);
        for (k = 0; k < PT_MORA_N; k++)
            printf(" %02x%02x", (unsigned)r[PT_MORA_HI_PITCH + k],
                   (unsigned)r[PT_MORA_HI_MARK + k]);
        putchar('\n');
    }

    printf("IP state %s %ld %d %d %d %d %d %ld %ld %ld\n", tag, a,
           (int)IPS(ip_room, IP_COUNT), (int)IPS(ip_room, IP_LEFT),
           (int)IPS(ip_room, IP_AT), (int)IPS(ip_room, IP_TOP),
           (int)IPS(ip_room, IP_MORE),
           ipWhichRow(IP_GET(ip_room, IP_TABLE)),
           ipWhichGroup(IP_GET(ip_room, IP_HEAD)),
           ipWhichGroup(IP_GET(ip_room, IP_CUR)));
    printf("IP inton %s %ld %d\n", tag, a,
           (int)IPS(ta_block, TA_INTON_FAILED));
    printf("IP free %s %ld %d %d %d\n", tag, a,
           (int)*(uint16_t *)(ta_block + TA_LAST),
           (int)*(uint16_t *)(ta_block + TA_SPARE_18),
           (int)*(uint16_t *)(ta_block + TA_TOP));

    g = IP_GET(ip_room, IP_HEAD);
    for (guard = 0; g != NULL && guard < 24; guard++) {
        const uint8_t *bg = (const uint8_t *)g;

        printf("IP group %s %ld %d %ld %d %d %d %d %d %d %d %ld\n", tag, a,
               guard, ipWhichGroup(g), (int)IPS(bg, IG_INDEX),
               (int)bg[IG_PHRASES], (int)bg[IG_LEVEL], (int)bg[IG_LEFT],
               (int)bg[IG_RIGHT], (int)bg[IG_KIND], (int)IPS(bg, IG_PAUSE),
               ipWhichGroup(IP_GROUP_NEXT((void *)bg)));
        for (i = 0; i < IG_PHRASE_N; i++) {
            const uint8_t *ph = bg + IG_PHRASE + i * IG_PHRASE_SIZE;

            printf("IP phrase %s %ld %d %d %d %d %d %d %d", tag, a, guard, i,
                   (int)ph[IH_FIRST], (int)ph[IH_COUNT],
                   (int)ph[IH_KANA_LEN], (int)ph[IH_FLAG],
                   (int)IPS(ph, IH_LONG_N));
            for (k = 0; k < IH_RUN_N; k++)
                printf(" %02x%02x%02x%02x%02x%04x", (unsigned)ph[IH_A + k],
                       (unsigned)ph[IH_MORAS + k], (unsigned)ph[IH_LEN + k],
                       (unsigned)ph[IH_PITCH + k], (unsigned)ph[IH_MARK + k],
                       (unsigned)(uint16_t)IPS(ph, IH_VAL + k * 2));
            printf(" |");
            for (k = 0; k < IH_E_N; k++)
                printf("%02x", (unsigned)ph[IH_KANA + k]);
            printf(" |");
            for (k = 0; k < 8; k++)
                printf(" %04x%02x", (unsigned)(uint16_t)IPS(ph, IH_E + k * 2),
                       (unsigned)ph[IH_F + k]);
            putchar('\n');
        }
        g = IP_GROUP_NEXT((void *)bg);
    }
}

/* And the same digested: one line a shape, over everything a pass can have
   touched. The link words are left out of both. */
static void ipDigestReport(const char *tag, long a, int n)
{
    const void *g;
    uint32_t    h = 2166136261u;
    int         guard;
    long        k;

    g = IP_GET(ip_room, IP_HEAD);
    for (guard = 0; g != NULL && guard < 32; guard++) {
        const uint8_t *bg = (const uint8_t *)g;

        for (k = 4; k < IP_GROUP_SIZE; k++)
            h = (h ^ bg[k]) * 16777619u;
        h = (h ^ (uint32_t)ipWhichGroup(g)) * 16777619u;
        g = IP_GROUP_NEXT((void *)bg);
    }
    printf("IP digest %s %ld %d %08lx %08lx %08lx %d %d %d %d %d\n", tag, a, n,
           ipRowsDigest(n), (unsigned long)h,
           ipDigest(ip_room + IP_LINK, IP_LINK_N * IP_LINK_SIZE),
           (int)IPS(ip_room, IP_COUNT), (int)IPS(ip_room, IP_LEFT),
           (int)IPS(ip_room, IP_AT), (int)IPS(ip_room, IP_TOP),
           (int)IPS(ta_block, TA_INTON_FAILED));
}

static void sweepIntonPhrase(void)
{
    long     i, j, k, m;
    uint32_t seed;
    uint32_t roll;

    /* ---- the leaves, exhaustively ----------------------------------- */

    /* Which of the seven states each becomes. */
    for (i = 0; i < 256; i++)
        printf("IP ptype %ld %d\n", i,
               (int)IPM(ModifyPType)(ip_room, (uint8_t)i));

    /* Whether the code at a place in a reading is a long vowel: every place
       a mora index can reach and every code that can be there. */
    roll = 2166136261u;
    for (i = 0; i <= 40; i++)
        for (j = 0; j < 256; j++) {
            memset(ip_one, 0, sizeof ip_one);
            ip_one[PT_KANA + i] = (char)j;
            roll = (roll
                    ^ (uint32_t)IPM(CheckChoon)(ip_room,
                                                (const uint8_t *)ip_one,
                                                (int16_t)i)) * 16777619u;
        }
    printf("IP choon %08lx\n", (unsigned long)roll);

    /* A phrase's three state bytes tidied: every pair of the first two over
       four values of the third, which is every conjunction the function can
       see. */
    roll = 2166136261u;
    for (k = 0; k < 4; k++) {
        static const int third[4] = { 0, 1, 0x80, 0xff };

        for (i = 0; i < 256; i++)
            for (j = 0; j < 256; j++) {
                uint8_t st[4];

                st[0] = (uint8_t)i;
                st[1] = (uint8_t)j;
                st[2] = (uint8_t)third[k];
                st[3] = 0;
                IPM(CheckPhraseToPhrase)(ip_room, st);
                roll = (roll ^ st[0]) * 16777619u;
                roll = (roll ^ st[1]) * 16777619u;
                roll = (roll ^ st[2]) * 16777619u;
                roll = (roll ^ st[3]) * 16777619u;
            }
    }
    printf("IP tophrase %08lx\n", (unsigned long)roll);

    /* Which breath group a phrase belongs to. The three bytes are rewritten
       where the kind is more than one, so the sweep digests them after as
       well as the answer. */
    roll = 2166136261u;
    for (k = 0; k < 4; k++)
        for (i = 0; i < 256; i++)
            for (j = 0; j < 256; j++) {
                uint8_t st[3];
                uint8_t right[4];
                int     v;

                st[0] = (uint8_t)i;
                st[1] = (uint8_t)j;
                st[2] = (uint8_t)(i ^ j);
                right[0] = (uint8_t)j;
                right[1] = (uint8_t)i;
                right[2] = 0x40;
                right[3] = 0x20;
                v = IPM(CheckBreathGroup)(ip_room, st, right, (uint8_t)k);
                roll = (roll ^ (uint32_t)v) * 16777619u;
                roll = (roll ^ st[0]) * 16777619u;
                roll = (roll ^ st[1]) * 16777619u;
                roll = (roll ^ st[2]) * 16777619u;
            }
    printf("IP breathgroup %08lx\n", (unsigned long)roll);

    /* What lies between two phrases. Eight bytes, a group number and a flag
       decide it, and the eight bytes conjoin: three sweeps, so that no
       conjunction of two of them is left to chance. First every pair of the
       two heads. */
    roll = 2166136261u;
    for (k = 0; k < 2; k++)
        for (i = 0; i < 256; i++)
            for (j = 0; j < 256; j++) {
                uint8_t l[4];
                uint8_t r[4];

                l[0] = (uint8_t)i;
                l[1] = 0xff;
                l[2] = (uint8_t)(k ? 0x91 : 0x00);
                l[3] = (uint8_t)(k ? 0xa8 : 0x28);
                r[0] = (uint8_t)j;
                r[1] = (uint8_t)(j ^ 0x55);
                r[2] = 0x0f;
                r[3] = 0x40;
                roll = (roll ^ IPM(PhraseParsing)(ip_room, l, r,
                                                  (uint8_t)(i & 0x7f),
                                                  (int32_t)k)) * 16777619u;
                roll = (roll ^ r[0]) * 16777619u;
            }
    printf("IP parsing heads %08lx\n", (unsigned long)roll);

    /* Then every group number against every value of the byte that chooses
       what kind of boundary it is. */
    roll = 2166136261u;
    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++)
            for (k = 0; k < 2; k++) {
                uint8_t l[4];
                uint8_t r[4];

                l[0] = 0xfc;
                l[1] = 0xff;
                l[2] = (uint8_t)(k ? 0x90 : 0x00);
                l[3] = (uint8_t)j;
                r[0] = 0xfc;
                r[1] = 0xfc;
                r[2] = 0xff;
                r[3] = 0x40;
                roll = (roll ^ IPM(PhraseParsing)(ip_room, l, r, (uint8_t)i,
                                                  0)) * 16777619u;
            }
    printf("IP parsing groups %08lx\n", (unsigned long)roll);

    /* And then the whole input drawn at once, so that a conjunction of three
       bytes cannot hide either. The draw favours the values the function
       tests for and is uniform the rest of the time. */
    roll = 2166136261u;
    seed = 0x51ed2701u;
    for (i = 0; i < 400000; i++) {
        static const uint8_t pool[16] = {
            0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x21,
            0x40, 0x80, 0x88, 0xa8, 0xfc, 0xff, 0x91, 0xe4
        };
        uint8_t l[4];
        uint8_t r[4];
        int     b;

        for (b = 0; b < 4; b++) {
            uint32_t v = ipNext(&seed);

            l[b] = (uint8_t)((v & 1) ? pool[(v >> 1) & 15] : (v >> 5));
            v = ipNext(&seed);
            r[b] = (uint8_t)((v & 1) ? pool[(v >> 1) & 15] : (v >> 5));
        }
        j = (long)(ipNext(&seed) & 0x7f);
        if ((ipNext(&seed) & 1) != 0)
            j = (long)(0x10 + ipNext(&seed) % 0x50);
        roll = (roll ^ IPM(PhraseParsing)(ip_room, l, r, (uint8_t)j,
                                          (int32_t)(ipNext(&seed) & 1)))
               * 16777619u;
        roll = (roll ^ r[0]) * 16777619u;
    }
    printf("IP parsing draws %08lx\n", (unsigned long)roll);

    /* ---- the free list ---------------------------------------------- */

    /* A pool small enough to run out, allocated dry and then walked past the
       end, which is the one answer TableAllocBG has besides an index. */
    for (m = 2; m <= 6; m++) {
        memset(ip_room, 0, sizeof ip_room);
        IPM(InitPhraseTable)(ip_room, (int16_t)m);
        printf("IP pool %ld %d %d %d %d\n", m,
               (int)IPS(ip_room, IP_COUNT), (int)IPS(ip_room, IP_LEFT),
               (int)IPS(ip_room, IP_AT), (int)IPS(ip_room, IP_TOP));
        for (i = 0; i < m + 2; i++) {
            int16_t got = IPM(TableAllocBG)(ip_room,
                                            (uint16_t *)(ip_room + IP_COUNT),
                                            (uint16_t *)(ip_room + IP_LEFT),
                                            (uint16_t *)(ip_room + IP_AT),
                                            (uint8_t *)(ip_room + IP_LINK),
                                            (uint16_t)m);
            printf("IP alloc %ld %ld %d %d %d %d %d %08lx\n", m, i, (int)got,
                   (int)IPS(ip_room, IP_COUNT), (int)IPS(ip_room, IP_LEFT),
                   (int)IPS(ip_room, IP_AT), (int)IPS(ip_room, IP_TOP),
                   ipDigest(ip_room + IP_LINK, (long)m * IP_LINK_SIZE));
        }
    }

    /* And a group taken out of the full pool and cleared, five times over, so
       that both what a fresh group holds and what the list does show. */
    memset(ip_room, 0, sizeof ip_room);
    IPM(InitPhraseTable)(ip_room, IP_LINK_N);
    for (i = 0; i < 5; i++) {
        void *bg = IPM(BreathGroupAlloc)(ip_room);

        printf("IP bgalloc %ld %ld %08lx %d %d\n", i, ipWhichGroup(bg),
               bg != NULL ? ipDigest((const char *)bg + 4, IP_GROUP_SIZE - 4)
                          : 0ul,
               (int)IPS(ip_room, IP_LEFT), (int)IPS(ip_room, IP_AT));
    }

    /* ---- folding two accent phrases together ------------------------ */

    /* Every slot of a phrase over a great many fixtures, printed whole for
       the first few and digested for the rest. A run whose two lengths agree
       is what the fold turns on, so a quarter of the fixtures are built to
       agree. */
    seed = 0x2f6ea1c3u;
    for (i = 0; i < 600; i++) {
        for (j = 0; j <= 11; j++) {
            int      v;
            uint32_t s2 = seed + (uint32_t)i * 104729u;

            memset(ip_phrase, 0, sizeof ip_phrase);
            for (k = 0; k < IH_RUN_N; k++) {
                uint8_t len = (uint8_t)(ipNext(&s2) & 7);

                ip_phrase[IH_A + k]     = (char)(ipNext(&s2) & 3);
                ip_phrase[IH_MORAS + k] = (char)((i % 4) == 0
                                                 ? len : (ipNext(&s2) & 7));
                ip_phrase[IH_LEN + k]   = (char)len;
                ip_phrase[IH_PITCH + k] = (char)(ipNext(&s2) % 8);
                ip_phrase[IH_MARK + k]  = (char)(ipNext(&s2) & 3);
            }
            ip_phrase[IH_COUNT] = (char)IH_RUN_N;
            ip_phrase[IH_FLAG]  = (char)(ipNext(&s2) % 8);
            v = IPM(SetAccentualPhrase)(ip_room, ip_phrase, (uint8_t)j);
            printf("IP accent %ld %ld %d %08lx\n", i, j, v,
                   ipDigest(ip_phrase, IG_PHRASE_SIZE));
        }
    }

    /* ---- the passes over a whole chain ------------------------------ */

    /* Each pass on its own, on the same shape, so that a difference names
       one pass rather than the lot. Then the entry point, which runs them in
       order and is the only thing that answers TextAnalysis. */
    for (m = 0; m < 96; m++) {
        int n = 1 + (int)(m % IP_ROW_N);
        int full = m < 6;

        seed = 0x13579bdfu + (uint32_t)m * 2654435761u;

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(SetPhraseState)(ip_room);
        if (full) ipReport("state", m, n);
        ipDigestReport("state", m, n);

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(SetPhraseState)(ip_room);
        IPM(RegroupPhrases)(ip_room);
        if (full) ipReport("regroup", m, n);
        ipDigestReport("regroup", m, n);

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(SetPitchValues)(ip_room);
        if (full) ipReport("pitch", m, n);
        ipDigestReport("pitch", m, n);

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(ProsodyControl)(ip_room);
        if (full) ipReport("prosody", m, n);
        ipDigestReport("prosody", m, n);

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(SetPhraseState)(ip_room);
        IPM(ProsodyControl)(ip_room);
        printf("IP inphrase %ld %d\n", m,
               (int)IPM(SetIntonationalPhrase)(ip_room));
        if (full) ipReport("inphrase", m, n);
        ipDigestReport("inphrase", m, n);

        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(ThreePhraseParsing)(ip_room, IP_ROW(0));
        if (full) ipReport("three", m, n);
        ipDigestReport("three", m, n);
    }

    /* Breaking a group that came out too long, called directly so that the
       bounds and the mark can be swept rather than left to whatever the chain
       happened to hold.
     *
     * The mora count has to be at least what the run itself adds up to plus
     * the floor. RegroupPhrases always passes exactly the sum and a floor of
     * nought, and both of its calls do; a count any smaller sends every
     * candidate to a negative score, nothing beats the minus one the best
     * starts at, and IBM writes the answer through a pointer it never set.
     * That is an IBM defect and it is not reachable from either caller, so
     * the sweep stays on the side of it that is. */
    for (m = 0; m < 40; m++) {
        static const int marks[5] = { 0x14, 0x0a, 0x0b, 0, 4 };
        int n = 2 + (int)(m % (IP_ROW_N - 2));

        seed = 0x0badc0deu + (uint32_t)m * 40503u;
        for (k = 0; k < 5; k++)
            for (j = 0; j < 4; j++) {
                int sum = 0;
                int moras;

                ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
                for (i = 0; i < n; i++)
                    sum += (int)IPB(IP_ROW(i), PT_MORAS);
                moras = sum + (int)j + (int)(m % 5);
                printf("IP separate %ld %ld %ld %d %d %08lx\n", m, k, j, sum,
                       (int)IPM(PhraseSeparate)(ip_room, IP_ROW(0),
                                                IP_ROW(n - 1),
                                                (int16_t)moras,
                                                (int16_t)(0x19 + j * 7),
                                                (int16_t)j,
                                                (int16_t)marks[k]),
                       ipRowsDigest(n));
            }
    }

    /* And how long each pause runs, over a chain whose groups the pass ahead
       of it has already made. */
    for (m = 0; m < 40; m++) {
        int n = 1 + (int)(m % IP_ROW_N);

        seed = 0xfeedfaceu + (uint32_t)m * 22699u;
        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        IPM(SetPhraseState)(ip_room);
        IPM(ProsodyControl)(ip_room);
        IPM(SetIntonationalPhrase)(ip_room);
        IPM(SetPauseLength)(ip_room);
        if (m < 4)
            ipReport("pause", m, n);
        ipDigestReport("pause", m, n);
    }
    /* The pitch pass's second walk turns on a phrase's own state code, and the
       four codes it names are one byte in two hundred and fifty-six apart from
       each other: a shape whose states are drawn at random reaches 0x82 about
       once in a thousand rows, and the sabotage of that arm moved nothing.
       Here every code it tests for is put on every row in turn, over both
       group marks the outer guard lets through, with the first mora made to
       agree with its accent slot in one row of each shape. */
    for (m = 0; m < 6; m++) {
        static const int codes[6] = { 0x82, 0x88, 0x8b, 0x8f, 0x00, 0x81 };

        for (k = 0; k < 2; k++)
            for (j = 0; j < 4; j++) {
                seed = 0x71c3e5a9u + (uint32_t)(m * 8 + k * 4 + j);
                ipSetUp(4, seed, 8);
                for (i = 0; i < 4; i++) {
                    IPB(IP_ROW(i), PT_STATE) = (uint8_t)codes[m];
                    IPB(IP_ROW(i), PT_GROUP) = (uint8_t)k;
                    if (i == (int)j)
                        IPB(IP_ROW(i), PT_MORA_ACC) = IPB(IP_ROW(i), PT_MORA);
                }
                IPM(SetPitchValues)(ip_room);
                printf("IP pitchstate %ld %ld %ld %08lx\n", m, k, j,
                       ipRowsDigest(4));
            }
    }

    /* And the three rules that rewrite a code as the reading is copied. Two of
       them want the code to be the first of the first phrase of the first
       group, which a reading built from a mora count reaches only when the
       count comes out at one, and the third wants a long vowel straight after
       a full stop -- a pair a reading of random codes never holds, since the
       full stop is only ever put last. So these are put there by hand. */
    for (m = 0; m < 5; m++) {
        static const int first[5] = { 0xfd, 0xfe, 0x50, 0xf0, 0x21 };

        for (k = 0; k < 3; k++) {
            seed = 0x2bd4f10cu + (uint32_t)(m * 4 + k);
            ipSetUp(3, seed, 16);
            IPB(IP_ROW(0), PT_KANA)     = (uint8_t)first[m];
            IPB(IP_ROW(0), PT_KANA + 1) = (uint8_t)(k == 0 ? 0x50
                                                    : k == 1 ? 0xfd : 0x30);
            IPB(IP_ROW(0), PT_MORAS)    = (uint8_t)(k == 2 ? 1 : 2);
            IPM(SetPhraseState)(ip_room);
            IPM(ProsodyControl)(ip_room);
            printf("IP firstcode %ld %ld %d\n", m, k,
                   (int)IPM(SetIntonationalPhrase)(ip_room));
            ipDigestReport("firstcode", m * 4 + k, 3);
        }
    }

    /* A group that comes out holding one phrase of no length at all is
       unlinked from the chain and given back, and where it is dropped from the
       middle its kind is carried back to the group in front of it. Both of
       those wanted a row with no reading at all, which nothing else here
       builds: every row otherwise gets at least one code, so no group's length
       is ever nought and the whole of that cleanup was unswept. */
    for (m = 0; m < 8; m++) {
        int n = 4;

        seed = 0x5c1d9e37u + (uint32_t)m * 65599u;
        ipSetUp(n, seed, (int)(m % 8));
        i = 1 + (long)(m % (n - 1));
        memset(IP_ROW(i) + PT_MORA, 0, PT_KANA - PT_MORA);
        IPB(IP_ROW(i), PT_MORAS) = 0;
        IPB(IP_ROW(i), PT_KIND)  = 2;
        IPB(IP_ROW(0), PT_KIND)  = 2;
        IPB(IP_ROW(n - 1), PT_KIND) = (uint8_t)(2 + (m & 3));
        IPM(SetPhraseState)(ip_room);
        IPM(ProsodyControl)(ip_room);
        printf("IP empty %ld %ld %d\n", m, i,
               (int)IPM(SetIntonationalPhrase)(ip_room));
        IPM(SetPauseLength)(ip_room);
        ipReport("empty", m, n);
    }
    /* `SetIntonationalPhrase' reads two things the pass in front of it has
       just rewritten: each row's group mark, which `ProsodyControl' replaces
       with one of `CheckBreathGroup's seven, and each mora's pitch, which
       `SetPitchValues' replaces with a one, a three or a four. Driving it only
       through its own caller therefore leaves three of its roads unreachable
       -- a group that fills all three phrases it holds, the walk back over a
       pitch of six, and the arm that rewrites a pitch of two -- and sabotages
       of all three moved nothing. Here it is driven on its own with the marks
       and the pitches set by hand. */
    for (m = 0; m < 24; m++) {
        static const int marks[4] = { 0x0a, 0x0a, 0x0a, 0x0b };
        int n = 5;

        seed = 0x3f9a17c5u + (uint32_t)m * 39916801u;
        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));
        for (i = 0; i < n; i++) {
            IPB(IP_ROW(i), PT_GROUP) =
                (uint8_t)(i == n - 1 ? 0x14 : marks[(m + i) & 3]);
            IPB(IP_ROW(i), PT_KIND) = (uint8_t)(1 + (m + i) % 5);
            for (k = 0; k < PT_MORA_N; k++) {
                IPB(IP_ROW(i), PT_MORA_PITCH + k) =
                    (uint8_t)((m + i + k) % 8);
                IPB(IP_ROW(i), PT_MORA_HI_PITCH + k) =
                    (uint8_t)((m + i + k + 3) % 8);
                IPB(IP_ROW(i), PT_MORA_ACC + k) = (uint8_t)((m + k) % 10);
            }
        }
        printf("IP direct %ld %d\n", m,
               (int)IPM(SetIntonationalPhrase)(ip_room));
        if (m < 4)
            ipReport("direct", m, n);
        ipDigestReport("direct", m, n);
    }
}



/* ---- ProsCtrl -------------------------------------------------------- */

/* The four records this class builds have their three pointers parked past
   them on our side and at IBM's own offsets on IBM's, so the sweep reaches
   them through these rather than as bytes -- and nothing below prints an
   address, only which entry of an array something is. */
#ifdef EVV_ROMPRIMS_OURS
#define PCBG_AT(a, i)   ((uint8_t *)(a) + (long)(i) * (long)BG_ROOM)
#define PCPH_AT(a, i)   ((uint8_t *)(a) + (long)(i) * (long)PH_ROOM)
#define PCAP_AT(a, i)   ((uint8_t *)(a) + (long)(i) * (long)AP_ROOM)
#define PCBG_PHRASE(p)  (*(void **)((uint8_t *)(p) + BG_PHRASE_AT))
#define PCPH_WORD(p)    (*(void **)((uint8_t *)(p) + PH_WORD_AT))
#define PCAP_MORA(p)    (*(void **)((uint8_t *)(p) + AP_MORA_AT))
#else
#define PCBG_AT(a, i)   ((uint8_t *)(a) + (long)(i) * BG_SIZE)
#define PCPH_AT(a, i)   ((uint8_t *)(a) + (long)(i) * PH_SIZE)
#define PCAP_AT(a, i)   ((uint8_t *)(a) + (long)(i) * AP_SIZE)
#define PCBG_PHRASE(p)  (*(void **)((uint8_t *)(p) + BG_PHRASE))
#define PCPH_WORD(p)    (*(void **)((uint8_t *)(p) + PH_WORD))
#define PCAP_MORA(p)    (*(void **)((uint8_t *)(p) + AP_MORA))
#endif
#define PCMO_AT(a, i)   ((uint8_t *)(a) + (long)(i) * MO_SIZE)
#define PCB(p, off)     (*((uint8_t *)(p) + (off)))
#define PCS16(p, off)   (*(int16_t *)((uint8_t *)(p) + (off)))

static char pc_room[PC_BYTES + 8];
static char pc_out[0x4000];
static char pc_buf[0x400];
static char pc_ap[AP_SIZE + 32];
static char pc_moras[12 * MO_SIZE];

/* A chain of breath groups built by hand rather than by IntonPhrase.
 *
 * The copy rewrites three codes as it goes and the only way to reach that
 * rewriting is a reading that holds them, which no reading built out of
 * printable bytes does. Building the group here rather than driving the pass
 * in front of it also lets the reading and the mora positions be made to
 * agree whatever the rewriting does to their lengths, which a real analysis
 * arranges and a fixture otherwise cannot.
 *
 * The chain link is a pointer on IBM's side and a stride count on ours, which
 * rom/jajp/intonphrase.h says why. */
static char pc_bgt[3][IP_GROUP_SIZE];

#ifdef EVV_ROMPRIMS_OURS
#define PCBGT_LINK(g, p) IG_NEXT_SET((g), (p))
#else
#define PCBGT_LINK(g, p) (*(void **)(g) = (p))
#endif
#define PCIH_AT(g, i) \
    ((uint8_t *)(g) + IG_PHRASE + (long)(i) * IG_PHRASE_SIZE)

/* One group of one phrase, whose reading is the codes given and whose words
   are cut at every second code. Every field ProsCtrl reads is set, and the
   mora positions are one every two codes, so a word of an even number of
   codes has whole moras in it however the copy rewrites them. */
static void pcGroup(int g, const uint8_t *codes, int len, int words,
                    int kind, uint32_t seed)
{
    uint8_t *bg = (uint8_t *)pc_bgt[g];
    uint8_t *ph;
    int      i;

    memset(bg, 0, IP_GROUP_SIZE);
    ph = PCIH_AT(bg, 0);

    for (i = 0; i < len; i++)
        ph[IH_KANA + i] = codes[i];
    ph[IH_KANA_LEN] = (uint8_t)len;
    ph[IH_COUNT]    = (uint8_t)words;
    ph[IH_FIRST]    = 1;
    ph[IH_FLAG]     = (uint8_t)(seed % 4);
    *(int16_t *)(ph + IH_AT66) = (int16_t)(seed % 3);

    for (i = 0; i < words; i++) {
        ph[IH_LEN + i]   = (uint8_t)(len / words
                                     + (i < len % words ? 1 : 0));
        /* A word holds no more moras than it holds codes. The copy trims a
           reading that ends in a full stop by looking at the code its mora
           count names, so a mora count above the code count reads past the
           codes that were written -- into IBM's heap, which is not ours.
           That is an IBM defect and a real analysis cannot reach it, since
           whatever made the codes made the count with them. */
        ph[IH_MORAS + i] = (uint8_t)(ph[IH_LEN + i] == 0 ? 0
                                     : 1 + (seed >> i) % ph[IH_LEN + i]);
        ph[IH_A + i]     = (uint8_t)((seed >> (i + 2)) % 2);
        ph[IH_PITCH + i] = (uint8_t)(1 + (seed >> (i + 4)) % 4);
    }
    /* One mora a code, which is the only arrangement whose arithmetic
       cannot drift: the walk that counts a word's moras compares a count of
       rewritten codes against a position in the reading, and two codes to a
       mora leaves room for those to disagree. */
    for (i = 0; i < IH_E_N; i++) {
        *(int16_t *)(ph + IH_E + i * 2) = (int16_t)(i < len ? i : -1);
        ph[IH_F + i] = (uint8_t)(1 + (seed >> i) % 10);
    }

    bg[IG_PHRASES] = 1;
    bg[IG_LEVEL]   = (uint8_t)len;
    bg[IG_KIND]    = (uint8_t)kind;
    *(int16_t *)(bg + IG_PAUSE) = (int16_t)(0x10 + seed % 0x40);
    PCBGT_LINK(bg, NULL);
}

/* One accent phrase built by hand, with a chosen run of mora kinds and codes.
   A code is a consonant times eight plus a vowel, which is what the writer
   takes it apart into, so the codes are built that way rather than drawn. */
static void pcPhrase(const int *kinds, int n, uint32_t seed)
{
    int i, j;

    memset(pc_ap, 0, sizeof pc_ap);
    memset(pc_moras, 0, sizeof pc_moras);
    for (i = 0; i < n; i++) {
        uint8_t *mo = PCMO_AT(pc_moras, i);
        int      codes = 1 + (int)((seed >> (i & 7)) % 4);

        PCB(mo, MO_CODES) = (uint8_t)codes;
        for (j = 0; j < codes; j++) {
            int cons  = (int)((seed >> j) % 0x20);
            int vowel = (int)((seed >> (j + 3)) % 8);

            /* Never a doubling as the last code of a mora: it would write
               the consonant of the code after it, and there is none, so IBM
               reads a local it never set. */
            if (j == codes - 1 && vowel == 5)
                vowel = 4;
            PCB(mo, MO_CODE + j) = (uint8_t)(cons * 8 + vowel);
        }
        PCS16(mo, MO_KIND) = (int16_t)kinds[i];
    }
    PCB(pc_ap, AP_MORA_N) = (uint8_t)n;
    PCB(pc_ap, AP_CODES)  = (uint8_t)n;
    PCB(pc_ap, AP_MORAS)  = (uint8_t)n;
    PCB(pc_ap, AP_HEAD)   = (uint8_t)(seed % 3);
    PCB(pc_ap, AP_LEN)    = (uint8_t)n;
    PCB(pc_ap, AP_PITCH)  = (uint8_t)(seed % 5);
    PCS16(pc_ap, AP_LONG) = (int16_t)(seed % 7);
    PCAP_MORA(pc_ap) = pc_moras;
}

/* The tree, printed level by level and never as bytes: what a level says is
   the same on both sides, and the pointers between the levels are not. */
static void pcTree(const char *tag, long a, void *groups, int32_t count)
{
    int32_t g, p, w, m;

    printf("PC tree %s %ld %d\n", tag, a, (int)count);
    for (g = 0; g < count; g++) {
        uint8_t *bg = PCBG_AT(groups, g);

        printf("PC group %s %ld %d %d %d %d %d\n", tag, a, (int)g,
               (int)PCS16(bg, BG_PAUSE), (int)PCB(bg, BG_LEVEL),
               (int)PCB(bg, BG_PHRASES), (int)PCB(bg, BG_KIND));
        for (p = 0; p < PCB(bg, BG_PHRASES); p++) {
            uint8_t *ph = PCPH_AT(PCBG_PHRASE(bg), p);

            printf("PC phrase %s %ld %d %d %d %d %d %d %d\n", tag, a,
                   (int)g, (int)p, (int)PCB(ph, PH_MORAS),
                   (int)PCB(ph, PH_FLAG), (int)PCB(ph, PH_FIRST),
                   (int)PCB(ph, PH_WORDS), (int)PCS16(ph, PH_AT4));
            for (w = 0; w < PCB(ph, PH_WORDS); w++) {
                uint8_t *ap = PCAP_AT(PCPH_WORD(ph), w);
                int      k;

                printf("PC word %s %ld %d %d %d %d %d %d %d %d %d %d", tag,
                       a, (int)g, (int)p, (int)w, (int)PCB(ap, AP_CODES),
                       (int)PCB(ap, AP_MORAS), (int)PCS16(ap, AP_LONG),
                       (int)PCB(ap, AP_MORA_N), (int)PCB(ap, AP_LAST),
                       (int)PCB(ap, AP_HEAD), (int)PCB(ap, AP_LEN));
                printf(" %d ", (int)PCB(ap, AP_PITCH));
                /* Only the codes it says it holds. What follows them in the
                   block is whatever the heap had, which is not the same heap
                   on the two sides and is read by nothing. */
                for (k = 0; k < PCB(ap, AP_CODES) && k < 0x19; k++)
                    printf("%02x", (unsigned)PCB(ap, AP_CODE + k));
                putchar('\n');
                for (m = 0; m < PCB(ap, AP_MORA_N); m++) {
                    uint8_t *mo = PCMO_AT(PCAP_MORA(ap), m);

                    printf("PC mora %s %ld %d %d %d %d %d %d ", tag, a,
                           (int)g, (int)p, (int)w, (int)m,
                           (int)PCB(mo, MO_CODES), (int)PCS16(mo, MO_KIND));
                    for (k = 0; k < PCB(mo, MO_CODES) && k < 0x19; k++)
                        printf("%02x", (unsigned)PCB(mo, MO_CODE + k));
                    putchar('\n');
                }
            }
        }
    }
}

#ifndef EVV_ROMPRIMS_OURS
/* How long IBM's user-index escape is. IBM works it out with a strlen in a
   dynamic initialiser, and the reference build removes the section that would
   have run it, so the value stays nought and the one method that divides by
   it faults. It is four -- the escape is a backtick, u, i and a space -- and
   reference/Makefile globalizes the symbol so this can say so. Ours has it as
   a constant in rom/jajp/prosctrl.h and needs nothing. */
extern int s_nLenUserIdx;
#endif

static void sweepProsCtrl(void)
{
    long     i, j, k, m;
    uint32_t seed;
    uint32_t roll;

#ifndef EVV_ROMPRIMS_OURS
    s_nLenUserIdx = PC_USER_IDX_LEN;
#endif
    memset(pc_room, 0, sizeof pc_room);
    PCM(Ctor)(pc_room);
    printf("PC ctor %d %d %d\n", (int)*(int32_t *)(pc_room + PC_MODE),
           (int)*(int32_t *)(pc_room + PC_UNREAD_04),
           (int)*(int32_t *)(pc_room + PC_ARG));
    PCM(Dtor)(pc_room);

    /* ---- the two phoneme predicates, every code ---------------------- */

    for (i = 0; i < 256; i++)
        printf("PC cons %ld %d %d\n", i,
               (int)PCM(IsBurstCons)(pc_room, (uint8_t)i),
               (int)PCM(IsValidConsForSokuOn)(pc_room, (uint8_t)i));

    /* ---- a word's prominence ---------------------------------------- */

    for (i = -8; i <= 16; i++)
        for (j = -8; j <= 16; j++)
            for (k = 0; k < 2; k++) {
                int32_t prom = (int32_t)i;

                PCM(ModifyWordProminence)(pc_room, &prom, (int32_t)j,
                                          (int32_t)k);
                printf("PC prom %ld %ld %ld %d\n", i, j, k, (int)prom);
            }

    /* ---- text into the caller's buffer ------------------------------ */

    /* Every length of string against every cap that could refuse it, and the
       length carried in as well, since what the writer refuses on is the sum
       of the two rather than either. */
    for (i = 0; i <= 12; i++)
        for (j = 0; j <= 16; j++)
            for (k = 0; k <= 4; k++) {
                char     what[16];
                uint32_t len = (uint32_t)k;
                int32_t  rc;

                pc_out[0] = '\0';
                for (m = 0; m < i; m++)
                    what[m] = (char)('a' + m);
                what[i] = '\0';
                rc = PCM(WriteToOutBuf)(pc_room, what, pc_out, (uint32_t)j,
                                        &len);
                printf("PC outbuf %ld %ld %ld %d %u [%s]\n", i, j, k,
                       (int)rc, (unsigned)len, pc_out);
            }

    /* The index marks, which go out in batches, and a cap that stops it
       part way through. */
    for (i = 0; i <= 260; i += 7)
        for (j = 0; j < 3; j++) {
            static const unsigned caps[3] = { 0x1000, 300, 4 };
            uint32_t len = 1;
            int32_t  rc;

            pc_out[0] = '\0';
            rc = PCM(WriteUserIndex)(pc_room, (int32_t)i, pc_out, caps[j],
                                     &len);
            printf("PC useridx %ld %ld %d %u %d\n", i, j, (int)rc,
                   (unsigned)len, (int)strlen(pc_out));
        }
    /* And exactly a batch, twice a batch and three times, which is the only
       place the test that cuts the last batch decides anything. */
    for (i = 0; i < 6; i++) {
        static const long big[6] = { 62, 124, 186, 300, 1000, 4000 };
        uint32_t len = 1;

        pc_out[0] = '\0';
        printf("PC useridxbig %ld %d %u %d\n", i,
               (int)PCM(WriteUserIndex)(pc_room, (int32_t)big[i], pc_out,
                                        sizeof pc_out, &len),
               (unsigned)len, (int)strlen(pc_out));
    }

    /* The pause and boundary a group closes with, in all four of its forms
       and over a cap that refuses. */
    for (i = 0; i <= 2000; i += 137)
        for (j = 0; j <= 8; j++)
            for (k = 0; k < 3; k++) {
                static const unsigned caps[3] = { 0x1000, 0x0c, 2 };
                uint32_t len = 1;
                int32_t  rc;

                pc_out[0] = '\0';
                rc = PCM(WriteBGInfo)(pc_room, (int32_t)i, (int32_t)j,
                                      (int32_t)(k & 1), pc_out, caps[k],
                                      &len);
                printf("PC bginfo %ld %ld %ld %d %u [%s]\n", i, j, k,
                       (int)rc, (unsigned)len, pc_out);
            }

    /* Whether a mora is stressed: every place inside and outside the word's
       own run, and the flag that overrides it. */
    for (i = -2; i <= 6; i++)
        for (j = -2; j <= 6; j++)
            for (k = -2; k <= 6; k++)
                for (m = 0; m < 2; m++) {
                    uint32_t len = 1;
                    int32_t  rc;

                    pc_out[0] = '\0';
                    rc = PCM(WriteStressLevel)(pc_room, (int32_t)i,
                                               (int32_t)j, (int32_t)k,
                                               pc_out, sizeof pc_out, &len,
                                               (int32_t)m);
                    printf("PC stress %ld %ld %ld %ld %d [%s]\n", i, j, k, m,
                           (int)rc, pc_out);
                }

    for (i = 0; i <= 6; i++)
        for (j = 0; j < 3; j++) {
            static const unsigned caps[3] = { 0x1000, 0x10, 3 };
            uint32_t len = 1;
            int32_t  rc;

            pc_out[0] = '\0';
            rc = PCM(WriteDummyF0Pair)(pc_room, (int32_t)i, pc_out, caps[j],
                                       &len);
            printf("PC dummyf0 %ld %ld %d %u [%s]\n", i, j, (int)rc,
                   (unsigned)len, pc_out);
        }

    /* ---- how far a word runs, and what it is called ----------------- */

    /* The mora kinds are what this turns on: one of ten, and the ten fall
       into five roads. Every run of kinds up to four long over the ten, plus
       longer runs drawn from them, and every boundary kind on top. */
    roll = 2166136261u;
    seed = 0x6f1d3ac7u;
    for (i = 0; i < 4000; i++) {
        int kinds[8];
        int n = 1 + (int)(ipNext(&seed) % 8);
        int q;

        for (q = 0; q < n; q++)
            kinds[q] = 1 + (int)(ipNext(&seed) % 10);
        pcPhrase(kinds, n, seed);
        for (j = 0; j < n; j++)
            for (k = 0; k <= 8; k++) {
                int32_t     at = (int32_t)j;
                int32_t     first = -1;
                int32_t     upto = -1;
                int32_t     pos = 0;
                const char *name = NULL;
                int32_t     rc;

                rc = PCM(GetGokiInfoToWrite)(pc_room,
                                             (const uint8_t *)pc_ap, &at,
                                             &first, &upto, &pos, &name,
                                             (int32_t)(k & 1), (int32_t)k);
                roll = (roll ^ (uint32_t)rc) * 16777619u;
                roll = (roll ^ (uint32_t)at) * 16777619u;
                roll = (roll ^ (uint32_t)first) * 16777619u;
                roll = (roll ^ (uint32_t)upto) * 16777619u;
                roll = (roll ^ (uint32_t)pos) * 16777619u;
                roll = (roll ^ (uint32_t)(name == NULL ? 0
                                          : (int)strlen(name))) * 16777619u;
                if (name != NULL) {
                    const char *c;

                    for (c = name; *c; c++)
                        roll = (roll ^ (uint32_t)(uint8_t)*c) * 16777619u;
                }
                if (i < 8)
                    printf("PC goki %ld %ld %ld %d %d %d %d %d %s\n", i, j, k,
                           (int)rc, (int)at, (int)first, (int)upto, (int)pos,
                           name != NULL ? name : "(none)");
            }
    }
    printf("PC goki roll %08lx\n", (unsigned long)roll);

    /* ---- one mora's codes as phonemes ------------------------------- */

    roll = 2166136261u;
    seed = 0x11e5c3d9u;
    for (i = 0; i < 3000; i++) {
        int kinds[8];
        int n = 1 + (int)(ipNext(&seed) % 8);
        int q;

        for (q = 0; q < n; q++)
            kinds[q] = 1 + (int)(ipNext(&seed) % 10);
        pcPhrase(kinds, n, seed);
        for (j = 0; j < n; j++)
            for (k = 0; k <= 6; k++) {
                int32_t  at = 0;
                uint32_t len = 1;
                int32_t  rc;

                pc_out[0] = '\0';
                pc_buf[0] = '\0';
                rc = PCM(WriteGokiInfo)(pc_room, (const uint8_t *)pc_ap,
                                        (int32_t)j, (int32_t)k, &at, pc_buf,
                                        pc_out, sizeof pc_out, &len);
                roll = (roll ^ (uint32_t)rc) * 16777619u;
                roll = (roll ^ (uint32_t)at) * 16777619u;
                roll = (roll ^ len) * 16777619u;
                for (m = 0; pc_out[m]; m++)
                    roll = (roll ^ (uint32_t)(uint8_t)pc_out[m]) * 16777619u;
                for (m = 0; pc_buf[m]; m++)
                    roll = (roll ^ (uint32_t)(uint8_t)pc_buf[m]) * 16777619u;
                if (i < 12) {
                    int q;

                    printf("PC gokiinfo %ld %ld %ld %d %d %u [%s] [%s]", i,
                           j, k, (int)rc, (int)at, (unsigned)len, pc_out,
                           pc_buf);
                    for (q = 0; q < PCB(pc_ap, AP_MORA_N); q++) {
                        uint8_t *one = PCMO_AT(pc_moras, q);
                        int      z;

                        printf(" %d:%d:", (int)PCS16(one, MO_KIND),
                               (int)PCB(one, MO_CODES));
                        for (z = 0; z < PCB(one, MO_CODES); z++)
                            printf("%02x", (unsigned)PCB(one, MO_CODE + z));
                    }
                    putchar('\n');
                }
            }
        printf("PC gokiinfo at %ld %08lx\n", i, (unsigned long)roll);
    }
    printf("PC gokiinfo roll %08lx\n", (unsigned long)roll);

    /* ---- the whole chain, from IntonPhrase's own answer -------------- */

    /* The only honest fixture for the copy and the writer is a chain the pass
       in front of them really built, so IntonPhrase builds one and its head
       is handed over. That also holds the two classes to each other: a field
       either of them reads at the wrong offset shows here and nowhere else. */
    /* Twenty-four shapes of at most six rows. The writer appends to the
       caller's buffer with strcat every time, so what it costs grows with the
       square of what it has written already and the shapes cannot be as large
       as IntonPhrase's without the sweep taking minutes -- and a real sentence
       is this size anyway. */
    for (m = 0; m < 24; m++) {
        static const int32_t modes[3] = { 1, 2, 3 };
        int      n = 1 + (int)(m % 6);
        void    *groups = NULL;
        int32_t  count = 0;
        int32_t  rc;
        int32_t  env[4];

        seed = 0x2c9e5f13u + (uint32_t)m * 2246822519u;
        ipSetUp(n, seed, (int)(m % IP_SHIFT_N));

        /* The long entries have to be positions in the row's own reading,
           rising, and IntonPhrase does not care what they are: it copies
           them. ProsCtrl does care, because it takes the gaps between them as
           mora lengths and sizes its phoneme buffer from the reading's length
           instead -- so entries that say nothing about the reading write more
           codes into that buffer than the group says it holds, and IBM
           overruns its own heap block. Ours is caught by the arena guard
           rather than silently corrupting, which is how this was found.
         *
         * One mora a code here, and no code that is a doubling: a doubling
           writes the consonant of the code after it out of a local IBM never
           sets when there is none. The copy's own rewriting is swept by the
           hand-built groups below, where the reading and the mora positions
           can be made to agree whatever the rewriting does to them. */
        for (i = 0; i < n; i++)
            for (j = 0; j < IH_E_N; j++)
                *(int16_t *)(IP_ROW(i) + PT_LONG + j * 2) =
                    (int16_t)(j < (long)IPB(IP_ROW(i), PT_MORAS) ? j : -1);

        for (i = 0; i < n; i++)
            for (j = 0; j < PT_MORA_N * 2; j++) {
                uint8_t *c = (uint8_t *)IP_ROW(i) + PT_KANA + j;

                if ((*c & 7) == 5)
                    *c = (uint8_t)(*c ^ 1);
            }

        IPM(SetPhraseState)(ip_room);
        IPM(ProsodyControl)(ip_room);
        IPM(SetIntonationalPhrase)(ip_room);
        IPM(SetPauseLength)(ip_room);

        memset(pc_room, 0, sizeof pc_room);
        PCM(Ctor)(pc_room);
        rc = PCM(BG_T2BreathGroups)(pc_room, IP_GET(ip_room, IP_HEAD),
                                    &groups, &count);
        printf("PC copy %ld %d %d\n", m, (int)rc, (int)count);
        if (rc == 0 && m < 4)
            pcTree("copy", m, groups, count);
        if (rc == 0) {
            uint32_t len = 1;

            pc_out[0] = '\0';
            pc_out[0] = '\0';
            *(int32_t *)(pc_room + PC_ARG) = (int32_t)(m % 3);
            printf("PC espr %ld %d [%s]\n", m,
                   (int)PCM(WriteESPR2)(pc_room, groups, count, 0x64, pc_out,
                                        sizeof pc_out), pc_out);
            /* Again on the same instance and with no constructor between,
               which is the only way the parameter being cleared after the
               first group decides anything. */
            pc_out[0] = '\0';
            printf("PC espr again %ld %d [%s]\n", m,
                   (int)PCM(WriteESPR2)(pc_room, groups, count, 0x64, pc_out,
                                        sizeof pc_out), pc_out);
            (void)len;
            PCM(FreeBreathGroups)(pc_room, groups, count);
        }

        /* And the entry point, which does the two of those in order and
           refuses four ways before it starts. */
        for (i = 0; i < 3; i++) {
            memset(pc_room, 0, sizeof pc_room);
            PCM(Ctor)(pc_room);
            pc_out[0] = '\0';
            pc_out[0] = '\0';
            env[0] = modes[i];
            printf("PC espr2 %ld %ld %d [%s]\n", m, i,
                   (int)PCM(GenerateESPR)(pc_room, env, (int32_t)(m & 3),
                                          NULL, IP_GET(ip_room, IP_HEAD),
                                          pc_out, sizeof pc_out),
                   pc_out);
        }
    }

    /* The copy's own rewriting, on groups built here rather than by the pass
       in front. No reading made of printable bytes holds the codes it
       rewrites, so ten sabotages of those roads moved nothing until this went
       in, and the readings here are made of exactly those codes.
     *
     * Two of the rewritings are left out and this is where the line is. The
       mark for a devoiced vowel and the mark for a nasal each become two
       codes where they were one, and nothing puts the mora positions right
       afterwards: the walk that decides how many moras a word holds compares
       a count of rewritten codes against a position in the reading, so a
       word that grew reaches into the next word's positions, and a few words
       later the arithmetic goes negative and IBM writes a couple of hundred
       bytes past its own mora array. Ours is stopped by the arena guard
       instead. It cannot be reached from a real reading, since the analyser
       that made the reading made the positions with it, so the sweep does
       not go there and the two arms stay unswept. */
    for (m = 0; m < 300; m++) {
        /* The five bands the class test names are far apart in the byte, so
           the pool has to reach each of them: the code after a full stop is
           divided by eight and one added, and the test asks whether that is
           at most ten, twelve, twenty-one, over twenty-two, or at most
           twenty-six. Codes in the printable range only ever reach fourteen,
           and two of the five bands were unswept until these went in. */
        static const uint8_t pool[16] = {
            0xfd, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
            0x50, 0x21, 0x92, 0x32,
            0xa2, 0xaa, 0xc2, 0xca, 0x60
        };
        uint8_t codes[16];
        int     len = 2 + (int)(m % 5);
        void   *groups = NULL;
        int32_t count = 0;
        int32_t rc;
        int     q;

        seed = 0x4d2a9c07u + (uint32_t)m * 1000003u;
        for (q = 0; q < len; q++) {
            uint32_t v = ipNext(&seed);

            codes[q] = (uint8_t)((v & 1) ? pool[(v >> 1) % 16]
                                         : 0x30 + (v >> 4) % 0x40);
        }
        pcGroup(0, codes, len, 1, (int)(m % 7), seed);

        memset(pc_room, 0, sizeof pc_room);
        PCM(Ctor)(pc_room);
        rc = PCM(BG_T2BreathGroups)(pc_room, pc_bgt[0], &groups, &count);
        printf("PC hand %ld %d %d %d ", m, (int)rc, (int)count, len);
        for (q = 0; q < len; q++)
            printf("%02x", (unsigned)codes[q]);
        putchar('\n');
        if (rc == 0) {
            pcTree("hand", m, groups, count);
            PCM(FreeBreathGroups)(pc_room, groups, count);
        }
    }

    /* And one more shape for the copy: two codes to a mora, all of them
       ordinary but the last, which is a full stop. That is the only way to
       reach the trim at the end of the mora loop, which takes a code back
       from a mora of more than one that ends in a full stop -- with one code
       to a mora it can never fire. Two codes are safe here because nothing
       in the reading changes length: the full stop is last, so the code
       after it is the nought the group was cleared to, whose class lets the
       stop stand as itself. */
    for (m = 0; m < 60; m++) {
        uint8_t codes[16];
        int     len = 2 + (int)(m % 4) * 2;
        void   *groups = NULL;
        int32_t count = 0;
        int32_t rc;
        int     q;

        seed = 0x7b3f61d5u + (uint32_t)m * 40499u;
        for (q = 0; q < len; q++)
            codes[q] = (uint8_t)(0x30 + ipNext(&seed) % 0x40);
        codes[len - 1] = 0xfd;
        pcGroup(0, codes, len, 1, (int)(m % 7), seed);
        {
            uint8_t *ph = PCIH_AT((uint8_t *)pc_bgt[0], 0);

            for (q = 0; q < IH_E_N; q++)
                *(int16_t *)(ph + IH_E + q * 2) =
                    (int16_t)(q * 2 < len ? q * 2 : -1);
        }

        memset(pc_room, 0, sizeof pc_room);
        PCM(Ctor)(pc_room);
        rc = PCM(BG_T2BreathGroups)(pc_room, pc_bgt[0], &groups, &count);
        printf("PC pair %ld %d %d %d ", m, (int)rc, (int)count, len);
        for (q = 0; q < len; q++)
            printf("%02x", (unsigned)codes[q]);
        putchar('\n');
        if (rc == 0) {
            pcTree("pair", m, groups, count);
            PCM(FreeBreathGroups)(pc_room, groups, count);
        }
    }

    /* The four refusals, and the road that is not implemented. */
    {
        int32_t env[4];
        void   *head;

        env[0] = 1;
        ipSetUp(4, 0x5a5a5a5au, 0);
        IPM(SetPhraseState)(ip_room);
        IPM(ProsodyControl)(ip_room);
        IPM(SetIntonationalPhrase)(ip_room);
        head = IP_GET(ip_room, IP_HEAD);
        memset(pc_room, 0, sizeof pc_room);
        PCM(Ctor)(pc_room);
        pc_out[0] = '\0';
        printf("PC refuse noenv %d\n",
               (int)PCM(GenerateESPR)(pc_room, NULL, 0, NULL, head, pc_out,
                                      sizeof pc_out));
        printf("PC refuse nothing %d\n",
               (int)PCM(GenerateESPR)(pc_room, env, 0, NULL, NULL, pc_out,
                                      sizeof pc_out));
        printf("PC refuse noout %d\n",
               (int)PCM(GenerateESPR)(pc_room, env, 0, NULL, head, NULL,
                                      sizeof pc_out));
        printf("PC refuse nocap %d\n",
               (int)PCM(GenerateESPR)(pc_room, env, 0, NULL, head, pc_out,
                                      0));
        printf("PC refuse text %d\n",
               (int)PCM(GenerateESPR)(pc_room, env, 0, "abc", head, pc_out,
                                      sizeof pc_out));
        env[0] = 7;
        printf("PC refuse mode %d\n",
               (int)PCM(GenerateESPR)(pc_room, env, 0, NULL, head, pc_out,
                                      sizeof pc_out));
    }
}


/* ---- MakeReadableJP --------------------------------------------------- */

/* Both classes hold nothing but a vtable, and neither side dispatches
   through it, so a block of a few bytes serves as an instance. Ours parks
   that vtable past the record; IBM's has it at nought. */
static char mr_room[MR_ROOM + 8];

/* The buffer a normaliser is handed. It has to have been allocated, because
   every road through this class frees it before replacing it. */
static char    *mr_buf;
static uint32_t mr_cap;

#ifdef EVV_ROMPRIMS_OURS
#define MR_TAKE(n)  cpp_new((uint32_t)(n))
#define MR_GIVE(p)  cpp_delete((p))
#else
extern void *ibm_operatorNew(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  ibm_operatorDelete(void *p) MANGLED("??3@YAXPAX@Z");
#define MR_TAKE(n)  ibm_operatorNew((uint32_t)(n))
#define MR_GIVE(p)  ibm_operatorDelete((p))
#endif

/* The buffer a reader is handed has to come from the allocator the reader
   grows it with, which is the C++ runtime's operator new on both sides: on
   a sixty-four bit build that is the arena and the C library's free will not
   take one of its blocks. */
static void mrFresh(uint32_t cap)
{
    if (mr_buf != NULL)
        MR_GIVE(mr_buf);
    mr_buf = (char *)MR_TAKE(cap ? cap : 1);
    mr_cap = cap;
    /* Cleared rather than merely terminated. A reader that gives up part way
       -- the phoneme reader does, on a group longer than its own buffer --
       returns without putting a nought after what it wrote, and what is
       printed then runs on into whatever the allocator last had in that
       block. The two sides do not use the same allocator, so that is the one
       thing in the sweep neither side decides. */
    if (mr_buf != NULL)
        memset(mr_buf, 0, cap ? cap : 1);
}

/* What a normaliser left, printed with the bytes as bytes: the answers are
   Shift-JIS and a terminal would make nonsense of them. */
static void mrShow(const char *what, long a, long b, int32_t rc)
{
    uint32_t i;

    printf("MR %s %ld %ld %d %u ", what, a, b, (int)rc, (unsigned)mr_cap);
    /* Only what a reader that finished left. One that gave up part way
       returns without putting a nought after what it wrote, so what follows
       is whatever the allocator last had in that block -- and the two sides
       do not allocate from the same place. */
    if (mr_buf != NULL && rc == 0)
        for (i = 0; mr_buf[i] != '\0' && i < 0x200; i++)
            printf("%02x", (unsigned)(uint8_t)mr_buf[i]);
    putchar('\n');
}

/* The texts the normalisers are given. Each is written as bytes rather than
   as characters so that this file's own encoding decides nothing: what is in
   them is the half-width and full-width digits, the symbols each table holds,
   and the shapes each normaliser is looking for. */
static const char *const MR_CASES[] = {
    "", "0", "00", "000", "9", "12", "123", "1234", "12345",
    "0930a", "0930p", "0930h", "0930x", "1234a", "000a", "0000a",
    "1:2", "12:34", "12:34:56", "1:2:3:4", "01:02:03", "00:00",
    "12:34a", "12:34:56:78", ":12", "12:", "::",
    "03-1234-5678", "0120-000-000", "#123", "*456", "1-2-3",
    "\x82\x50\x82\x51\x82\x52",                       /* full-width 123 */
    "\x82\x50\x82\x51\x3a\x82\x52\x82\x53",           /* mixed time */
    "+1", "-1", "\x81\x7b" "1", "\x81\x7c" "1", "\x81\x61" "1",
    "1.5", "1\x81\x44" "5", "1\x81\x43" "5", "1.", ".5", "1.5.7",
    "\x82\xcd\x82\xa2", "\x82\xa2\x82\xa2\x82\xa6",   /* hai, iie */
    "Y", "N", "y", "n", "\x82\x99", "\x82\x8d",
    "\\1200", "$34.56", "\x81\x8f" "500", "JPY1", "USD2",
    "1999/12/31", "12/31", "1999.12.31", "1999\x94\x4e",
    "\x8c\x8e", "\x89\xce", "\x93\x79", "\x93\xfa",   /* days of the week */
    "(1)", "\x81\x69" "1\x81\x6a", "1\x81\x60" "2", "1~2",
    "abc", "\x82\xa0\x82\xa2\x82\xa4",                /* kana */
    "1,234,567", "1\x81\x43" "234",
    "12h34", "12a34", "12p34", "0000", "9999", "99999",
    "\x82\x4f\x82\x4f\x82\x50",                       /* full-width 001 */
    "\x8e\x9e", "\x95\xaa", "\x95\x62",               /* ji, fun, byou */
    /* The seven arms the first sabotage matrix found nothing reaching: the
       two truth words, plus-or-minus, a yen amount with a fraction, a
       currency whose name is three letters and no digit after it, the two
       won currencies, and a day of the week in the brackets that make it
       one. */
    "true", "false", "truefalse", "\x81\x7d" "1", "\x81\x7d",
    "\\12.34", "\x81\x8f" "12.34", "JPY12.34", "$1.2", "\\1.234",
    "JPYx", "USDx", "\x82\x69\x82\x6f\x82\x78x", "EUR", "\x81\x8f",
    "KPW100", "KRW100", "CNY100", "XAG1", "XAU1", "XPT1", "RUR1", "TRL1",
    "\x81\x69\x8c\x8e\x81\x6a", "\x81\x69\x89\xce\x81\x6a",
    "\x81\x69\x90\x85\x81\x6a", "\x81\x69\x96\xd8\x81\x6a",
    "\x81\x69\x8b\xe0\x81\x6a", "\x81\x69\x93\x79\x81\x6a",
    "\x81\x69\x93\xfa\x81\x6a",
    "1999/12/31\x81\x69\x8c\x8e\x81\x6a",
    "12/31\x81\x69\x93\xfa\x81\x6a", "\x8c\x8e\x81\x6a" "1999/12/31"
};
#define MR_CASES_N ((int)(sizeof MR_CASES / sizeof MR_CASES[0]))
/* Every distinct spelling in IBM's own phone table, taken out of the
   object rather than retyped, and each one made into a group of its
   own so that every entry of the table is reached. */
static const char *const SPR_PHONES[] = {
    "ka", "ki", "ku", "ke", "ko", "kya", "\x81@", "kyu", "kyo", "sa", "Si",
    "su", "se", "so", "Ca", "Ci", "Cu", "Ce", "Co", "ta", "ti", "tu", "te",
    "to", "tCa", "tCi", "tCu", "tCe", "tCo", "tSi", "tsu", "pa", "pi",
    "pu", "pe", "po", "pya", "pyu", "pyo", "ha", "hi", "fu", "he", "ho",
    "hya", "hyu", "hyo", "fa", "fi", "fe", "fo", "na", "ni", "nu", "ne",
    "no", "nya", "nyu", "nyo", "ma", "mi", "mu", "me", "mo", "mya", "myu",
    "myo", "ra", "ri", "ru", "re", "ro", "rya", "ryu", "ryo", "ya", "i",
    "yu", "ye", "yo", "wa", "u", "e", "o", "ga", "gi", "gu", "ge", "go",
    "gya", "gyu", "gyo", "za", "dZi", "zu", "ze", "zo", "dZa", "dZu",
    "dZe", "dZo", "da", "di", "du", "de", "do", "ba", "bi", "bu", "be",
    "bo", "bya", "byu", "byo", "tyu", "fyu", "dyu", "H", "a", "Q", "*",
    "tte",
};
#define SPR_PHONES_N ((int)(sizeof SPR_PHONES / sizeof SPR_PHONES[0]))

/* And the paths no single spelling reaches: an accent on each of its two
   marks, the two letters the reader renames, a long vowel, a doubled
   consonant at the end of a group and in front of a vowel, a group of more
   than the sixteen letters the tag keeps, a two-byte character in the middle,
   an empty group, and a run long enough to overrun the group buffer. */
static const char *const SPR_CASES[] = {
    "", ".", "..", "ka", "ka.", "ka..", ".ka", "ka.ki.ku.",
    "ka1.", "ka1ki.", "ka'.", "ka0.", "ka9.", "1ka.", "ka1",
    "N.", "S.", "NN.", "SS.", "kaN.", "kaS.", "Na.", "Sa.",
    "A.", "I.", "U.", "E.", "O.", "AH.", "kA.", "kAH.", "kaA.",
    "AIUEO.", "aiueo.", "AaIiUuEeOo.",
    "k.", "kk.", "kka.", "ktk.", "tta.", "ssa.", "kkka.",
    "Ck.", "Cka.", "gga.", "bba.", "hha.", "zza.", "ssu.",
    "ttte.", "tte.", "ttte",
    "abcdefghijklmnop.", "abcdefghijklmnopq.", "abcdefghijklmnopqrstuvwxyz.",
    "\x82\xa9ka.", "ka\x82\xa9.", "ka\x81\x40ki.",
    "kakikukeko.sasiSuseso.taCitutetto.",
    "kakikukekokakikukekokakikukekokakikukekokakikukekokakikukeko"
    "kakikukekokakikukekokakikukekokakikukeko.",
    "AHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAHAH"
    "AHAHAHAHAHAHAHAHAHAH.",
    "koNnitiwa.", "arigatoH.", "toHkyoH.", "nihoNgo."
};
#define SPR_CASES_N ((int)(sizeof SPR_CASES / sizeof SPR_CASES[0]))


static void sweepMakeReadable(void)
{
    long     i, j, k;
    uint32_t roll;

    memset(mr_room, 0, sizeof mr_room);
    MRM(Ctor)(mr_room);
    MRM(Dtor)(mr_room);

    /* ---- the twelve predicates, over every two-byte character -------- */

    /* Each table's strings are one or two bytes, so a pair of bytes is every
       input any of them can tell apart. All 65,536 through all twelve, with
       the length the two that answer one reported as well. */
    roll = 2166136261u;
    for (i = 0; i < 0x10000; i++) {
        char     t[4];
        uint32_t howLong = 0xffffffffu;
        int32_t  v;

        t[0] = (char)(i >> 8);
        t[1] = (char)(i & 0xff);
        t[2] = '\0';
        t[3] = '\0';

        v = MRM(IsCurrencySymbol)(mr_room, t, &howLong);
        roll = (roll ^ (uint32_t)v) * 16777619u;
        roll = (roll ^ howLong) * 16777619u;
        howLong = 0xffffffffu;
        v = MRM(IsBoolSymbol)(mr_room, t, &howLong);
        roll = (roll ^ (uint32_t)v) * 16777619u;
        roll = (roll ^ howLong) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsCurrencyPunct)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsDecimalPoint)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsParenthesis)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsTimeDelimiter)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsPlusMinusSymbol)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsDayOfWeek)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsRangeSymbol)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsDateSeparator)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsTelSymbol)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsDBCSDigit)(mr_room, t)) * 16777619u;
        roll = (roll ^ (uint32_t)MRM(IsDigit)(mr_room, t)) * 16777619u;
        if ((i & 0xfff) == 0)
            printf("MR asks %ld %08lx\n", i, (unsigned long)roll);
    }
    printf("MR asks all %08lx\n", (unsigned long)roll);

    /* ---- the buffer machinery --------------------------------------- */

    /* Every length of text against every size of buffer that could refuse
       it, and the length carried in as well, since what the appenders decide
       on is the sum. The buffer is printed after, so a grown one shows. */
    for (i = 0; i <= 6; i++)
        for (j = 0; j <= 8; j++)
            for (k = 0; k <= 3; k++) {
                char     what[8];
                uint32_t len = (uint32_t)k;
                long     q;
                int32_t  rc;

                for (q = 0; q < i; q++)
                    what[q] = (char)('a' + q);
                what[i] = '\0';
                mrFresh((uint32_t)j);
                rc = MRM(AppendText)(mr_room, what, &mr_buf, &mr_cap, &len);
                printf("MR append %ld %ld %ld %d %u %u\n", i, j, k, (int)rc,
                       (unsigned)mr_cap, (unsigned)len);

                mrFresh((uint32_t)j);
                len = (uint32_t)k;
                rc = MRM(AppendChar)(mr_room, what, &mr_buf, &mr_cap, &len);
                printf("MR appendc %ld %ld %ld %d %u %u\n", i, j, k, (int)rc,
                       (unsigned)mr_cap, (unsigned)len);

                mrFresh((uint32_t)j);
                rc = MRM(lCopyAndReturn)(mr_room, what, (uint32_t)i, &mr_buf,
                                         &mr_cap);
                mrShow("copy", i * 100 + j * 10 + k, 0, rc);
            }

    /* A two-byte character through the appender that asks whether it is one,
       which is the only road where two bytes go in at once. */
    for (i = 0; i < 0x10000; i += 7) {
        char     t[4];
        uint32_t len = 0;

        t[0] = (char)(i >> 8);
        t[1] = (char)(i & 0xff);
        t[2] = '\0';
        mrFresh(0x40);
        MRM(AppendChar)(mr_room, t, &mr_buf, &mr_cap, &len);
        roll = (roll ^ len) * 16777619u;
    }
    printf("MR appendc all %08lx\n", (unsigned long)roll);

    /* ---- leading zeros, and a number cut at its point ---------------- */

    for (i = 0; i < MR_CASES_N; i++) {
        const char *t = MR_CASES[i];
        size_t      n = strlen(t);
        const char *got = MRM(SuppressZero)(mr_room, t, t + n);
        const char *whole[2];
        const char *left[2];
        const char *right[2];
        int32_t     rc;

        printf("MR zero %ld %ld\n", i, (long)(got - t));

        whole[0] = t;
        whole[1] = t + n;
        left[0] = left[1] = right[0] = right[1] = NULL;
        rc = MRM(Separate)(mr_room, whole, left, right);
        printf("MR split %ld %d %ld %ld %ld %ld\n", i, (int)rc,
               left[0] == NULL ? -1L : (long)(left[0] - t),
               left[1] == NULL ? -1L : (long)(left[1] - t),
               right[0] == NULL ? -1L : (long)(right[0] - t),
               right[1] == NULL ? -1L : (long)(right[1] - t));

        for (k = 0; k < 2; k++) {
            uint32_t len = 0;
            const char *num[2];

            num[0] = t;
            num[1] = t + n;
            mrFresh(0x40);
            rc = MRM(AppendNumber)(mr_room, num, &mr_buf, &mr_cap, &len,
                                   (int32_t)k);
            printf("MR number %ld %ld %d %u %ld\n", i, k, (int)rc,
                   (unsigned)len, (long)(num[0] - t));
        }
    }

    /* ---- the six normalisers ---------------------------------------- */

    /* Every case through every one of them, at three buffer sizes -- one
       that fits, one that has to grow, and none at all -- and at the two
       values of the flag that the telephone reader is the only one to read.
       What each leaves is printed byte for byte. */
    for (i = 0; i < MR_CASES_N; i++)
        for (j = 0; j < 3; j++) {
            static const unsigned sizes[3] = { 0x200, 4, 0 };
            const char *t = MR_CASES[i];
            uint32_t    n = (uint32_t)strlen(t);
            long        which;

            for (which = 0; which < 8; which++)
                for (k = 0; k < 13; k++) {
                    static const int32_t flags[13] = {
                        0, 0x10301, 0x20100, 0x20200, 0x20300, 0x20400,
                        0x20500, 0x20600, 0x20700, 0x20800, 0x20900,
                        0x20a00, 0x20b00
                    };
                    int32_t f = flags[k];
                    int32_t rc = 0;

                    mrFresh(sizes[j]);
                    switch (which) {
                    case 0:
                        rc = MRM(NormalizeDigits)(mr_room, t, n, &mr_buf,
                                                  &mr_cap, f);
                        break;
                    case 1:
                        rc = MRM(NormalizeLiteral)(mr_room, t, n, &mr_buf,
                                                   &mr_cap, f);
                        break;
                    case 2:
                        rc = MRM(NormalizeBool)(mr_room, t, n, &mr_buf,
                                                &mr_cap, f);
                        break;
                    case 3:
                        rc = MRM(NormalizeNumber)(mr_room, t, n, &mr_buf,
                                                  &mr_cap, f);
                        break;
                    case 4:
                        rc = MRM(NormalizePhone)(mr_room, t, n, &mr_buf,
                                                 &mr_cap, f);
                        break;
                    case 5:
                        rc = MRM(NormalizeTime)(mr_room, t, n, &mr_buf,
                                                &mr_cap, f);
                        break;
                    case 6:
                        rc = MRM(NormalizeCurrency)(mr_room, t, n, &mr_buf,
                                                    &mr_cap, f);
                        break;
                    case 7:
                        rc = MRM(NormalizeDate)(mr_room, t, n, &mr_buf,
                                                &mr_cap, f);
                        break;
                    default:
                        break;
                    }
                    mrShow("norm", i * 100 + j * 10 + which, k, rc);
                }
        }
    mrFresh(1);

    /* ---- the phoneme string, turned back into kana ------------------- */

    /* Each of IBM's own spellings as a group of its own, then the paths a
       single spelling never takes, at the three buffer sizes. */
    for (i = 0; i < SPR_PHONES_N + SPR_CASES_N; i++)
        for (j = 0; j < 3; j++) {
            static const unsigned sizes[3] = { 0x200, 4, 0 };
            char        what[256];
            const char *t;
            int32_t     rc;

            if (i < SPR_PHONES_N) {
                sprintf(what, "%s.", SPR_PHONES[i]);
                t = what;
            } else {
                t = SPR_CASES[i - SPR_PHONES_N];
            }
            mrFresh(sizes[j]);
            rc = MRM(ConvertSPR)(mr_room, t, (uint32_t)strlen(t), &mr_buf,
                                 &mr_cap);
            mrShow("spr", i * 10 + j, 0, rc);

            /* And again with an accent on it, which is the one thing the
               reader writes that is not a kana. */
            if (i < SPR_PHONES_N) {
                sprintf(what, "%s1.", SPR_PHONES[i]);
                mrFresh(sizes[j]);
                rc = MRM(ConvertSPR)(mr_room, what, (uint32_t)strlen(what),
                                     &mr_buf, &mr_cap);
                mrShow("spra", i * 10 + j, 0, rc);
            }
        }

}

/* The texts TextNormalizer is given: an annotation of every name the table
   holds, a name in none of them, the shapes that are not annotations at all,
   and text long enough to make both buffers grow. Written as bytes so that
   this file's own encoding decides nothing. */
static const char *const TN_CASES[] = {
    "", "a", "abc", "`", "``", "```", "`abc", "`[", "`[]", "`]",
    "`card[123]", "`card[0]", "`card[]", "`ord[3]", "`ord[12345]",
    "`tel[03-1234-5678]", "`telpunc[03-1234-5678]", "`tel[#123*456]",
    "`dateymd[1999/12/31]", "`dateydm[1999/31/12]", "`datemdy[12/31/1999]",
    "`datedmy[31/12/1999]", "`datemd[12/31]", "`datedm[31/12]",
    "`datemy[12/1999]", "`dateym[1999/12]", "`datey[1999]", "`datem[12]",
    "`time[12:34:56]", "`time[0930a]", "`time[12:34]",
    "`cur[\\1200]", "`cur[$34.56]", "`cur[\\12.34]", "`cur[1\x81\x60" "2]",
    "`bool[true]", "`bool[false]", "`bool[truefalse]",
    "`nosuch[123]", "`CARD[123]", "`card123]", "`card[123", "`[123]",
    "`[kaHnji.]", "`[koNnitiwa.]", "`[ka1.ki.]", "`[]",
    "`card[1]`ord[2]", "abc`card[1]def", "`card[1] `card[2]",
    "\x82\xa0\x82\xa2`card[1]", "`card[\x82\x50\x82\x51]",
    "`\x82\xa0\x82\xa2", "`x", "`x\x82\xa0",
    "`card[1\x93\xfa]", "`time[\x82\x50\x82\x51:\x82\x52\x82\x53]",
    "\x82\xa0\x82\xa2\x82\xa4\x82\xa6\x82\xa8",
    "`card[123456789012345678901234567890123456789012345678901234567890]",
    "`tel[0120-000-000-0120-000-000-0120-000-000-0120-000-000-0120-000]",
    /* Long enough that the reading outgrows the answer's first buffer: a
       digit costs one byte going in and six coming out, and the buffer
       starts at the text's length and a quarter of a kilobyte. It has to be
       `telpunc' rather than `tel': the reader only says a digit as a word
       for the one number of the two, so a `tel' annotation comes back the
       length it went in. */
    "`telpunc[2222222222222222222222222222222222222222"
    "2222222222222222222222222222222222222222"
    "5555555555555555555555555555555555555555]",
    /* And the same again with more text behind it, and twice over: what a
       reader put in the answer has to be carried into the bigger buffer, and
       a grown answer whose content starts at nought is copied over anyway by
       the very next thing written. */
    "`telpunc[22222222222222222222222222222222222222222222222222"
    "5555555555555555555555555555555555555555555555555]abcdefg",
    "`telpunc[2222222222222222222222222222222222222222222222222]"
    "`telpunc[5555555555555555555555555555555555555555555555555]",
    "0123456789012345678901234567890123456789012345678901234567890123"
    "0123456789012345678901234567890123456789012345678901234567890123"
    "0123456789012345678901234567890123456789012345678901234567890123"
    "0123456789012345678901234567890123456789012345678901234567890123"
    "0123456789012345678901234567890123456789012345678901234567890123",
    "`card[1]`card[2]`card[3]`card[4]`card[5]`card[6]`card[7]`card[8]"
    "`card[9]`card[10]`card[11]`card[12]`card[13]`card[14]`card[15]"
};
#define TN_CASES_N ((int)(sizeof TN_CASES / sizeof TN_CASES[0]))

/* Every number the table holds, and the two kinds of cardinal the telephone
   test splits. A number the switch has no arm for is not swept: IBM returns
   whatever its uninitialised local held, which is the fourteenth deliberate
   divergence and is described in docs/quirks.md. */
static const int32_t TN_FLAGS[] = {
    -1, 0x10000, 0x10100, 0x10200, 0x102ff, 0x10300, 0x10301, 0x10400,
    0x20100, 0x20200, 0x20300, 0x20400, 0x20500, 0x20600, 0x20700,
    0x20800, 0x20900, 0x20a00, 0x20b00,
    0x30000, 0x40000, 0x50000, 0xff0000
};
#define TN_FLAGS_N ((int)(sizeof TN_FLAGS / sizeof TN_FLAGS[0]))

static char tn_room[TN_ROOM + 8];

/* The answer normalizeText leaves is the caller's, and this one never gives
   it back: the harness runs once and stops, and freeing it here would be one
   more thing that could differ between the two sides. */
static void sweepTextNormalizer(void)
{
    long i, j;

    memset(tn_room, 0, sizeof tn_room);
    TNM(Ctor)(tn_room);
    TNM(Dtor)(tn_room);

    /* ---- what an annotation's name says ------------------------------ */

    /* getAnnoType is handed what follows the backquote, so a case that has
       one is asked about from the character after it, and one that has not
       is asked about as it stands. Both offsets it writes are reported. */
    for (i = 0; i < TN_CASES_N; i++) {
        const char *t = TN_CASES[i];
        const char *from = t[0] == '`' ? t + 1 : t;
        const char *argStart = NULL;
        const char *after = NULL;
        int32_t     type;

        memset(tn_room, 0, sizeof tn_room);
        TNM(Ctor)(tn_room);
        type = TNM(GetAnnoType)(tn_room, from, &argStart, &after);
        printf("TN anno %ld %ld %ld %ld\n", i, (long)type,
               argStart == NULL ? -1L : (long)(argStart - t),
               after == NULL ? -1L : (long)(after - t));
        TNM(Dtor)(tn_room);
    }

    /* ---- one annotation read, at every number ------------------------ */

    for (i = 0; i < TN_CASES_N; i++)
        for (j = 0; j < TN_FLAGS_N; j++) {
            const char *t = TN_CASES[i];
            int32_t     rc;

            memset(tn_room, 0, sizeof tn_room);
            TNM(Ctor)(tn_room);
            mrFresh(0x200);
            rc = TNM(MakeReadable)(tn_room, t, (int32_t)strlen(t), &mr_buf,
                                   &mr_cap, TN_FLAGS[j]);
            mrShow("read", i, j, rc);
            TNM(Dtor)(tn_room);
        }

    /* ---- the whole text, each case on an instance of its own --------- */

    for (i = 0; i < TN_CASES_N; i++) {
        const char *t = TN_CASES[i];
        char       *out = NULL;
        uint32_t    len = 0;
        uint32_t    k;
        int32_t     rc;

        memset(tn_room, 0, sizeof tn_room);
        TNM(Ctor)(tn_room);
        rc = TNM(NormalizeText)(tn_room, t, (uint32_t)strlen(t), &out, &len);
        printf("TN text %ld %d %u %u %u ", i, (int)rc, (unsigned)len,
               (unsigned)*(uint32_t *)(tn_room + TN_OUT_CAP),
               (unsigned)*(uint32_t *)(tn_room + TN_WORK_CAP));
        if (rc == 0 && out != NULL)
            for (k = 0; k < len && k < 0x400; k++)
                printf("%02x", (unsigned)(uint8_t)out[k]);
        putchar('\n');
        TNM(Dtor)(tn_room);
    }

    /* And every case again through one instance. The working buffer is kept
       between calls and never shrinks, so what a case is given to work in
       here is whatever the longest case before it needed -- which is the
       one thing a run of separate instances cannot show. */
    memset(tn_room, 0, sizeof tn_room);
    TNM(Ctor)(tn_room);
    for (i = 0; i < TN_CASES_N; i++) {
        const char *t = TN_CASES[i];
        char       *out = NULL;
        uint32_t    len = 0;
        uint32_t    k;
        int32_t     rc;

        rc = TNM(NormalizeText)(tn_room, t, (uint32_t)strlen(t), &out, &len);
        printf("TN again %ld %d %u %u %u ", i, (int)rc, (unsigned)len,
               (unsigned)*(uint32_t *)(tn_room + TN_OUT_CAP),
               (unsigned)*(uint32_t *)(tn_room + TN_WORK_CAP));
        if (rc == 0 && out != NULL)
            for (k = 0; k < len && k < 0x400; k++)
                printf("%02x", (unsigned)(uint8_t)out[k]);
        putchar('\n');
    }
    TNM(Dtor)(tn_room);
}

/* ---- PhraseTable ------------------------------------------------------ */

static char ptb_room[PTB_ROOM + 8];

/* A phrase to ask about, built from nothing so that both sides are given the
   same bytes: how many words, what kind of phrase, how many function words
   and which, and for each word its part of speech, its attribute byte and its
   accent. Everything else is nought. */
static void ptbPhrase(char *wp, int words, int kind, int fzks, int fzk,
                      int pos0, int posL, int attr, int accent)
{
    memset(wp, 0, PB_SLOT_SIZE);
    wp[WP_WORDS] = (char)words;
    wp[WP_TYPE]  = (char)kind;
    wp[WP_FZKS]  = (char)fzks;
    wp[WP_FZK]   = (char)fzk;
    *(WW_SLOT(wp, 0) + WW_POS) = (char)pos0;
    *(WW_SLOT(wp, 0) + WW_ATTR) = (char)attr;
    *(int16_t *)(WW_SLOT(wp, 0) + WW_ACCENT) = (int16_t)accent;
    if (words > 1) {
        *(WW_SLOT(wp, words - 1) + WW_POS) = (char)posL;
        *(WW_SLOT(wp, words - 1) + WW_ATTR) = (char)attr;
        *(int16_t *)(WW_SLOT(wp, words - 1) + WW_ACCENT) = (int16_t)accent;
    }
}

static void sweepPhraseTable(void)
{
    long     i, j, k;
    uint32_t roll;

    memset(ptb_room, 0, sizeof ptb_room);
    PTB_SET(ptb_room, PTB_OWNER, ta_block);

    /* ---- what a tag says a word is ---------------------------------- */

    /* Every value a tag byte can take through both readers. */
    for (i = 0; i < 0x100; i++)
        printf("PTB pos %ld %d %d\n", i,
               (int)PTBM(GetPosFromTG)(ptb_room, (uint8_t)i),
               (int)PTBM(GetFzkPosFromTG)(ptb_room, (uint8_t)i));

    /* And the affix reader over every pattern its five tests can tell
       apart, in all four bytes of the block it is handed. */
    roll = 2166136261u;
    for (i = 0; i < 0x10000; i++) {
        uint8_t tg[4];

        tg[0] = (uint8_t)(i >> 8);
        tg[1] = (uint8_t)(i & 0xff);
        tg[2] = (uint8_t)(i & 0xff);
        tg[3] = (uint8_t)(i >> 8);
        roll = (roll ^ (uint32_t)PTBM(GetAffixType)(ptb_room, tg)) * 16777619u;
        tg[2] = (uint8_t)(i >> 8);
        tg[3] = (uint8_t)(i & 0xff);
        roll = (roll ^ (uint32_t)PTBM(GetAffixType)(ptb_room, tg)) * 16777619u;
    }
    printf("PTB affix all %08lx\n", (unsigned long)roll);

    /* ---- the free-list splice --------------------------------------- */

    /* A chain of sixteen, built the way InitPhraseTable builds one -- each
       entry pointing at the next and the last at the count -- then taken
       apart one row at a time, with the three words and the whole chain
       printed after each. */
    for (k = 0; k < 3; k++) {
        static uint8_t link[16 * 4];
        uint16_t first, last, free_;
        long     q;

        for (q = 0; q < 16; q++) {
            *(uint16_t *)(link + q * 4)     = (uint16_t)(q == 0 ? 16 : q - 1);
            *(uint16_t *)(link + q * 4 + 2) = (uint16_t)(q + 1);
        }
        first = 16;
        last  = (uint16_t)(k == 0 ? 16 : k - 1);
        free_ = 0;
        for (q = 0; q < 18; q++) {
            int16_t got = PTBM(TableAllocPhrase)(ptb_room, &first, &last,
                                                 &free_, link, 16);
            long    e;

            printf("PTB alloc %ld %ld %d %u %u %u ", k, q, (int)got,
                   (unsigned)first, (unsigned)last, (unsigned)free_);
            for (e = 0; e < 16; e++)
                printf("%u:%u ", (unsigned)*(uint16_t *)(link + e * 4),
                       (unsigned)*(uint16_t *)(link + e * 4 + 2));
            putchar('\n');
        }
    }

    /* ---- a row taken and cleared ------------------------------------ */

    /* Over the analysis's own chain, which is filled in here rather than by
       InitPhraseTable: that method is not written and what matters is that
       both sides start from the same bytes. Twelve rows are taken, and what
       each one holds afterwards is printed in full. */
    {
        uint8_t *ta = (uint8_t *)ta_block;
        long     q;

        memset(ta + TA_LINK, 0, (size_t)TA_LINK_N * TA_LINK_SIZE);
        for (q = 0; q < TA_LINK_N; q++) {
            *(uint16_t *)(ta + TA_LINK + q * 4) =
                (uint16_t)(q == 0 ? TA_LINK_N : q - 1);
            *(uint16_t *)(ta + TA_LINK + q * 4 + 2) = (uint16_t)(q + 1);
        }
        memset(ta + TA_PHRASE, 0xcc, (size_t)16 * PT_ROW_SIZE);
        *(uint16_t *)(ta + TA_FIRST)    = TA_LINK_N;
        *(uint16_t *)(ta + TA_LAST)     = TA_LINK_N;
        *(uint16_t *)(ta + TA_SPARE_18) = 0;
        PTB_SET(ptb_room, PTB_HEAD, NULL);
        PTB_SET(ptb_room, PTB_TAIL, NULL);

        for (q = 0; q < 12; q++) {
            void *row = PTBM(GeneratePhraseTable)(ptb_room);
            long  e;

            printf("PTB row %ld %ld ", q,
                   row == NULL ? -1L
                               : (long)(((uint8_t *)row - (ta + TA_PHRASE))
                                        / PT_ROW_SIZE));
            if (row != NULL)
                for (e = 0; e < PT_ROW_SIZE; e++)
                    printf("%02x", (unsigned)((uint8_t *)row)[e]);
            putchar('\n');
        }
    }

    /* ---- the three that turn a tag into kakari bits ------------------ */

    for (i = 0; i < 0x80; i++) {
        uint8_t kkr[8];
        uint8_t other[2];
        uint8_t uke[8];
        uint8_t flag;
        long    e;

        /* Twice over, once on a record of noughts and once on one of ones:
           three of these arms clear a bit rather than set one, and a record
           of noughts cannot show that. */
        for (j = 0; j < 4; j++) {
            memset(kkr, (int)(j >> 1 ? 0xff : 0x00), sizeof kkr);
            other[0] = (uint8_t)(j & 1);
            other[1] = 0;
            PTBM(ExtKKRPhrase)(ptb_room, kkr, (int16_t)i, other);
            printf("PTB kkr %ld %ld ", i, j);
            for (e = 0; e < 8; e++)
                printf("%02x", (unsigned)kkr[e]);
            putchar('\n');
        }
        for (j = 0; j < 2; j++) {
            memset(uke, (int)(j ? 0xff : 0x00), sizeof uke);
            flag = 0;
            PTBM(SetSubUkeType)(ptb_room, uke, (int16_t)i, &flag);
            printf("PTB sub %ld %ld %d ", i, j, (int)flag);
            for (e = 0; e < 8; e++)
                printf("%02x", (unsigned)uke[e]);
            putchar('\n');
        }
    }

    /* ---- and the two that read a whole phrase ------------------------ */

    /* Every part of speech through both, at two word counts, four phrase
       kinds, with and without a function word, and at three accents. */
    roll = 2166136261u;
    for (i = 0; i < 0x100; i++)
        for (j = 0; j < 16; j++) {
            static const int kinds[4] = { 0, 6, 10, 3 };
            char    wp[PB_SLOT_SIZE];
            uint8_t kkr[8];
            uint8_t uke[8];
            int16_t rc;
            long    e;

            /* The kind and whether there is a function word are chosen
               apart: tying them together left the arm for a phrase of the
               tenth kind with one behind it unreached. */
            ptbPhrase(wp, (int)(j & 1) + 1, kinds[(j >> 1) & 3],
                      (int)((j >> 3) & 1), (int)i,
                      (int)i, (int)((i + 37) & 0xff),
                      (int)(j & 2 ? 0x80 : 0), (int)(j & 4 ? 2 : 1));

            memset(kkr, 0, sizeof kkr);
            PTBM(SetNoneFzkKKR)(ptb_room, kkr, wp);
            for (e = 0; e < 8; e++)
                roll = (roll ^ kkr[e]) * 16777619u;

            memset(uke, 0, sizeof uke);
            rc = PTBM(SetUkeTypePhrase)(ptb_room, uke, wp);
            roll = (roll ^ (uint32_t)rc) * 16777619u;
            for (e = 0; e < 8; e++)
                roll = (roll ^ uke[e]) * 16777619u;

            printf("PTB phrase %ld %ld %d ", i, j, (int)rc);
            for (e = 0; e < 8; e++)
                printf("%02x", (unsigned)kkr[e]);
            putchar(' ');
            for (e = 0; e < 8; e++)
                printf("%02x", (unsigned)uke[e]);
            putchar('\n');
        }
    printf("PTB phrase all %08lx\n", (unsigned long)roll);

    /* ---- the words of a phrase joined into one ----------------------- */

    /* A phrase of up to four words, each with its own part of speech,
       accent, reading length, attribute byte and offset, and a row that says
       which word to start at. The long-reading store is filled here as well,
       since a word whose reading is more than ten codes is read out of it. */
    {
        uint8_t *ta = (uint8_t *)ta_block;
        long     q;

        for (q = 0; q < (long)TA_LONGWORD_N * TA_LONGWORD_SIZE; q++)
            /* A pattern whose top five bits move quickly, since what the
               reader asks of a byte here is which of two classes those
               five put it in. */
            ta[TA_LONGWORD + q] = (uint8_t)(q * 29 + 5);
    }
    roll = 2166136261u;
    for (i = 0; i < 0x2000; i++)
        for (j = 0; j < 8; j++) {
            /* Two slots, and the phrase is the second: two of the arms ask
               about the word before the one in hand without asking whether
               there is one, and at the first word that reads the six bytes
               in front of the run. Both sides have to find the same six. */
            char    buf[2 * PB_SLOT_SIZE];
            char   *wp = buf + PB_SLOT_SIZE;
            char    row[PT_ROW_SIZE];
            int16_t got;
            long    e, q, words = 1 + (j & 3);

            memset(buf, 0x5a, sizeof buf);
            memset(wp, 0, PB_SLOT_SIZE);
            memset(row, 0, sizeof row);
            wp[WP_WORDS] = (char)words;
            row[PT_FIRST_WORD] = (char)((j >> 2) & 1);
            for (e = 0; e < words; e++) {
                char *w = (char *)WW_SLOT(wp, e);

                w[WW_POS]     = (char)((i * 37 + e * 53) & 0xff);
                w[WW_KANALEN] = (char)(1 + ((i >> 3) + e * 5) % 14);
                w[WW_ATTR]    = (char)((i >> 11) & 0xff);
                *(int16_t *)(w + WW_ACCENT) = (int16_t)((i >> 7) & 0x0f);
                *(int16_t *)(w + WW_OFFSET) = (int16_t)(e * 3 + 1);
                for (q = 0; q < WW_KANA_N; q++)
                    w[WW_KANA + q] = (char)(i & 1
                        ? 0xf0 + ((i + e + q) & 0x0f)
                        : (i * 3 + e * 7 + q * 13) & 0xff);
            }
            got = PTBM(CompoundWord)(ptb_room, wp, row);
            roll = (roll ^ (uint32_t)got) * 16777619u;
            for (e = 0; e < PT_MORA_N; e++) {
                roll = (roll ^ (uint32_t)(uint8_t)row[PT_MORA + e])
                       * 16777619u;
                roll = (roll ^ (uint32_t)(uint8_t)row[PT_MORA_ACC + e])
                       * 16777619u;
                roll = (roll
                        ^ (uint32_t)*(int16_t *)(row + PT_MORA_VAL + e * 2))
                       * 16777619u;
            }
            if ((i & 0xff) == 0) {
                printf("PTB comp %ld %ld %d ", i, j, (int)got);
                for (e = 0; e < 6; e++)
                    printf("%02x%02x/%d ", (unsigned)(uint8_t)row[PT_MORA + e],
                           (unsigned)(uint8_t)row[PT_MORA_ACC + e],
                           (int)*(int16_t *)(row + PT_MORA_VAL + e * 2));
                printf("%08lx\n", (unsigned long)roll);
            }
        }
    printf("PTB comp all %08lx\n", (unsigned long)roll);

    /* And again over sequences chosen by their affix rather than by a
       number. Which affix a word is decides which arm it takes, and three of
       the arms only differ from each other once a second word has seen what
       the first left behind; a part of speech picked out of the air reaches
       two of the seven affixes and none of those pairs. */
    {
        static uint8_t posFor[13];
        static const int wanted[7] = { 2, 3, 4, 7, 8, 11, 12 };
        long a, b, c, e, q;

        for (a = 0; a < 13; a++)
            posFor[a] = 0xff;
        for (a = 0; a < 0x100; a++) {
            int16_t af = PTBM(GetAffixType)(ptb_room,
                                            (uint8_t *)DM(GetTGAt)((uint8_t)a));

            if (af >= 0 && af < 13 && posFor[af] == 0xff)
                posFor[af] = (uint8_t)a;
        }
        printf("PTB posfor");
        for (a = 0; a < 13; a++)
            printf(" %u", (unsigned)posFor[a]);
        putchar('\n');

        roll = 2166136261u;
        for (a = 0; a < 7; a++)
            for (b = 0; b < 7; b++)
                for (c = 0; c < 7; c++)
                    for (j = 0; j < 40; j++) {
                        static const int accents[5] = { 0, 6, 7, 9, 15 };
                        static const int lens[2]    = { 1, 11 };
                        static const int attrs[4]   = { 0, 1, 2, 4 };
                        const int seq[3] = { wanted[a], wanted[b], wanted[c] };
                        char    buf[2 * PB_SLOT_SIZE];
                        char   *wp = buf + PB_SLOT_SIZE;
                        char    row[PT_ROW_SIZE];
                        int16_t got;

                        memset(buf, 0x5a, sizeof buf);
                        memset(wp, 0, PB_SLOT_SIZE);
                        memset(row, 0, sizeof row);
                        wp[WP_WORDS] = 3;
                        for (e = 0; e < 3; e++) {
                            char *w = (char *)WW_SLOT(wp, e);

                            w[WW_POS]     = (char)posFor[seq[e]];
                            w[WW_KANALEN] = (char)lens[(j >> 3) & 1];
                            w[WW_ATTR]    = (char)attrs[(j >> 4) & 3];
                            *(int16_t *)(w + WW_ACCENT) =
                                (int16_t)accents[j % 5];
                            *(int16_t *)(w + WW_OFFSET) = (int16_t)(e * 3 + 1);
                            for (q = 0; q < WW_KANA_N; q++)
                                w[WW_KANA + q] = (char)(0xf0 + ((e + q) & 7));
                            /* The first code doubles as the number of the
                               long reading a word too long to hold its own
                               is kept in, so it has to be one of the thirty
                               there are: left at a mora code it would index
                               a quarter of the way through the analysis and
                               read the same nought either way. */
                            w[WW_KANA] = (char)((e * 7 + (j & 3)) % 30);
                        }
                        got = PTBM(CompoundWord)(ptb_room, wp, row);
                        roll = (roll ^ (uint32_t)got) * 16777619u;
                        for (e = 0; e < PT_MORA_N; e++) {
                            roll = (roll
                                    ^ (uint32_t)(uint8_t)row[PT_MORA + e])
                                   * 16777619u;
                            roll = (roll
                                    ^ (uint32_t)(uint8_t)row[PT_MORA_ACC + e])
                                   * 16777619u;
                            roll = (roll ^ (uint32_t)*(int16_t *)
                                    (row + PT_MORA_VAL + e * 2)) * 16777619u;
                        }
                        if (j == 0)
                            printf("PTB seq %ld %ld %ld %d %02x%02x %08lx\n",
                                   a, b, c, (int)got,
                                   (unsigned)(uint8_t)row[PT_MORA],
                                   (unsigned)(uint8_t)row[PT_MORA_ACC],
                                   (unsigned long)roll);
                    }
        printf("PTB seq all %08lx\n", (unsigned long)roll);
    }

    /* ---- what a run of function words does to the accent ------------- */

    /* Every rule byte through it, over a phrase of a few lengths and
       accents, with the reading made of moras that do and do not carry one.
       What is carried through is only printed for the groups the walk
       reached: past those the answer is whatever was on the stack, and the
       two sides do not share one. */
    roll = 2166136261u;
    for (i = 0; i < 0x1000; i++)
        for (j = 0; j < 4; j++) {
            /* Room well past the record's own length: the accent may land
               a good way past the reading and IBM reads there, so both sides
               are given the same bytes to find rather than each its own
               stack. */
            uint8_t  in[AI_ROOM];
            uint8_t  out[0x40];
            uint8_t  rule[3][3];
            long     e;

            memset(in, 0, sizeof in);
            memset(out, 0, sizeof out);
            in[AI_MORAS]  = (uint8_t)(1 + (j & 1) * 3);
            in[AI_ACCENT] = (uint8_t)(j & 2 ? 5 : 1);
            in[AI_WORDS]  = (uint8_t)(1 + (i & 1));
            in[AI_KIND]   = (uint8_t)((i >> 1) & 3);
            for (e = 0; e < 3; e++) {
                rule[e][0] = (uint8_t)((i >> 3) & 0xff);
                rule[e][1] = (uint8_t)((i >> 4) & 0xff);
                /* Not a slice of the same number as the second byte: the
                   two are compared against each other in one arm, and while
                   they were the same nibble that arm could not be reached. */
                rule[e][2] = (uint8_t)((i * 7 + e * 29) & 0xff);
                AI_RULE_SET(in, e, rule[e]);
                in[AI_LEN + e]  = (uint8_t)(1 + ((i >> 6) & 3) + (e & 1));
                in[AI_ENDS + e] = (uint8_t)((e + (j & 1) * 2) % 3);
                *(int16_t *)(in + AI_MARK + e * 2) = (int16_t)(100 + e);
            }
            for (e = 0; e + AI_KANA < AI_TAIL; e++)
                in[AI_KANA + e] = (uint8_t)(e * 7);
            in[AI_AT79] = (uint8_t)(j & 1 ? 3 : 0);

            PTBM(FzkAccent)(ptb_room, in, out);
            roll = (roll ^ out[AO_MORAS]) * 16777619u;
            roll = (roll ^ out[AO_ACCENT]) * 16777619u;
            for (e = 0; e < 15; e++) {
                roll = (roll ^ out[AO_LEN + e]) * 16777619u;
                roll = (roll ^ out[AO_ACC + e]) * 16777619u;
                /* The mark of a group the walk never reached is whatever was
                   on the stack, so only the ones it did are compared. */
                if (out[AO_LEN + e] != 0)
                    roll = (roll
                            ^ (uint32_t)*(int16_t *)(out + AO_MARK + e * 2))
                           * 16777619u;
            }
            printf("PTB fzk %ld %ld %u %u ", i, j,
                   (unsigned)out[AO_MORAS], (unsigned)out[AO_ACCENT]);
            for (e = 0; e < 15; e++)
                printf("%02x%02x/%d ", (unsigned)out[AO_LEN + e],
                       (unsigned)out[AO_ACC + e],
                       out[AO_LEN + e] != 0
                           ? (int)*(int16_t *)(out + AO_MARK + e * 2) : 0);
            putchar('\n');
        }
    printf("PTB fzk all %08lx\n", (unsigned long)roll);
}

int main(void)
{
    Param *p;
    Conv  *c;

    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_start();
#endif
    evvRunStaticInitialisers();

    STATIC_DICT_INIT();

    p = makeParam("");
    if (p == NULL) {
        printf("romprims: no parameter block\n");
        return 1;
    }
    the_param = p;
    c = makeConv(p);
    if (c == NULL) {
        printf("romprims: no converter\n");
        return 1;
    }

    sweepToUcs2(c);
    sweepToMbcs(c);
    sweepStrings(c);
    sweepDictMan();
    sweepRules();
    sweepBytes();
    sweepPairs();
    sweepTwoByte();
    sweepRomaji();
    sweepDakuten();
    sweepKatakana();
    sweepYomi();
    sweepTableFree();
    sweepSkipList();
    sweepDictSearch();
    sweepDictSearchRest();
    sweepUserDict();
    sweepDictionaries();
    sweepInputChar();
    sweepReader();
    sweepNumbers();
    sweepDo();
    sweepFzk();
    sweepCodeconv();
    sweepInputManager();
    sweepConverter();
    sweepPhraseBuf();
    sweepJPath();
    sweepNumRead();
    sweepIntonPhrase();
    sweepProsCtrl();
    sweepMakeReadable();
    sweepTextNormalizer();
    sweepPhraseTable();

    fflush(stdout);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_finish();
#endif
    return 0;
}

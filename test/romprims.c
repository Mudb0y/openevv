/* The romanizer's converters, ours against IBM's, one call at a time.
 *
 * test/romcan.sh cannot reach these. It proves the engine below the romanizer
 * by replaying what IBM's romanizer answered, and a class the romanizer reaches
 * for itself is never called on that path at all -- the codeset conversion is
 * exactly that. So this is the same arrangement test/prims.c uses for the
 * machine's primitives: one file compiled twice, once against our romanizer and
 * once against IBM's own objects, both printing the same lines for the same
 * sweep, and test/romprims.sh diffing them.
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
#include "jpath.h"

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
   not: tools/lift-rom.py reads those stores and writes the arrays out with
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
#include "jpath.h"

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
   constant, ours the count tools/lift-rom.py wrote beside the array -- so a
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
   accessor uses. The lengths are what tools/lift-romtables.py measured, which
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
static void jpPath(int p, int spare, const int *ents, int n)
{
    uint8_t *path = JP_PATH_AT(jp_room, p);
    int      i;

    memset(path, 0, JP_PATH_SIZE);
    path[JPT_COUNT] = (uint8_t)n;
    path[JPT_SPARE] = (uint8_t)spare;
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

    fflush(stdout);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_finish();
#endif
    return 0;
}

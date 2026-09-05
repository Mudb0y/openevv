/* What lang/jajp/rom_tables_jajp.c defines.
 *
 * Written by tools/rom/tables.py beside that file, so
 * that a table cannot be declared one way and defined
 * another. Each pointer is into its object's own block and
 * each length is that table's, in bytes.
 */

#ifndef ROM_TABLES_JAJP_H
#define ROM_TABLES_JAJP_H

#include <stdint.h>

/* What a symbol table's entries are: what the symbol
 * means and how it is written. */
typedef struct { int32_t what; const char *how; }
    jajp_symbol;

/* dictman.obj */
extern const uint8_t *const jajp_s_aFuncWordDict;
extern const int32_t jajp_s_aFuncWordDict_n;
extern const uint8_t *const jajp_s_aFuncWordDictEx;
extern const int32_t jajp_s_aFuncWordDictEx_n;
extern const uint8_t *const jajp_s_aKakariTable;
extern const int32_t jajp_s_aKakariTable_n;
extern const uint8_t *const jajp_s_aPenaltyTable;
extern const int32_t jajp_s_aPenaltyTable_n;
extern const uint8_t *const jajp_s_aPhrVectorTable;
extern const int32_t jajp_s_aPhrVectorTable_n;
extern const uint8_t *const jajp_s_aAccentTable;
extern const int32_t jajp_s_aAccentTable_n;
extern const uint8_t *const jajp_s_aNumMDTable;
extern const int32_t jajp_s_aNumMDTable_n;
extern const uint8_t *const jajp_s_aNumYomiTable;
extern const int32_t jajp_s_aNumYomiTable_n;
extern const uint8_t *const jajp_s_aNumJMDTable;
extern const int32_t jajp_s_aNumJMDTable_n;
extern const uint8_t *const jajp_s_aNumJCCTable;
extern const int32_t jajp_s_aNumJCCTable_n;
extern const uint8_t *const jajp_s_aHash4NDict;
extern const int32_t jajp_s_aHash4NDict_n;
extern const uint8_t *const jajp_s_aHash4TDict;
extern const int32_t jajp_s_aHash4TDict_n;
extern const uint8_t *const jajp_s_aHash4KDict;
extern const int32_t jajp_s_aHash4KDict_n;
extern const uint8_t *const jajp_s_aHash4KNDict;
extern const int32_t jajp_s_aHash4KNDict_n;
extern const uint8_t *const jajp_s_aHash4KTDict;
extern const int32_t jajp_s_aHash4KTDict_n;
extern const uint8_t *const jajp_s_aHash4EDict;
extern const int32_t jajp_s_aHash4EDict_n;
extern const uint8_t *const jajp_s_aItaijiHashTable;
extern const int32_t jajp_s_aItaijiHashTable_n;
extern const uint8_t *const jajp_s_aItaijiTable;
extern const int32_t jajp_s_aItaijiTable_n;
extern const uint8_t *const jajp_s_aTGTable;
extern const int32_t jajp_s_aTGTable_n;
extern const uint8_t *const jajp_s_aYomiDataTable;
extern const int32_t jajp_s_aYomiDataTable_n;
extern const uint8_t *const jajp_s_aPhraseDataTable;
extern const int32_t jajp_s_aPhraseDataTable_n;
extern const uint8_t *const jajp_s_aNumberDataTable;
extern const int32_t jajp_s_aNumberDataTable_n;
extern const uint8_t *const jajp_s_szFromStringOfRoman2Kana;
extern const int32_t jajp_s_szFromStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_szToStringOfRoman2Kana;
extern const int32_t jajp_s_szToStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_szRemainStringOfRoman2Kana;
extern const int32_t jajp_s_szRemainStringOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anFromPositionOfRoman2Kana;
extern const int32_t jajp_s_anFromPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anToPositionOfRoman2Kana;
extern const int32_t jajp_s_anToPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anRemainPositionOfRoman2Kana;
extern const int32_t jajp_s_anRemainPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anAccentValueOfRoman2Kana;
extern const int32_t jajp_s_anAccentValueOfRoman2Kana_n;
extern const uint8_t *const jajp_s_anAccentPositionOfRoman2Kana;
extern const int32_t jajp_s_anAccentPositionOfRoman2Kana_n;
extern const uint8_t *const jajp_s_nRoman2Kana;
extern const int32_t jajp_s_nRoman2Kana_n;
extern const uint8_t *const jajp_s_szFromStringOfEng2Roman;
extern const int32_t jajp_s_szFromStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_szToStringOfEng2Roman;
extern const int32_t jajp_s_szToStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_szRemainStringOfEng2Roman;
extern const int32_t jajp_s_szRemainStringOfEng2Roman_n;
extern const uint8_t *const jajp_s_anFromPositionOfEng2Roman;
extern const int32_t jajp_s_anFromPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anToPositionOfEng2Roman;
extern const int32_t jajp_s_anToPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anRemainPositionOfEng2Roman;
extern const int32_t jajp_s_anRemainPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_anAccentValueOfEng2Roman;
extern const int32_t jajp_s_anAccentValueOfEng2Roman_n;
extern const uint8_t *const jajp_s_anAccentPositionOfEng2Roman;
extern const int32_t jajp_s_anAccentPositionOfEng2Roman_n;
extern const uint8_t *const jajp_s_nEng2Roman;
extern const int32_t jajp_s_nEng2Roman_n;

/* unicodeconvt.obj */
extern const uint8_t *const jajp_USERINDEXSTR;
extern const int32_t jajp_USERINDEXSTR_n;
extern const uint8_t *const jajp_m_pAITable;
extern const int32_t jajp_m_pAITable_n;
extern const uint8_t *const jajp_m_pRTable;
extern const int32_t jajp_m_pRTable_n;
extern const uint8_t *const jajp_m_pKanaTable;
extern const int32_t jajp_m_pKanaTable_n;
extern const uint8_t *const jajp_m_pLeadByteTable1;
extern const int32_t jajp_m_pLeadByteTable1_n;
extern const uint8_t *const jajp_m_pLeadByteTable2;
extern const int32_t jajp_m_pLeadByteTable2_n;

/* jpnutil.obj */
extern const uint8_t *const jajp_k_gyo;
extern const int32_t jajp_k_gyo_n;
extern const uint8_t *const jajp_ky_gyo;
extern const int32_t jajp_ky_gyo_n;
extern const uint8_t *const jajp_s_gyo;
extern const int32_t jajp_s_gyo_n;
extern const uint8_t *const jajp_sh_gyo;
extern const int32_t jajp_sh_gyo_n;
extern const uint8_t *const jajp_t_gyo;
extern const int32_t jajp_t_gyo_n;
extern const uint8_t *const jajp_ch_gyo;
extern const int32_t jajp_ch_gyo_n;
extern const uint8_t *const jajp_ts_gyo;
extern const int32_t jajp_ts_gyo_n;
extern const uint8_t *const jajp_p_gyo;
extern const int32_t jajp_p_gyo_n;
extern const uint8_t *const jajp_py_gyo;
extern const int32_t jajp_py_gyo_n;
extern const uint8_t *const jajp_h_gyo;
extern const int32_t jajp_h_gyo_n;
extern const uint8_t *const jajp_hy_gyo;
extern const int32_t jajp_hy_gyo_n;
extern const uint8_t *const jajp_f_gyo;
extern const int32_t jajp_f_gyo_n;
extern const uint8_t *const jajp_n_gyo;
extern const int32_t jajp_n_gyo_n;
extern const uint8_t *const jajp_ny_gyo;
extern const int32_t jajp_ny_gyo_n;
extern const uint8_t *const jajp_m_gyo;
extern const int32_t jajp_m_gyo_n;
extern const uint8_t *const jajp_my_gyo;
extern const int32_t jajp_my_gyo_n;
extern const uint8_t *const jajp_r_gyo;
extern const int32_t jajp_r_gyo_n;
extern const uint8_t *const jajp_ry_gyo;
extern const int32_t jajp_ry_gyo_n;
extern const uint8_t *const jajp_y_gyo;
extern const int32_t jajp_y_gyo_n;
extern const uint8_t *const jajp_w_gyo;
extern const int32_t jajp_w_gyo_n;
extern const uint8_t *const jajp_g_gyo;
extern const int32_t jajp_g_gyo_n;
extern const uint8_t *const jajp_gy_gyo;
extern const int32_t jajp_gy_gyo_n;
extern const uint8_t *const jajp_z_gyo;
extern const int32_t jajp_z_gyo_n;
extern const uint8_t *const jajp_j_gyo;
extern const int32_t jajp_j_gyo_n;
extern const uint8_t *const jajp_d_gyo;
extern const int32_t jajp_d_gyo_n;
extern const uint8_t *const jajp_b_gyo;
extern const int32_t jajp_b_gyo_n;
extern const uint8_t *const jajp_by_gyo;
extern const int32_t jajp_by_gyo_n;
extern const uint8_t *const jajp_ty_gyo;
extern const int32_t jajp_ty_gyo_n;
extern const uint8_t *const jajp_fy_gyo;
extern const int32_t jajp_fy_gyo_n;
extern const uint8_t *const jajp_dy_gyo;
extern const int32_t jajp_dy_gyo_n;
extern const uint8_t *const jajp_lv_gyo;
extern const int32_t jajp_lv_gyo_n;
extern const uint8_t *const jajp_v_gyo;
extern const int32_t jajp_v_gyo_n;
extern const uint8_t *const jajp_QQ;
extern const int32_t jajp_QQ_n;
extern const uint8_t *const jajp_NN;
extern const int32_t jajp_NN_n;
extern const uint8_t *const jajp_k_romaji;
extern const int32_t jajp_k_romaji_n;
extern const uint8_t *const jajp_ky_romaji;
extern const int32_t jajp_ky_romaji_n;
extern const uint8_t *const jajp_s_romaji;
extern const int32_t jajp_s_romaji_n;
extern const uint8_t *const jajp_sh_romaji;
extern const int32_t jajp_sh_romaji_n;
extern const uint8_t *const jajp_t_romaji;
extern const int32_t jajp_t_romaji_n;
extern const uint8_t *const jajp_ch_romaji;
extern const int32_t jajp_ch_romaji_n;
extern const uint8_t *const jajp_ts_romaji;
extern const int32_t jajp_ts_romaji_n;
extern const uint8_t *const jajp_p_romaji;
extern const int32_t jajp_p_romaji_n;
extern const uint8_t *const jajp_py_romaji;
extern const int32_t jajp_py_romaji_n;
extern const uint8_t *const jajp_h_romaji;
extern const int32_t jajp_h_romaji_n;
extern const uint8_t *const jajp_hy_romaji;
extern const int32_t jajp_hy_romaji_n;
extern const uint8_t *const jajp_f_romaji;
extern const int32_t jajp_f_romaji_n;
extern const uint8_t *const jajp_n_romaji;
extern const int32_t jajp_n_romaji_n;
extern const uint8_t *const jajp_ny_romaji;
extern const int32_t jajp_ny_romaji_n;
extern const uint8_t *const jajp_m_romaji;
extern const int32_t jajp_m_romaji_n;
extern const uint8_t *const jajp_my_romaji;
extern const int32_t jajp_my_romaji_n;
extern const uint8_t *const jajp_r_romaji;
extern const int32_t jajp_r_romaji_n;
extern const uint8_t *const jajp_ry_romaji;
extern const int32_t jajp_ry_romaji_n;
extern const uint8_t *const jajp_y_romaji;
extern const int32_t jajp_y_romaji_n;
extern const uint8_t *const jajp_w_romaji;
extern const int32_t jajp_w_romaji_n;
extern const uint8_t *const jajp_g_romaji;
extern const int32_t jajp_g_romaji_n;
extern const uint8_t *const jajp_gy_romaji;
extern const int32_t jajp_gy_romaji_n;
extern const uint8_t *const jajp_z_romaji;
extern const int32_t jajp_z_romaji_n;
extern const uint8_t *const jajp_j_romaji;
extern const int32_t jajp_j_romaji_n;
extern const uint8_t *const jajp_d_romaji;
extern const int32_t jajp_d_romaji_n;
extern const uint8_t *const jajp_b_romaji;
extern const int32_t jajp_b_romaji_n;
extern const uint8_t *const jajp_by_romaji;
extern const int32_t jajp_by_romaji_n;
extern const uint8_t *const jajp_ty_romaji;
extern const int32_t jajp_ty_romaji_n;
extern const uint8_t *const jajp_fy_romaji;
extern const int32_t jajp_fy_romaji_n;
extern const uint8_t *const jajp_dy_romaji;
extern const int32_t jajp_dy_romaji_n;
extern const uint8_t *const jajp_lv_romaji;
extern const int32_t jajp_lv_romaji_n;
extern const uint8_t *const jajp_v_romaji;
extern const int32_t jajp_v_romaji_n;
extern const uint8_t *const jajp_Hrgn2KtknTbl;
extern const int32_t jajp_Hrgn2KtknTbl_n;

/* userdict.obj */
extern const uint8_t *const jajp_s_anUserDictData;
extern const int32_t jajp_s_anUserDictData_n;

/* phrasebuf.obj */
extern const uint8_t *const jajp_SokonTnknVerb;
extern const int32_t jajp_SokonTnknVerb_n;

/* numread.obj */
extern const uint8_t *const jajp_m_sanTCodes;
extern const int32_t jajp_m_sanTCodes_n;
extern const uint8_t *const jajp_SINDX;
extern const int32_t jajp_SINDX_n;

/* PCProsCtrl.obj */
extern const uint8_t *const jajp_s_aszCname;
extern const int32_t jajp_s_aszCname_n;
extern const uint8_t *const jajp_s_aszVname;
extern const int32_t jajp_s_aszVname_n;
extern const uint8_t *const jajp_s_aszLVname;
extern const int32_t jajp_s_aszLVname_n;

/* MakeReadableJP.obj */
extern const uint8_t *const jajp_szKANA_DASH;
extern const int32_t jajp_szKANA_DASH_n;
extern const uint8_t *const jajp_szKANA_ARS;
extern const int32_t jajp_szKANA_ARS_n;
extern const uint8_t *const jajp_szKANA_CLP;
extern const int32_t jajp_szKANA_CLP_n;
extern const uint8_t *const jajp_szKANA_COP;
extern const int32_t jajp_szKANA_COP_n;
extern const uint8_t *const jajp_szKANA_MXN;
extern const int32_t jajp_szKANA_MXN_n;

/* MakeReadableJP_SPR.obj */
extern const uint8_t *const jajp_j_phones;
extern const int32_t jajp_j_phones_n;
extern const uint8_t *const jajp_j_kana;
extern const int32_t jajp_j_kana_n;

/* TextNormalizer.obj */
extern const jajp_symbol jajp_aMakeReadableAnnos[];
extern const int32_t jajp_aMakeReadableAnnos_n;

/* MakeReadableJP.obj */
extern const jajp_symbol jajp_aCURRENCY_SYMBOLS[];
extern const int32_t jajp_aCURRENCY_SYMBOLS_n;
extern const jajp_symbol jajp_aCURRENCY_PUNCTS[];
extern const int32_t jajp_aCURRENCY_PUNCTS_n;
extern const jajp_symbol jajp_aDECIMAL_POINTS[];
extern const int32_t jajp_aDECIMAL_POINTS_n;
extern const jajp_symbol jajp_aPLUS_MINUS_SYMBOLS[];
extern const int32_t jajp_aPLUS_MINUS_SYMBOLS_n;
extern const jajp_symbol jajp_aRANGE_SYMBOLS[];
extern const int32_t jajp_aRANGE_SYMBOLS_n;
extern const jajp_symbol jajp_aDATE_SEPARATORS[];
extern const int32_t jajp_aDATE_SEPARATORS_n;
extern const jajp_symbol jajp_aDAYOFWEEK_SYMBOLS[];
extern const int32_t jajp_aDAYOFWEEK_SYMBOLS_n;
extern const jajp_symbol jajp_aPARENTHESIS_SYMBOLS[];
extern const int32_t jajp_aPARENTHESIS_SYMBOLS_n;
extern const jajp_symbol jajp_aTIME_DELIMS[];
extern const int32_t jajp_aTIME_DELIMS_n;
extern const jajp_symbol jajp_aTEL_SYMBOLS[];
extern const int32_t jajp_aTEL_SYMBOLS_n;
extern const jajp_symbol jajp_aBOOL_SYMBOLS[];
extern const int32_t jajp_aBOOL_SYMBOLS_n;

/* PCProsCtrl.obj */
extern const char *const jajp_s_aszPosInfo[];
extern const int32_t jajp_s_aszPosInfo_n;
extern const char *const jajp_s_aszSpecialPosInfo[];
extern const int32_t jajp_s_aszSpecialPosInfo_n;

#endif

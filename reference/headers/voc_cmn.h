/************************************************************************************/
/*                                                                                  */
/* Licensed Materials - Property of IBM					                            */
/* 11K6192 V2.1   AT7AENA, AT7PRNA V2.3  AT7A3NA V3.1  AS8P7ZZ V4.1  ASSF3ZZ V4.2  AT2T5ZZ V4.3 */
/* (C) Copyright IBM Corp. 2001. 2004  All Rights Reserved.			                */
/* US Government Users Restricted Rights - Use, duplication or disclosure           */
/* restricted by GSA ADP Schedule Contract with IBM Corp.			                */
/*										                                            */
/* The following IBM source code is provided to assist you in your		            */
/* development.  You may use this code only in accordance with the		            */
/* IBM License Agreement accompanying the Licensed Materials.		                */
/*										                                            */
/* This copyright statement may not be removed.				                        */
/************************************************************************************/

#ifndef INCL_VOC_CMN_H
#define INCL_VOC_CMN_H


#define VBUFTYPE_VOCAB 1
#define VBUFTYPE_WORDS 2
#define VBUFTYPE_BASEFORM 3

typedef int VOCRC;

#define VOC_BASE_OFFSET                     0x90000

#define VOC_RC_OK                           0

#define VOC_RC_DUP_VOCABID                  (VOC_BASE_OFFSET + 0x8000)
#define VOC_RC_GRAMMAR_TOO_COMPLEX          (VOC_BASE_OFFSET + 0x8001)
#define VOC_RC_GRAMMAR_SYNTAX_ERROR         (VOC_BASE_OFFSET + 0x8002)
#define VOC_RC_INVALID_BNF_FORMAT           (VOC_BASE_OFFSET + 0x8002)
#define VOC_RC_INVALID_POOL_FILE            (VOC_BASE_OFFSET + 0x8003)
#define VOC_RC_INVALID_ROOT_NAME            (VOC_BASE_OFFSET + 0x8004)
#define VOC_RC_INVALID_SPELLING             (VOC_BASE_OFFSET + 0x8005)
#define VOC_RC_INVALID_WORDS                (VOC_BASE_OFFSET + 0x8006)
#define VOC_RC_INVALID_GRAMMAR_FORMAT       (VOC_BASE_OFFSET + 0x8007)
#define VOC_RC_INVALID_GRAMMAR_TYPE         (VOC_BASE_OFFSET + 0x8007)
#define VOC_RC_INVALID_GRAMMAR_NAME         (VOC_BASE_OFFSET + 0x8008)
#define VOC_RC_INVALID_GRAMMAR_ENCODING     (VOC_BASE_OFFSET + 0x8009)
#define VOC_RC_ALLOC_CB_NOT_REGISTERED      (VOC_BASE_OFFSET + 0x800A)
#define VOC_RC_ALLOC_CB_REGISTERED          (VOC_BASE_OFFSET + 0x800B)
#define VOC_RC_INCLUDE_CB_NOT_REGISTERED    (VOC_BASE_OFFSET + 0x800C)
#define VOC_RC_INCLUDE_CB_REGISTERED        (VOC_BASE_OFFSET + 0x800D)
#define VOC_RC_LEXICON_CB_NOT_REGISTERED    (VOC_BASE_OFFSET + 0x800E)
#define VOC_RC_LEXICON_CB_REGISTERED        (VOC_BASE_OFFSET + 0x800F)
#define VOC_RC_INVALID_OPTIMIZATION         (VOC_BASE_OFFSET + 0x8010)
#define VOC_RC_INVALID_VSETPARMS            (VOC_BASE_OFFSET + 0x8011)
#define VOC_RC_INVALID_ANNOTATION           (VOC_BASE_OFFSET + 0x8012)
#define VOC_RC_UNDEFINED_NONTERMINAL        (VOC_BASE_OFFSET + 0x8013)
#define VOC_RC_INVALID_NONTERMINAL          (VOC_BASE_OFFSET + 0x8013)
#define VOC_RC_LOOP_IN_GRAMMAR              (VOC_BASE_OFFSET + 0x8014)
#define VOC_RC_GRAMMAR_TYPE_MISMATCH        (VOC_BASE_OFFSET + 0x8015)
#define VOC_RC_MISSING_INCLUDE              (VOC_BASE_OFFSET + 0x8016)
#define VOC_RC_MISSING_LEXICON              (VOC_BASE_OFFSET + 0x8017)
#define VOC_RC_VSET_INITIALIZED             (VOC_BASE_OFFSET + 0x8018)
#define VOC_RC_BFMODEL_REGISTERED           (VOC_BASE_OFFSET + 0x8019)
#define VOC_RC_BFMODEL_NOT_REGISTERED       (VOC_BASE_OFFSET + 0x8020)
#define VOC_RC_COMPILATION_ABORTED          (VOC_BASE_OFFSET + 0x8021)
#define VOC_RC_NO_SWAP                      (VOC_BASE_OFFSET + 0x8022)
#define VOC_RC_INVALID_IMAGE                (VOC_BASE_OFFSET + 0x8023)
#define VOC_RC_AM_MISMATCH                  (VOC_BASE_OFFSET + 0x8024)
#define VOC_RC_ENDIAN_MISMATCH              (VOC_BASE_OFFSET + 0x8025)
#define VOC_RC_ENGINE_MISMATCH              (VOC_BASE_OFFSET + 0x8026)
#define VOC_RC_VOCABSET_MISMATCH            (VOC_BASE_OFFSET + 0x8027)

#define VOC_RC_INVALID_ENG_FILE             (VOC_BASE_OFFSET + 0xC000)
#define VOC_RC_INVALID_VOCABSET_TYPE        (VOC_BASE_OFFSET + 0xC001)
#define VOC_RC_TTS_ERROR                    (VOC_BASE_OFFSET + 0xC002)
#define VOC_RC_BFMODEL_ERROR                (VOC_BASE_OFFSET + 0xC003)
#define VOC_RC_INVALID_BFMODEL              (VOC_BASE_OFFSET + 0xC004)
#define VOC_RC_NOT_SUPPORTED                (VOC_BASE_OFFSET + 0xC005)

#define VOC_RC_ALREADY_INITIALIZED          (VOC_BASE_OFFSET + EVV_RC_ALREADY_INITIALIZED )
#define VOC_RC_NOT_INITIALIZED              (VOC_BASE_OFFSET + EVV_RC_NOT_INITIALIZED )
#define VOC_RC_MEM_ALLOC_ERROR              (VOC_BASE_OFFSET + EVV_RC_MEM_ALLOC_ERROR)
#define VOC_RC_INVALID_STRING_TYPE          (VOC_BASE_OFFSET + EVV_RC_INVALID_STRING_TYPE)
#define VOC_RC_INVALID_HANDLE               (VOC_BASE_OFFSET + EVV_RC_INVALID_HANDLE)
#define VOC_RC_INVALID_LANGUAGE             (VOC_BASE_OFFSET + EVV_RC_INVALID_LANGUAGE)
#define VOC_RC_INVALID_BASEFORM             (VOC_BASE_OFFSET + EVV_RC_INVALID_BASEFORM)
#define VOC_RC_INVALID_REQUEST              (VOC_BASE_OFFSET + EVV_RC_INVALID_REQUEST)
#define VOC_RC_LOCKFAIL                     (VOC_BASE_OFFSET + EVV_RC_LOCK_FAIL)
#define VOC_RC_INVALID_FLAG                 (VOC_BASE_OFFSET + EVV_RC_INVALID_FLAG)
#define VOC_RC_INVALID_VALUE                (VOC_BASE_OFFSET + EVV_RC_INVALID_VALUE)
#define VOC_RC_BASEFORM_TOO_LONG            (VOC_BASE_OFFSET + EVV_RC_BASEFORM_TOO_LONG)
#define VOC_RC_CB_NOT_REGISTERED            (VOC_BASE_OFFSET + EVV_RC_CB_NOT_REGISTERED)
#define VOC_RC_CB_REGISTERED                (VOC_BASE_OFFSET + EVV_RC_CB_REGISTERED)

#define VOC_RC_INVALID_DATA_FILE            (VOC_BASE_OFFSET + EVV_RC_INVALID_DATA_FILE)
#define VOC_RC_LANGUAGE_MISMATCH            (VOC_BASE_OFFSET + EVV_RC_LANGUAGE_MISMATCH)
#define VOC_RC_INVALID_INST                 (VOC_BASE_OFFSET + EVV_RC_INVALID_INST)
#define VOC_RC_INVALID_CALLBACK             (VOC_BASE_OFFSET + EVV_RC_INVALID_CALLBACK)
#define VOC_RC_INTERNAL_ERROR               (VOC_BASE_OFFSET + EVV_RC_INTERNAL_ERROR)
#define VOC_RC_NOT_AVAILABLE                (VOC_BASE_OFFSET + EVV_RC_FUNCTION_UNAVAILABLE)

typedef void *VOCInstHandle ;
typedef void *VOCPoolHandle ;
typedef void *VOCEBGModelHandle;

typedef unsigned long VOCBlockFlags;

#define VOC_BLOCK_TR                   0x00000001

#define VOC_BLOCK_ALL                  0x00000001

typedef struct _VOCInitializationData
{
    VOCBlockFlags ulValidBlocks;
    
    void *pTrBlock;
} VOCInitializationData;

typedef enum
{
    VOC_VSTYPE_DETAILEDMATCH = 0,
        VOC_VSTYPE_FASTMATCH = 1
} VOCVocabSetType;

typedef enum
{
    VOC_OPT_SPEED = 0,
        VOC_OPT_MEMORY = 1
} VOCOptimizationType;

typedef enum
{
    VOC_GRAMMAR_BNF = 0,
    VOC_GRAMMAR_JSGF = 1,
    VOC_GRAMMAR_ABNF = 2,
    VOC_GRAMMAR_XML  = 3,

    VOC_GRAMMAR_INCLUDE_BNF = 50,
    VOC_GRAMMAR_IMPORT_JSGF = 51,
    VOC_GRAMMAR_IMPORT_ABNF = 52,
    VOC_GRAMMAR_IMPORT_XML  = 53
} VOCGrammarType;


#define VOC_ATTRS_VOCABSETTYPE                  0x00000001
#define VOC_ATTRS_OPTIMIZATION                  0x00000002
#define VOC_ATTRS_GRAMMARTYPE                   0x00000004
#define VOC_ATTRS_RESERVED1                     0x00000008
#define VOC_ATTRS_RESERVED2                     0x00000010
#define VOC_ATTRS_VSETPARMS                     0x00000020
#define VOC_ATTRS_BSFMMETHOD                    0x00000040
#define VOC_ATTRS_MAXBASEFORM                   0x00000080

typedef enum {
    VOC_BSFM_TTS        = 0x00000001,
    VOC_BSFM_PHONETIC   = 0x00000002
} VOCBsfmMethod;

#define VOC_VSET_STANDARD                       0x00000001
#define VOC_VSET_EXTENDED                       0x00000002

typedef unsigned long VOCAttrFlags;
typedef unsigned long VOCVsetParms;
typedef unsigned long VOCBfAttrFlags;

typedef struct _VOCAttrs
{
    VOCAttrFlags        ulValidAttrs;
    VOCVocabSetType     ulVocabSetType; 
    VOCOptimizationType ulOptimization;
    VOCGrammarType      ulGrammarType;
    VOCVsetParms        VsetParms; 
    VOCBsfmMethod       BsfmMethod;
    unsigned long       ulMaxBaseforms;
    
    unsigned long       ulReserved1;
    unsigned long       ulReserved2;  
} VOCAttrs;


typedef struct _VOCDocResolutionData
{
    unsigned char *pszDocumentURI;
    unsigned char *pszParentGrammarName;
    unsigned char *pszMediaType;
    unsigned char *pszBaseURI;
    
} VOCDocResolutionData;


/*---------------------*/
/* Baseform Generation */
/*---------------------*/
typedef enum
{
    EBG_MODELTYPE_PBG = 1,  /*Phonetic Baseform generation model*/
} EBGModelType;

typedef struct _VOCEBGModel
{
    EVVLocale       Lang;
    EBGModelType    BsfmModelType;
    void *          pBsfmModelData;
} VOCEBGModel;


typedef unsigned char VOCPool;

typedef VOCRC (VocAllocCBnp)(void **ppBuffer, int iSize, int iType, void *pUserData);
typedef VocAllocCBnp  *VocAllocCB;
typedef int (*VocAllocBufferCB)(void **ppBuffer, int iSize, int iType, void *pUserData);

typedef int (VocVsetIncludeCBnp)(VOCInstHandle hVocInst, VOCDocResolutionData *pIncludeData,
                              EStringType StrType, VOCGrammarType ulGrammarFormat, VOCVocabSetType ulVocabSetType, void *pUserData);
typedef VocVsetIncludeCBnp  *VocVsetIncludeCB;

typedef int (VocVsetLexiconCBnp)(VOCInstHandle hVocInst, VOCDocResolutionData *pLexiconData,
                              EStringType StrType, VOCGrammarType ulGrammarFormat, void *pUserData);
typedef VocVsetLexiconCBnp  *VocVsetLexiconCB;


/*----------------------------*/
/* Precompiled Vocabulary Set */
/*----------------------------*/
typedef struct _VOCVsetInfo
{    
    int             iVsetVersion;
    int             iGCVersion;          
    EVVEndianness   Endianness;
    char            szAMName[EVV_MAX_AM_NAMELEN + 1];
} VOCVsetInfo;                  


#endif

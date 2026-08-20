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

#ifndef INCL_VOC_H
#define INCL_VOC_H

#include "esr.h"
#include "esr_cmn.h"
#include "edu.h"
#include "ecomdefs.h"
#include "voc_cmn.h"


#ifdef __cplusplus
extern "C" {
#endif
    VOCRC vocCreate( VOCInstHandle *phVocInst  );
    VOCRC vocDestroy( VOCInstHandle hVocInst );
    VOCRC vocRegisterAllocCB( VOCInstHandle hVocInst, VocAllocCB pCB, void *pUserData );
    VOCRC vocUnregisterAllocCB( VOCInstHandle hVocInst, void **ppUserData );
    VOCRC vocPoolCreate( VOCInstHandle hVocInst, EVVLocale Lang, VOCPoolHandle *pHandle );
    VOCRC vocPoolAddSpelling(VOCInstHandle hVocInst, VOCPoolHandle hPool, 
        const unsigned char *pszSpelling, EStringType StrType, ESRBaseformResult  *pBaseform );
    VOCRC vocPoolDeleteSpelling( VOCInstHandle hVocInst, VOCPoolHandle hPool, 
        unsigned char *pszSpelling, EStringType StrType, ESRBaseformResult *pBaseform );
    VOCRC vocPoolEnable( VOCInstHandle hVocInst, VOCPoolHandle hPool );
    VOCRC vocPoolDisable( VOCInstHandle hVocInst, VOCPoolHandle hPool );
    VOCRC vocPoolDestroy( VOCInstHandle hVocInst, VOCPoolHandle hPool );
    VOCRC vocBsfmGenerate(  VOCInstHandle hVocInst,  EVVLocale Lang, unsigned char *pszSpelling,  
        EStringType StrType,  ESRBaseformResult **ppESRBaseform, int *piESRBaseformSize );    
    VOCRC vocBsfmRegisterModel(VOCInstHandle hVocInst,  VOCEBGModel *pBfData, VOCEBGModelHandle *phBsfmModel);
    VOCRC vocBsfmUnregisterModel(VOCInstHandle hVocInst, VOCEBGModelHandle hBsfmModel);
    VOCRC vocVsetRegisterIncludeCB( VOCInstHandle hVocInst, VocVsetIncludeCB pCB, void *pUserData );
    VOCRC vocVsetUnregisterIncludeCB( VOCInstHandle hVocInst, void **ppUserData );
    VOCRC vocVsetRegisterLexiconCB( VOCInstHandle hVocInst, VocVsetLexiconCB pCB, void *pUserData );
    VOCRC vocVsetUnregisterLexiconCB( VOCInstHandle hVocInst, void **ppUserData );
    VOCRC vocSetAttrs( VOCInstHandle hVocInst, VOCAttrs *pInitialAttrs );
    VOCRC vocGetAttrs( VOCInstHandle hVocInst, VOCAttrs *pVocAttrs );
    VOCRC vocVsetInitialize(  VOCInstHandle hVocInst, EStringType StrType, VOCInitializationData *pInitializationData);
    VOCRC vocVsetUninitialize( VOCInstHandle hVocInst );
    VOCRC vocVsetAddSource( VOCInstHandle hVocInst, VOCGrammarType ulGrammartype, VOCVocabSetType ulVocabSetType, 
         EStringType StrType, unsigned char *pszGrammarName, unsigned char *pszGrammarBuffer ,int iGrammarLength);
    VOCRC vocVsetGenerate(  VOCInstHandle hVocInst, VOCPool **ppPools, int iNumPools,          
         ESRVocabSet **ppVocabSet, int *piVocabSetSize, unsigned char **ppszInvalidWords );
    VOCRC vocVsetAbort( VOCInstHandle hVocInst );


    VOCRC vocVsetQueryInfo( VOCInstHandle hVocInst, ESRVocabSet * pVset, int iVsetSize, VOCVsetInfo * pVsetInfo );
    VOCRC vocVsetCompareInfo( VOCInstHandle hVocInst, VOCVsetInfo * pVsetInfo, ESREngineInfo * pEsrEngineInfo );
    VOCRC vocVsetByteSwap( VOCInstHandle hVocInst, ESRVocabSet * pVset, int iVsetSize, ESRVocabSet * pVsetOutput );

    //------------------
    // Deprecated API
    //------------------
    VOCRC vocGenerateBaseform(  VOCInstHandle hVocInst,  EVVLocale Lang, 
                                unsigned char *pszSpelling,  EStringType StrType,  ESRBaseformResult **ppESRBaseform, 
                                int *piESRBaseformSize );
    VOCRC vocGenerateVocabSet(  VOCInstHandle hVocInst, VOCInitializationData *pInitializationData,
                                VOCAttrs *pInitialAttrs, unsigned char **ppszBnfBuffer, int iNumBnfBuffers,
                                EStringType StrType, VOCPool **ppPools, int iNumPools, ESRVocabSet **ppVocabSet,
                                int *piVocabSetSize, unsigned char **ppszInvalidWords );


#ifdef __cplusplus
}
#endif

#endif

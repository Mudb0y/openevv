/*******************************************************************************/
/*                                                                             */
/* Licensed Materials - Property of IBM                                        */
/* 11K6192 V2.1  AT2T5ZZ v4.3						       */                      
/* (C) Copyright IBM Corp. 1999. 2004  All Rights Reserved.                    */
/* US Government Users Restricted Rights - Use, duplication or disclosure      */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                      */
/*                                                                             */
/* The following IBM source code is provided to assist you in your             */
/* development.  You may use this code only in accordance with the             */
/* IBM License Agreement accompanying the Licensed Materials.                  */
/*                                                                             */
/* This copyright statement may not be removed.                                */
/*                                                                             */
/*******************************************************************************/

#ifndef INCL_BFM_H
#define INCL_BFM_H
#include <esr.h>

typedef int BFMRC;

#define BFM_BASE_OFFSET                  0x20000

#define BFM_RC_OK                        0


#define BFM_RC_INVALID_BASEFORM                              (BFM_BASE_OFFSET + EVV_RC_INVALID_BASEFORM)
#define BFM_RC_INVALID_LANGUAGE                              (BFM_BASE_OFFSET + EVV_RC_INVALID_LANGUAGE)
#define BFM_RC_INTERNAL_ERROR                                (BFM_BASE_OFFSET + EVV_RC_INTERNAL_ERROR)
#define BFM_RC_MEM_ALLOC                                     (BFM_BASE_OFFSET + EVV_RC_MEM_ALLOC_ERROR)
#define BFM_RC_LANGUAGE_MISMATCH                             (BFM_BASE_OFFSET + EVV_RC_LANGUAGE_MISMATCH)
#define BFM_RC_BUF_OVERFLOW                                  (BFM_BASE_OFFSET + 0x8000)
#define BFM_RC_INVALID_INDEX                                 (BFM_BASE_OFFSET + 0x8001)


#ifdef __cplusplus
extern "C" {
#endif
BFMRC bfmConvertEsrToSpr(EVVLocale Lang, ESRBaseformResult *pSrc, int iSrcSize, char *pszDest, int *piDestSize);
BFMRC bfmConvertSprToEsr(EVVLocale Lang, char *pszSrc, ESRBaseformResult *pDest, int *piDestSize);
BFMRC bfmMergeEsr(EVVLocale Lang, ESRBaseformResult *pSrcA, int iSrcASize, ESRBaseformResult *pSrcB, int iSrcBSize, ESRBaseformResult *pDest, int *piDestSize );
BFMRC bfmJoinEsr(EVVLocale Lang, ESRBaseformResult *pSrcA, int iSrcASize, ESRBaseformResult *pSrcB, int iSrcBSize, ESRBaseformResult *pDest, int *piDestSize );

BFMRC bfmEsrSetPronunciation( EVVLocale Lang, char *pszSrc, ESRBaseformResult *pDest, int *piDestSize );
BFMRC bfmEsrGetPronunciationInfo( ESRBaseformResult *pSrc, int piSrcSize, int *piNumPronunciations, EVVLocale *pLang );
BFMRC bfmEsrGetPronunciationStr( ESRBaseformResult *pSrc, int iSrcSize, int iPronunciatonIndex, char *pszDest, int *piDestSize );

#ifdef __cplusplus
}
#endif

#endif

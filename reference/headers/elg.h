/*=========================================================================*/
/*                                                                         */
/* elg.h                                                                   */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1, AT2T5ZZ v4.3 			                           */
/* (C) Copyright IBM Corp. 1999, 2004  All Rights Reserved.                */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement for the IBM Embedded ViaVoice, Multiplatform      */
/* Edition.                                                                */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*                                                                         */
/*=========================================================================*/

#ifndef _ELG_H_
#define _ELG_H_

#include "ecomdefs.h"

typedef int ELGRC;

#define ELG_RC_OK                                                            0
#define ELG_RC_INVALID_DEBUG_LEVEL                                           1
#define ELG_RC_INVALID_COMPONENT_ID                                          2
#define ELG_RC_DEBUGLOG_OUTPUT_CALLBACK_ALREADY_REGISTERED                   3
#define ELG_RC_TRACELOG_OUTPUT_CALLBACK_ALREADY_REGISTERED                   4
#define ELG_RC_DEBUGLOG_FAILED                                               5
#define ELG_RC_TRACELOG_FAILED                                               6
#define ELG_RC_INVALID_STRING_TYPE                                           7

#define ELG_DEBUGLOG_OUTPUT_MAXIMUM                                        512
#define ELG_TRACELOG_OUTPUT_MAXIMUM                                        512


typedef enum
{
        ELG_COMPID_MINIMUM,

        ELG_COMPID_AOP,
        ELG_COMPID_DEV,
        ELG_COMPID_EAL,
        ELG_COMPID_EDU,
        ELG_COMPID_ENG,
        ELG_COMPID_ESR,
        ELG_COMPID_RAL,
        ELG_COMPID_TTS,
        ELG_COMPID_VOC,
        ELG_COMPID_BFM,

        ELG_COMPID_MAXIMUM

} ELGCompId;

typedef enum
{
        ELG_LEVEL_MINIMUM,

        ELG_LEVEL_DISABLED,
        ELG_LEVEL_APIERRORS,
        ELG_LEVEL_ENTRYEXIT,
        ELG_LEVEL_INTERNALERRORS,
        ELG_LEVEL_PARMVALUES,
        ELG_LEVEL_ADD_INFO,

        ELG_LEVEL_MAXIMUM

} ELGLevels;

VVE_DECLSPEC  int g_iENGLogLevel;
VVE_DECLSPEC  int g_iTTSLogLevel;
VVE_DECLSPEC  int g_iESRLogLevel;
VVE_DECLSPEC  int g_iEALLogLevel;
VVE_DECLSPEC  int g_iAOPLogLevel;
VVE_DECLSPEC  int g_iRALLogLevel;
VVE_DECLSPEC  int g_iEDULogLevel;
VVE_DECLSPEC  int g_iDEVLogLevel;
VVE_DECLSPEC  int g_iVOCLogLevel;
VVE_DECLSPEC  int g_iBFMLogLevel;

#define ELG_ENG_LOGLEVEL   (g_iENGLogLevel)
#define ELG_TTS_LOGLEVEL   (g_iTTSLogLevel)
#define ELG_ESR_LOGLEVEL   (g_iESRLogLevel)
#define ELG_EAL_LOGLEVEL   (g_iEALLogLevel)
#define ELG_AOP_LOGLEVEL   (g_iAOPLogLevel)
#define ELG_RAL_LOGLEVEL   (g_iRALLogLevel)
#define ELG_EDU_LOGLEVEL   (g_iEDULogLevel)
#define ELG_DEV_LOGLEVEL   (g_iDEVLogLevel)
#define ELG_VOC_LOGLEVEL   (g_iVOCLogLevel)
#define ELG_BFM_LOGLEVEL   (g_iBFMLogLevel)

VVE_DECLSPEC ELGRC elgSetTraceLevel(
                              ELGCompId CompId,
                              ELGLevels Level
                             );

VVE_DECLSPEC ELGRC elgGetTraceLevel(
                              ELGCompId CompId,
                              ELGLevels *pLevel
                             );

typedef void (ELGTraceLogCB)(EStringType, void *, void *);


VVE_DECLSPEC ELGRC elgRegisterTraceLogOutputCB(ELGTraceLogCB *pFunc,
                                          void *pUserData
                                         );

VVE_DECLSPEC void  elgUnregisterTraceLogOutputCB(void **ppUserData);


#endif

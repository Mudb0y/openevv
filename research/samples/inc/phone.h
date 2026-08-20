/*=========================================================================*/
/*                                                                         */
/* phone.h                                                                 */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1 AT2T5ZZ V4.3 		                                   */
/* (C) Copyright IBM Corp. 2000, 2004  All Rights Reserved.                */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement for the IBM Embedded ViaVoice, Multiplatform      */
/* Edition                                                                 */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*                                                                         */
/*=========================================================================*/

#if !defined( _PHONE_H_ )
#define _PHONE_H_


#include "os.h"
#include "esr.h"

#include "appevent.h"

#define NAME_LEN        32

typedef struct _RecoStateChangeData
{
   SEM_HANDLE hSemEvent;
   ESRRecoStates SignalState;
   ESRRecoStates LastState;
} RecoStateChangeData;

typedef struct _AcbfStateChangeData
{
   SEM_HANDLE hSemEvent;
   ESRRecoStates SignalState;
   ESRRecoStates LastState;
} AcbfStateChangeData;

typedef struct _AcbfResultData
{
   long lErr;
   ESRBaseformResult *pResult;
   int iLen;
} AcbfResultData;

typedef struct _RecoResultData
{
   AppEvent *pEvent;
} RecoResultData;

typedef struct _AudioCBData
{
   unsigned char *pAudioBuf;
   unsigned long AudioBufSize;
} AudioCBData;

typedef struct _ImportNameList
{
        char szName[NAME_LEN+1];
        int iPhoneNum[11];
} ImportNameList;


/* Application function return codes */
#define APPRC_OK     0
#define APPRC_ERROR  1

#endif   /* _PHONE_H_ */

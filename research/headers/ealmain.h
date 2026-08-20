/*=========================================================================*/
/*                                                                         */
/* ealmain.h                                                               */
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

#ifndef __EALMAIN_H__
#define __EALMAIN_H__


#include "ecomdefs.h"
#include "ealrc.h"
#include "ealdefs.h"


#ifdef __cplusplus
extern "C" {
#endif

VVE_DECLSPEC EALRC ealOpen( void );
VVE_DECLSPEC EALRC ealClose( void );

VVE_DECLSPEC EALRC ealQueryDevCount  ( EALDevType     devType,
                                       unsigned long  *pCount );

VVE_DECLSPEC EALRC ealQueryDevInfo   ( EALDevId          devId,
                                       EALDevType        devType,
                                       EALQueryInfoFlags *pFlags,
                                       EALQueryInfo      *pInfo);

VVE_DECLSPEC EALRC ealQueryDevCaps   ( EALDevId           devId,
                                       EALDevType         devType,
                                       EALQueryCapsFlags *pFlags,
                                       EALQueryCaps      *pCaps );

VVE_DECLSPEC EALRC ealDevCreate      ( EALDevId             devId,
                                       EALDevType           devType,
                                       EALDevHandle         *pDevHandle,
                                       EALDevCreateOptions  *options,
                                       OSTaskInfo           *pTaskInfo );

VVE_DECLSPEC EALRC ealDevDestroy     ( EALDevHandle *pDevHandle );

VVE_DECLSPEC EALRC ealDevOpen        ( EALDevHandle      hDev,
                                       EALAudioFormat    *pFormat,
                                       EALDevOpenOptions *pOptions  );

VVE_DECLSPEC EALRC ealDevClose       ( EALDevHandle      hDev );

VVE_DECLSPEC EALRC ealDevStart       ( EALDevHandle         hDev,
                                       EALDevStartOptions   *pOptions);

VVE_DECLSPEC EALRC ealDevStop        ( EALDevHandle hDev );

VVE_DECLSPEC EALRC ealDevReset       ( EALDevHandle hDev );

VVE_DECLSPEC EALRC ealDevQueryState  ( EALDevHandle   hDev,
                                       EALState       *pState );

VVE_DECLSPEC EALRC ealDevQueryID     ( EALDevHandle   hDev,
                                       EALDevId       *pDevId );

VVE_DECLSPEC EALRC ealDevRegisterBuffer    ( EALDevHandle   hDev,
                                             EALBufInfo     *pBufInfo);

VVE_DECLSPEC EALRC ealDevUnregisterBuffer  ( EALDevHandle   hDev,
                                             EALBufInfo     *pBufInfo);

VVE_DECLSPEC EALRC ealDevRegisterStateCB   ( EALDevHandle   hDev,
                                             EALDevStateCB  *pFunc,
                                             void           *pUserData );

VVE_DECLSPEC EALRC ealDevRegisterErrorCB   ( EALDevHandle   hDev,
                                             EALDevErrorCB  *pFunc,
                                             void           *pUserData );

VVE_DECLSPEC EALRC ealDevRegisterBufDoneCB ( EALDevHandle      hDev,
                                             EALDevBufDoneCB   *pFunc,
                                             void              *pUserData );

VVE_DECLSPEC EALRC ealDevUnregisterStateCB   ( EALDevHandle   hDev,
                                               void           **ppUserData);

VVE_DECLSPEC EALRC ealDevUnregisterErrorCB   ( EALDevHandle   hDev,
                                               void           **ppUserData);

VVE_DECLSPEC EALRC ealDevUnregisterBufDoneCB ( EALDevHandle   hDev,
                                               void           **ppUserData);


VVE_DECLSPEC EALRC ealMixGetVolume  (  EALDevId devId,
                                       EALDevType devType,
                                       EALMixVolumeData *pVolumeData );

VVE_DECLSPEC EALRC ealMixSetVolume  (  EALDevId devId,
                                       EALDevType devType,
                                       EALMixVolumeData *pVolumeData );


VVE_DECLSPEC EALRC ealMixGetMute  ( EALDevId       devId,
                                    EALDevType     devType,
                                    EALMixMuteData *pMuteData );

VVE_DECLSPEC EALRC ealMixSetMute  ( EALDevId       devId,
                                    EALDevType     devType,
                                    EALMixMuteData *pMuteData );

VVE_DECLSPEC EALRC ealQueryDevFormat ( EALDevId       devId,
                                       EALDevType     devType,
                                       EALAudioFormat *pFormat);

VVE_DECLSPEC EALRC ealRegisterCtrl   ( EALDevId        devId,
                                       EALDevType      devType,
                                       EALCtrl         *pFunc,
                                       void            *pRegData,
                                       EALCtrlErrorCB  *pErrorCB );

VVE_DECLSPEC EALRC ealUnregisterCtrl ( EALDevId   devId,
                                       EALDevType devType,
                                       EALCtrl    *pFunc,
                                       void       *pUnregData);

VVE_DECLSPEC EALRC ealDevRegisterFltr  ( EALDevHandle     hDev,
                                         EALFltr         *pFunc,
                                         void            *pRegData,
                                         EALFltrErrorCB  *pErrorCB );

VVE_DECLSPEC EALRC ealDevUnregisterFltr( EALDevHandle   hDev,
                                         EALFltr       *pFunc,
                                         void          *pUnregData);

VVE_DECLSPEC EALRC ealEnableCtrl  ( EALDevId   devId,
                                    EALDevType devType,
                                    EALCtrl    *pFunc);

VVE_DECLSPEC EALRC ealDevEnableFltr  ( EALDevHandle  hDev,
                                       EALFltr       *pFunc);

VVE_DECLSPEC EALRC ealDisableCtrl ( EALDevId        devId,
                                    EALDevType      devType,
                                    EALCtrl        *pFunc );

VVE_DECLSPEC EALRC ealDevDisableFltr ( EALDevHandle hDev,
                                       EALFltr *pFunc);

VVE_DECLSPEC EALRC ealCallCtrlErrorCB( EALCtrlHandle hCtrl,
                                       void *pData );

VVE_DECLSPEC EALRC ealCallFltrErrorCB( EALFltrHandle hFltr,
                                       void *pData );

VVE_DECLSPEC EALRC ealMixRegisterMap   (  EALDevId devId,
                                          EALDevType devType,
                                          const EALMixMap *pMap);

VVE_DECLSPEC EALRC ealMixUnregisterMap  ( EALDevId devId,
                                          EALDevType devType,
                                          EALMixMap **ppMap);

VVE_DECLSPEC EALRC ealMixQueryMapValues ( EALDevId devId,
                                          EALDevType devType,
                                          unsigned long initialVolume,
                                          long inPercent,
                                          EALMapPair *pPairHi,
                                          EALMapPair *pPairLo);

VVE_DECLSPEC EALRC ealMixQueryMapNextStep( EALDevId devId,
                                           EALDevType devType,
                                           unsigned long initialVolume,
                                           EALMapPair *pPair);

VVE_DECLSPEC EALRC ealMixQueryMapPrevStep( EALDevId devId,
                                           EALDevType devType,
                                           unsigned long initialVolume,
                                           EALMapPair *pPair);
#ifdef __cplusplus
}
#endif
#endif   /* __EALMAIN_H__ */

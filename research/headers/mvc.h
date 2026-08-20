/*=========================================================================*/
/*                                                                         */
/* mvc.h                                                                   */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* AS8P7ZZ V4.1 AT2T5ZZ v4.3 		                                   */
/* (C) Copyright IBM Corp. 2001,2004  All Rights Reserved.                 */
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
#ifndef _MVC_H_
#define _MVC_H_

#include<windows.h>
#ifdef __cplusplus
extern "C" {

#endif
/* EXPORT_IMPORT define */
#ifdef EVV
   #define EXPORT_IMPORT __declspec(dllexport)
   EXPORT_IMPORT DILRC evvDakAudioInit(HWAVEIN devHandle);
   EXPORT_IMPORT DILRC evvDakAudioInitChan(HWAVEIN devHandle,unsigned int *channels);
   EXPORT_IMPORT DILRC evvDakAudioUninit(HWAVEIN devHandle);
   EXPORT_IMPORT DILRC evvDakAudioGetMicVol(HWAVEIN devHandle, int *piMicVol);
   EXPORT_IMPORT DILRC evvDakAudioSetMicVol(HWAVEIN devHandle, int iMicVol);
   EXPORT_IMPORT DILRC evvDakAudioGetMicVolChan(HWAVEIN devHandle, int *piMicVol,int channel);
   EXPORT_IMPORT DILRC evvDakAudioSetMicVolChan(HWAVEIN devHandle, int iMicVol,int channel);
   EXPORT_IMPORT DILRC evvDakAudioGetAGCState(HWAVEIN devHandle, int *piState);
   EXPORT_IMPORT DILRC evvDakAudioSetAGCState(HWAVEIN devHandle, int iState);

#else
   #define EXPORT_IMPORT __declspec(dllimport)


   /* Data Types for function pointers */
   typedef DILRC (*PFUNCInit)(HWAVEIN devHandle);
   typedef DILRC (*PFUNCInitChan)(HWAVEIN devHandle,int *channels);
   typedef DILRC (*PFUNCUninit)(HWAVEIN devHandle);
   typedef DILRC (*PFUNCGetMicVol)(HWAVEIN devHandle, int *piMicVol);
   typedef DILRC (*PFUNCSetMicVol)(HWAVEIN devHandle, int iMicVol);
   typedef DILRC (*PFUNCGetMicVolChan)(HWAVEIN devHandle, int *piMicVol,int channel);
   typedef DILRC (*PFUNCSetMicVolChan)(HWAVEIN devHandle, int iMicVol,int channel);
   typedef DILRC (*PFUNCGetAGC)(HWAVEIN devHandle, int *piState);
   typedef DILRC (*PFUNCSetAGC)(HWAVEIN devHandle, int iState);

#endif /* #ifdef EVV */

__inline DILRC ConvertMMRCtoDILRC( const MMRESULT mrc)
{
   DILRC rc = DIL_RC_INTERNAL_ERROR;
   switch( mrc )
   {
      case MMSYSERR_NOERROR     : rc = DIL_RC_OK;                break;
      case WAVERR_STILLPLAYING  : rc =DIL_RC_DEVICE_ACTIVE;      break;
      case MMSYSERR_NOMEM       : rc = DIL_RC_MEM_ALLOC_ERROR;   break;
      case MMSYSERR_ALLOCATED   : rc = DIL_RC_DEVICE_IN_USE;     break;
      case WAVERR_BADFORMAT     : rc = DIL_RC_INVALID_FMT_COMBO; break;
      case MMSYSERR_BADDEVICEID : rc = DIL_RC_INVALID_DEV_ID;    break;
      /*case WAVERR_UNPREPARED     :
      case WAVERR_SYNC           : */
      default:
         rc = DIL_RC_INTERNAL_ERROR;
   }
   return rc;
}

#ifdef __cplusplus
}
#endif
#endif /* #ifndef _MVC_H_ */

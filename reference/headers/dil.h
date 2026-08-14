/*=========================================================================*/
/*                                                                         */
/* dil.h                                                                   */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* AS8P7ZZ V4.1 AT2T5ZZ v4.3    	                                   */
/* (C) Copyright IBM Corp. 2003, 2004  All Rights Reserved.                */
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
#
#ifndef  __DIL_H__
#define __DIL_H__
#include "evvrc.h"

#define DIL_BASE_OFFSET  0xb0000

typedef enum DILRC_E {
   DIL_RC_OK = 0,  
   DIL_RC_INVALID_PARAM       = EVV_RC_INVALID_PARAM + DIL_BASE_OFFSET,
   DIL_RC_NO_DEVICES          = EVV_RC_NO_DEVICES + DIL_BASE_OFFSET,
   DIL_RC_INVALID_FMT_COMBO   = EVV_RC_INVALID_FMT_COMBO + DIL_BASE_OFFSET,
   DIL_RC_NOT_OPEN            = EVV_RC_NOT_OPEN  + DIL_BASE_OFFSET,
   DIL_RC_DEVICE_NOT_OPEN     = EVV_RC_DEVICE_NOT_OPEN  + DIL_BASE_OFFSET,
   DIL_RC_DEVICE_ACTIVE       = EVV_RC_DEVICE_ACTIVE + DIL_BASE_OFFSET,
   DIL_RC_INVALID_REQUEST     = EVV_RC_INVALID_REQUEST + DIL_BASE_OFFSET,
   DIL_RC_DEVICE_IN_USE       = EVV_RC_DEVICE_IN_USE + DIL_BASE_OFFSET,
   DIL_RC_INTERNAL_ERROR      = EVV_RC_INTERNAL_ERROR + DIL_BASE_OFFSET,  
   DIL_RC_MEM_ALLOC_ERROR     = EVV_RC_MEM_ALLOC_ERROR + DIL_BASE_OFFSET,
   DIL_RC_TOO_HIGH            = EVV_RC_TOO_HIGH + DIL_BASE_OFFSET,
   DIL_RC_TOO_LOW             = EVV_RC_TOO_LOW + DIL_BASE_OFFSET,
   DIL_RC_SYS_XFER_FAILED     = EVV_RC_SYS_XFER_FAILED + DIL_BASE_OFFSET,
   DIL_RC_SYSTEM_ERROR        = EVV_RC_SYSTEM_ERROR + DIL_BASE_OFFSET,
   DIL_RC_UNABLE_TO_STARTTASK = EVV_RC_UNABLE_TO_STARTTASK + DIL_BASE_OFFSET,
   DIL_RC_INVALID_HANDLE      = EVV_RC_INVALID_HANDLE   + DIL_BASE_OFFSET, 
   DIL_RC_INVALID_TYPE        = EVV_RC_INVALID_TYPE + DIL_BASE_OFFSET,
   DIL_RC_LIBRARY_LOAD_ERROR  = EVV_RC_LIBRARY_LOAD_ERROR + DIL_BASE_OFFSET,
   DIL_RC_INVALID_LIBRARY     = EVV_RC_INVALID_LIBRARY + DIL_BASE_OFFSET,
   DIL_RC_INVALID_DEV_ID      = 0x8001 + DIL_BASE_OFFSET,
   DIL_RC_SYS_UNDERRUN        = 0x8002 + DIL_BASE_OFFSET,
   DIL_RC_SYS_OVERRUN         = 0x8003 + DIL_BASE_OFFSET,
   DIL_RC_INVALID_CHANNEL     = 0x8004 + DIL_BASE_OFFSET,

/* compatability defines */
   DIL_RC_DYN_LIB_LOAD_FAILED    = DIL_RC_LIBRARY_LOAD_ERROR,
   DIL_RC_DYN_LIB_NOT_FOUND    = DIL_RC_INVALID_LIBRARY ,
   DIL_RC_INVALID_DEV_TYPE = DIL_RC_INVALID_TYPE,
   DIL_RC_INVALID_DEV_HANDLE = DIL_RC_INVALID_HANDLE  ,
   DIL_RC_TASK_CREATION =  DIL_RC_UNABLE_TO_STARTTASK , 
} DILRC;

typedef unsigned long   DILDevId;
typedef void          * DILDevHandle;
typedef unsigned long   DILStdFormats;
typedef unsigned long   DILStdFmtCombos;
typedef unsigned long   DILQueryCapsFlags;
typedef unsigned long   DILQueryInfoFlags;
typedef unsigned long   DILBufFlags;
typedef unsigned char * DILCharPtr;

typedef enum
{
   DIL_DEVTYPE_UNKNOWN         = 0,
   DIL_DEVTYPE_AUDIO_PLAYER    = 1,
   DIL_DEVTYPE_AUDIO_RECORDER  = 2
} DILDevType;

/* caps flags */
#define DIL_FLAG_QUERY_FORMATS         0x00000001
#define DIL_FLAG_QUERY_SAMPLERATES     0x00000002
#define DIL_FLAG_QUERY_BITSPERSAMPLE   0x00000004
#define DIL_FLAG_QUERY_CHANNELS        0x00000008
#define DIL_FLAG_QUERY_FMTCOMBOS       0x00000010

/* info flags */
#define DIL_FLAG_QUERY_MFGID           0x00000020
#define DIL_FLAG_QUERY_MFGNAME         0x00000040
#define DIL_FLAG_QUERY_HWNAME          0x00000080
#define DIL_FLAG_QUERY_DRVNAME         0x00000100
#define DIL_FLAG_QUERY_HWVER           0x00000200
#define DIL_FLAG_QUERY_DRVVER          0x00000400
#define DIL_FLAG_QUERY_SYSHWNAME       0x00000800
#define DIL_FLAG_QUERY_SYSDRVNAME      0x00001000
#define DIL_FLAG_QUERY_ALL             0xFFFFFFFF

#define DIL_BUF_FLAG_DEFAULTS      0x00000000
#define DIL_BUF_FLAG_SYNC          0x00000001
#define DIL_BUF_FLAG_BAD_DATA      0x80000000

typedef struct _DILQueryInfo
{
   unsigned long ulMfgId;
   unsigned long ulHwVer;
   unsigned long ulDrvVer;
   char szMfgName[32];
   char szDevName[32];
   char szDrvName[32];
   char szSysDevName[32];
   char szSysDrvName[32];
} DILQueryInfo;

typedef struct _DILQueryCaps
{
   DILStdFormats     formats;
   unsigned long     ulMaxSampleRate;
   unsigned long     ulMaxSampleWidth;
   unsigned long     ulMaxChannels;
   unsigned long     reserved;
   DILStdFmtCombos   fmtCombos;
}  DILQueryCaps;

typedef struct _DILAudioFormat
{
   unsigned long  ulFormat;
   unsigned long  ulSampleRate;
   unsigned long  ulSampleWidth;
   unsigned long  ulChannels;
}  DILAudioFormat;

typedef enum
{
   DIL_BUF_STATE_UNINIT       = 0,
   DIL_BUF_STATE_INIT         = 1,
   DIL_BUF_STATE_DONE         = 4,
   DIL_BUF_STATE_QUEUED       = 6
} DILBufState;

typedef struct _DILBufInfo
{
   DILCharPtr           pBuffer;
   int                  state;
   unsigned long        ulLength;
   unsigned long        ulXferCount;
   struct _DILBufInfo  *pNext;
   struct _DILBufInfo  *pPrev;
   DILBufState          dilState;
   void                *pErrorData;
   DILBufFlags          flags;
} DILBufInfo;

typedef void DIL_BUFDONE_CALLBACK(
   void *pEalData,
   DILBufInfo * pBuffer
);

typedef void DIL_ERR_CALLBACK(
   void *pEalData,
   DILRC ulError
);

/* Various DIL flags */
#define DIL_FLAG_USE_CALLBACK 0x00000001
typedef struct _DIL_CB_DATA
{
    DIL_ERR_CALLBACK *        ulErrCBAddr;
    DIL_BUFDONE_CALLBACK *    ulBufDoneCBAddr;
    void *pEALData;
} DIL_CB_DATA;

#define DIL_CAPS_FMT_PCM  0x00000001

#define DIL_CAPS_SAMPLE_RATE_11    0x00000004

#define DIL_CAPS_COMBO_08M16        0x00000001
#define DIL_CAPS_COMBO_08S16        0x00000002
#define DIL_CAPS_COMBO_08M08        0x00000004
#define DIL_CAPS_COMBO_08S08        0x00000008
#define DIL_CAPS_COMBO_11M16        0x00000010
#define DIL_CAPS_COMBO_11S16        0x00000020
#define DIL_CAPS_COMBO_11M08        0x00000040
#define DIL_CAPS_COMBO_11S08        0x00000080
#define DIL_CAPS_COMBO_22M16        0x00000100
#define DIL_CAPS_COMBO_22S16        0x00000200
#define DIL_CAPS_COMBO_22M08        0x00000400
#define DIL_CAPS_COMBO_22S08        0x00000800
#define DIL_CAPS_COMBO_44M16        0x00001000
#define DIL_CAPS_COMBO_44S16        0x00002000
#define DIL_CAPS_COMBO_44M08        0x00004000
#define DIL_CAPS_COMBO_44S08        0x00008000
#define DIL_CAPS_COMBO_16M16        0x00010000
#define DIL_CAPS_COMBO_16S16        0x00020000
#define DIL_CAPS_COMBO_16M08        0x00040000
#define DIL_CAPS_COMBO_16S08        0x00080000

#ifdef __cplusplus
extern "C" {
#endif

typedef DILRC dilOpen_t(int iStackSize);
dilOpen_t dilOpen;

typedef DILRC dilClose_t();

dilClose_t  dilClose;

typedef DILRC dilQueryDevCount_t(  DILDevType devType,
                          unsigned long *pDevCount );
dilQueryDevCount_t dilQueryDevCount;

typedef DILRC dilQueryDevInfo_t(  const DILDevId      devId,
                          const DILDevType    devType,
                          DILQueryInfoFlags   *pFlags,
                          DILQueryInfo        *pInfo );
dilQueryDevInfo_t dilQueryDevInfo;

typedef DILRC dilQueryDevCaps_t(  const DILDevId      devId,
                          const DILDevType    devType,
                          DILQueryCapsFlags   *pFlags,
                          DILQueryCaps        *pCaps );
dilQueryDevCaps_t dilQueryDevCaps;

typedef DILRC dilDevOpen_t(  DILDevId ulDevId,
                        DILDevType ulDevType,
                        DILDevHandle *devHandle,
                        const DILAudioFormat   *format,
                        DIL_CB_DATA *cbData,
                        unsigned long *pFlags);
dilDevOpen_t dilDevOpen;

typedef DILRC dilDevClose_t(  DILDevHandle ulDevHandle );
dilDevClose_t dilDevClose;

typedef DILRC dilDevWrite_t(  DILDevHandle ulDevHandle,
                        DILBufInfo *bufInfo );
dilDevWrite_t dilDevWrite;

typedef DILRC dilDevRead_t(  DILDevHandle ulDevHandle,
                        DILBufInfo *bufInfo );
dilDevRead_t dilDevRead;

typedef DILRC dilDevReadStop_t(  DILDevHandle ulDevHandle );
dilDevReadStop_t dilDevReadStop;

typedef DILRC dilDevWriteStop_t(  DILDevHandle ulDevHandle );
dilDevWriteStop_t dilDevWriteStop;

typedef DILRC dilDevReadStart_t   ( DILDevHandle ulDevHandle );
dilDevReadStart_t   dilDevReadStart;

typedef DILRC dilDevWriteStart_t  ( DILDevHandle ulDevHandle );
dilDevWriteStart_t  dilDevWriteStart;

typedef DILRC dilDevReadReset_t  ( DILDevHandle ulDevHandle );
dilDevReadReset_t  dilDevReadReset;

typedef DILRC dilDevWriteReset_t ( DILDevHandle ulDevHandle );
dilDevWriteReset_t dilDevWriteReset;

typedef DILRC dilSetVolume_t( DILDevHandle ulDevHandle,
                    int iChannel,
                    unsigned long *pVolume);
dilSetVolume_t dilSetVolume;

typedef DILRC dilGetVolume_t( DILDevHandle ulDevHandle,
                    int iChannel,
                    unsigned long *pVolume);
dilGetVolume_t dilGetVolume;

typedef DILRC dilGetMute_t( DILDevHandle ulDevHandle,
                  unsigned long *pMuteState);
dilGetMute_t dilGetMute;

typedef DILRC dilSetMute_t( DILDevHandle ulDevHandle,
                  unsigned long *pMuteState);
dilSetMute_t dilSetMute;

#ifdef __cplusplus
}
#endif

#define DIL_DRV_STATUS_DONE  3
#define DIL_DRV_ERROR        4

#endif

/*=========================================================================*/
/*                                                                         */
/* ealdefs.h                                                               */
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

#ifndef  __EALDEFS_H__
#define  __EALDEFS_H__

#include "ealrc.h"
#include "dil.h"

typedef int EALRC;
typedef DILCharPtr EALCharPtr;
typedef DILDevId EALDevId;
typedef DILDevHandle EALDevHandle;
typedef DILStdFormats EALStdFormats;
typedef DILStdFmtCombos EALStdFmtCombos;
typedef unsigned long EALExtCaps;
typedef DILQueryCapsFlags EALQueryCapsFlags;
typedef DILQueryInfoFlags EALQueryInfoFlags;
typedef DILBufFlags EALBufFlags;

typedef void          * EALCtrlHandle;
typedef void          * EALFltrHandle;

typedef enum
{
   EAL_STATE_CAUSE_DATA_DONE = 0,
   EAL_STATE_CAUSE_REQUESTED = 1,
   EAL_STATE_CAUSE_HW        = 2,
   EAL_STATE_CAUSE_UNKNOWN   = 4,
   EAL_STATE_CAUSE_ERROR     = 5
} EALStateCause;

#define EAL_CAPS_FMT_PCM  DIL_CAPS_FMT_PCM  

#define EAL_CAPS_SAMPLE_RATE_11    EAL_CAPS_SAMPLE_RATE_11    

#define EAL_CAPS_COMBO_08M16 DIL_CAPS_COMBO_08M16    
#define EAL_CAPS_COMBO_08S16 DIL_CAPS_COMBO_08S16
#define EAL_CAPS_COMBO_08M08 DIL_CAPS_COMBO_08M08
#define EAL_CAPS_COMBO_08S08 DIL_CAPS_COMBO_08S08
#define EAL_CAPS_COMBO_11M16 DIL_CAPS_COMBO_11M16
#define EAL_CAPS_COMBO_11S16 DIL_CAPS_COMBO_11S16
#define EAL_CAPS_COMBO_11M08 DIL_CAPS_COMBO_11M08
#define EAL_CAPS_COMBO_11S08 DIL_CAPS_COMBO_11S08
#define EAL_CAPS_COMBO_22M16 DIL_CAPS_COMBO_22M16
#define EAL_CAPS_COMBO_22S16 DIL_CAPS_COMBO_22S16
#define EAL_CAPS_COMBO_22M08 DIL_CAPS_COMBO_22M08
#define EAL_CAPS_COMBO_22S08 DIL_CAPS_COMBO_22S08
#define EAL_CAPS_COMBO_44M16 DIL_CAPS_COMBO_44M16
#define EAL_CAPS_COMBO_44S16 DIL_CAPS_COMBO_44S16
#define EAL_CAPS_COMBO_44M08 DIL_CAPS_COMBO_44M08
#define EAL_CAPS_COMBO_44S08 DIL_CAPS_COMBO_44S08
#define EAL_CAPS_COMBO_16M16 DIL_CAPS_COMBO_16M16
#define EAL_CAPS_COMBO_16S16 DIL_CAPS_COMBO_16S16
#define EAL_CAPS_COMBO_16M08 DIL_CAPS_COMBO_16M08
#define EAL_CAPS_COMBO_16S08 DIL_CAPS_COMBO_16S08

typedef enum
{
   EAL_DEVTYPE_UNKNOWN         = DIL_DEVTYPE_UNKNOWN,
   EAL_DEVTYPE_AUDIO_PLAYER    = DIL_DEVTYPE_AUDIO_PLAYER,
   EAL_DEVTYPE_AUDIO_RECORDER  = DIL_DEVTYPE_AUDIO_RECORDER
} EALDevType;


typedef enum
{
   EAL_STATE_INIT             = 0,
   EAL_STATE_OPEN             = 1,
   EAL_STATE_ACTIVE           = 2,
   EAL_STATE_ACTIVE_AND_EMPTY = 3,
   EAL_STATE_NEW              = 4
} EALState;

typedef enum
{
   EAL_BUF_STATE_UNINIT       = DIL_BUF_STATE_UNINIT,
   EAL_BUF_STATE_INIT         = DIL_BUF_STATE_INIT,
   EAL_BUF_STATE_DONE         = DIL_BUF_STATE_DONE,
   EAL_BUF_STATE_SYNCED       = DIL_BUF_STATE_DONE +1,
   EAL_BUF_STATE_QUEUED       = DIL_BUF_STATE_QUEUED
} EALBufState;

#define EAL_FLAG_QUERY_FORMATS DIL_FLAG_QUERY_FORMATS
#define EAL_FLAG_QUERY_SAMPLERATES DIL_FLAG_QUERY_SAMPLERATES
#define EAL_FLAG_QUERY_BITSPERSAMPLE DIL_FLAG_QUERY_BITSPERSAMPLE
#define EAL_FLAG_QUERY_CHANNELS DIL_FLAG_QUERY_CHANNELS
#define EAL_FLAG_QUERY_FMTCOMBOS DIL_FLAG_QUERY_FMTCOMBOS
#define EAL_FLAG_QUERY_MFGID DIL_FLAG_QUERY_MFGID
#define EAL_FLAG_QUERY_MFGNAME DIL_FLAG_QUERY_MFGNAME
#define EAL_FLAG_QUERY_HWNAME DIL_FLAG_QUERY_HWNAME
#define EAL_FLAG_QUERY_DRVNAME DIL_FLAG_QUERY_DRVNAME
#define EAL_FLAG_QUERY_HWVER DIL_FLAG_QUERY_HWVER
#define EAL_FLAG_QUERY_DRVVER DIL_FLAG_QUERY_DRVVER
#define EAL_FLAG_QUERY_SYSHWNAME DIL_FLAG_QUERY_SYSHWNAME
#define EAL_FLAG_QUERY_SYSDRVNAME DIL_FLAG_QUERY_SYSDRVNAME
#define EAL_FLAG_QUERY_ALL DIL_FLAG_QUERY_ALL

#define EAL_BUF_FLAG_DEFAULTS DIL_BUF_FLAG_DEFAULTS
#define EAL_BUF_FLAG_SYNC DIL_BUF_FLAG_SYNC
#define EAL_BUF_FLAG_BAD_DATA DIL_BUF_FLAG_BAD_DATA

typedef DILQueryInfo EALQueryInfo;
typedef DILQueryCaps EALQueryCaps;
typedef DILAudioFormat EALAudioFormat;
typedef DILBufInfo EALBufInfo;

typedef struct _EALMixMuteData
{
   unsigned long state;
} EALMixMuteData;

#define EAL_FLAG_MIX_VOLUME_AS_PERCENTAGE_OF_RANGE    1
#define EAL_FLAG_MIX_VOLUME_AS_PERCENTAGE_OF_CURRENT  2
#define EAL_FLAG_MIX_VOLUME_AS_ABSOLUTE               3

typedef struct _EALMixVolumeData
{
   unsigned long ulVolume;
   unsigned long ulFlags;
} EALMixVolumeData;

typedef void EALDevStateCB (  EALDevHandle  hDev,
                              EALState      newState,
                              EALState      oldState,
                              EALStateCause cause,
                              void          *pUserData );

typedef void EALDevErrorCB (  EALDevHandle hDev,
                              EALRC        rc,
                              void         *pUserData);

typedef void EALDevBufDoneCB  (  EALDevHandle hDev,
                                 EALBufInfo   *pBufInfo,
                                 void         *pUserData);

typedef struct _EALPlayCreateOptions
{
   unsigned long  reserved;
}  EALPlayCreateOptions;

typedef struct _EALRecordCreateOptions
{
   unsigned long reserved;
}  EALRecordCreateOptions;

typedef struct _EALDevCreateOptions
{
   unsigned long  reserved;
   union OPTIONS
   {
      EALPlayCreateOptions    *play;
      EALRecordCreateOptions  *record;
   } options;
}  EALDevCreateOptions;

typedef struct _EALPlayOpenOptions
{
   void *reserved;
}  EALPlayOpenOptions;

typedef struct _EALRecordOpenOptions
{
   void *reserved;
}  EALRecordOpenOptions;

typedef struct _EALDevOpenOptions
{
   EALPlayOpenOptions   *optionsPlay;
   EALRecordOpenOptions *optionsRec;
}  EALDevOpenOptions;

typedef struct _EALPlayStartOptions
{
   unsigned long reserved;
}  EALPlayStartOptions;

typedef struct _EALRecordStartOptions
{
   unsigned long reserved;
}  EALRecordStartOptions;

typedef struct _EALDevStartOptions
{
   unsigned long reserved;
   union START_OPTIONS
   {
      EALPlayStartOptions    *play;
      EALRecordStartOptions  *record;
   } options;
}  EALDevStartOptions;

typedef void EALCtrlErrorCB(	EALDevId       devId,
                       	      EALDevType     devType,
                              EALDevHandle   hDev,
                              void           *pCtrlErrorData);

typedef void EALFltrErrorCB(	EALDevId       devId,
                       	      EALDevType     devType,
                              EALDevHandle   hDev,
                              void           *pFltrErrorData);


#define EAL_CTRL_FLAG_STREAM_END 0x00000001
#define EAL_FLTR_FLAG_STREAM_END 0x00000001

typedef unsigned long EALCtrlCB  (  EALCtrlHandle     hCtrl,
                                    const EALCharPtr  pBuf,
                                    unsigned long     length,
                                    EALAudioFormat    format,
                                    EALDevId          devId,
                                    EALDevType        devType,
                                    unsigned long     flags,
                                    void              *pUserData);

typedef unsigned long EALFltrCB (   EALFltrHandle     hFltr,
                                    EALCharPtr        pBuf,
                                    unsigned long     length,
                                    EALAudioFormat    format,
                                    unsigned long     flags,
                                    void              *pUserData);

typedef struct _EALCtrlData
{
   EALCtrlCB      *pCB;
   void           *pData;
} EALCtrlData;

typedef struct _EALFltrData
{
   EALFltrCB      *pCB;
   void           *pData;
} EALFltrData;


typedef enum _EALCtrlMsg
{
   EAL_MSG_CTRL_REGISTER  = 1,
   EAL_MSG_CTRL_UNREGISTER = 2
} EALCtrlMsg;

typedef enum _EALFltrMsg
{
   EAL_MSG_FLTR_REGISTER  = 3,
   EAL_MSG_FLTR_UNREGISTER = 4
} EALFltrMsg;

typedef unsigned long EALCtrl( EALCtrlHandle  hCtrl,
                               EALCtrlMsg     msg,
                               void          *pParam1,
                               void          *pParam2 );

typedef unsigned long EALFltr( EALFltrHandle  hFltr,
                               EALFltrMsg     msg,
                               void          *pParam1,
                               void          *pParam2 );

typedef struct _EALMapPair
{
   unsigned long volume;	
   unsigned long percent;	
} EALMapPair;

enum
{
   EAL_MAP_CURRENT_VOL = -1
};

#define EAL_MAP_FLAG_BE          0x01
#define EAL_MAP_FLAG_SIGNED      0x02
#define EAL_MAP_FLAG_FULL        0x04
#define EAL_MAP_FLAG_BI_DIR      0x08
#define EAL_MAP_FLAG_BINARYMAP   0x10
#define EAL_MAP_FLAG_UNUSED2     0x20
#define EAL_MAP_FLAG_WARNING     0x40
#define EAL_MAP_FLAG_ERROR       0x80

typedef struct _EALMixMap
{
   unsigned char  signature1;
   unsigned char  signature2;
   unsigned char  major_version;
   unsigned char  minor_version;
   unsigned char  flags;
   unsigned char  reserved_1;
   unsigned char  reserved_2;
   unsigned char  reserved_3;
   unsigned char  codec_id_1;
   unsigned char  codec_id_2;
   unsigned char  codec_id_3;
   unsigned char  codec_id_4;
   unsigned char  mic_id_1;
   unsigned char  mic_id_2;
   unsigned char  mic_id_3;
   unsigned char  mic_id_4;
   int  bits_per_sample;
   int  channels;
   int  max;
   int  min;
   int  step;
   int  count;
   long  user_value;
   int * gainmap;
} EALMixMap ;

#endif   /* __EALDEFS_H__ */

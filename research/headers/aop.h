/***************************************************************************/
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* AS8P7ZZ V4.1 AT2T5ZZ v4.3		                                   */
/* (C) Copyright IBM Corp. 2002, 2004  All Rights Reserved.                */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement accompanying the Licensed Materials.              */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/***************************************************************************/

#if !defined (_AOP_H_)
#define _AOP_H_

/* Needed for the EALDevID definition */
#include "ealdefs.h"

/* Needed for the SpeechDetectorFrameStatsStruct definition */
#include "esr.h"

/* Needed for return code definitions */
#include "evvrc.h"

/* Defines VVE_DECLSPEC */
#include "ecomdefs.h"

/* NOTE!! Check evvrc.h for the EVV_RC_* values */
#define AOP_RC_BASE				0x10000
typedef enum
{
    AOP_RC_OK                   = 0,
    AOP_RC_ALREADY_OPEN         = (AOP_RC_BASE + EVV_RC_ALREADY_INITIALIZED ), /* 0x00010010 */
    AOP_RC_DEVICE_NOT_OPEN      = (AOP_RC_BASE + EVV_RC_DEVICE_NOT_OPEN     ), /* 0x0001001C */
    AOP_RC_EAL_NOT_OPEN         = (AOP_RC_BASE + EVV_RC_EAL_ERROR           ), /* 0x00010020 */
    AOP_RC_INVALID_FLAG         = (AOP_RC_BASE + EVV_RC_INVALID_FLAG        ), /* 0x0001002C */
    AOP_RC_INVALID_HANDLE       = (AOP_RC_BASE + EVV_RC_INVALID_HANDLE      ), /* 0x0001002F */
    AOP_RC_INVALID_VALUE        = (AOP_RC_BASE + EVV_RC_INVALID_VALUE       ), /* 0x0001003C */
    AOP_RC_MEM_ALLOC_ERROR      = (AOP_RC_BASE + EVV_RC_MEM_ALLOC_ERROR     ), /* 0x0001003F */
    AOP_RC_NOT_OPEN             = (AOP_RC_BASE + EVV_RC_NOT_OPEN            ), /* 0x00010051 */
    AOP_RC_AT_MAX_VOL           = (AOP_RC_BASE + EVV_RC_TOO_HIGH            ), /* 0x0001005E */
    AOP_RC_AT_MIN_VOL           = (AOP_RC_BASE + EVV_RC_TOO_LOW             ), /* 0x0001005F */
    AOP_RC_INTERNAL_ERROR       = (AOP_RC_BASE + EVV_RC_INTERNAL_ERROR      ), /* 0x00014000 */
    AOP_RC_STOP_NOT_CALLED      = (AOP_RC_BASE + 0x8000                     ), /* 0x00018000 */
    AOP_RC_START_NOT_CALLED     = (AOP_RC_BASE + 0x8001                     ), /* 0x00018001 */
    AOP_RC_TOO_MANY_INSTANCES   = (AOP_RC_BASE + 0x8002                     ), /* 0x00018002 */
    AOP_RC_INVALID_DEV_ID       = (AOP_RC_BASE + 0x8003                     ), /* 0x00018003 */
    AOP_RC_NOT_FULL             = (AOP_RC_BASE + 0x8004                     ), /* 0x00018004 */
    AOP_RC_NO_UTTERANCE_AVAIL   = (AOP_RC_BASE + 0x8005                     ), /* 0x00018005 */
    AOP_RC_NO_SIGNAL            = (AOP_RC_BASE + 0x8006                     ), /* 0x00018006 */
    AOP_RC_NO_NOISE             = (AOP_RC_BASE + 0x8007                     )  /* 0x00018007 */
} AOPRCenum;

typedef unsigned long AOPRC;

typedef struct _AOPAttrs
{
    unsigned long   ulValidSettings;
    int             iGainBufWindowSize;
    int             iClipBufWindowSize;
    int             iNumSilenceFrames;
    int             iNumClippedFrames;
    int             iMaxOptimalEnergy;
    int             iMinOptimalEnergy;
    int             iUseSmoothedSD;
    int             iUseSpeechStateFlag;
    int             iAutoChangeGain;
    int             iSNRBufWindowSize;
    int             iSNRThresholdWindowSize;
    int             iSNRThreshold;
} AOPAttrs;

#define AOP_ATTRS_GAIN_BUF_WINDOW_SIZE          0x00000001
#define AOP_ATTRS_CLIP_BUF_WINDOW_SIZE          0x00000002
#define AOP_ATTRS_NUM_SILENCE_FRAMES            0x00000004
#define AOP_ATTRS_NUM_CLIPPED_FRAMES            0x00000008 
#define AOP_ATTRS_MAX_OPTIMAL_ENERGY            0x00000010
#define AOP_ATTRS_MIN_OPTIMAL_ENERGY            0x00000020
#define AOP_ATTRS_USE_SMOOTHED_SD               0x00000040
#define AOP_ATTRS_USE_SPEECH_STATE_FLAG         0x00000080
#define AOP_ATTRS_AUTO_CHANGE_GAIN_FLAG         0x00000100
#define AOP_ATTRS_SNR_BUF_WINDOW_SIZE           0x00000200
#define AOP_ATTRS_SNR_THRESHOLD_WINDOW_SIZE     0x00000400
#define AOP_ATTRS_SNR_THRESHOLD                 0x00000800

#define AOP_DEFAULT_GAIN_BUF_SIZE               200
#define AOP_DEFAULT_CLIP_BUF_SIZE               32
#define AOP_DEFAULT_MAX_SILENCE_COUNT           0
#define AOP_DEFAULT_MAX_CLIP_COUNT              10
#define AOP_DEFAULT_MAX_OPTIMAL_ENERGY          2200
#define AOP_DEFAULT_MIN_OPTIMAL_ENERGY          1200
#define AOP_DEFAULT_USE_SMOOTHED_SD             true
#define AOP_DEFAULT_USE_SPEECH_STATE            false
#define AOP_DEFAULT_AUTO_CHANGE_GAIN            true
#define AOP_DEFAULT_SNR_BUF_SIZE                128
#define AOP_DEFAULT_SNR_THRESHOLD_WINDOW_SIZE   32
#define AOP_DEFAULT_SNR_THRESHOLD               5

#define AOP_INVALID_SNR 0x7FFFFFFF

typedef enum 
{
    AOP_STATUS_AT_MAX_VOL,
    AOP_STATUS_AT_MIN_VOL,
    AOP_STATUS_CLIPPING_DETECTED,
    AOP_STATUS_DATA_NOT_ANALYZED,
    AOP_STATUS_NO_CHANGE_REQUIRED,
    AOP_STATUS_HIGH_GAIN_DETECTED,
    AOP_STATUS_LOW_GAIN_DETECTED,
    AOP_STATUS_NO_FRAME_INFO,
    AOP_STATUS_SILENCE_DETECTED,
} AOPStatus;

typedef unsigned int AOPHandle;

VVE_DECLSPEC AOPRC aopCreate                ( AOPHandle * phAop, EALDevId EalId, AOPAttrs * pAopAttrs );
VVE_DECLSPEC AOPRC aopDestroy               ( AOPHandle hAop );
VVE_DECLSPEC AOPRC aopSetAttrs              ( AOPHandle hAop, AOPAttrs * pAopAttrs );
VVE_DECLSPEC AOPRC aopGetAttrs              ( AOPHandle hAop, AOPAttrs * pAopAttrs );
VVE_DECLSPEC AOPRC aopProcessFrameStats     ( AOPHandle hAop, ESRSpeechDetectorFrameStatsStruct * pSDFrameStats, AOPStatus * pAopStatus );
VVE_DECLSPEC AOPRC aopStartUtterance        ( AOPHandle hAop );
VVE_DECLSPEC AOPRC aopStopUtterance         ( AOPHandle hAop );
VVE_DECLSPEC AOPRC aopQuerySNR              ( AOPHandle hAop, int * piSNR );
VVE_DECLSPEC AOPRC aopQueryLastUtteranceSNR ( AOPHandle hAop, int * piSNR );

#endif

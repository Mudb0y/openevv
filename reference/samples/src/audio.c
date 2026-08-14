/*=========================================================================*/
/*                                                                         */
/* audio.c                                                                 */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1		   AT2T5ZZ V4.3                                    */
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

/* Standard include files */
#include <stdlib.h>
#include <string.h>

/* VVE SDK include files */
#include "esr.h"
#include "ealmain.h"
#include "aop.h"
#include "tbm2gnrl.h"

/* Application include files */
#include "os.h"
#include "audio.h"
#include "uifuncs.h"


AOPHandle g_hAOP;


/******************************************************************************
*******************************************************************************
*                    L O C A L   D E F I N I T I O N S
*******************************************************************************
******************************************************************************/

/*
   For recording, use 10, 1/10th of a second buffer.

   Note: The individual size and total number of buffers required to ensure
         there is no loss of data during recording will be system dependent.
         For the ThinkPad 770Z, see the readme file.
*/
#define NUM_REC_BUFFERS    10
#define REC_BUF_SIZE       ((11025/10) *2)

/* Structure representing user data for recording buffer done callback. */
typedef struct _AudioRecBufDoneCBData
{
   /* Handle to the ESR engine for esrEnqueue(). */
   ESREngineHandle hEsr;
   /* Pointer to the buffer to save audio. */
   unsigned char *pAudioBuffer;
   /* Size of the audio buffer pointed to by pAudioBuffer. */
   unsigned long ulAudioBufferLen;
   /* Index of the current insert point of pAudioBuffer. */
   unsigned long ulAudioBufferIndex;
   /* Flags that determine whether a close is in progress. */
   int iClosePending;

} AudioRecBufDoneCBData;


/* Structure representing user data for recording state change callback. */
typedef struct _AudioRecStateChangeCBData
{
   SEM_HANDLE hEvent;
   EALState EalState;
} AudioRecStateChangeCBData;

/* Structure representing user data for player buffer done callback. */
typedef struct _AudioPlayStateChangeCBData
{
   SEM_HANDLE hEvent;
   SEM_HANDLE hEventActive;
   SEM_HANDLE hEventActiveAndEmpty;
   EALState EalState;
} AudioPlayStateChangeCBData;

/* Structure representing user data for recording error callback. */
typedef struct _AudioRecErrorCBData
{
   SEM_HANDLE hEvent;
   EALRC EalRc;
} AudioRecErrorCBData;

/* Structure representing user data for player error callback. */
typedef struct _AudioPlayErrorCBData
{
   SEM_HANDLE hEvent;
   SEM_HANDLE hEventActive;
   SEM_HANDLE hEventActiveAndEmpty;
   EALRC EalRc;
} AudioPlayErrorCBData;


/******************************************************************************
*******************************************************************************
*                    G L O B A L   D E C L A R A T I O N S
*******************************************************************************
******************************************************************************/

/* Handles to recording and playback devices. */
static EALDevHandle gs_hRecDev;
static EALDevHandle gs_hPlayDev;

/* Structures used to contain callback user data. */
static AudioRecBufDoneCBData gs_RecBufDoneCBData;
static AudioRecStateChangeCBData gs_RecStateChangeCBData;
static AudioPlayStateChangeCBData gs_PlayStateChangeCBData;
static AudioRecErrorCBData gs_RecErrorCBData;
static AudioPlayErrorCBData gs_PlayErrorCBData;

/******************************************************************************
*******************************************************************************
*           L O C A L   F U N C T I O N   P R O T O T Y P E S
*******************************************************************************
******************************************************************************/
int AudioInitRec( ESREngineHandle hEsr );
int AudioInitPlay( void );
void AudioRecErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData );
void AudioRecStateCB( EALDevHandle ulDevHandle, EALState newState,
                      EALState oldState, EALStateCause cause,
                      void *pUserData );
void AudioRecBufDoneCB( EALDevHandle ulDevHandle, EALBufInfo *pBufInfo,
                        void *pUserData );
void AudioPlayErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData );
void AudioPlayStateCB( EALDevHandle ulDevHandle, EALState newState,
                       EALState oldState, EALStateCause cause,
                       void *pUserData );
void EalCtrlErrorCB(	EALDevId devId, EALDevType devType,
                     EALDevHandle hDev, void *pCtrlErrorData );


/******************************************************************************
*******************************************************************************
*           E X T E R N A L   F U C N T I O N   D E F I N I T I O N S
*******************************************************************************
******************************************************************************/

/******************************************************************************
* int AudioInit( ESREngineHandle hEsr )
******************************************************************************/
int AudioInit( ESREngineHandle hEsr )
{
   int irc;
   EALRC erc;
   unsigned long ulDevCount;

   /* Initialize the EAL library. */
   erc = ealOpen();
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealOpen() = 0x%x in AudioInit()\n", erc );
      return( AUDIO_ERROR );
   }

   /* Check to determine whether the EAL has found any recording devices. */
   erc = ealQueryDevCount( EAL_DEVTYPE_AUDIO_RECORDER, &ulDevCount );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealQueryDevCount(EAL_DEVTYPE_AUDIO_RECORDER) = 0x%x "
                 "in AudioInit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Verify that there are devices that the EAL can handle. */
   if( ulDevCount == 0 )
   {
      ErrPrintf( "No EAL recorder devices found in AudioInit().\n" );
      return( AUDIO_NODEVS );
   }

   /* Check to determine whether the EAL has found any playback devices. */
   erc = ealQueryDevCount( EAL_DEVTYPE_AUDIO_PLAYER, &ulDevCount );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealQueryDevCount(EAL_DEVTYPE_AUDIO_PLAYER) = 0x%x"
                 "in AudioInit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Verify that there are devices that the EAL can handle. */
   if( ulDevCount == 0 )
   {
      ErrPrintf( "No EAL player devices found in AudioInit().\n" );
      return( AUDIO_NODEVS );
   }

   /* Initialize the recording device. */
   irc = AudioInitRec( hEsr );
   if( irc != AUDIO_OK )
      return( irc );

   /* Initialize the playback device. */
   irc = AudioInitPlay();
   if( irc != AUDIO_OK )
      return( irc );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioUninit( void )
******************************************************************************/
int AudioUninit( void )
{
   EALRC erc;
   AOPRC aoprc;
   EALMixMap *pMixMap;

   /* Unregister the callbacks for the player. */
   erc = ealDevUnregisterStateCB( gs_hPlayDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevUnregisterStateCB(gs_hPlayDev) = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   erc = ealDevUnregisterErrorCB( gs_hPlayDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevUnregisterErrorCB(gs_hPlayDev) =0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Destroy the player device. */
   erc = ealDevDestroy( &gs_hPlayDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevDestroy(gs_hPlayDev) = 0x%x in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Close the AOP */
   aoprc = aopDestroy( g_hAOP );
   if( aoprc != AOP_RC_OK )
   {
      ErrPrintf( "aopDestroy() = 0x%x in AudioUninit().\n", aoprc );
      return( AUDIO_ERROR );
   }

   /* Remove the volume map that was registered.  Note that the last parameter
      is set to NULL because it is not necessary to retrieve the registered
      data to free it, since it is used from its global definition in the
      header file. */
   erc = ealMixUnregisterMap( 0, EAL_DEVTYPE_AUDIO_RECORDER, &pMixMap );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealMixUnregisterMap() = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Unregister the callbacks for the recorder. */
   erc = ealDevUnregisterBufDoneCB( gs_hRecDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevUnregisterBufDoneCB(gs_hRecDev) = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   erc = ealDevUnregisterStateCB( gs_hRecDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevUnregisterStateCB(gs_hRecDev) = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   erc = ealDevUnregisterErrorCB( gs_hRecDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevUnregisterErrorCB(gs_hRecDev) = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Destroy the recording device. */
   erc = ealDevDestroy( &gs_hRecDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevDestroy(gs_hRecDev) = 0x%x "
                 "in AudioUninit().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Uninitialize the EAL library. */
   ealClose();

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioOpenRec( void )
******************************************************************************/
int AudioOpenRec( void )
{
   EALRC erc;
   int i, irc;
   EALAudioFormat AudFormat;
   SEM_HANDLE hEventSem;
   EALBufInfo *pBufInfo;

   /* Create the event semaphore used to notify listeners of state changes
      or errors during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioOpenRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Open the recording device with the PCM format required for recognition. */
   AudFormat.ulFormat = EAL_CAPS_FMT_PCM;
   AudFormat.ulSampleRate = 11025;
   AudFormat.ulSampleWidth = 16;
   AudFormat.ulChannels = 1;

   /* Set the semaphore to signal state changes or errors. */
   gs_RecStateChangeCBData.hEvent = hEventSem;
   gs_RecErrorCBData.hEvent = hEventSem;

   gs_RecStateChangeCBData.EalState = EAL_STATE_INIT;
   gs_RecErrorCBData.EalRc = 0;

   erc = ealDevOpen( gs_hRecDev, &AudFormat, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevOpen() = 0x%x in AudioOpenRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioOpenRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( gs_RecStateChangeCBData.EalState != EAL_STATE_OPEN )
   {
      ErrPrintf( "Expected EAL state OPEN but got %d.  EalError = 0x%x "
                 "in AudioOpenRec().\n",
                 gs_RecStateChangeCBData.EalState, gs_RecErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_RecStateChangeCBData.hEvent = NULL;
   gs_RecErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   /* Allocate and register the buffers used for recording. */
   for( i = 0; i < NUM_REC_BUFFERS; i++ )
   {
      /* Allocate the buffer info structure used by the EAL. */
      pBufInfo = (EALBufInfo *)malloc( sizeof(EALBufInfo) );
      if( pBufInfo == NULL )
         return( AUDIO_NO_MEM );

      /* Allocate the buffer to contain the actual audio data. */
      pBufInfo->pBuffer = (EALCharPtr)malloc( REC_BUF_SIZE );
      if( pBufInfo->pBuffer == NULL )
         return( AUDIO_NO_MEM );
      pBufInfo->ulLength = REC_BUF_SIZE;

      /* Set the state of this buffer to UNINITIALIZED. */
      pBufInfo->state = EAL_BUF_STATE_UNINIT;

      /* Register the buffer with the EAL. */
      erc = ealDevRegisterBuffer( gs_hRecDev, pBufInfo );
      if( erc != EAL_RC_OK )
      {
         ErrPrintf( "ealDevRegisterBuffer() = 0x%x in AudioOpenRec().\n", irc );
         return( AUDIO_ERROR );
      }
   }

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioStartRec( unsigned char *pRecBuf, unsigned long ulRecBufSize )
******************************************************************************/
int AudioStartRec( unsigned char *pRecBuf, unsigned long ulRecBufSize )
{
   int irc;
   EALRC erc;
   SEM_HANDLE hEventSem;

   /* Create the event semaphore used to notify of state changes or errors
      during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioStartRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Set the semaphore to signal state changes or errors. */
   gs_RecStateChangeCBData.hEvent = hEventSem;
   gs_RecErrorCBData.hEvent = hEventSem;
   gs_RecErrorCBData.EalRc = 0;

   /* Recording is about to start, so set the close pending flag
      appropriately. */
   gs_RecBufDoneCBData.iClosePending = 0;

   /* Setup the record buffer info for the buffer done callback. */
   gs_RecBufDoneCBData.pAudioBuffer = pRecBuf;
   gs_RecBufDoneCBData.ulAudioBufferLen = ulRecBufSize;
   gs_RecBufDoneCBData.ulAudioBufferIndex = 0;

   /* Start the device. */
   erc = ealDevStart( gs_hRecDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevStart() = 0x%x in AudioStartRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to ACTIVE. */
   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioStartRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( (gs_RecStateChangeCBData.EalState != EAL_STATE_ACTIVE) &&
       (gs_RecStateChangeCBData.EalState != EAL_STATE_ACTIVE_AND_EMPTY) )
   {
      ErrPrintf( "Expected EAL state ACTIVE or ACTIVE_AND_EMPTY but got %d."
                 "  EalError = 0x%x in AudioStartRec().\n",
                 gs_RecStateChangeCBData.EalState, gs_RecErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_RecStateChangeCBData.hEvent = NULL;
   gs_RecErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioStopRec( unsigned long *pulBytesRec )
******************************************************************************/
int AudioStopRec( unsigned long *pulBytesRec )
{
   int irc;
   EALRC erc;
   SEM_HANDLE hEventSem;

   /* Create the event semaphore used to notify of state changes or errors
      during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioStopRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Set the semaphore to signal state changes or errors. */
   gs_RecStateChangeCBData.hEvent = hEventSem;
   gs_RecErrorCBData.hEvent = hEventSem;
   gs_RecErrorCBData.EalRc = 0;

   /* Start the device. */
   erc = ealDevStop( gs_hRecDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevStop() = 0x%x in AudioStopRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to OPEN. */
   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioStopRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( gs_RecStateChangeCBData.EalState != EAL_STATE_OPEN )
   {
      ErrPrintf( "Expected EAL state OPEN but got %d.  EalError = 0x%x "
                 "in AudioStopRec().\n",
                 gs_RecStateChangeCBData.EalState, gs_RecErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_RecStateChangeCBData.hEvent = NULL;
   gs_RecErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   /* Store the number of bytes recorded for the caller. */
   *pulBytesRec = gs_RecBufDoneCBData.ulAudioBufferIndex;

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioCloseRec( void )
******************************************************************************/
int AudioCloseRec( void )
{
   int irc;
   EALRC erc;
   SEM_HANDLE hEventSem;

   /* Create the event semaphore used to notify of state changes or errors
      during the call to this function.  */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioCloseRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Set the semaphore to signal state changes or errors. */
   gs_RecStateChangeCBData.hEvent = hEventSem;
   gs_RecErrorCBData.hEvent = hEventSem;
   gs_RecErrorCBData.EalRc = 0;

   /* The recording device is about to close, so notify the buffer
      done callback. */
   gs_RecBufDoneCBData.iClosePending = 1;

   /* Start the device. */
   erc = ealDevClose( gs_hRecDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevClose() = 0x%x in AudioCloseRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to INIT. */
   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioCloseRec().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( gs_RecStateChangeCBData.EalState != EAL_STATE_INIT )
   {
      ErrPrintf( "Expected EAL state INIT but got %d.  EalError = 0x%x "
                 "in AudioCloseRec().\n",
                 gs_RecStateChangeCBData.EalState, gs_RecErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_RecStateChangeCBData.hEvent = NULL;
   gs_RecErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioOpenPlay( void )
******************************************************************************/
int AudioOpenPlay( void )
{
   EALRC erc;
   int irc;
   EALAudioFormat AudFormat;
   SEM_HANDLE hEventSem;

   /* Create the event semaphore used to notify of state changes or errors
      during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioOpenPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Open the player device with the PCM format required for recognition. */
   AudFormat.ulFormat = EAL_CAPS_FMT_PCM;
   AudFormat.ulSampleRate = 11025;
   AudFormat.ulSampleWidth = 16;
   AudFormat.ulChannels = 1;

   /* Set the semaphore to signal state changes or errors. */
   gs_PlayStateChangeCBData.hEvent = hEventSem;
   gs_PlayErrorCBData.hEvent = hEventSem;

   gs_PlayStateChangeCBData.EalState = EAL_STATE_INIT;
   gs_PlayErrorCBData.EalRc = 0;

   erc = ealDevOpen( gs_hPlayDev, &AudFormat, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevOpen() = 0x%x in AudioOpenPlay().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioOpenPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( gs_PlayStateChangeCBData.EalState != EAL_STATE_OPEN )
   {
      ErrPrintf( "Expected EAL state OPEN but got %d."
                 "  EalError = 0x%x in AudioOpenPlay().\n",
                 gs_PlayStateChangeCBData.EalState, gs_PlayErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_PlayStateChangeCBData.hEvent = NULL;
   gs_PlayErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioPlay( unsigned char *pPlayBuf, unsigned long ulPlayBufSize )
******************************************************************************/
int AudioPlay( unsigned char *pPlayBuf, unsigned long ulPlayBufSize )
{
   int irc;
   EALRC erc;
   SEM_HANDLE hEventSem;
   SEM_HANDLE hEventSemActive;
   SEM_HANDLE hEventSemActiveAndEmpty;
   EALBufInfo *pBufInfo;

   /* Create the event semaphore used to notify of state changes or
      errors during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   irc = osCreateEventSem( &hEventSemActive );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   irc = osCreateEventSem( &hEventSemActiveAndEmpty );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Set the semaphore to signal state changes or errors. */
   gs_PlayStateChangeCBData.hEvent = hEventSem;
   gs_PlayStateChangeCBData.hEventActive = hEventSemActive;
   gs_PlayStateChangeCBData.hEventActiveAndEmpty = hEventSemActiveAndEmpty;
   gs_PlayErrorCBData.hEventActive = hEventSemActive;
   gs_PlayErrorCBData.hEventActiveAndEmpty = hEventSemActiveAndEmpty;
   gs_PlayErrorCBData.EalRc = 0;

   /* Register the audio data to be played. */
   pBufInfo = (EALBufInfo *)malloc( sizeof(EALBufInfo) );
   if( pBufInfo == NULL )
      return( AUDIO_NO_MEM );

   /* Set the buffer info structure to point to the user-supplied data. */
   pBufInfo->pBuffer = pPlayBuf;
   pBufInfo->ulLength = ulPlayBufSize;

   /* Set the buffer to synchronized so that the EAL will not report
      the buffer as complete until it has actually been played. */
   pBufInfo->flags = EAL_BUF_FLAG_SYNC;

   /* Set the state of this buffer to UNINITIALIZED. */
   pBufInfo->state = EAL_BUF_STATE_UNINIT;

   /* Register the buffer with the EAL. */
   erc = ealDevRegisterBuffer( gs_hPlayDev, pBufInfo );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterBuffer() = 0x%x in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Start the device. */
   erc = ealDevStart( gs_hPlayDev, NULL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevStart() = 0x%x in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to ACTIVE. */
   irc = osWaitOnEventSem( hEventSemActive, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem(hEventSemActive) = %ld in AudioPlay()\n",
                 irc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to ACTIVE_AND_EMPTY.
      Wait at least a second longer than the actual time required for
      the request buffer.  */
   irc = osWaitOnEventSem( hEventSemActiveAndEmpty,
                           1000 + 1000*(ulPlayBufSize / (11025*2)+1) );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem(hEventSemActiveAndEmpty) = %ld "
                 "in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Start the device. */
   erc = ealDevStop( gs_hPlayDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevStop() = 0x%x in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to OPEN. */
   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioPlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify that the state changed as expected. */
   if( gs_PlayStateChangeCBData.EalState != EAL_STATE_OPEN )
   {
      ErrPrintf( "Expected EAL state OPEN but got %d."
                 "  EalError = 0x%x in AudioPlay().\n",
                 gs_PlayStateChangeCBData.EalState, gs_PlayErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Free the buffer pointer being used by the EAL. */
   free( pBufInfo );

   /* Cleanup the semaphore used for event notification. */
   gs_PlayStateChangeCBData.hEvent = NULL;
   gs_PlayStateChangeCBData.hEventActive = NULL;
   gs_PlayStateChangeCBData.hEventActiveAndEmpty = NULL;
   gs_PlayErrorCBData.hEvent = NULL;
   gs_PlayErrorCBData.hEventActive = NULL;
   gs_PlayErrorCBData.hEventActiveAndEmpty = NULL;
   osDestroyEventSem( hEventSem );
   osDestroyEventSem( hEventSemActive );
   osDestroyEventSem( hEventSemActiveAndEmpty );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioClosePlay( void )
******************************************************************************/
int AudioClosePlay( void )
{
   int irc;
   EALRC erc;
   SEM_HANDLE hEventSem;

   /* Create the event semaphore used to notify of state changes or errors
      during the call to this function. */
   irc = osCreateEventSem( &hEventSem );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %ld in AudioClosePlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Set the semaphore to signal state changes or errors. */
   gs_PlayStateChangeCBData.hEvent = hEventSem;
   gs_PlayErrorCBData.hEvent = hEventSem;
   gs_PlayErrorCBData.EalRc = 0;

   /* Start the device. */
   erc = ealDevClose( gs_hPlayDev );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevClose() = 0x%x in AudioClosePlay().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Wait for the EAL state to change to INIT. */
   /* Wait for the EAL state to become OPEN, or for an error to occur. */
   irc = osWaitOnEventSem( hEventSem, 3000 );
   if( irc != OS_OK )
   {
      ErrPrintf( "osWaitOnEventSem() = %ld in AudioClosePlay().\n", irc );
      return( AUDIO_ERROR );
   }

   /* Verify the state changed as expected. */
   if( gs_PlayStateChangeCBData.EalState != EAL_STATE_INIT )
   {
      ErrPrintf( "Expected EAL state INIT but got %d."
                 "  EalError = 0x%x in AudioClosePlay().\n",
                 gs_PlayStateChangeCBData.EalState, gs_PlayErrorCBData.EalRc );
      return( AUDIO_ERROR );
   }

   /* Cleanup the semaphore used for event notification. */
   gs_PlayStateChangeCBData.hEvent = NULL;
   gs_PlayErrorCBData.hEvent = NULL;
   osDestroyEventSem( hEventSem );

   return( AUDIO_OK );
}


/******************************************************************************
*******************************************************************************
*                    I N T E R N A L  F U N C T I O N S
*******************************************************************************
******************************************************************************/

/******************************************************************************
* int AudioInitRec( ESREngineHandle hEsr )
******************************************************************************/
int AudioInitRec( ESREngineHandle hEsr )
{
   OSTaskInfo TaskInfo;
   EALRC erc;
   EALDevId ealDevId = 0;
   AOPRC aoprc;

   /* Create the recording device. */
   /* use variable for device id instead of _0_: LM */
   erc = ealDevCreate( ealDevId, EAL_DEVTYPE_AUDIO_RECORDER, &gs_hRecDev,
                       NULL, &TaskInfo );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevCreate(EAL_DEVTYPE_AUDIO_RECORDER) = 0x%x "
                 "in AudioInitRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Register the recording device state change callback. */
   erc = ealDevRegisterStateCB( gs_hRecDev, AudioRecStateCB,
                                (void *)&gs_RecStateChangeCBData );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterStateCB() = 0x%x in AudioInitRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Register the recording device error callback. */
   erc = ealDevRegisterErrorCB( gs_hRecDev, AudioRecErrorCB,
                                (void *)&gs_RecErrorCBData );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterErrorCB() = 0x%x in AudioInitRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Register the recording buffer done callback.
      Note that the user data is a structure containing the data required by
      this function.  */
   memset( &gs_RecBufDoneCBData, 0, sizeof(gs_RecBufDoneCBData) );
   gs_RecBufDoneCBData.hEsr = hEsr;

   erc = ealDevRegisterBufDoneCB( gs_hRecDev, AudioRecBufDoneCB,
                                  (void *)&gs_RecBufDoneCBData );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterBufDoneCB() = 0x%x in AudioInitRec().\n", erc );
      return( AUDIO_ERROR );
   }


   /* Register the required volume map for the target audio device.
      Note that the variable TBM2GNRL is declared in the tbm2gnrl.h header file.  */
   erc = ealMixRegisterMap( 0, EAL_DEVTYPE_AUDIO_RECORDER,
                           (EALMixMap *)&TBM2GNRL );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealMixRegisterMap(&TBM2GNRL) = 0x%x in AudioInitRec().\n",
                  erc );
      return( AUDIO_ERROR );
   }

   aoprc = aopCreate( &g_hAOP, ealDevId, NULL );
   if( aoprc != AOP_RC_OK)
   {
      ErrPrintf( "aopCreate() = 0x%x in AudioInitRec().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Display which mixer map is being used. */
   UIPrintf( "Using Mixer Map:  Codec-%c%c%c%c  Mic-%c%c%c%c\n\n",
               TBM2GNRL.codec_id_1, TBM2GNRL.codec_id_2,
               TBM2GNRL.codec_id_3, TBM2GNRL.codec_id_4,
               TBM2GNRL.mic_id_1, TBM2GNRL.mic_id_2,
               TBM2GNRL.mic_id_3, TBM2GNRL.mic_id_4 );

   return( AUDIO_OK );
}

/******************************************************************************
* int AudioInitPlay( void )
******************************************************************************/
int AudioInitPlay( void )
{
   OSTaskInfo TaskInfo;
   EALRC erc;

   /* Create the recording device. */
   erc = ealDevCreate( 0, EAL_DEVTYPE_AUDIO_PLAYER, &gs_hPlayDev,
                       NULL, &TaskInfo );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevCreate(EAL_DEVTYPE_AUDIO_PLAYER) = 0x%x "
                 "in AudioInitPlay().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Register the recording device state change callback. */
   erc = ealDevRegisterStateCB( gs_hPlayDev, AudioPlayStateCB,
                                (void *)&gs_PlayStateChangeCBData );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterStateCB() = 0x%x in AudioInitPlay().\n", erc );
      return( AUDIO_ERROR );
   }

   /* Register the recording device error callback. */
   erc = ealDevRegisterErrorCB( gs_hPlayDev, AudioPlayErrorCB,
                                (void *)&gs_PlayErrorCBData );
   if( erc != EAL_RC_OK )
   {
      ErrPrintf( "ealDevRegisterErrorCB() = 0x%x in AudioInitPlay().\n", erc );
      return( AUDIO_ERROR );
   }

   return( AUDIO_OK );
}

/******************************************************************************
*******************************************************************************
*                  C A L L B A C K   F U N C T I O N S
*******************************************************************************
******************************************************************************/

/******************************************************************************
* void AudioRecErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData )
******************************************************************************/
void AudioRecErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData )
{
   int irc;
   AudioRecErrorCBData *pErrorData = (AudioRecErrorCBData *)pUserData;
   pErrorData->EalRc = rc;

   if( pErrorData->hEvent != NULL )
   {
      irc = osSignalEventSem( pErrorData->hEvent );
      if( irc != OS_OK )
      {
         ErrPrintf( "osSignalEventSem() = 0x%x in AudioRecErrorCB().\n", irc );
      }
   }
}

/******************************************************************************
* void AudioRecStateCB( EALDevHandle ulDevHandle, EALState newState,
                        EALState oldState, EALStateCause cause,
                        void *pUserData )
******************************************************************************/
void AudioRecStateCB( EALDevHandle ulDevHandle, EALState newState,
                      EALState oldState, EALStateCause cause,
                      void *pUserData )
{
   int irc = OS_OK;
   AudioRecStateChangeCBData *pStateData;

   pStateData = (AudioRecStateChangeCBData *)pUserData;
   pStateData->EalState = newState;

   if( pStateData->hEvent != NULL  )
   {
	  if ( oldState == EAL_STATE_ACTIVE || oldState == EAL_STATE_ACTIVE_AND_EMPTY )
      {
		 if ( newState == EAL_STATE_OPEN )
		    irc = osSignalEventSem( pStateData->hEvent );
	  }
	  else
		 irc = osSignalEventSem( pStateData->hEvent );
      if( irc != OS_OK )
      {
         ErrPrintf( "osSignalEventSem() = 0x%x in AudioRecStateCB().\n", irc );
      }
   }
}

/******************************************************************************
* void AudioRecBufDoneCB( EALDevHandle ulDevHandle, EALBufInfo *pBufInfo,
                          void *pUserData )
******************************************************************************/
void AudioRecBufDoneCB( EALDevHandle ulDevHandle, EALBufInfo *pBufInfo,
                        void *pUserData )
{
   AudioRecBufDoneCBData *pData = (AudioRecBufDoneCBData *)pUserData;
   EALRC erc;
   ESRRC esrrc;
   unsigned long ulBytesToCopy, ulNumBytesWritten;

   /* If any data was stored in the buffer by the EAL, process it. */
   if( (pBufInfo->ulXferCount > 0) && (pData->iClosePending == 0) )
   {
      /* Record the audio to the buffer, if required. */
      if( (pData->pAudioBuffer != NULL) &&
          (pData->ulAudioBufferIndex < pData->ulAudioBufferLen) )
      {
         if( pData->ulAudioBufferIndex + pBufInfo->ulXferCount >
             pData->ulAudioBufferLen )
            ulBytesToCopy =
                        pData->ulAudioBufferLen - pData->ulAudioBufferIndex;
         else
            ulBytesToCopy = pBufInfo->ulXferCount;
         memmove( pData->pAudioBuffer + pData->ulAudioBufferIndex,
                  pBufInfo->pBuffer, ulBytesToCopy );
         pData->ulAudioBufferIndex += ulBytesToCopy;
      }

      /* Enqueue the data to the ESR. */
      esrrc = esrEnqueuePCM( pData->hEsr, pBufInfo->pBuffer,
                             pBufInfo->ulXferCount, &ulNumBytesWritten );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrEnqueuePCM() = 0x%x in AudioRecBufDoneCB().\n", esrrc );
      }
   }

   /* If the audio interface is not being closed, then register the buffer
      so that it can be filled again. Otherwise, free the buffer header and data
      pointers because the buffer is no longer needed. */
   if( pData->iClosePending == 0 )
   {
      /* The audio is not closing: reset the buffer state to UNINITIALIZED
         so that the EAL will use it again and then return it to the EAL. */
      pBufInfo->state = EAL_BUF_STATE_UNINIT;
      erc = ealDevRegisterBuffer( ulDevHandle, pBufInfo );
      if( erc != EAL_RC_OK )
      {
         ErrPrintf( "ealDevRegisterBuffer() = 0x%x in AudioRecBufDoneCB()\n",
                     erc );
      }
   }
   else
   {
      /* A close is in progress. */
      free( pBufInfo->pBuffer );
      free( pBufInfo );
   }
}

/******************************************************************************
* void AudioPlayErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData )
******************************************************************************/
void AudioPlayErrorCB( EALDevHandle devHandle, EALRC rc, void *pUserData )
{
   int irc;
   AudioPlayErrorCBData *pErrorData = (AudioPlayErrorCBData *)pUserData;

   pErrorData->EalRc = rc;

   /* There is an error, so signal all active event semaphores to unblock
      the waiting function. */

   if( pErrorData->hEventActive != NULL )
   {
      irc = osSignalEventSem( pErrorData->hEventActive );
      if( irc != OS_OK )
      {
         ErrPrintf( "osSignalEventSem(hEventActive) = %ld "
                    "in AudioPlayErrorCB().\n", irc );
      }
   }
   if( pErrorData->hEventActiveAndEmpty != NULL )
   {
      irc = osSignalEventSem( pErrorData->hEventActiveAndEmpty );
      if( irc != OS_OK )
      {
         ErrPrintf( "osSignalEventSem(hEventActiveAndEmpty) = %ld "
                    "in AudioPlayErrorCB().\n", irc );
      }
   }
   if( pErrorData->hEvent != NULL )
   {
      irc = osSignalEventSem( pErrorData->hEvent );
      if( irc != OS_OK )
      {
         ErrPrintf( "osSignalEventSem(hEvent) = %ld in AudioPlayErrorCB().\n",
                     irc );
      }
   }
}

/******************************************************************************
* void AudioPlayStateCB( EALDevHandle ulDevHandle, EALState newState,
                         EALState oldState, EALStateCause cause,
                         void *pUserData )
******************************************************************************/
void AudioPlayStateCB( EALDevHandle ulDevHandle, EALState newState,
                       EALState oldState, EALStateCause cause,
                       void *pUserData )
{
   int irc;
   AudioPlayStateChangeCBData *pStateData;
   pStateData = (AudioPlayStateChangeCBData *)pUserData;

   pStateData->EalState = newState;

   /* Based on the new state, signal the appropriate event. */
   if( newState == EAL_STATE_ACTIVE )
   {
      if( pStateData->hEventActive != NULL )
      {
         irc = osSignalEventSem( pStateData->hEventActive );
         if( irc != OS_OK )
         {
            ErrPrintf( "osSignalEventSem(hEventActive) = %ld "
                       "in AudioPlayStateCB().\n", irc );
         }
      }
   }
   else if( newState == EAL_STATE_ACTIVE_AND_EMPTY )
   {
      if( pStateData->hEventActiveAndEmpty != NULL )
      {
         irc = osSignalEventSem( pStateData->hEventActiveAndEmpty );
         if( irc != OS_OK )
         {
            ErrPrintf( "osSignalEventSem(hEventActiveAndEmpty) = %ld "
                       "in AudioPlayStateCB().\n", irc );
         }
      }
   }
   else
   {
      if( pStateData->hEvent != NULL )
      {
         irc = osSignalEventSem( pStateData->hEvent );
         if( irc != OS_OK )
         {
            ErrPrintf( "osSignalEventSem(hEvent) = %ld "
                       "in AudioPlayStateCB().\n", irc );
         }
      }
   }
}

/******************************************************************************
* void EalCtrlErrorCB(	EALDevId devId, EALDevType devType,
                        EALDevHandle hDev, void *pCtrlErrorData )
******************************************************************************/
void EalCtrlErrorCB(	EALDevId devId, EALDevType devType,
                     EALDevHandle hDev, void *pCtrlErrorData )
{
   ErrPrintf( "ealCtrlErrorCB(): DevId: %ld Type: %d hDev: %p\n",
               devId, devType, hDev );
}


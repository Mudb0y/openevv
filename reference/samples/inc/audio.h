/*=========================================================================*/
/*                                                                         */
/* audio.h                                                                 */
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

#define AUDIO_OK     100
#define AUDIO_ERROR  101
#define AUDIO_NODEVS 102
#define AUDIO_NO_MEM 103

int AudioInit( ESREngineHandle hEsr );
int AudioUninit( void );
int AudioOpenRec( void );
int AudioStartRec( unsigned char *pRecBuf, unsigned long ulRecBufSize );
int AudioStopRec( unsigned long *pulBytesRec );
int AudioCloseRec( void );
int AudioOpenPlay( void );
int AudioPlay( unsigned char *pPlayBuf, unsigned long ulPlayBufSize );
int AudioClosePlay( void );

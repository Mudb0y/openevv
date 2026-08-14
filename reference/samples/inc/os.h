/*=========================================================================*/
/*                                                                         */
/* os.h                                                                    */
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

#if !defined(_OS_H_)
#define _OS_H_

#include <windows.h>
#define SEM_HANDLE     HANDLE

void osSleep( unsigned long ulMilliSecs );

unsigned long osBeginThread( void( __cdecl *pFunc)( void * ), int iStackSize, void *pParm );

void osEndThread( void );
unsigned long osGetCurrentThreadId( void );

int osCreateBinarySem( SEM_HANDLE *phSem );
int osDestroyBinarySem( SEM_HANDLE hSem );
int osClaimBinarySem( SEM_HANDLE hSem, unsigned long ulTimeout );
int osReleaseBinarySem( SEM_HANDLE hSem );
int osCreateEventSem( SEM_HANDLE *phSem );
int osWaitOnEventSem( SEM_HANDLE hSem, unsigned long ulTimeOut );
int osDestroyEventSem( SEM_HANDLE hSem );
int osSignalEventSem( SEM_HANDLE hSem );
int osResetEventSem( SEM_HANDLE hSem );

int osKeyHit( void );

void osDebugBreak( char *pszFmt, ... );
unsigned long osGetTickCount( void );

#define OS_OK           0
#define OS_ERROR        1
#define OS_SEM_TIMEOUT  2

#endif

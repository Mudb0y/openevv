/*=========================================================================*/
/*                                                                         */
/* oswin32.c                                                               */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1		AT2T5ZZ V4.3                                       */
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
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <conio.h>
#include <process.h>

/* Application include files */
#include "os.h"
#include "uifuncs.h"

/******************************************************************************
* void osSleep( unsigned long ulMilliSecs )
******************************************************************************/
void osSleep( unsigned long ulMilliSecs )
{
   Sleep( ulMilliSecs );
}

/******************************************************************************
* unsigned long osBeginThread( void( __cdecl *pFunc)( void * ), int iStackSize,
*                              void *pParm )
******************************************************************************/
unsigned long osBeginThread(
    void ( __cdecl *pFunc)( void * ),
    int iStackSize,
    void *pParm )
{
   return( _beginthread( pFunc, iStackSize, pParm ) );
}

/******************************************************************************
* void osEndThread( void )
******************************************************************************/
void osEndThread( void )
{
   _endthread();
}

/******************************************************************************
* unsigned long osGetCurrentThreadId( void )
******************************************************************************/
unsigned long osGetCurrentThreadId( void )
{
   return( GetCurrentThreadId() );
}

/******************************************************************************
* int osCreateBinarySem( SEM_HANDLE *phSem )
******************************************************************************/
int osCreateBinarySem( SEM_HANDLE *phSem )
{
   /* Create an unnamed mutex */
   *phSem = CreateMutex( NULL, FALSE, NULL );

   if( *phSem == NULL )
      return( OS_ERROR );
   else
      return( OS_OK );
}

/******************************************************************************
* int osDestroyBinarySem( SEM_HANDLE hSem )
******************************************************************************/
int osDestroyBinarySem( SEM_HANDLE hSem )
{
   CloseHandle( hSem );
   return( OS_OK );
}

/******************************************************************************
* int osClaimBinarySem( SEM_HANDLE hSem, unsigned long ulTimeout )
******************************************************************************/
int osClaimBinarySem( SEM_HANDLE hSem, unsigned long ulTimeout )
{
   DWORD dwrc;

   dwrc = WaitForSingleObject( hSem, ulTimeout );
   if( dwrc == WAIT_TIMEOUT )
      return( OS_SEM_TIMEOUT );
   else if( dwrc == WAIT_OBJECT_0 )
      return( OS_OK );
   else
      return( OS_ERROR );
}

/******************************************************************************
* int osReleaseBinarySem( SEM_HANDLE hSem )
******************************************************************************/
int osReleaseBinarySem( SEM_HANDLE hSem )
{
   if( ReleaseMutex( hSem ) == 0 )
      return( OS_ERROR );
   else
      return( OS_OK );
}

/******************************************************************************
* int osCreateEventSem( SEM_HANDLE *phSem )
******************************************************************************/
int osCreateEventSem( SEM_HANDLE *phSem )
{
   /* Create an unnamed event, which must be manually reset and is
      initially not signalled. */
   *phSem = CreateEvent( NULL, TRUE, FALSE, NULL );
   if( *phSem == NULL )
      return( OS_ERROR );
   else
      return( OS_OK );
}

/******************************************************************************
* int osDestroyEventSem( SEM_HANDLE hSem )
******************************************************************************/
int osDestroyEventSem( SEM_HANDLE hSem )
{
   CloseHandle( hSem );
   return( OS_OK );
}

/******************************************************************************
* int osWaitOnEventSem( SEM_HANDLE hSem, unsigned long ulTimeOut )
******************************************************************************/
int osWaitOnEventSem( SEM_HANDLE hSem, unsigned long ulTimeOut )
{
   DWORD dwrc;

   dwrc = WaitForSingleObject( hSem, ulTimeOut );
   if( dwrc == WAIT_TIMEOUT )
      return( OS_SEM_TIMEOUT );
   else if( dwrc == WAIT_OBJECT_0 )
      return( OS_OK );
   else
      return( OS_ERROR );

}

/******************************************************************************
* int osSignalEventSem( SEM_HANDLE hSem )
******************************************************************************/
int osSignalEventSem( SEM_HANDLE hSem )
{
   if( SetEvent( hSem ) == 0 )
      return( OS_ERROR );
   else
      return( OS_OK );
}

/******************************************************************************
* int osResetEventSem( SEM_HANDLE hSem )
******************************************************************************/
int osResetEventSem( SEM_HANDLE hSem )
{
   if( ResetEvent( hSem ) == 0 )
      return( OS_ERROR );
   else
      return( OS_OK );
}

/******************************************************************************
* int osKeyHit( void )
******************************************************************************/
int osKeyHit( void )
{
   return( _kbhit() );
}


/******************************************************************************
* void osDebugBreak( char *pszFmt, ... )
******************************************************************************/
void osDebugBreak( char *pszFmt, ... )
{
   va_list ArgPtr;
   static char szBuffer[1024];

   szBuffer[0] = '\0';

   va_start( ArgPtr, pszFmt );
   _vsnprintf( szBuffer, sizeof(szBuffer), pszFmt, ArgPtr );
   va_end( ArgPtr );

   ErrPrintf( 0, "Fatal error-<%s>\n", szBuffer );

   _asm int 3;
}

/******************************************************************************
* unsigned long osGetTickCount( void )
******************************************************************************/
unsigned long osGetTickCount( void )
{
   return( GetTickCount() );
}

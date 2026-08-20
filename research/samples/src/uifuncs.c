/*=========================================================================*/
/*                                                                         */
/* unifcns.c                                                               */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1 	AT2T5ZZ V4.3                                       */
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

#include <stdio.h>
#include <stdarg.h>

#include "phone.h"
#include "eci.h"

#define MAX_MSG_LEN  1024


/******************************************************************************
* void ErrPrintf( char *pszFmt, ... )
******************************************************************************/
void ErrPrintf( char *pszFmt, ... )
{
   va_list ArgPtr;
   char szBuffer[MAX_MSG_LEN+1];

   szBuffer[0] = '\0';

   va_start( ArgPtr, pszFmt );
   vsprintf( szBuffer, pszFmt, ArgPtr );
   va_end( ArgPtr );

   printf( szBuffer );
}

/******************************************************************************
* void UIPrintf( char *pszFmt, ... )
******************************************************************************/
void UIPrintf( char *pszFmt, ... )
{
   va_list ArgPtr;
   char szBuffer[MAX_MSG_LEN+1];

   szBuffer[0] = '\0';

   va_start( ArgPtr, pszFmt );
   vsprintf( szBuffer, pszFmt, ArgPtr );
   va_end( ArgPtr );

   printf( szBuffer );
}

/******************************************************************************
* void TracePrintf( char *pszFmt, ... )
******************************************************************************/
void TracePrintf( char *pszFmt, ... )
{
   va_list ArgPtr;
   char szBuffer[MAX_MSG_LEN+1];

   return;

   szBuffer[0] = '\0';

   va_start( ArgPtr, pszFmt );
   vsprintf( szBuffer, pszFmt, ArgPtr );
   va_end( ArgPtr );

   printf( "Tr:" );
   printf( szBuffer );
}

/******************************************************************************
* int TTSPrompt( int iWaitForTTS, ECIHand hEci, char *pszFmt, ... )
******************************************************************************/
int TTSPrompt( int iWaitForTTS, ECIHand hEci, char *pszFmt, ... )
{
   va_list ArgPtr;
   char szBuffer[MAX_MSG_LEN+1];

   szBuffer[0] = '\0';

   va_start( ArgPtr, pszFmt );
   vsprintf( szBuffer, pszFmt, ArgPtr );
   va_end( ArgPtr );

   printf( szBuffer );

   /* Give the requested text to the TTS engine. */
   if( eciAddText( hEci, szBuffer ) == 0 )
   {
      ErrPrintf( "eciAddText() = 0 in TTSPrompt().\n" );
      return( APPRC_ERROR );
   }

   /* Instruct TTS to speak the data just queued. */
   if( eciSynthesize( hEci ) == 0 )
   {
      ErrPrintf( "eciSynthesize() = 0 in TTSPrompt().\n" );
      return( APPRC_ERROR );
   }

   /* If requested, wait for the text to be spoken. */
   if( iWaitForTTS != 0 )
      eciSynchronize( hEci );

   return( APPRC_OK );

}

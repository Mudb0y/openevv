/*=========================================================================*/
/*                                                                         */
/* uifuncs.h                                                               */
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

void ErrPrintf( char *pszFmt, ... );
void UIPrintf( char *pszFmt, ... );
void TracePrintf( char *pszFmt, ... );
int TTSPrompt( int iWaitForTTS, char *pszFmt, ... );


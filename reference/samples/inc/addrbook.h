/*=========================================================================*/
/*                                                                         */
/* addrbook.h                                                              */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1	AT2T5ZZ V4.3		                                   */
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

#include "phone.h"

#define NUM_ADDR_BOOK_ENTRIES   99
#define AUDIO_BUF_LEN   4*(11025*2)

#define MAX_BASEFORM_LEN   128

typedef struct _AddressBookEntry
{
   int iInUse;
        char szName[NAME_LEN+1];
        int iPhoneNum[11];
        ESRBaseformResult Baseform[MAX_BASEFORM_LEN];
        int iBaseformLen;
        unsigned char AudioBuffer[AUDIO_BUF_LEN];
        unsigned long ulAudioBufferLen;
} AddressBookEntry;


void *AddrBookGetPointer( void );
int AddrBookInit( void );
int AddrBookUninit( void );
int AddrBookGetFreeEntry( AddressBookEntry **ppEntry );
int AddrBookHasEntries( void );
AddressBookEntry *AddrBookGetEntry( int iIndex );
int AddrBookGetDataBlock( void **ppData );
int AddrBookSave( FILE *fp );
int AddrBookLoad( FILE *fp );
void AddrBookCleanupEntry( int i );

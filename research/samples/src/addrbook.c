/*=========================================================================*/
/*                                                                         */
/* addrbook.c                                                              */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1 AT2T5ZZ V4.3                       			   */
/* (C) Copyright IBM Corp. 2000, 2004       All Rights Reserved.           */                                     
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
#include <stdio.h>

/* Application include files */
#include "uifuncs.h"
#include "phone.h"
#include "addrbook.h"


/* Declare array of address book entries. */
static AddressBookEntry AddrBook[NUM_ADDR_BOOK_ENTRIES];


/******************************************************************************
* void *AddrBookGetPointer( void )
******************************************************************************/
void *AddrBookGetPointer( void )
{
   return( AddrBook );
}

/******************************************************************************
* int AddrBookInit( void )
******************************************************************************/
int AddrBookInit( void )
{
   int i;

   /* Ensure that the entire address book is empty to start. */
   for( i = 0; i < NUM_ADDR_BOOK_ENTRIES; i++ )
   {
      memset( &(AddrBook[i]), 0, sizeof( AddrBook[i] ) );
   }

   return( APPRC_OK );
}

/******************************************************************************
* int AddrBookUninit( void )
******************************************************************************/
int AddrBookUninit( void )
{
   return( APPRC_OK );
}

/******************************************************************************
* int AddrBookGetFreeEntry( void )
******************************************************************************/
int AddrBookGetFreeEntry( AddressBookEntry **pEntry )
{
   int i;

   for( i = 0; i < NUM_ADDR_BOOK_ENTRIES; i++ )
   {
      if( AddrBook[i].iInUse == 0 )
      {
         *pEntry = &(AddrBook[i]);
         return( i );
      }
   }
   return( -1 );
}

/******************************************************************************
* int AddrBookHasEntries( void )
******************************************************************************/
int AddrBookHasEntries( void )
{
   int i;

   for( i = 0; i < NUM_ADDR_BOOK_ENTRIES; i++ )
      if( AddrBook[i].iInUse != 0 )
         return( 1 );
   return( 0 );

}

/******************************************************************************
* AddressBookEntry *AddrBookGetEntry( int iIndex )
******************************************************************************/
AddressBookEntry *AddrBookGetEntry( int iIndex )
{
   if( iIndex >= NUM_ADDR_BOOK_ENTRIES )
      return( NULL );
   else
      return( &(AddrBook[iIndex]) );
}

/******************************************************************************
* int AddrBookGetDataBlock( void **ppData )
******************************************************************************/
int AddrBookGetDataBlock( void **ppData )
{
   *ppData = (void *)AddrBook;
   return( sizeof(AddrBook) );
}

/******************************************************************************
* int AddrBookSave( FILE *fp )
******************************************************************************/
int AddrBookSave( FILE *fp )
{
   int i;

   for( i = 0; i < NUM_ADDR_BOOK_ENTRIES; i++ )
   {
      if( AddrBook[i].iInUse != 0 )
      {
         /* Save this entry. */
         fwrite( &i, sizeof(int), 1, fp );
         fwrite( &(AddrBook[i]), sizeof(AddrBook[i]), 1, fp );
      }
   }

   /* Write out the end of the address book marker. */
   i = -1;
   fwrite( &i, sizeof(int), 1, fp );

   return( APPRC_OK );
}

/******************************************************************************
* int AddrBookLoad( FILE *fp )
******************************************************************************/
int AddrBookLoad( FILE *fp )
{
   int i;

   /* Reset the current address book. */
   AddrBookInit();

   /* Read the first index. */
   fread( &i, sizeof(int), 1, fp );

   while( i != -1 )
   {
      /* Load the entry. */
      fread( &(AddrBook[i]), sizeof(AddrBook[i]), 1, fp );

      /* Read the next index. */
      fread( &i, sizeof(int), 1, fp );
   }

   return( APPRC_OK );
}

/******************************************************************************
* void AddrBookCleanupEntry( int i )
******************************************************************************/
void AddrBookCleanupEntry( int i )
{
   AddrBook[i].iInUse = 0;
   AddrBook[i].ulAudioBufferLen = 0;
}

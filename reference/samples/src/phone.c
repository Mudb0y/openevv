/*=========================================================================*/
/*                                                                         */
/* phone.c                                                                 */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1		AT2T5ZZ V4.3                                       */
/* (C) Copyright IBM Corp. 2000, 2004       All Rights Reserved.           */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement accompanying the Licensed Materials.              */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*                                                                         */
/*=========================================================================*/

/* Standard include files. */
#include <stdio.h>

/* EVV SDK include files. */
#include "edu.h"
#include "esr.h"
#include "eci.h"
#include "voc.h"
#include "aop.h"

/* Application include files. */
#include "os.h"
#include "audio.h"
#include "uifuncs.h"
#include "phone.h"
#include "appevent.h"
#include "esrcbs.h"
#include "addrbook.h"

/* Include bin file collection IDs. */
#include "engdata.h"
#include "vocabs.h"


/******************************************************************************
*******************************************************************************
*                    L O C A L   D E F I N I T I O N S
*******************************************************************************
******************************************************************************/

/* Base block ID of engine data in the engine data collection. */
#define ENGDATA_ID_BASE    EAK01AGE_DSP
#define VOCABSET_BLOCK_ID  PHONE_VOC
#define BLAZER_BLOCK_ID	   1
#define STARTUS_BLOCK_ID   2
#define W95NAV_BLOCK_ID	   3
#define NUM_POOLS          3
#define CTTS_BLOCK_ID	   1


#define VOCABID_MAIN       1
#define VOCABID_NAMES      2
#define VOCABID_OPTIONS    3
#define VOCABID_YESNO      4
#define VOCABID_DIRECTIONS 500

#define EXTRNLIST_NAMES		1000

#define DYNVOCABID_EMAIL    200

#define READY_PCM_ID        1000

/* Language indexes used as block ID offsets when accessing data blocks within
   the collections. */
typedef enum
{
   LANGINDEX_EN_US = 0
} AppLangIndex;

/******************************************************************************
*******************************************************************************
*                    G L O B A L   D E C L A R A T I O N S
*******************************************************************************
******************************************************************************/

/* Pointer to the collection containing the ESR data and the application
   vocabulary sets.

   Note: If this was an embedded application, these would be set to the
         ROM address where the data is stored.
*/
static EDUCollection *gs_EngineDataColl = NULL;
static EDUCollection *gs_VocabSetsColl = NULL;
static EDUCollection *gs_DtmfColl = NULL;
static EDUCollection *gs_PoolColl = NULL;
static EDUCollection *gs_CTTSColl = NULL;

/* Stores the CTTS Voice Attributes returned from eciRegisterVoice() to be
   used with eciUnregisterVoice()
*/
static ECIVoiceAttrib gs_vAttrib;

/* The 'Goodbye' command sets the value to 1, signaling the
   process user input loop to exit.
*/
static int gs_iExit = 0;

/* Listening mode of the application; defaults to ESR_LMODE_PUSHTOTALK. */
static ESRListeningMode gs_ListeningMode = ESR_LMODE_PUSHTOTALK;

/* Handle to the current recognition engine instance. */
static ESREngineHandle gs_hEsr = 0;

/* Handle to the current synthesis engine instance. */
static ECIHand gs_hEci = 0;

/* Handle to the voc instance the application will use */
static VOCInstHandle gs_hVoc = 0;

/* Handle to the vocabulary set the application will use. */
static ESRVocabSetHandle gs_hVocabSet = 0;

/* Handle to the email vocabulary set that is generated
   at run time.
*/
static ESRVocabSetHandle gs_hEmailVocabSet = 0;

static RecoStateChangeData gs_RecoStateChangeData;
static AcbfStateChangeData gs_AcbfStateChangeData;

static AcbfResultData gs_AcbfResultData;
static RecoResultData gs_RecoResultData = { NULL };

static AudioCBData gs_AudioCBData = { NULL, 0 };

typedef struct _WCISTableEntry
{
   int iActive;
   int iVocabId;
   char *pszText;
} WCISTableEntry;

static WCISTableEntry gs_WCISTable[] =
{
   {  0, VOCABID_MAIN,
            "Dial followed by a phone number.  Add New Name.  "
            "Change Options.  Directions. Check Email. Goodbye."  },
   {  0, VOCABID_NAMES,
            "Call followed by a name from your name list.  "
            "Delete followed by a name from your name list. " },
   {  0, VOCABID_OPTIONS,
            "Save or Restore Address Book.  Import Name List.  " },
   {  0, VOCABID_YESNO,    "Yes.  No. " },
   {  0, DYNVOCABID_EMAIL,
            "Read Mail From followed by an author name. "
            "Tell Me About The Mail From followed by an author name.  "
            "Exit Email. " },
   { 0, VOCABID_DIRECTIONS, 
	   "Ohio. " 
	   "New Brunswick. "
	   "Yellowstone. "
	   "Forest Hill Boulevard. "
	   "Pleasantville. "
	   "Saint Petersburg. "
	   "Massachusetts. " 
	   "Staten Island. "
	   "Universal Studios. "   }
};
static int gs_WCISTableSize = sizeof(gs_WCISTable)/sizeof(gs_WCISTable[0]);

/* aop handle */
extern AOPHandle g_hAOP;


/******************************************************************************
*******************************************************************************
*           L O C A L   F U N C T I O N   P R O T O T Y P E S
*******************************************************************************
******************************************************************************/
int ExecuteRecoSequence( AppEvent *pEvent );


/******************************************************************************
*******************************************************************************
*           F U C N T I O N   D E F I N I T I O N S
*******************************************************************************
******************************************************************************/

/******************************************************************************
* void UpdateWCISEntry( int iVocabId, int iState )
******************************************************************************/
void UpdateWCISEntry( int iVocabId, int iState )
{
   int i;

   for( i = 0; i < gs_WCISTableSize; i++ )
   {
      if( gs_WCISTable[i].iVocabId == iVocabId )
      {
         gs_WCISTable[i].iActive = iState;
         break;
      }
   }
}

/******************************************************************************
* void SayWCIS( void )
******************************************************************************/
void SayWCIS( void )
{
   int i, irc;

   /* Say each of the active WCIS elements without waiting for each to
      complete.
   */
   for( i = 0; i < gs_WCISTableSize; i++ )
   {
      if( gs_WCISTable[i].iActive != 0 )
      {
         irc = TTSPrompt( 0, gs_hEci, "%s\n", gs_WCISTable[i].pszText );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt(ESR_RC_NO_MATCH) = %d in "
                       "SayWCIS()\n", irc );
            return;
         }
      }
   }

   /* Now that all the text has been added, wait for it to be synthesized. */
   eciSynchronize( gs_hEci );

   UIPrintf( "\n" );
}

/******************************************************************************
* EDUCollection *LoadCollection( void )
******************************************************************************/
EDUCollection *LoadCollection( char *pszBinFile )
{
   EDUCollection *pData;
   unsigned long ulCollectionSize;
   int irc;

   /* Query the size of the collection. */
   irc = eduLoadCollection( pszBinFile, NULL, &ulCollectionSize );
   if( irc != EDU_RC_OK )
   {
      ErrPrintf( "eduLoadCollection(%s) Query = 0x%x in LoadCollections()\n",
                  pszBinFile, irc );
      return( NULL );
   }

   /* Allocate the memory to contain the collection data. */
   pData = (EDUCollection *)malloc( ulCollectionSize );
   if( pData == NULL )
   {
      ErrPrintf( "malloc() failed in LoadCollections().\n" );
      return( NULL );
   }

   /* Now load the collection data itself. */
   irc = eduLoadCollection( pszBinFile, pData, &ulCollectionSize );
   if( irc != EDU_RC_OK )
   {
      ErrPrintf( "eduLoadCollection(%s) Load = 0x%x in LoadCollections().\n",
                  pszBinFile, irc );
      return( NULL );
   }

   return( pData );
}

/******************************************************************************
* int LoadBinFiles( void )
******************************************************************************/
int LoadBinFiles( void )
{

   /* Load the engine data collection. */
   gs_EngineDataColl = LoadCollection( "engdata.bin" );
   if( gs_EngineDataColl == NULL )
      return( APPRC_ERROR );

   /* Load the vocabulary set collection. */
   gs_VocabSetsColl = LoadCollection( "vocabs.bin" );
   if( gs_VocabSetsColl == NULL )
      return( APPRC_ERROR );

   /* Load the dtmf collection. */
   gs_DtmfColl = LoadCollection( "audprmts.bin" );
   if( gs_DtmfColl == NULL )
      return( APPRC_ERROR );

   /* Load the pool collection */
   gs_PoolColl = LoadCollection( "pools.bin" );
   if( gs_PoolColl == NULL )
      return( APPRC_ERROR );

   /* Load the CTTS collection */
   gs_CTTSColl = LoadCollection( "ctts.bin" );
   if( gs_CTTSColl == NULL )
      return( APPRC_ERROR );

   return( APPRC_OK );
}

/******************************************************************************
* ESRRecoInitializationData *GetRecoInitBlock( AppLangIndex LangIndex )
******************************************************************************/
int GetRecoInitBlock( AppLangIndex LangIndex, ESRInitializationData *pInitData )
{
   unsigned long ulBlockSize;
   int irc;

   /* Get the address of the recognition engine initialization data block for the
      requested language.
   */
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 0,
                           &ulBlockSize, &pInitData->pDspBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 0, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 1,
                           &ulBlockSize, &pInitData->pSdBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 1, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 2,
                           &ulBlockSize, &pInitData->pLblBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 2, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 3,
                           &ulBlockSize, &pInitData->pDcdBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 3, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 4,
                           &ulBlockSize, &pInitData->pAbsBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 4, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 5,
                           &ulBlockSize, &pInitData->pMnBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 5, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 6,
                           &ulBlockSize, &pInitData->pPqBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 6, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 7,
                     &ulBlockSize, &pInitData->pCqBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 7, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 8,
                  &ulBlockSize, &pInitData->pRqBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 8, irc );
      return( APPRC_ERROR );
   }
   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 9,
                           &ulBlockSize, &pInitData->pTrBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 9, irc );
      return( APPRC_ERROR );
   }

   irc = eduGetBlockByID( gs_EngineDataColl, ENGDATA_ID_BASE + LangIndex + 13,
                           &ulBlockSize, &pInitData->pPsBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in GetRecoInitBlock().\n",
                     ENGDATA_ID_BASE + LangIndex + 13, irc );
      return( APPRC_ERROR );
   }
 
   /* ESR_BLOCK_ALL is not appropriate since we do not use all the blocks. */
   pInitData->ulValidBlocks = ESR_BLOCK_DSP |
			      ESR_BLOCK_SD  |
				ESR_BLOCK_LBL |
				ESR_BLOCK_DCD |
				ESR_BLOCK_ABS |
				ESR_BLOCK_MN |
				ESR_BLOCK_PQ |
				ESR_BLOCK_CQ | 
				ESR_BLOCK_RQ |
				ESR_BLOCK_TR |
				ESR_BLOCK_PS;
   return( 0 );
}

/******************************************************************************
* ESRVocabSet *GetVocabSetBlock( unsigned long ulVocabSetBlockId,
*                                AppLangIndex LangIndex )
******************************************************************************/
ESRVocabSet *GetVocabSetBlock( unsigned long ulVocabSetBlockId,
                               AppLangIndex LangIndex )
{
   ESRVocabSet *pData;
   unsigned long ulBlockSize;
   int irc;

   /* Get the address of the vocabulary set data block for the
      requested language.
   */
   irc = eduGetBlockByID( gs_VocabSetsColl, ulVocabSetBlockId + LangIndex,
                           &ulBlockSize, (void **)&pData );
   if( irc != EDU_RC_OK )
   {
      ErrPrintf( "eduGetBlockByID()  = 0x%x in GetVocabSetBlock().\n", irc );
      return( NULL );
   }

   return( pData );
}

/******************************************************************************
* VOCPool *GetPoolBlock( unsigned long ulPoolBlockId )
******************************************************************************/
VOCPool *GetPoolBlock( unsigned long ulPoolBlockId )
{
   VOCPool *pData;
   unsigned long ulBlockSize;
   int irc;

   /* Get the address of the pool data block for the
      requested language.
   */
   irc = eduGetBlockByID( gs_PoolColl, ulPoolBlockId,
                           &ulBlockSize, (void **)&pData );
   if( irc != EDU_RC_OK )
   {
      ErrPrintf( "eduGetBlockByID()  = 0x%x in GetPoolBlock().\n", irc );
      return( NULL );
   }

   return( pData );
}

/******************************************************************************
* void *GetCTTSBlock( unsigned long ulCTTSBlockId )
******************************************************************************/
void *GetCTTSBlock( unsigned long ulCTTSBlockId )
{
   void *pData;
   unsigned long ulBlockSize;
   int irc;

   /* Get the address of the CTTS data block */
   irc = eduGetBlockByID( gs_CTTSColl, ulCTTSBlockId,
                           &ulBlockSize, (void **)&pData );
   if ( irc != EDU_RC_OK )
   {
      ErrPrintf( "eduGetBlockByID()  = 0x%x in GetCTTSBlock().\n", irc );
      return( NULL );
   }

   return( pData );
}

/******************************************************************************
* LoadCTTSVoice
* Loads the US English Male TTS Concatenative Voice 
* A failure to load will result in Formant TTS synthesis
******************************************************************************/
void LoadCTTSVoice() 
{
   int rc = 0;
   void *voiceData;
 
   /* Load the CTTS voice collection data */
   voiceData = GetCTTSBlock( CTTS_BLOCK_ID );
   if ( voiceData == NULL )
   {
       ErrPrintf( "GetCTTSBlock() = NULL in LoadCTTSVoice().\n" );
       return;
   }

   /* register the concatenative TTS voice */
   rc = eciRegisterVoice( gs_hEci, 1, voiceData, &gs_vAttrib );
   if ( rc != VoiceNoError )
   {
	  ErrPrintf( "eciRegisterVoice() failure in LoadCTTSVoice().\n" );
	  return;
   }

   /* copy the voice to the correct slot (0) so it becomes the active voice */
   rc = eciCopyVoice( gs_hEci, 1, 0 );
   if ( !rc )
   {
	  ErrPrintf( "eciCopyVoice() failure in LoadCTTSVoice().\n");
	  return;  
   }

   return;
}

void UnloadCTTSVoice( void )
{
   int rc = 0;
   void *voiceData;

   rc = eciUnregisterVoice( gs_hEci, 1, &gs_vAttrib, &voiceData );
   if ( rc != VoiceNoError )
   {
	  ErrPrintf( "eciUnregisterVoice() failure in UnloadCTTSVoice().\n" );
	  return;
   }
   /* memory associated with the voice (collection) will be deleted in AppUninitialize */

}

/******************************************************************************
* int ProcessCommandLine( int iArgCnt, char *apszArgs[] )
******************************************************************************/
int ProcessCommandLine( int iArgCnt, char *apszArgs[] )
{
   int i, iDisplayUsage = 0;  /* Assume that there's a valid command line. */
   char szParm[4];

   /* If there is more than one argument or the argument given
      is not valid, then display the application usage information.
   */
   if( iArgCnt > 2 )
      iDisplayUsage = 1;
   else if( iArgCnt == 2 )
   {
      if( strlen(apszArgs[1]) <= 3)
      {
         /* Create an uppercase version of the parameter. */
         for( i = 0; i < (int)strlen(apszArgs[1]); i++ )
            szParm[i] = toupper(apszArgs[1][i] );
         szParm[i] = '\0';

         if( strcmp( szParm, "P2T" ) == 0 )
         {
            gs_ListeningMode = ESR_LMODE_PUSHTOTALK;
         }
         else if( strcmp( szParm, "P2A" ) == 0 )
         {
            gs_ListeningMode = ESR_LMODE_PUSHTOACTIVATE;
         }
         else
            iDisplayUsage = 1;
      }
      else
      {
         iDisplayUsage = 1;
      }
   }

   if( iDisplayUsage == 1 )
   {
      printf(  "Usage: PHONE <listening mode>\n"
               "           where listeing mode is\n"
               "                 P2T   Push-To-Talk\n"
               "                 P2A   Push-To-Activate\n\n" );

      return( APPRC_ERROR );
   }

   if( gs_ListeningMode == ESR_LMODE_PUSHTOTALK )
      UIPrintf( "Listening mode is Push-To-Talk.\n\n" );
   else
      UIPrintf( "Listening mode is Push-To-Activate.\n\n" );

   return( APPRC_OK );
}


/******************************************************************************
* int AppInitialize( void )
******************************************************************************/
int AppInitialize( void )
{
   int irc;
   ESRAttrs Attrs;
   ESRPCMAttrs PCMAttrs;
   ESRRC esrrc;
   ESRInitializationData EsrInitData;
   OSTaskInfo TaskInfo;
   int vocrc;

   /* Initialize the address book. */
   if( AddrBookInit() != APPRC_OK )
   {
      ErrPrintf( "AddrBookInit() failed in AppInitialize().\n" );
      return( APPRC_ERROR );
   }

   /* Load into memory the binary image files that represent the ROM data. */
   if( LoadBinFiles() != APPRC_OK )
   {
      ErrPrintf( "LoadBinFiles() failed in AppInitialize().\n" );
      return( APPRC_ERROR );
   }

   /* Get a pointer to the proper recognition engine initialization data from the
      collection. */
   irc = GetRecoInitBlock( LANGINDEX_EN_US, &EsrInitData );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "GetRecoInitBlock() failed in AppInitialize().\n" );
      return( APPRC_ERROR );
   }


   /**** Initialize the ESR library component. *****/

   /* Set the PCM setting to match the audio module. */
   PCMAttrs.ulSamplingRate = 11025;
   PCMAttrs.ulBitsPerSample = 16;
   PCMAttrs.ulNumChannels = 1;

   /* Set the default ESR Attrs that need to be overridden. */
   Attrs.ulListeningMode = gs_ListeningMode;
   /* The CEP queue size must be large enough to hold the largest ABS utterance.
      Set to 2.5 seconds, there are 867 queue elements per seconds */
   Attrs.ulCepQueueSize = 2168;
 
   Attrs.ulValidAttrs = ESR_ATTRS_LISTENINGMODE | ESR_ATTRS_CEPQUEUESIZE ;
 
   /* Create an instance of the recognition engine. */
   esrrc = esrCreate( &gs_hEsr, &EsrInitData, &PCMAttrs, &Attrs );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrCreate()  = 0x%x in AppInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the global error callback. */
   esrrc = esrRegisterErrorCB( gs_hEsr, AppEsrErrorCB, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRegisterErrorCB()  = 0x%x in AppInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the global audio control callback. */
   esrrc = esrRegisterAudioCB( gs_hEsr, AppEsrAudioCB,
                                 (void *)(&gs_AudioCBData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRegisterAudioCB()  = 0x%x in AppInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Start the engine task so that the engine operates in a separate
      thread. */
   esrrc = esrStartEngineTask( gs_hEsr, &TaskInfo, 1024 );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrStartEngineTask()  = 0x%x in AppInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Initialize the audio interface module. */
   irc = AudioInit( gs_hEsr );
   if( irc != AUDIO_OK )
      return( APPRC_ERROR );

   /* Initialize the speech synthesis engine.  Note that this must occur after the call to
      ealOpen(), which initializes the Embedded Audio Layer (EAL). This call
      occurs in the AudioInit() function (above).
   */
   gs_hEci = eciNew();
   if( gs_hEci == NULL_ECI_HAND )
   {
      ErrPrintf( "eciNew() = NULL in AppInitialize().\n" );
      return( APPRC_ERROR );
   }

   /* If LoadCTTSVoice fails, formant TTS will be used */
   printf("Loading CTTS...\n");
   LoadCTTSVoice();

   /* Create a voc instance to be used by the application for all vocabulary management */
   vocrc = vocCreate( &gs_hVoc );
   if (vocrc != VOC_RC_OK )
   {
      ErrPrintf( "vocCreate() = 0x%x in AppInitialize().\n", vocrc );
      return( APPRC_ERROR );
   }

   return( APPRC_OK );
}


/******************************************************************************
* int AppUninitialize( void )
******************************************************************************/
int AppUninitialize( void )
{
   ESRRC esrrc;
   int irc;
   int vocrc;

   /* Unload CTTS */
   UnloadCTTSVoice();

   /* Destroy the voc instance */
   vocrc = vocDestroy( gs_hVoc );
   if (vocrc != VOC_RC_OK )
   {
      ErrPrintf( "vocDestroy) = 0x%x in AppUninitialize().\n", vocrc );
      return( APPRC_ERROR );
   }

   /* Clean up the audio module. */
   irc = AudioUninit();
   if( irc != AUDIO_OK )
      return( APPRC_ERROR );

   /* Clean up the speech synthesis engine. */
   eciDelete( gs_hEci );

   /* Unregister the global audio control callback. */
   esrrc = esrUnregisterAudioCB( gs_hEsr, AppEsrAudioCB, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrUnregisterAudioCB()  = 0x%x in AppInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Unregister the global error callback. */
   esrrc = esrUnregisterErrorCB( gs_hEsr, AppEsrErrorCB, NULL);
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrUnregisterErrorCB()  = 0x%x in AppUninitialize().\n",
                  esrrc );
      return( APPRC_ERROR );
   }

   /* Stop the engine thread. */
   esrrc = esrStopEngineTask( gs_hEsr );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrStopEngineTask()  = 0x%x in AppUninitialize().\n",
                  esrrc );
      return( APPRC_ERROR );
   }

   /* Clean up the instance of the recognition engine. */
   esrrc = esrDestroy( gs_hEsr, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrDestroy()  = 0x%x in AppUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Free the memory containing the collection data. */
   free( gs_EngineDataColl );
   free( gs_VocabSetsColl );
   free( gs_DtmfColl );
   free( gs_PoolColl );
   free( gs_CTTSColl );

   /* Clean up the address book. */
   if( AddrBookUninit() != APPRC_OK )
   {
      ErrPrintf( "AddrBookUninit() failed in AppUninitialize().\n" );
      return( APPRC_ERROR );
   }

   return( APPRC_OK );
}

/******************************************************************************
* int AppRecoInitialize( void )
******************************************************************************/
int AppRecoInitialize( void )
{
   ESRRC esrrc;
   ESRVocabSet *pVocabSet;
   int irc;

   /* Allocate the semaphores used to flag the events. */
   irc = osCreateEventSem( &gs_RecoStateChangeData.hSemEvent );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %d in AppRecoInitialize().\n", irc );
      return( APPRC_ERROR );
   }

   /* Initialize the recognition service. */
   esrrc = esrRecoInitialize( gs_hEsr );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoInitialize() = 0x%x in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the recognition error callback. */
   esrrc = esrRecoRegisterErrorCB( gs_hEsr, AppEsrRecoErrorCB,
                                       (void *)(&gs_RecoResultData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterErrorCB() = 0x%x in AppRecoInitialize().\n",
                  esrrc );
      return( APPRC_ERROR );
   }

   /* Register the recognition state change callback. */
   esrrc = esrRecoRegisterStateChangeCB( gs_hEsr, AppEsrRecoStateCB,
                                          (void *)(&gs_RecoStateChangeData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterStateChangeCB() = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the vocabulary set that the application will use. */
   pVocabSet = GetVocabSetBlock( VOCABSET_BLOCK_ID, LANGINDEX_EN_US );
   if( pVocabSet == NULL )
   {
      ErrPrintf( "GetVocabSetBlock() = NULL in AppRecoInitialize().\n" );
      return( APPRC_ERROR );
   }

   esrrc = esrVmgrRegisterVocabSet( pVocabSet, &gs_hVocabSet );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrVmgrRegisterVocabSet() = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register all the vocabularies so they can be easily enabled and
      disabled later.  Note that they could be registered and unregistered
      (as needed) to conserve memory. For this application and the number
      of vocabularies, however, the savings in minimal.
   */
   esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_MAIN,
                                 AppRecoResultMain, &gs_RecoResultData );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterVocab(MAIN) = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_YESNO,
                                 AppRecoResultYesNo, &gs_RecoResultData );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterVocab(YESNO) = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_OPTIONS,
                                 AppRecoResultOptions, &gs_RecoResultData );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterVocab(OPTIONS) = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_DIRECTIONS,
                                 AppRecoResultDirections, &gs_RecoResultData );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterVocab(DIRECTIONS) = 0x%x "
                 "in AppRecoInitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register audio stats callback for the AOP */
   esrrc = esrRecoRegisterFrameStatsCB( gs_hEsr, AppEsrFrameStatsCB, NULL);

   if (esrrc != ESR_RC_OK) 
   {
      ErrPrintf( "esrRecoRegisterFrameStatsCB() = 0x%x in "
		 "AppRecoInitialize().\n", esrrc);
      return( APPRC_ERROR );
   }

   return( APPRC_OK );
}

/******************************************************************************
* int AppRecoUninitialize( void )
******************************************************************************/
int AppRecoUninitialize( void )
{
   ESRRC esrrc;

   /* Unregister the vocabulary. */
   esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_MAIN, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterVocab(MAIN) = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_YESNO, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterVocab(YESNO) = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet,
                                          VOCABID_OPTIONS, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterVocab(OPTIONS) = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet,
                                          VOCABID_DIRECTIONS, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterVocab(DIRECTIONS) = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* If there are any entries in the address book, then the names vocabulary
      should have been registered.
   */
   if( AddrBookHasEntries() != 0 )
   {
      esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet,
                                          VOCABID_NAMES, NULL );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoUnregisterVocab(NAMES) = 0x%x "
                    "in AppRecoUninitialize().\n", esrrc );
         return( APPRC_ERROR );
      }
   }

   /* Unregister the vocabulary set from the vocabulary manager. */
   esrrc = esrVmgrUnregisterVocabSet( gs_hVocabSet, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrVmgrUnregisterVocabSet() = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Unregister the callbacks. */
   esrrc = esrRecoUnregisterStateChangeCB( gs_hEsr, AppEsrRecoStateCB, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterStateChangeCB() = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoUnregisterErrorCB( gs_hEsr, AppEsrRecoErrorCB, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUnregisterErrorCB() = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrRecoUnregisterFrameStatsCB( gs_hEsr, AppEsrFrameStatsCB, NULL );
   if( esrrc != ESR_RC_OK)
   {
      ErrPrintf( "esrRecoUnregisterFrameStatsCB() = 0x%x "
		  "in AppRecoUninitialize().\n", esrrc);
      return( APPRC_ERROR );
   }

   /* Uninitialize the recognition service. */
   esrrc = esrRecoUninitialize( gs_hEsr );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoUninitialize() = 0x%x "
                 "in AppRecoUninitialize().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Clean up allocated OS resources. */
   osDestroyEventSem( gs_RecoStateChangeData.hSemEvent );

   return( APPRC_OK );
}

/******************************************************************************
* int AppInitializeMainContext( void )
******************************************************************************/
int AppInitializeMainContext( void )
{
   ESRRC esrrc;

   /* Enable the main vocabulary so that its commands are active. */
   esrrc = esrRecoEnableVocab( gs_hEsr, gs_hVocabSet, VOCABID_MAIN );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoEnableVocab(MAIN) = 0x%x "
                 "in AppInitializeMainContext().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Update the active WCIS text. */
   UpdateWCISEntry( VOCABID_MAIN, 1 );

   /* If there are any names in the address book, then there are names
      added to the external list for the names vocabulary so as to
      enable it now.  Note that a vocabulary with an external list must
      contain data in the external list before it can be enabled.
   */
   if( AddrBookHasEntries() != 0 )
   {
      /* If the vocabulary is already registered, ignore the error. */
      esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hVocabSet, VOCABID_NAMES,
                                    AppRecoResultNames, &gs_RecoResultData );
      if( (esrrc != ESR_RC_OK) && (esrrc != ESR_RC_ALREADY_REGISTERED) )
      {
         ErrPrintf( "esrRecoRegisterVocab(NAMES) = 0x%x "
                    "in AppInitializeMainContext().\n", esrrc );
         return( APPRC_ERROR );
      }

      esrrc = esrRecoEnableVocab( gs_hEsr, gs_hVocabSet, VOCABID_NAMES );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoEnableVocab(NAMES) = 0x%x "
                    "in AppInitializeMainContext().\n", esrrc );
         return( APPRC_ERROR );
      }

      /* Update the active WCIS text. */
      UpdateWCISEntry( VOCABID_NAMES, 1 );
   }

   return( APPRC_OK );
}

/******************************************************************************
* int AppUninitializeMainContext( void )
******************************************************************************/
int AppUninitializeMainContext( void )
{
   ESRRC esrrc;

   /* Disable the main vocabulary. */
   esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_MAIN );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoDisableVocab(MAIN) = 0x%x "
                 "in AppUninitializeMainContext().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Update the active WCIS text. */
   UpdateWCISEntry( VOCABID_MAIN, 0 );

   /* If there are any entries in the address book, then the names vocabulary
      will be active; so disable and unregister it.  Then
      if the last entry in the list is deleted, do not re-register
      the names vocabulary after returning to the main context.  This is because
      a vocabulary that contains an external list can not be registered
      unless there is at least one baseform in the list.
   */
   if( AddrBookHasEntries() != 0 )
   {
      esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_NAMES );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoDisableVocab(NAMES) = 0x%x "
                    "in AppUninitializeMainContext().\n", esrrc );
         return( APPRC_ERROR );
      }

      esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hVocabSet,
                                       VOCABID_NAMES, NULL );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoUnregisterVocab(NAMES) = 0x%x "
                    "in AppUninitializeMainContext().\n", esrrc );
         return( APPRC_ERROR );
      }

      /* Update the active WCIS text. */
      UpdateWCISEntry( VOCABID_NAMES, 0 );

   }

   return( APPRC_OK );
}

/******************************************************************************
* void PlayReadyPrompt( void )
******************************************************************************/
void PlayReadyPrompt( void )
{
   int irc;
   unsigned long ulBlockSize;
   unsigned char *pPcm;

   /* Make the audio device ready for playback. */
   irc = AudioOpenPlay();
   if( irc != AUDIO_OK )
   {
      ErrPrintf( "AudioOpenPlay() = 0x%x in PlayReadyPrompt()\n", irc );
      return;
   }

   do /* loop for easy breakout on error */
   {
		/* Get the PCM for the dtmf tone from the dtmf collection. */
		irc = eduGetBlockByID( gs_DtmfColl, READY_PCM_ID, &ulBlockSize, &pPcm );
		if (irc != EDU_RC_OK)
		{
			ErrPrintf( "eduGetBlockByID(%d) = 0x%x in PlayReadyPrompt().\n",
							READY_PCM_ID, irc );
			break;
		}

		/* Play the tone. */
		irc = AudioPlay( pPcm, ulBlockSize );
		if( irc != AUDIO_OK )
		{
			ErrPrintf( "AudioPlay() = 0x%x in PlayReadyPrompt().\n", irc );
			break;
		}
   }
   while (0);

   /* Clean up the playback device. */
   irc = AudioClosePlay();
   if( irc != AUDIO_OK )
   {
      ErrPrintf( "AudioClosePlay() = 0x%x in PlayReadyPrompt().\n", irc );
      return;
   }
}

/******************************************************************************
*******************************************************************************
*                 M A I N
*******************************************************************************
******************************************************************************/

/******************************************************************************
* void ProcessRecoErrorEvent( void )
******************************************************************************/
void ProcessRecoErrorEvent( AppEvent *pEvent )
{
   int irc;

   switch( pEvent->Event.Error.lErr )
   {
      case ESR_RC_UNABLE_TO_FIND_MATCH:
         irc = TTSPrompt( 1, gs_hEci, "Command Not Recognized.  "
                                      "Try Again.\n\n" );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt(ESR_RC_NO_MATCH) = %d in "
                        "ProcessRecoErrorEvent().\n", irc );
            return;
         }
         break;

      default:
         irc = TTSPrompt( 1, gs_hEci, "Reco Error 0x%x occurred.\n\n",
                           pEvent->Event.Error.lErr );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt(default) = %d in ProcessRecoErrorEvent().\n",
                           irc );
            return;
         }
         break;
   }
}

/******************************************************************************
* void ProcessRecoRejectionEvent( void )
******************************************************************************/
void ProcessRecoRejectionEvent( AppEvent *pEvent )
{
   int irc;

   if ( pEvent->szRecoText )
   {
	   if ( strlen(pEvent->szRecoText) > 0 )
	   {
			irc = TTSPrompt( 1, gs_hEci, "Command Rejected - %s.  "
                                         "Try Again.\n\n", pEvent->szRecoText );
	   }
	   else
			irc = TTSPrompt( 1, gs_hEci, "Command Rejected.  Try Again.\n\n" );
   }
   else
		irc = TTSPrompt( 1, gs_hEci, "Command Rejected.  Try Again.\n\n" );

   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in "
                  "ProcessRecoRejectionEvent().\n", irc );
      return;
   }

}

/******************************************************************************
* void ProcessDialEvent( AppEvent *pEvent )
******************************************************************************/
void ProcessDialEvent( AppEvent *pEvent )
{
   int i, irc;
   unsigned long ulBlockSize;
   unsigned char *pPcm;
   char szTemp[10];

   /* Use TTS to echo the number to be dialed. */
   UIPrintf( "Dialing " );
   if( eciAddText( gs_hEci, "Dialing" ) == 0 )
   {
      ErrPrintf( "eciAddText() = 0 in ProcessDialEvent().\n" );
      return;
   }

   for( i = 0; i < pEvent->Event.Dial.iNumDigits; i++ )
   {
      /* Handle the conversion of the pound (#) and star (*) keys for TTS. */
      switch( pEvent->Event.Dial.aiDigits[i] )
      {
         case 10:
            sprintf( szTemp, "Pound" );
            break;
         case 11:
            sprintf( szTemp, "Star" );
            break;
         default:
            sprintf( szTemp, "%d", pEvent->Event.Dial.aiDigits[i] );
            break;
      }
      UIPrintf( "%s ", szTemp );
      if( eciAddText( gs_hEci, szTemp ) == 0 )
      {
         ErrPrintf( "eciAddText() = 0 in ProcessDialEvent().\n" );
         return;
      }
   }

   UIPrintf( "\n\n" );

   /* Instruct TTS to speak the data just queued. */
   if( eciSynthesize( gs_hEci ) == 0 )
   {
      ErrPrintf( "eciSynthesize() = 0 in ProcessDialEvent().\n" );
      return;
   }

   /* Wait for the TTS to complete the text already added. */
   eciSynchronize( gs_hEci );

   /* Make ready the audio device for playback. */
   irc = AudioOpenPlay();
   if( irc != AUDIO_OK )
   {
      ErrPrintf( "AudioOpenPlay() = 0x%x in ProcessDialEvent().\n", irc );
      return;
   }

   /* For each digit, play the appropriate dtmf tone. */
   for( i = 0; i < pEvent->Event.Dial.iNumDigits; i++ )
   {
      /* Get the PCM for the dtmf tone from the dtmf collection. */
      irc = eduGetBlockByID( gs_DtmfColl, 100 + pEvent->Event.Dial.aiDigits[i], &ulBlockSize, &pPcm );
      if (irc != EDU_RC_OK)
      {
         ErrPrintf( "eduGetBlockByID(%d) = 0x%x in ProcessDialEvent().\n",
                        100 + pEvent->Event.Dial.aiDigits[i], irc );
         break;
      }

      /* Play the tone. */
      irc = AudioPlay( pPcm, ulBlockSize );
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioPlay() = 0x%x in ProcessDialEvent().\n", irc );
         break;
      }
   }

   /* Clean up the playback device. */
   irc = AudioClosePlay();
   if( irc != AUDIO_OK )
      ErrPrintf( "AudioClosePlay() = 0x%x in ProcessDialEvent().\n", irc );

   return;
}

/******************************************************************************
* int AppAcbfInit( void )
******************************************************************************/
int AppAcbfInit( void )
{
   ESRRC esrrc;
   int irc;

   /* Allocate the semaphores used to flag the events. */
   irc = osCreateEventSem( &gs_AcbfStateChangeData.hSemEvent );
   if( irc != OS_OK )
   {
      ErrPrintf( "osCreateEventSem() = %d in AppAcbfInit().\n", irc );
      return( APPRC_ERROR );
   }

   /* Initialize the acoustic baseform generation service. */
   esrrc = esrAcbfInitialize( gs_hEsr );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfInitialize()  = 0x%x in AppAcbfInit().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the callback that will process errors. */
   esrrc = esrAcbfRegisterErrorCB( gs_hEsr, AppEsrAcbfError,
                                    (void *)(&gs_AcbfResultData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfRegisterErrorCB()  = 0x%x in AppAcbfInit().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Register the state change callback for acbf. */
   esrrc = esrAcbfRegisterStateChangeCB( gs_hEsr, AppEsrAcbfStateChange,
                                          (void *)(&gs_AcbfStateChangeData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfRegisterStateChangeCB()  = 0x%x in AppAcbfInit().\n",
                     esrrc );
      return( APPRC_ERROR );
   }

   /* Register the result callback.  This is the callback that will
      receive the new baseform data.
   */
   esrrc = esrAcbfRegisterResultCB( gs_hEsr, AppEsrAcbfResult,
                                       (void *)(&gs_AcbfResultData) );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfRegisterResultCB()  = 0x%x in AppAcbfInit().\n",
                     esrrc );
      return( APPRC_ERROR );
   }

   /* Register audio stats callback for the AOP */
   esrrc = esrAcbfRegisterFrameStatsCB( gs_hEsr, AppEsrFrameStatsCB, 
		NULL );
   if (esrrc != ESR_RC_OK) 
   {
      ErrPrintf( "esrRecoRegisterFrameStatsCB() = 0x%x in "
		 "AppRecoInitialize().\n", esrrc);
      return( APPRC_ERROR );
   }

   return( APPRC_OK );

}

/******************************************************************************
* int ExecuteAcbfGeneration( unsigned char *pAudioBuffer,
                             unsigned long *pulAudioBufferSize )
******************************************************************************/
int ExecuteAcbfGeneration( unsigned char *pAudioBuffer,
                           unsigned long *pulAudioBufferSize )
{
   ESRRC esrrc;
   AOPRC aoprc;
   int irc;

   /* Reset the current result data so that old data cannot be processed. */
   memset( &gs_AcbfResultData, 0, sizeof(gs_AcbfResultData) );

   /* Set up the ESR audio callback data so that it will record the
      new name to a buffer.
   */
   gs_AudioCBData.pAudioBuf = pAudioBuffer;
   gs_AudioCBData.AudioBufSize = *pulAudioBufferSize;


   /* Mark start of an utterance; delay AOP gain changes */
   aoprc = aopStartUtterance(g_hAOP);
   if( aoprc != AOP_RC_OK) 
   {
      ErrPrintf( "aopStartUtterance() = 0x%x "
		 "in ExecuteAcbfGeneration().\n", aoprc );
      return( APPRC_ERROR );
   }


   /* Start the engine processing data.  This will call the audio
      callback to signal when to start enqueueing PCM to the engine.
   */
   do  /* loop for easy break out on error */
   { 
		esrrc = esrAcbfStartListening( gs_hEsr, ESR_AUDIO_CB_ACTIVE );
		if( esrrc != ESR_RC_OK )
		{
			ErrPrintf( "esrAcbfStartListening() = 0x%x "
						"in ExecuteAcbfGeneration().\n", esrrc );
			break;
		}

		/* Because acoustic baseforms only function in Push-To-Talk mode,
			the user must signal when they are finished speaking.
		*/
		UIPrintf( "Press 'Enter' when finished speaking.\n" );
		getchar();

		/* Set up the state change semaphore to signal when the state of the
			engine returns to IDLE.  This signifies that the engine is done
			processing the data.
		*/
		osResetEventSem( gs_AcbfStateChangeData.hSemEvent );
		gs_AcbfStateChangeData.SignalState = ESR_RECO_STATE_IDLE;

		/* Inform the engine to stop processing data.  This will call
			the audio callback to signal when to stop enqueueing PCM to
			the engine.
		*/
		esrrc = esrAcbfStopListening( gs_hEsr );
		if( esrrc != ESR_RC_OK )
		{
			ErrPrintf( "esrAcbfStopListening() = 0x%x "
						"in ExecuteAcbfGeneration().\n", esrrc );
			break;
		}

		/* Wait for the signal signifying that the engine has returned
			to the idle state.
		*/
		irc = osWaitOnEventSem( gs_AcbfStateChangeData.hSemEvent, 5000 );
		if( irc != OS_OK )
		{
			ErrPrintf( "osWaitOnEventSem() = %ld "
						"in ExecuteAcbfGeneration().\n", esrrc );
			break;
		}

		/* Save the actual size of the data recorded. */
		*pulAudioBufferSize = gs_AudioCBData.AudioBufSize;

		/* Clear the audio buffer data callback interface so that
			the next utternence will not be recorded. */
		gs_AudioCBData.pAudioBuf = NULL;
		gs_AudioCBData.AudioBufSize = 0;
   }
   while (0);

	/* Mark end of an utterance */
    aoprc = aopStopUtterance(g_hAOP);
    if( aoprc != AOP_RC_OK) 
    {
        ErrPrintf( "aopStartUtterance() = 0x%x "
		"in ExecuteAcbfGeneration().\n", aoprc );
    }

	// Close the EAL 
    irc = AudioCloseRec();
    if( irc != AUDIO_OK )
    {
        ErrPrintf( "AudioCloseRec() = %d in ExecuteAcbfGeneration().\n", irc );
    }

   if (irc != AUDIO_OK || aoprc != AOP_RC_OK || esrrc != ESR_RC_OK)
	   return APPRC_ERROR;
   return APPRC_OK;
}

/******************************************************************************
* int AppAcbfUninit( void )
******************************************************************************/
int AppAcbfUninit( void )
{
   ESRRC esrrc;
   int irc;

   /* Unregister the callback for the acoustic baseform. */
   esrrc = esrAcbfUnregisterResultCB( gs_hEsr, AppEsrAcbfResult, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfUnregisterResultCB()  = 0x%x in AppAcbfUninit().\n",
                  esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrAcbfUnregisterStateChangeCB( gs_hEsr, AppEsrAcbfStateChange,
                                             NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfUnregisterStateChangeCB()  = 0x%x in "
                  "AppAcbfUninit().\n", esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrAcbfUnregisterErrorCB( gs_hEsr, AppEsrAcbfError, NULL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfUnregisterErrorCB()  = 0x%x in AppAcbfUninit().\n",
                     esrrc );
      return( APPRC_ERROR );
   }

   esrrc = esrAcbfUnregisterFrameStatsCB( gs_hEsr, AppEsrFrameStatsCB, NULL );
   if( esrrc != ESR_RC_OK)
   {
      ErrPrintf( "esrRecoUnregisterFrameStatsCB() = 0x%x in "
		  "AppRecoUninitialize().\n", esrrc);
      return( APPRC_ERROR );
   }

   /* Clean up the acoustic baseform service. */
   esrrc = esrAcbfUninitialize( gs_hEsr );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrAcbfUninitialize()  = 0x%x in AppAcbfUninit().\n", esrrc );
      return( APPRC_ERROR );
   }

   /* Clean up allocated OS resources. */
   irc = osDestroyEventSem( gs_AcbfStateChangeData.hSemEvent );
   if( irc != OS_OK )
   {
      ErrPrintf( "osDestroyEventSem() = %d in AppAcbfUninit().\n", irc );
      return( APPRC_ERROR );
   }

   return( APPRC_OK );
}


/******************************************************************************
* void GetAddrBookEntryInfo( AddressBookEntry *pEntry )
******************************************************************************/
void GetAddrBookEntryInfo( AddressBookEntry *pEntry )
{
   int i, iPhoneNumIndex;
   char szLine[81];

   /* Prompt the user for address book entry data. */
   UIPrintf( "Type the name (32 char max):" );
   szLine[0] = '\0';
   gets( szLine );
   strncpy( pEntry->szName, szLine, sizeof(pEntry->szName)-1 );
   pEntry->szName[sizeof(pEntry->szName)-1] = '\0';

   UIPrintf( "Type the phone number (0-9,*,#, All other character will "
               "be ignored):" );
   szLine[0] = '\0';
   gets( szLine );

   for( i = 0; i < 11; i++ )
      pEntry->iPhoneNum[i] = -1;

   iPhoneNumIndex = 0;
   for( i = 0; i < (int)strlen(szLine); i++ )
   {
      switch( szLine[i] )
      {
         case '0':
         case '1':
         case '2':
         case '3':
         case '4':
         case '5':
         case '6':
         case '7':
         case '8':
         case '9':
            pEntry->iPhoneNum[iPhoneNumIndex] = szLine[i] - '0';
            iPhoneNumIndex++;
            break;
         case '*':
            pEntry->iPhoneNum[iPhoneNumIndex] = 10;
            iPhoneNumIndex++;
            break;
         case '#':
            pEntry->iPhoneNum[iPhoneNumIndex] = 11;
            iPhoneNumIndex++;
            break;
      }
   }
}

/******************************************************************************
* void ProcessAddNameEvent( void )
******************************************************************************/
void ProcessAddNameEvent( void )
{
   ESRRC esrrc;
   int irc;
   int iAddrBookIndex;
   ESRBaseformInfo BaseformInfo;
   AddressBookEntry *pEntry;
   char *pszPrompt;

   /* Initialize the acoustic baseform service. */
   irc = AppAcbfInit();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppAcbfInit() = %d in ProcessAddNameEvent().\n", irc );
      return;
   }

   /* Use a while loop to allow error to break out and jump to the cleanup
      code without using labels and gotos.
   */
   while( 1 )
   {
      /* Get a free address book entry. */
      iAddrBookIndex = AddrBookGetFreeEntry( &pEntry );
      if( iAddrBookIndex < 0 )
      {
         irc = TTSPrompt( 1, gs_hEci, "Address book is full.\n" );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt() = %d in ProcessAddNameEvent().\n", irc );
            break;
         }
      }

      /* Prompt the user to press <ENTER> and to say a name. */
      irc = TTSPrompt( 1, gs_hEci, "Press 'Enter' and say new name.\n" );
      if( irc != APPRC_OK )
      {
         ErrPrintf( "TTSPrompt() = %d in ProcessAddNameEvent().\n", irc );
         break;
      }
      getchar();

      /* Set up the size of the audio buffer used to record the phrase. */
      pEntry->ulAudioBufferLen = sizeof( pEntry->AudioBuffer );

      /* Execute the steps required for the user to say the new phrase */
      irc = ExecuteAcbfGeneration( pEntry->AudioBuffer,
                                   &(pEntry->ulAudioBufferLen) );
      if( irc != APPRC_OK )
      {
         ErrPrintf( "ExecuteAcbfGeneration() = %d in ProcessAddNameEvent().\n", irc );
         break;
      }

      /* Check to determine whether there was an error. */
      if( gs_AcbfResultData.lErr != 0 )
      {
         if( gs_AcbfResultData.lErr == ESR_RC_BASEFORM_TOO_LONG )
            pszPrompt = "The new name is too long.  Add Name aborted. Error=0x%x.\n";
         else if( gs_AcbfResultData.lErr == ESR_RC_UNABLE_TO_FIND_MATCH )
            pszPrompt = "No speech detected.  Add Name aborted. Error=0x%x.\n";
         else
            pszPrompt = "Error 0x%x occurred adding new name.\n";

         irc = TTSPrompt( 1, gs_hEci, pszPrompt, gs_AcbfResultData.lErr );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt() = %d in ProcessAddNameEvent().\n", irc );
            break;
         }

         break;
      }

      /* Ask the user for the address book entry data. */
      GetAddrBookEntryInfo( pEntry );

      /* Prepare the structure needed to add the new baseform
         to the external list.
      */
      BaseformInfo.pszSpelling = pEntry->szName;
      BaseformInfo.strType = ESTRING_ASCII;
      BaseformInfo.pUserData = (void *)iAddrBookIndex;

      esrrc = esrVmgrAddBaseformToList( gs_hVocabSet, EXTRNLIST_NAMES,
                           &BaseformInfo, gs_AcbfResultData.pResult,
                           gs_AcbfResultData.iLen );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrVmgrAddBaseformToList() = 0x%x "
                    "in ProcessAddNameEvent().\n", esrrc );
         break;
      }

      /* Have the vocabulary manager process the newly-added baseform.
         This allows for multiple baseforms to be added without having to
                 process each one individually.
      */
      esrrc = esrVmgrCommitBaseformChanges();
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrVmgrCommitBaseformChanges() = 0x%x "
                    "in ProcessAddNameEvent().\n", esrrc );
         break;
      }

      /* Fill in the address book entry. */
      pEntry->iInUse = 1;
      pEntry->iBaseformLen = gs_AcbfResultData.iLen;

      /* Save the acoustic baseform.  */
      memcpy( pEntry->Baseform, gs_AcbfResultData.pResult,
                  gs_AcbfResultData.iLen );

      /* Free the memory used to hold the baseform that was allocated
         from the callback.
      */
      free( gs_AcbfResultData.pResult );

      /* We are done, so break from the loop. */
      break;
   }

   /* Clean up the acoustic baseform service. */
   irc = AppAcbfUninit();
   if( irc != APPRC_OK )
      ErrPrintf( "AppAcbfUninit() = %d in ProcessAddNameEvent().\n", irc );

   /* The main context is still active, so have it update the active vocabularies.
      This will enable the names vocabularies if an acoustic baseform exists.
   */
   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in "
                  "ProcessAddNameEvent().\n", irc );
	  gs_iExit = 1;
   }

   return;
}

/******************************************************************************
* void ProcessCallEvent( AppEvent *pEvent )
******************************************************************************/
void ProcessCallEvent( AppEvent *pEvent )
{
   AddressBookEntry *pEntry;
   int i, irc;
   unsigned long ulBlockSize;
   unsigned char *pPcm;

   /* Get a pointer to the address book entry information. */
   pEntry = AddrBookGetEntry( pEvent->Event.Call.iAddrBookIndex );
   if( pEntry == NULL )
   {
      /* Invalid index. */
      ErrPrintf( "AddrBookGetEntry() failed in ProcessCallEvent().\n" );
      return;
   }

   /* Prompt the user about the event. */
   irc = TTSPrompt( 1, gs_hEci, "Calling " );
   if( irc != APPRC_OK )
      ErrPrintf( "TTSPrompt() = %d in ProcessCallEvent().\n", irc );

   UIPrintf( "%s\n", pEntry->szName );

   /* Now play the recorded name, if a recording exists. */
   if( pEntry->ulAudioBufferLen > 0 )
   {
      /* Make the audio device ready for playback. */
      irc = AudioOpenPlay();
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioOpenPlay() = 0x%x in ProcessCallEvent().\n", irc );
         return;
      }

      /* Play the name. */
      irc = AudioPlay( pEntry->AudioBuffer, pEntry->ulAudioBufferLen );
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioPlay() = 0x%x in ProcessCallEvent().\n", irc );
         return;
      }

      /* Clean up the playback device. */
      irc = AudioClosePlay();
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioClosePlay() = 0x%x in ProcessCallEvent().\n", irc );
         return;
      }
   }
   else
   {
      /* Because there is no recording, use TTS to say the name */
      irc = TTSPrompt( 1, gs_hEci, "%s\n", pEntry->szName );
      if( irc != APPRC_OK )
      {
         ErrPrintf( "TTSPrompt() = %d in ProcessCallEvent().\n", irc );
      }
   }

   /* Make the audio device ready for playback. */
   irc = AudioOpenPlay();
   if( irc != AUDIO_OK )
   {
      ErrPrintf( "AudioOpenPlay() = 0x%x in ProcessCallEvent().\n", irc );
      return;
   }

   /* Play the digits of the phone number. */
   for( i = 0; (i < 11) && (pEntry->iPhoneNum[i] != -1); i++ )
   {

      UIPrintf( "%d", pEntry->iPhoneNum[i] );

      /* Get the PCM data for the dtmf tone from the dtmf collection. */
      irc = eduGetBlockByID( gs_DtmfColl, 100 + pEntry->iPhoneNum[i],
                              &ulBlockSize, &pPcm );
      if (irc != EDU_RC_OK)
      {
         ErrPrintf( "eduGetBlockByID(%d) = 0x%x in ProcessCallEvent()\n",
                        100 + pEvent->Event.Dial.aiDigits[i], irc );
         return;
      }

      /* Play the tone. */
      irc = AudioPlay( pPcm, ulBlockSize );
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioPlay() = 0x%x in ProcessCallEvent().\n", irc );
         return;
      }
   }
   UIPrintf( "\n\n" );

   /* Clean up the playback device. */
   irc = AudioClosePlay();
   if( irc != AUDIO_OK )
   {
      ErrPrintf( "AudioClosePlay() = 0x%x in ProcessCallEvent().\n", irc );
      return;
   }

}

/******************************************************************************
* void ProcessDeleteNameEvent( AppEvent *pEvent )
******************************************************************************/
void ProcessDeleteNameEvent( AppEvent *pEvent )
{
   AddressBookEntry *pEntry;
   int irc, iDone;
   ESRRC esrrc;
   AppEvent AppEventYesNo;


	/* Get a pointer to the address book entry information. */
	pEntry = AddrBookGetEntry( pEvent->Event.DeleteName.iAddrBookIndex );
	if( pEntry == NULL )
	{
		/* Invalid index */
		ErrPrintf( "AddrBookGetEntry() failed in ProcessDeleteNameEvent().\n" );
		return;
	}

	/* Prompt the user about the event. */
	irc = TTSPrompt( 1, gs_hEci, "Confirm Deletion of " );
	if( irc != APPRC_OK )
		ErrPrintf( "TTSPrompt() = %d in ProcessDeleteNameEvent().\n", irc );

	UIPrintf( "%s\n", pEntry->szName );

	/* Now play the recorded name. */

	/* Make the audio device ready for playback. */
	irc = AudioOpenPlay();
	if( irc != AUDIO_OK )
	{
		ErrPrintf( "AudioOpenPlay() = 0x%x in ProcessDeleteNameEvent().\n", irc );
		return;
	}

	/* Play the name. */
	irc = AudioPlay( pEntry->AudioBuffer, pEntry->ulAudioBufferLen );
	if( irc != AUDIO_OK )
	{
		ErrPrintf( "AudioPlay() = 0x%x in ProcessDeleteNameEvent().\n", irc );
		return;
	}

	/* Clean up the playback device. */
	irc = AudioClosePlay();
	if( irc != AUDIO_OK )
	{
		ErrPrintf( "AudioClosePlay() = 0x%x in ProcessDeleteNameEvent().\n", irc );
		return;
	}

	/* Give yes/no prompting information. */
	irc = TTSPrompt( 1, gs_hEci, "Say Yes or No.\n" );
	if( irc != APPRC_OK )
		ErrPrintf( "TTSPrompt() = %d in ProcessDeleteNameEvent().\n", irc );

	/* The yes/no confirmation must be recognized.
		Disable the current vocabularies (main context) and then enable the
			yes/no context.
	*/
	irc = AppUninitializeMainContext();
	if( irc != APPRC_OK )
	{
		ErrPrintf( "AppUninitializeMainContext() = %d in "
					"ProcessDeleteNameEvent().\n", irc );
		return;
	}

	do /* loop for easy error breakout; need to initialize main context on error */
	{	
		/* Enable the main vocabulary so that its commands are active. */
		esrrc = esrRecoEnableVocab( gs_hEsr, gs_hVocabSet, VOCABID_YESNO );
		if( esrrc != ESR_RC_OK )
		{
			ErrPrintf( "esrRecoEnableVocab() = 0x%x "
						"in ProcessDeleteNameEvent().\n", esrrc );
			break;
		}

		/* Update the active WCIS text. */
		UpdateWCISEntry( VOCABID_YESNO, 1 );

		/* Now recognize speech from the yes/no context, which is not enabled. */
		AppEventYesNo.Type = APPEVENT_INVALID;
		AppEventYesNo.szRecoText[0] = '\0';

		iDone = ExecuteRecoSequence( &AppEventYesNo );

		/* Check the confirmation. */
		if( AppEventYesNo.Type == APPEVENT_NO )
		{
			/* The user wanted to abort the operation. */
			irc = TTSPrompt( 1, gs_hEci, "Delete operation canceled.\n\n" );
			if( irc != APPRC_OK )
				ErrPrintf( "TTSPrompt() = %d in ProcessDeleteNameEvent().\n", irc );
		}
		else
		{
			/* Remove the acoustic baseform from the external list. */
			esrrc = esrVmgrRemoveBaseformFromList( gs_hVocabSet, EXTRNLIST_NAMES,
							pEntry->Baseform, pEntry->iBaseformLen );
			if( esrrc != ESR_RC_OK )
			{
				ErrPrintf( "esrVmgrRemoveBaseformFromList() = 0x%x in "
							"ProcessDeleteNameEvent().\n", esrrc );
				break;
			}

			/* Have the vocabulary manager process this change. */
			esrrc = esrVmgrCommitBaseformChanges();
			if( esrrc != ESR_RC_OK )
			{
				ErrPrintf( "esrVmgrCommitBaseformChanges() = 0x%x "
							"in ProcessDeleteNameEvent().\n", esrrc );
				break;
			}

			/* Clean up the address book entry. */
			AddrBookCleanupEntry( pEvent->Event.DeleteName.iAddrBookIndex );

			/* Let the user know when the cleanup is done. */
			irc = TTSPrompt( 1, gs_hEci, "Entry Deleted.\n\n" );
			if( irc != APPRC_OK )
				ErrPrintf( "TTSPrompt() = %d in ProcessDeleteNameEvent().\n", irc );
		}

		/* Reset the contexts back to the main context. */
		esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_YESNO );
		if( esrrc != ESR_RC_OK )
		{
			ErrPrintf( "esrRecoDisableVocab(YESNO) = 0x%x "
						"in ProcessDeleteNameEvent().\n", esrrc );
			break;
		}

		/* Update the active WCIS text. */
		UpdateWCISEntry( VOCABID_YESNO, 0 );
   }
   while (0);

   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in "
                  "ProcessDeleteNameEvent()\n", irc );
	  gs_iExit = 1;
   }

   return;
}

/******************************************************************************
* void ProcessModifyOptionsEvent( void )
******************************************************************************/
void ProcessModifyOptionsEvent( void )
{
   int irc;
   ESRRC esrrc;

   /* Disable the main context vocabularies. */
   irc = AppUninitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppUninitializeMainContext() = %d in "
                  "ProcessModifyOptionsEvent().\n", irc );
      return;
   }

   /* Enable the Options vocabulary. */
   esrrc = esrRecoEnableVocab( gs_hEsr, gs_hVocabSet, VOCABID_OPTIONS );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoEnableVocab(OPTIONS) = 0x%x "
                 "in ProcessModifyOptionsEvent().\n", esrrc );
   }

   /* Update the active WCIS text. */
   UpdateWCISEntry( VOCABID_OPTIONS, 1 );

   /* Inform the user that the command was accepted. */
   irc = TTSPrompt( 1, gs_hEci, "Say an options command.\n\n" );
   if( irc != APPRC_OK )
      ErrPrintf( "TTSPrompt() = %d in ProcessModifyOptionsEvent().\n", irc );

   return;
}

/******************************************************************************
* ImportNameList *ImportNameListGet( int *piCount )
******************************************************************************/
ImportNameList *ImportNameListGet( int *piCount )
{
   static ImportNameList List[] =
   {
      {  "John Smith",  {5,5,5,1,2,3,4,-1,-1,-1,-1}  },
      {  "Tom at Home", {5,5,5,4,3,2,1,-1,-1,-1,-1}  },
      {  "Office",      {5,5,5,5,5,5,5,-1,-1,-1,-1}  }
   };

   /* This function would import the actual name list from a link,
      but for the sample, a hardcoded table is used instead.
   */
   *piCount = sizeof(List)/sizeof(List[0]);

   return( List );
}

/******************************************************************************
* void ProcessImportNameList( void )
******************************************************************************/
void ProcessImportNameList( void )
{
   ESRRC esrrc;
   int i, irc, iNumNames, iAddrBookIndex;
   unsigned long ulBlockSize;
   void *pLblDataBlock;
   ImportNameList *pNameList;
   ESRBaseformInfo BaseformInfo;
   AddressBookEntry *pEntry;
   ESRBaseformResult *pESRBaseform;
   int iESRBaseformSize;


   /* This command imports a name list for the address book and uses TTS
      to generate the pronounciations.
   */

   /* Get the list of names to import. */
   pNameList = ImportNameListGet( &iNumNames);

   /* Get a pointer to the engine initialization data block required to generate
      the acoustic baseform.
   */
   irc = eduGetBlockByID( gs_EngineDataColl,
                          ENGDATA_ID_BASE + LANGINDEX_EN_US + 2,
                          &ulBlockSize, &pLblDataBlock );
   if (irc != EDU_RC_OK)
   {
      ErrPrintf( "eduGetBlockByID(%d) = 0x%x in ProcessImportNameList().\n",
                 ENGDATA_ID_BASE + LANGINDEX_EN_US + 2, irc );
      return;
   }

   /* Inform the user that the list is being imported.
      Note that TTS processing does not have to be complete before beginning
      the import process.
   */
   irc = TTSPrompt( 0, gs_hEci, "Importing name list.\n\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessImportNameList().\n", irc );
   }


   /* For each of the names, generate a pronounciation and then add it
      to the names external list and address book.
   */
   for( i = 0; i < iNumNames; i++ )
   {
      /* Use the unlimited vocabulary support to generate the acoustic baseform
         using the TTS engine.
      */
	   
	  irc = vocBsfmGenerate( gs_hVoc, EVV_EN_US, pNameList[i].szName, ESTRING_ASCII, &pESRBaseform, &iESRBaseformSize );
      if( irc != VOC_RC_OK )
      {
         ErrPrintf( "vocBsfmGenerate() = 0x%x in ProcessImportNameList().\n", irc );
         break;
      }

      /* Get an address book entry. */
      iAddrBookIndex = AddrBookGetFreeEntry( &pEntry );
      if( iAddrBookIndex < 0 )
      {
         ErrPrintf( "AddrBookGetFreeEntry() = %ld in "
                     "ProcessImportNameList().\n", irc );
         break;
      }

      /* Prepare the structure needed to add the new acoustic baseform
         to the external list.
      */
      BaseformInfo.pszSpelling = pNameList[i].szName;
      BaseformInfo.strType = ESTRING_ASCII;
      BaseformInfo.pUserData = (void *)iAddrBookIndex;

      esrrc = esrVmgrAddBaseformToList( gs_hVocabSet, EXTRNLIST_NAMES,
                     &BaseformInfo, pESRBaseform,
                     iESRBaseformSize );
      if( esrrc == ESR_RC_DUPLICATE_BASEFORM )
      {
         /* Inform the user that the name already exists and go to the next name. */
         irc = TTSPrompt( 0, gs_hEci, "%s is already in address book\n\n",
                        pNameList[i].szName );
         if( irc != APPRC_OK )
         {
            ErrPrintf( "TTSPrompt() = %d in ProcessImportNameList().\n", irc );
         }
         continue;
      } else if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrVmgrAddBaseformToList() = 0x%x "
                    "in ProcessAddNameEvent().\n", esrrc );
         break;
      }

      /* Fill in the address book entry. */
      pEntry->iInUse = 1;
      memcpy( &(pEntry->iPhoneNum), &(pNameList[i].iPhoneNum),
                  sizeof(pNameList[i].iPhoneNum) );
      strcpy( pEntry->szName, pNameList[i].szName );
      pEntry->iBaseformLen = iESRBaseformSize;
      memcpy( pEntry->Baseform, pESRBaseform,
                  iESRBaseformSize );

   /* Because we did not implement the VOC callback for memory allocation,
      the array for the acoustic baseform was allocated using malloc within the
      function.  So free it now.
   */
      free( pESRBaseform );
      
   }

   /* Have the vocabulary manager process the newly-added acoustic baseform.
      This allows for multiple baseforms to be added without having to process
          each one individually.
   */
   esrrc = esrVmgrCommitBaseformChanges();
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrVmgrCommitBaseformChanges() = 0x%x "
                 "in ProcessImportNameList().\n", esrrc );
   }

   /* Disable the Options vocabulary. */
   esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_OPTIONS );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoDisableVocab(OPTIONS) = 0x%x "
                 "in ProcessImportNameList().\n", esrrc );
   }

   /* Update the active WCIS text. */
   UpdateWCISEntry( VOCABID_OPTIONS, 0 );

   /* Make the main context vocabularies active again. */
   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in "
                  "ProcessImportNameList().\n", irc );
	  gs_iExit = 1;
      return;
   }

   /* Inform the user that the import is complete. */
   irc = TTSPrompt( 1, gs_hEci, "Import complete.\n\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessImportNameList().\n", irc );
   }
   return;
}

/******************************************************************************
* int GetEmailList( char ***pppszSubjects, char ***pppszAuthor,
                    char ***pppszMsgs )
******************************************************************************/
int GetEmailList( char ***pppszSubjects, char ***pppszAuthors,
                  char ***pppszMsgs )
{
   static char *pszSubjects[] =
   {
      "New Customer Contacts",
      "Bring Home Milk",
      "IRS Audit Pending"
   };
   static char *pszAuthors[] =
   {
      "Bill",
      "Jenny",
      "The IRS"
   };
   static char *pszMsgs[] =
   {
      "New contacts for my company are Richard Smith and Kim Jones.  "
         "Good luck, Bill ",
      "Don't forget to bring home the milk and cookies for the kids.  "
         "Love ya. ",
      "An IRS representative will be at your home February 26, 2000 to "
         "review your tax return.  Thank you. "
   };

   /* Return a simulated list of email subjects and authors. */
   *pppszSubjects = pszSubjects;
   *pppszAuthors = pszAuthors;
   *pppszMsgs = pszMsgs;

   return( sizeof(pszSubjects)/sizeof(pszSubjects[0]) );
}

/******************************************************************************
* void ProcessCheckEmail( void )
******************************************************************************/
void ProcessCheckEmail( void )
{
   /* This command disables the options context, then retrieves and dynamically
      builds a vocabulary set to simulate a grammar that must be built at
      run time.  Then this new vocabulary will be registered and enabled.
      Also, a second grammar will be enabled that contains the 'exit' command,
      which returns the phone to the main context.
   */
   ESRRC esrrc;
   int i, irc, iItemCount;
   char **ppszSubjects, **ppszAuthors, **ppszMsgs;
   static char szBNF[1024];
   char *apszBNFs[1] = { szBNF };
   VOCInitializationData VocInit;
   VOCAttrs InitAttrs;
   ESRInitializationData EsrInitData;
   ESRVocabSet *pVocabSet;
   int iVocabSetSize;
   unsigned char *pszInvalidWords;
   VOCPool *pPool[3];

   /* Retrieve the run-time vocabulary. */
   iItemCount = GetEmailList( &ppszSubjects, &ppszAuthors, &ppszMsgs );

   /* Inform the user from whom the emails originate.  Note that the TTS need not be
      complete in order for the vocabulary generation to continue while the
          system is speaking.
   */
   irc = TTSPrompt( 0, gs_hEci, "There is new mail from:\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessCheckEmail().\n", irc );
   }

   for( i = 0; i < iItemCount; i++ )
   {
      irc = TTSPrompt( 0, gs_hEci, "%s.\n", ppszAuthors[i] );
      if( irc != APPRC_OK )
      {
         ErrPrintf( "TTSPrompt() = %d in ProcessCheckEmail().\n", irc );
      }
   }
   UIPrintf( "\n" );

   /* Create a grammar from the email list.
      Annotate the authors with their index in the authors array
      so that they can be accessed later by the index.
   */
   sprintf( szBNF, "<%d_root> = Read:%d Mail From <authors>\n"
                   "       | Tell me about:%d the? Mail From <authors>\n"
                   "       | What Can I Say:%d\n"
                   "       | Exit:%d Email?.\n"
                   "<authors> = ",
                   DYNVOCABID_EMAIL, APPEVENT_READMAIL, APPEVENT_SUMEMAIL,
                   APPEVENT_WCIS, APPEVENT_EXITEMAIL );

   for( i = 0; i < iItemCount; i++ )
   {
      if( i > 0 )
         strcat( szBNF, "| " );
      sprintf( &(szBNF[strlen(szBNF)]), "%s:%d\n", ppszAuthors[i], i );
   }
   strcat( szBNF, "." );

   VocInit.ulValidBlocks = VOC_BLOCK_TR;
   /* Get a pointer to the proper recognition engine initialization data from the
      collection. */
   irc = GetRecoInitBlock( LANGINDEX_EN_US, &EsrInitData );
   if( irc != APPRC_OK )
   {
       ErrPrintf( "GetRecoInitBlock() failed in ProcessCheckEmail().\n" );
       return;
   }
   
   VocInit.pTrBlock = EsrInitData.pTrBlock;
   
   InitAttrs.ulValidAttrs = VOC_ATTRS_VSETPARMS;
   InitAttrs.VsetParms = VOC_VSET_STANDARD;
   
   /* Load the pool array. */
   pPool[0] = GetPoolBlock( BLAZER_BLOCK_ID );
   if( pPool[0] == NULL )
   {
       ErrPrintf( "GetPoolBlock() = NULL in ProcessCheckEmail().\n" );
       return;
   }
   pPool[1] = GetPoolBlock( STARTUS_BLOCK_ID );
   if( pPool[1] == NULL )
   {
       ErrPrintf( "GetPoolBlock() = NULL in ProcessCheckEmail().\n" );
       return;
   }
   pPool[2] = GetPoolBlock( W95NAV_BLOCK_ID );
   if( pPool[2] == NULL )
   {
       ErrPrintf( "GetPoolBlock() = NULL in ProcessCheckEmail().\n" );
       return;
   }
   
   irc = vocVsetInitialize( gs_hVoc, ESTRING_ASCII, &VocInit );
   if( irc != VOC_RC_OK )
   {
       ErrPrintf( "vocVsetInitialize() = 0x%x in ProcessCheckEmail().\n", irc );
       return;
   }
   
   irc = vocSetAttrs( gs_hVoc, &InitAttrs );
   if( irc != VOC_RC_OK )
   {
       ErrPrintf( "vocSetAttrs() = 0x%x in ProcessCheckEmail().\n", irc );
       return;
   }
   
   irc = vocVsetAddSource( gs_hVoc, 
       VOC_GRAMMAR_BNF,       
       VOC_VSTYPE_DETAILEDMATCH,
       ESTRING_ASCII, 
       NULL,
       szBNF,
       strlen(szBNF) );
   if( irc != VOC_RC_OK )
   {
       ErrPrintf( "vocVsetAddSource() = 0x%x in ProcessCheckEmail().\n", irc );
       return;
   }
   
   irc = vocVsetGenerate(  gs_hVoc,
       pPool, 
       NUM_POOLS,
       &pVocabSet, 
       &iVocabSetSize, 
       &pszInvalidWords );
   if( irc != VOC_RC_OK )
   {
       ErrPrintf( "vocVsetGenerate() = 0x%x in ProcessCheckEmail().\n", irc );
       return;
   }
   
   irc  = vocVsetUninitialize( gs_hVoc );
   if( irc != VOC_RC_OK )
   {
       ErrPrintf( "vocVsetUninitialize() = 0x%x in ProcessCheckEmail().\n", irc );
       return;
   }
   
   /* Register the newly-created vocabulary set. */
   esrrc = esrVmgrRegisterVocabSet( pVocabSet, &gs_hEmailVocabSet );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrVmgrRegisterVocabSet() = 0x%x "
                 "in ProcessCheckEmail().\n", esrrc );
      return;
   }

   /* Now register the vocabulary from the new vocabulary set. */
   esrrc = esrRecoRegisterVocab( gs_hEsr, gs_hEmailVocabSet, DYNVOCABID_EMAIL,
                                 AppRecoResultEmail, &gs_RecoResultData );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoRegisterVocab() = 0x%x "
                 "in ProcessCheckEmail().\n", esrrc );
      return;
   }

   /* Disable the main context vocabularies. */
   irc = AppUninitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppUninitializeMainContext() = %d in ProcessCheckEmail().\n",
                  irc );
      return;
   }

   /* Enable the newly-created email vocabulary. */
   /* Enable the main vocabulary so that its commands are active. */
   esrrc = esrRecoEnableVocab( gs_hEsr, gs_hEmailVocabSet, DYNVOCABID_EMAIL );
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoEnableVocab() = 0x%x "
                 "in ProcessCheckEmail().\n", esrrc );
      return;
   }

   /* Update the active WCIS text. */
   UpdateWCISEntry( DYNVOCABID_EMAIL, 1 );

   /* Wait for the TTS to complete what it is saying before proceeding so
      that the output device can be used to play the ready prompt.
   */
   eciSynchronize( gs_hEci );
   return;
}

/******************************************************************************
* void ProcessReadEmail( AppEvent *pEvent )
******************************************************************************/
void ProcessReadEmail( AppEvent *pEvent )
{
   int irc;
   char **ppszSubjects, **ppszAuthors, **ppszMsgs;

   /* Get the arrays containing the information about the emails. */
   GetEmailList( &ppszSubjects, &ppszAuthors, &ppszMsgs );

   /* Read the appropriate subject. */
   irc = TTSPrompt( 1, gs_hEci, ppszMsgs[pEvent->Event.EmailNote.iNoteIndex] );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessReadEmail().\n", irc );
   }
   UIPrintf( "\n\n" );
   return;
}

/******************************************************************************
* void ProcessSummarizeEmail( AppEvent *pEvent )
******************************************************************************/
void ProcessSummarizeEmail( AppEvent *pEvent )
{
   int irc;
   char **ppszSubjects, **ppszAuthors, **ppszMsgs;

   /* Get the arrays containing the information about the emails. */
   GetEmailList( &ppszSubjects, &ppszAuthors, &ppszMsgs );

   /* Read the appropriate subject. */
   irc = TTSPrompt( 1, gs_hEci,
                     ppszSubjects[pEvent->Event.EmailNote.iNoteIndex] );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessSummarizeEmail().\n", irc );
   }
   UIPrintf( "\n\n" );

   return;
}

/******************************************************************************
* void ProcessExitEmail( void )
******************************************************************************/
void ProcessExitEmail( void )
{
   int irc;
   ESRRC esrrc;
   ESRVocabSet *pVocabSet;

   do	/* loop for easy error breakout; need to initialize main context on error */
   {
	/* Disable the email vocabulary. */
	esrrc = esrRecoDisableVocab( gs_hEsr, gs_hEmailVocabSet, DYNVOCABID_EMAIL );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoDisableVocab() = 0x%x "
					"in ProcessExitEmail().\n", esrrc );
		break;
	}

	/* Update the active WCIS text. */
	UpdateWCISEntry( DYNVOCABID_EMAIL, 0 );

	/* Unregister the vocabulary. */
	esrrc = esrRecoUnregisterVocab( gs_hEsr, gs_hEmailVocabSet,
				DYNVOCABID_EMAIL, NULL );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoUnregisterVocab(MAIN) = 0x%x "
					"in ProcessExitEmail()\n", esrrc );
		break;
	}

	/* Unregister the vocabulary set.  Note that this function returns a
		pointer to the vocabulary set to be unregistered and in this case,
		because we allocated the memory for the vocabulary set
		(created with ProcessCheckEmail()), we will use this pointer to free the memory.
	*/
	esrrc = esrVmgrUnregisterVocabSet( gs_hEmailVocabSet, &pVocabSet );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrVmgrUnregisterVocabSet() = 0x%x "
					"in ProcessExitEmail().\n", esrrc );
		break;
	}

	free( pVocabSet );
   }
   while (0);

   /* Enable the vocabularies for the main context. */
   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in ProcessExitEmail().\n",
                 irc );
	  gs_iExit = 1;
      return;
   }

   /* Inform the user of being back in the main vocabulary. */
   irc = TTSPrompt( 1, gs_hEci, "Email processing complete.\n\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessExitEmail().\n", irc );
   }
   return;
}

/******************************************************************************
* void ProcessGoodbye( void )
******************************************************************************/
void ProcessGoodbye( void )
{
   int irc;

   /* Set up the exiting flag to stop the process user input loop. */
   gs_iExit = 1;

   irc = TTSPrompt( 1, gs_hEci, "Goodbye.\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessGoodbye().\n", irc );
   }
   return;
}


/******************************************************************************
* void Navigate( char *recoText )
******************************************************************************/

void Navigate( char *recoText )
{
	int irc;

	/* Give the user some directions */
	irc = TTSPrompt( 1, gs_hEci, "Directions to %s.\n", recoText );

	switch(tolower(recoText[0]))
	{
	case 'a': case 'b': case 'c': case 'd': case 'e':
		irc = TTSPrompt( 1, gs_hEci, "Go straight one mile. Then turn right.\n\n" );
		break;
	case 'f': case 'g': case 'h': case 'i': case 'j':
		irc = TTSPrompt( 1, gs_hEci, "Turn left on Main Street. Proceed three miles to your destination.\n\n" );
		break;
	case 'k': case 'l': case 'm': case 'n': case 'o':
		irc = TTSPrompt( 1, gs_hEci, "Proceed 2.5 miles to West Road. Make a right. Your destination will be a quarter mile on your left.\n\n" );
		break;
	case 'p': case 'q': case 'r': case 's': case 't':
		irc = TTSPrompt( 1, gs_hEci, "Turn right at the next intersection. Proceed two miles to your destination.\n\n" );
		break;
	default:
		irc = TTSPrompt( 1, gs_hEci, "Your destination is straight ahead in 2.3 miles.\n\n" );
		break;
	}
	
	if( irc != APPRC_OK )
	{
		ErrPrintf( "TTSPrompt() = %d in Navigate().\n", irc );
	}
	return;
}

/******************************************************************************
* void ProcessDirections( void )
******************************************************************************/
void ProcessDirections( void )
{
   int irc;
   int iDone = 0;
   ESRRC esrrc;
   AppEvent AppEventDirections;

   /* Disable the main context vocabularies. */
   irc = AppUninitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppUninitializeMainContext() = %d in "
                  "ProcessDirections().\n", irc );
	  gs_iExit = 1;
      return;
   }

   do /* loop for easy error breakout; need to initialize main context on error */
   {
	/* Enable the Options vocabulary. */
	esrrc = esrRecoEnableVocab( gs_hEsr, gs_hVocabSet, VOCABID_DIRECTIONS );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoEnableVocab(DIRECTIONS) = 0x%x "
					"in ProcessDirections().\n", esrrc );
		break;
	}

	/* Update the active WCIS text. */
	UpdateWCISEntry( VOCABID_DIRECTIONS, 1 );

	/* Set up the exiting flag to stop the process user input loop. */
	while (!iDone)
	{
		irc = TTSPrompt( 1, gs_hEci, "Say the destination you wish to find.\n" );
		if( irc != APPRC_OK )
		{
			ErrPrintf( "TTSPrompt() = %d in ProcessDirections().\n", irc );
			break;
		}

		UIPrintf("%s\n", "Ohio. New Brunswick. Yellowstone. Forest Hill Boulevard. Pleasantville. Saint Petersburg. Massachusetts. Staten Island. Universal Studios. ");

		/* Now recognize speech from the yes/no context, which is not enabled. */
		AppEventDirections.Type = APPEVENT_INVALID;
		AppEventDirections.szRecoText[0] = '\0';

		iDone = ExecuteRecoSequence( &AppEventDirections );

		/* Check the confirmation. */
		if( AppEventDirections.Type != APPEVENT_RECOREJECTION )
		{
			if (AppEventDirections.Type == APPEVENT_WCIS)
			{
				irc = TTSPrompt( 1, gs_hEci, "You can say a destination name or Cancel.\n");
				if( irc != APPRC_OK )
				{
					ErrPrintf( "TTSPrompt() = %d in ProcessDirections().\n", irc );
					break;
				}
				iDone = 0;
			}
			else if (AppEventDirections.Type != APPEVENT_RECOERROR)
			{   /* Done if user cancel's or the destination is recognized */
				if (strcmp(AppEventDirections.szRecoText, "Cancel"))
				{
					Navigate(AppEventDirections.szRecoText);
				}
				else
				{
					irc = TTSPrompt( 1, gs_hEci, "Exiting destinations.\n\n" );
					if( irc != APPRC_OK )
					{
						ErrPrintf( "TTSPrompt() = %d in ProcessDirections().\n", irc );
						break;
					}
				}
				iDone = 1;
			}
			else
			{ /* reco error occurred */
				irc = (int)AppEventDirections.Event.Error.lErr;
			}			  
		}
		else
		{
			/* reco rejected */
			TTSPrompt( 1, gs_hEci, "Recognition rejected.\n" );
			UIPrintf( "Rejected word: <%s>.\n", AppEventDirections.szRecoText );
			iDone = 0;
		}
	} /* end while - checking for valid destination */

	if( irc != APPRC_OK )
		break;

	/* Reset the contexts back to the main context. */
	esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_DIRECTIONS );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoDisableVocab(DIRECTIONS) = 0x%x "
					"in ProcessDirections().\n", esrrc );
		break;
	}

	/* Update the active WCIS text. */
	UpdateWCISEntry( VOCABID_DIRECTIONS, 0 );
   }
   while (0);

   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in "
                  "ProcessDirections()\n", irc );
	  gs_iExit = 1;
   }
   return;
}

/******************************************************************************
* void ProcessSaveAddrBook( void )
******************************************************************************/
void ProcessSaveAddrBook( void )
{
   int irc, iAcbfDataSize;
   void *pAcbfData;
   ESRRC esrrc;
   FILE *fp;

   do  /* loop for easy error breakout; need to initialize main context on error */
   {
	/* Inform the user of being back in the main vocabulary. */
	irc = TTSPrompt( 0, gs_hEci, "Saving address book data.\n" );
	if( irc != APPRC_OK )
	{
		ErrPrintf( "TTSPrompt() = %d in ProcessSaveAddrBook().\n", irc );
		break;
	}

	/* Get the acoustic baseform data from the engine for the names already
		added.  First, get the size, then allocate the buffer space,
		and finally, get the actual data.
	*/
	esrrc = esrVmgrGetListFromSet( gs_hVocabSet, EXTRNLIST_NAMES, NULL,
										&iAcbfDataSize );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrVmgrGetListFromSet(NULL) = 0x%x in "
					"ProcessSaveAddrBook().\n", esrrc );
		break;
	}

	pAcbfData = malloc( iAcbfDataSize );
	if( pAcbfData == NULL )
	{
		ErrPrintf( "malloc() failed in ProcessSaveAddrBook().\n" );
		break;
	}

	esrrc = esrVmgrGetListFromSet( gs_hVocabSet, EXTRNLIST_NAMES, pAcbfData,
										&iAcbfDataSize );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrVmgrGetListFromSet() = 0x%x in ProcessSaveAddrBook().\n",
					esrrc );
		break;
	}

	/* Now that all the data is collected, write it out to a file. */
	fp = fopen( "addrbook.dat", "wb" );
	if( fp == NULL )
	{
		ErrPrintf( "fopen(addrbook.dat) = NULL in ProcessSaveAddrBook().\n" );
		break;
	}

	/* Write the file.  The format will be:
			Size in Bytes     Description
			Index n1          Index of entry to follow
			sizeof(entry)     Entry for index n1
			......            .......
			4                 Acbf Data Size
			n2                Acbf Data
	*/
	irc = AddrBookSave( fp );
	if( irc != APPRC_OK )
	{
		ErrPrintf( "AddrBookSave() failed in ProcessSaveAddrBook().\n" );
		break;
	}

	fwrite( &iAcbfDataSize, sizeof(int), 1, fp );
	fwrite( pAcbfData, sizeof(char), iAcbfDataSize, fp );

	fclose( fp );

	/* Free the memory allocated for the acbf data. */
	free( pAcbfData );

	/* Disable the Options vocabulary. */
	esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_OPTIONS );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoDisableVocab(OPTIONS) = 0x%x "
					"in ProcessSaveAddrBook().\n", esrrc );
		break;
	}

	/* Update the active WCIS text. */
	UpdateWCISEntry( VOCABID_OPTIONS, 0 );
   }
   while (0);

   /* Make the main context vocabularies active again. */
   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in ProcessSaveAddrBook().\n",
                  irc );
	  gs_iExit = 1;
      return;
   }

   /* Inform the user that the import is complete. */
   irc = TTSPrompt( 1, gs_hEci, "Save complete.\n\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessSaveAddrBook().\n", irc );
      return;
   }

}

/******************************************************************************
* int ClearNamesList( void )
******************************************************************************/
int ClearNamesList( void )
{
   ESRRC esrrc;
   int i;
   AddressBookEntry *pAddrBook;

   AddrBookGetDataBlock( &pAddrBook );

   /* To clear the external list without unregistering the vocabulary set,
      each acoustic baseform must be removed individually.
   */
   for( i = 0; i < NUM_ADDR_BOOK_ENTRIES; i++ )
   {
      if( pAddrBook[i].iInUse != 0 )
      {
         /* Remove the acoustic baseform from the external list. */
         esrrc = esrVmgrRemoveBaseformFromList( gs_hVocabSet, EXTRNLIST_NAMES,
                        pAddrBook[i].Baseform, pAddrBook[i].iBaseformLen );
         if( esrrc != ESR_RC_OK )
         {
            ErrPrintf( "esrVmgrRemoveBaseformFromList() = 0x%x in "
                        "ClearNameList().\n", esrrc );
            return( APPRC_ERROR );
         }

         /* Clean up the address book entry. */
         AddrBookCleanupEntry( i );
      }
   }

   /* Have the vocabulary manager process this change. */
   esrrc = esrVmgrCommitBaseformChanges();
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrVmgrCommitBaseformChanges() = 0x%x "
                 "in ClearNameList().\n", esrrc );
      return( APPRC_ERROR );
   }

   return( APPRC_OK );
}

/******************************************************************************
* void ProcessRestoreAddrBook( void )
******************************************************************************/
void ProcessRestoreAddrBook( void )
{
   int irc, iAcbfDataSize;
   void *pAcbfData;
   ESRRC esrrc;
   FILE *fp;

   do /* loop for easy error breakout; need to initialize main context on error */
   {
	/* Inform the user of being back in the main vocabulary. */
	irc = TTSPrompt( 1, gs_hEci, "Restoring address book data.\n" );
	if( irc != APPRC_OK )
	{
		ErrPrintf( "TTSPrompt() = %d in ProcessRestoreAddrBook().\n", irc );
		break;
	}

	/* Clear out the current external list. */
	irc = ClearNamesList();
	if( irc != APPRC_OK )
	{
		ErrPrintf( "ClearNamesList() = %d in ProcessRestoreAddrBook().\n", irc );
		break;
	}

	/* Clean out the current address book so as to reload the data. */
	AddrBookUninit();

	/* Retrieve the data from the file. */
	fp = fopen( "addrbook.dat", "rb" );
	if( fp == NULL )
	{
		ErrPrintf( "fopen(addrbook.dat) = NULL in ProcessRestoreAddrBook().\n" );
		break;
	}

	/* Load the address book data. */
	irc = AddrBookLoad( fp );
	if( irc != APPRC_OK )
	{
		ErrPrintf( "AddrBookLoad() = failed in ProcessRestoreAddrBook().\n" );
		break;
	}

	/* Read the acoustic baseform data for the external list. */
	if( fread( &iAcbfDataSize, sizeof(int), 1, fp ) != 1 )
	{
		ErrPrintf( "fread(iAcbfDataSize) failed in ProcessRestoreAddrBook().\n" );
		fclose( fp );
		break;
	}

	pAcbfData = malloc( iAcbfDataSize );
	if( pAcbfData == NULL )
	{
		ErrPrintf( "malloc() failed in ProcessRestoreAddrBook().\n" );
		break;
	}

	if( (int)fread( pAcbfData, sizeof(char), iAcbfDataSize, fp ) !=
		iAcbfDataSize )
	{
		ErrPrintf( "fread(pAcbfData) failed in ProcessRestoreAddrBook().\n" );
		fclose( fp );
		break;
	}

	/* Load the acoustic baseform data into the external list. */
	esrrc = esrVmgrPutListInSet( gs_hVocabSet, EXTRNLIST_NAMES, pAcbfData,
									iAcbfDataSize );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrVmgrPutListInSet() = 0x%x in ProcessRestoreAddrBook().\n",
					esrrc );
		break;
	}

	/* Free the allocated memory blocks. */
	free( pAcbfData );

	/* Have the vocabulary manager process this change. */
	esrrc = esrVmgrCommitBaseformChanges();
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrVmgrCommitBaseformChanges() = 0x%x "
					"in ProcessRestoreAddrBook().\n", esrrc );
		break;
	}

	/* Disable the Options vocabulary. */
	esrrc = esrRecoDisableVocab( gs_hEsr, gs_hVocabSet, VOCABID_OPTIONS );
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoDisableVocab(OPTIONS) = 0x%x "
					"in ProcessRestoreAddrBook().\n", esrrc );
		break;
	}

	/* Update the active WCIS text. */
	UpdateWCISEntry( VOCABID_OPTIONS, 0 );
   }
   while (0);

   /* Make the main context vocabularies active again. */
   irc = AppInitializeMainContext();
   if( irc != APPRC_OK )
   {
      ErrPrintf( "AppInitializeMainContext() = %d in "
                  "ProcessRestoreAddrBook().\n", irc );
	  gs_iExit = 1;
      return;
   }

   /* Inform the user that the import is complete. */
   irc = TTSPrompt( 1, gs_hEci, "Restore complete.\n\n" );
   if( irc != APPRC_OK )
   {
      ErrPrintf( "TTSPrompt() = %d in ProcessRestoreAddrBook().\n", irc );
   }
   return;

}


/******************************************************************************
* void ProcessEvent( AppEvent *pEvent )
******************************************************************************/
void ProcessEvent( AppEvent *pEvent )
{
   TracePrintf( "ProcessEvent(): Phrase Text=<%s>\n", pEvent->szRecoText );

   switch( pEvent->Type )
   {
      case APPEVENT_RECOERROR:
         ProcessRecoErrorEvent( pEvent );
         break;
      case APPEVENT_RECOREJECTION:
         ProcessRecoRejectionEvent( pEvent );
         break;
      case APPEVENT_DIAL:
         ProcessDialEvent( pEvent );
         break;
      case APPEVENT_ADDNAME:
         ProcessAddNameEvent();
         break;
      case APPEVENT_CALL:
         ProcessCallEvent( pEvent );
         break;
      case APPEVENT_DELETENAME:
         ProcessDeleteNameEvent( pEvent );
         break;
      case APPEVENT_MODIFYOPTIONS:
         ProcessModifyOptionsEvent();
         break;
      case APPEVENT_CMDTIMEOUT:
         UIPrintf( "Command Timeout.  Operation aborted.\n" );
         break;
      case APPEVENT_CMDERROR:
         UIPrintf( "An error occurred while processing the current command.  "
                     "Operation aborted.\n" );
         break;
      case APPEVENT_YES:
      case APPEVENT_NO:
         /* No specific action is taken for these events; they are processed by the
            code performing the confirmation.
         */
         break;

      case APPEVENT_SAVEADDR:
         ProcessSaveAddrBook();
         break;
      case APPEVENT_RESTOREADDR:
         ProcessRestoreAddrBook();
         break;
      case APPEVENT_IMPORTNAMELIST:
         ProcessImportNameList();
         break;

      case APPEVENT_CHECKEMAIL:
         ProcessCheckEmail();
         break;
      case APPEVENT_READMAIL:
         ProcessReadEmail( pEvent );
         break;
      case APPEVENT_SUMEMAIL:
         ProcessSummarizeEmail( pEvent );
         break;
      case APPEVENT_EXITEMAIL:
         ProcessExitEmail();
         break;

      case APPEVENT_WCIS:
         SayWCIS();
         break;

      case APPEVENT_GOODBYE:
         ProcessGoodbye();
         break;
	  case APPEVENT_DIRECTIONS:
		 ProcessDirections();
		 break;
   }
}

/******************************************************************************
* int ProcessUserInputP2T( void )
******************************************************************************/
int ProcessUserInputP2T( void )
{
   int irc, iKeyPressed;
   ESRRC esrrc;
   AOPRC aoprc;

   /* If a key was erroneously pressed before this prompt, throw it out */
   while (osKeyHit())
	   getchar();

   UIPrintf( "Press 'Enter' and begin speaking.  " );

   PlayReadyPrompt();

   iKeyPressed = getchar();

   if( toupper(iKeyPressed) == 'Q' )
   {
      return( 1 );
   }

   while( 1 )
   {
      /* Mark start of an utterance; delay AOP gain changes */
      aoprc = aopStartUtterance(g_hAOP);
      if( aoprc != AOP_RC_OK) 
      {
         ErrPrintf( "aopStartUtterance() = 0x%x "
		    "in ProcessUserInputP2T().\n", aoprc );
         return( APPRC_ERROR );
      }

      /* Start the engine processing data.  This calls the audio
         callback to signal when to start enqueueing PCM to the engine.
      */
      esrrc = esrRecoStartListening( gs_hEsr, ESR_AUDIO_CB_ACTIVE );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoStartListening() = 0x%x "
                    "in ProcessUserInputP2T().\n", esrrc );
         break;
      }

      /* Because this is Push-To-Talk mode, the user must signal after
         speaking the command.
      */
      UIPrintf( "Press 'Enter' when finished speaking.\n" );
      iKeyPressed = getchar();

      /* Set up the state change semaphore to signal when the state of the
         engine returns to IDLE.  This indicates that the engine has finished
         processing the data.
      */
      osResetEventSem( gs_RecoStateChangeData.hSemEvent );
      gs_RecoStateChangeData.SignalState = ESR_RECO_STATE_IDLE;

      /* Inform the engine to stop processing data.  This calls
         the audio callback to signal when to stop enqueueing PCM to
         the engine.
      */
      esrrc = esrRecoStopListening( gs_hEsr );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoStopListening() = 0x%x "
                    "in ProcessUserInputP2T().\n", esrrc );
         break;
      }

      /* Wait for the signal indicating that the engine has returned
         to the IDLE state.
      */
      irc = osWaitOnEventSem( gs_RecoStateChangeData.hSemEvent, 10000 );
      if( irc != OS_OK )
      {
         ErrPrintf( "osWaitOnEventSem() = %ld "
                    "in ProcessUserInputP2T().\n", irc );
         break;
      }

	  /* Mark end of an utterance */
      aoprc = aopStopUtterance(g_hAOP);
      if( aoprc != AOP_RC_OK) 
      {
         ErrPrintf( "aopStartUtterance() = 0x%x "
		    "in ProcessUserInputP2T().\n", aoprc );
         return( APPRC_ERROR );
      }

	  // Close the EAL
      irc = AudioCloseRec();
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioCloseRec() = %d in ProcessUserInputP2T().\n", irc );
      }

      break;
   }

   return( 0 );
}

/******************************************************************************
* int ProcessUserInputP2A( void )
******************************************************************************/
int ProcessUserInputP2A( void )
{
   int irc, iKeyPressed;
   ESRRC esrrc;
   AOPRC aoprc;

   /* If a key was erroneously pressed before this prompt, throw it out */
   while (osKeyHit())
	   getchar();

   UIPrintf( "Press 'Enter' and begin speaking. " );

   PlayReadyPrompt();

   iKeyPressed = getchar();

   if( toupper(iKeyPressed) == 'Q' )
   {
      return( 1 );
   }

   UIPrintf( "Say the command and wait for a response.\n\n" );

   /* Set up the state change semaphore to signal when the engine
       returns to the IDLE state indicating that it has finished
       processing its data.
   */
   osResetEventSem( gs_RecoStateChangeData.hSemEvent );
   gs_RecoStateChangeData.SignalState = ESR_RECO_STATE_IDLE;

   /* Mark start of an utterance; delay AOP gain changes */
   aoprc = aopStartUtterance(g_hAOP);
   if( aoprc != AOP_RC_OK) 
   {
       ErrPrintf( "aopStartUtterance() = 0x%x "
                  "in ProcessUserInputP2A().\n", aoprc );
       return( APPRC_ERROR );
   }

   while( 1 )
   {
      /* Start the engine processing data.  This calls the audio
         callback to signal when to start enqueueing PCM to the engine.
      */
      esrrc = esrRecoStartListening( gs_hEsr, ESR_AUDIO_CB_ACTIVE );
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoStartListening() = 0x%x "
                    "in ProcessUserInputP2A().\n", esrrc );
         break;
      }

      /* Allow the user 10 seconds to say the phrase, then have the engine
         complete its processing.  The engine will key off the silence at
         the end of the phrase to know that the user has finished speaking.
         The engine will then call the audio callback to request that PCM is no
         longer enqueued and call the appropriate error or result callback.
      */
      irc = osWaitOnEventSem( gs_RecoStateChangeData.hSemEvent, 10000 );
      if( irc == OS_SEM_TIMEOUT )
      {
         gs_RecoResultData.pEvent->Type = APPEVENT_CMDTIMEOUT;

         /* The engine did not return to the IDLE state within the alloted time, so
            abort this recognition attempt.
         */
         esrrc = esrRecoStopListening( gs_hEsr );
         if( esrrc != ESR_RC_OK )
         {
            ErrPrintf( "esrRecoStopListening() = 0x%x "
                       "in ProcessUserInputP2A().\n", esrrc );
            break;
         }

         /* Wait for the engine to return to the IDLE state. */
         irc = osWaitOnEventSem( gs_RecoStateChangeData.hSemEvent, 5000 );
         {
            ErrPrintf( "osWaitOnEventSem(Aborting) = %ld "
                       "in ProcessUserInputP2A().\n", esrrc );
			break;
         }
      }
      else if( irc != OS_OK )
      {
         gs_RecoResultData.pEvent->Type = APPEVENT_CMDERROR;
         ErrPrintf( "osWaitOnEventSem(WaitForEvent) = %ld "
                    "in ProcessUserInputP2A().\n", esrrc );
      }

      break;
   }

   /* Mark end of an utterance */
   aoprc = aopStopUtterance(g_hAOP);
   if( aoprc != AOP_RC_OK) 
   {
       ErrPrintf( "aopStartUtterance() = 0x%x "
				  "in ProcessUserInputP2A().\n", aoprc );
       return( APPRC_ERROR );
   }

   // Close the EAL
   irc = AudioCloseRec();
   if( irc != AUDIO_OK )
   {
       ErrPrintf( "AudioCloseRec() = %d in ProcessUserInputP2A().\n", irc );
   }

   return( 0 );
}

/******************************************************************************
* int ExecuteRecoSequence( void )
******************************************************************************/
int ExecuteRecoSequence( AppEvent *pEvent )
{
   int iDone = 0;

   /* Set up the recognition result callback data with the given event
      structure. The callback will store its results in this event.
   */
   gs_RecoResultData.pEvent = pEvent;

   switch( gs_ListeningMode )
   {
      case ESR_LMODE_PUSHTOTALK:
         iDone = ProcessUserInputP2T();
         break;
      case ESR_LMODE_PUSHTOACTIVATE:
         iDone = ProcessUserInputP2A();
         break;
   }

   return( iDone );
}

/******************************************************************************
* void ProcessUserInput( void )
******************************************************************************/
void ProcessUserInput( void )
{
   AppEvent AppEvent = { APPEVENT_INVALID, "" };
   int iDone;

   while( gs_iExit == 0 )
   {
      /* State with an invalid event. */
      memset( &AppEvent, 0, sizeof( AppEvent ) );
      AppEvent.Type = APPEVENT_INVALID;

      iDone = ExecuteRecoSequence( &AppEvent );

      if( iDone == 0 )
      {
         /* Process the event that resulted from the last command. */
         ProcessEvent( &AppEvent );
      }
   }
}

/******************************************************************************
* void DisplayBanner( void )
******************************************************************************/
void DisplayBanner( void )
{

   UIPrintf(
      "Licensed Materials - Property of IBM\n"
      "11K6192 V2.2  AT7AENA, AT7PRNA V2.3  AT2T5ZZ v4.3\n"
      "(C) Copyright IBM Corp. 2000, 2004  All Rights Reserved.\n"
      "US Government Users Restricted Rights - Use, duplication or disclosure\n"
      "restricted by GSA ADP Schedule Contract with IBM Corp.\n"
      "\n\n"
      "Sample Phone Application\n\n"
   );
}

/******************************************************************************
* int main( int argc, char *argv[] )
******************************************************************************/
int main( int argc, char *argv[] ){

   DisplayBanner();

   /* Use a while loop to allow the error to break out and jump
      to the cleanup code without using labels and gotos.
   */
   while( 1 )
   {
      /* Process the command line arguments. */
      if( ProcessCommandLine( argc, argv ) == APPRC_ERROR )
         break;

      /* Initialize the application. */
      if( AppInitialize() != APPRC_OK )
         break;

      /* Initialize the recognition service. */
      if( AppRecoInitialize() != APPRC_OK )
         break;

      /* Set up the application so that the main context will be active and
             ready the first time through the main execution loop.
      */
      if( AppInitializeMainContext() != APPRC_OK )
         break;

      /* Loop, processing input from the user and taking the proper actions. */
      ProcessUserInput();

      /* Uninitialize the recognition service. */
      if( AppRecoUninitialize() != APPRC_OK )
         break;

      /* Uninitialize the application. */
      if( AppUninitialize() != APPRC_OK )
         break;

      break;   /* Done! */
   }

   UIPrintf( "Press 'Enter' to Exit." );
   getchar();

   return( 0 );
}


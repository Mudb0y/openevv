/*=========================================================================*/
/*                                                                         */
/* esrcbs.c                                                                */
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

#include "esr.h"
#include "os.h"


#include "phone.h"
#include "uifuncs.h"
#include "audio.h"
#include "appevent.h"
#include "aop.h"

/* AOP handle */
extern AOPHandle g_hAOP;


/******************************************************************************
*******************************************************************************
*                  C A L L B A C K   F U N C T I O N S
*******************************************************************************
******************************************************************************/

/******************************************************************************
* ESRAudioCBRc AppEsrAudioCB( ESREngineHandle hEngine, int iAudioFlag,
*                             void *pUserData )
******************************************************************************/
ESRAudioCBRc AppEsrAudioCB( ESREngineHandle hEngine, int iAudioFlag,
                            void *pUserData )
{
   int irc;
   unsigned long ulBytesRecorded;
   AudioCBData *pData = (AudioCBData *)pUserData;

   TracePrintf( "AppEsrAudioCB(): Entry\n" );

   if( iAudioFlag == ESR_AUDIO_START )
   {
      /* The PCM flow needs to start, so open and start the recording
         device.  */
      irc = AudioOpenRec();
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioOpenRec() = %d in AppEsrAudioCB().\n", irc );
      }
      else
      {
         irc = AudioStartRec( pData->pAudioBuf, pData->AudioBufSize );
         if( irc != AUDIO_OK )
         {
            ErrPrintf( "AudioStartRec() = %d in AppEsrAudioCB().\n", irc );
         }
      }
   }
   else if( iAudioFlag == ESR_AUDIO_STOP )
   {
      /* The PCM flow needs to stop, so stop and close the recording
         device.  */
      irc = AudioStopRec( &ulBytesRecorded );
      if( irc != AUDIO_OK )
      {
         ErrPrintf( "AudioStopRec() = %d in AppEsrAudioCB().\n", irc );
      }
      else
      {
         pData->AudioBufSize = ulBytesRecorded;
      }
   }

   TracePrintf( "AppEsrAudioCB(): Exit\n" );

   return( 0 );
}


/******************************************************************************
* ESRErrorCBRc AppEsrErrorCB( ESREngineHandle hEngine, long lErrNum,
*                             void *pUserData)
******************************************************************************/
ESRErrorCBRc AppEsrErrorCB( ESREngineHandle hEngine, long lErrNum,
                            void *pUserData)
{
   UIPrintf( "AppEsrErrorCB(): 0x%x\n", lErrNum );
   return( ESR_ERROR_PROCESSED );
}



/******************************************************************************
* ESRErrorCBRc AppEsrRecoErrorCB( ESREngineHandle hEngine, long lErrNum,
*                                 void *pUserData)
******************************************************************************/
ESRErrorCBRc AppEsrRecoErrorCB( ESREngineHandle hEngine, long lErrNum,
                                void *pUserData)
{
   RecoResultData *pData;

   TracePrintf( "AppEsrRecoErrorCB(): 0x%x\n", lErrNum );
   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;

   /* Set the event to show that a recognition error occurred.*/
   pData->pEvent->Type = APPEVENT_RECOERROR;

   pData->pEvent->Event.Error.lErr = lErrNum;

   return( ESR_ERROR_PROCESSED );
}

/******************************************************************************
* void AppEsrRecoStateCB( ESREngineHandle hEngine, ESRRecoStates PrevState,
*                         ESRRecoStates NewState, void *pUserData)
******************************************************************************/
void AppEsrRecoStateCB( ESREngineHandle hEngine, ESRRecoStates PrevState,
                        ESRRecoStates NewState, void *pUserData)
{
   RecoStateChangeData *pData = (RecoStateChangeData *)pUserData;

   TracePrintf( "AppEsrRecoStateCB(): State Change: %d -> %d\n",
                PrevState, NewState );

   pData->LastState = NewState;

   /* If the new state matches the requested state to signal upon,
      then signal the semaphore. */
   if( (pData->hSemEvent != NULL) && (NewState == pData->SignalState) )
   {
      TracePrintf( "AppEsrRecoStateCB(): Signalling State\n" );
      osSignalEventSem( pData->hSemEvent );
   }
}


/******************************************************************************
* int AppEsrAcbfResult( ESREngineHandle hEngine, ESRBaseformResult *baseform,
*                      int iLen, void *pUserData)
******************************************************************************/
int AppEsrAcbfResult( ESREngineHandle hEngine, ESRBaseformResult *baseform,
                      int iLen, void *pUserData)
{
   AcbfResultData *pResultData = (AcbfResultData *)pUserData;

   pResultData->lErr = 0;
   pResultData->pResult = malloc( sizeof(ESRBaseformResult) * iLen );
   if( pResultData->pResult == NULL )
   {
      ErrPrintf( "malloc() failed in AppEsrAcbfResult().\n" );
   }
   else
   {
      memcpy( pResultData->pResult, baseform, sizeof(ESRBaseformResult) * iLen );
      pResultData->iLen = iLen;
   }
   return( 0 );
}

/******************************************************************************
* ESRErrorCBRc AppEsrAcbfError( ESREngineHandle hEngine, long lErrNum,
*                               void *pUserData)
******************************************************************************/
ESRErrorCBRc AppEsrAcbfError( ESREngineHandle hEngine, long lErrNum,
                              void *pUserData)
{
   AcbfResultData *pResultData = (AcbfResultData *)pUserData;
   TracePrintf( "AppEsrAcbfErrorCB(): 0x%x\n", lErrNum );

   pResultData->lErr = lErrNum;
   pResultData->pResult = NULL;
   pResultData->iLen = 0;

   return( ESR_ERROR_PROCESSED );
}

/******************************************************************************
* void AppEsrAcbfStateChange( ESREngineHandle hEngine, ESRAcbfStates PrevState,
*                            ESRAcbfStates NewState, void *pUserData)
******************************************************************************/
void AppEsrAcbfStateChange( ESREngineHandle hEngine, ESRAcbfStates PrevState,
                            ESRAcbfStates NewState, void *pUserData)
{
   AcbfStateChangeData *pData = (AcbfStateChangeData *)pUserData;

   TracePrintf( "AppEsrAcbfStateChange(): State Change: %d -> %d\n",
                PrevState, NewState );

   pData->LastState = NewState;

   /* If the new state matches the requested state to signal upon,
      then signal the semaphore. */
   if( (pData->hSemEvent != NULL) && (NewState == pData->SignalState) )
   {
      TracePrintf( "AppEsrAcbfStateChange(): Signalling State\n" );
      osSignalEventSem( pData->hSemEvent );
   }
}

/******************************************************************************
* AppEventType MatchValidEvent( ESRAnnotationInfo Annot,
                                AppEventType *pEventTable, int iNumEvents )
******************************************************************************/
AppEventType MatchValidEvent( ESRAnnotationInfo *pAnnots, int iNumAnnots,
                              AppEventType *pEventTable, int iNumEvents )
{
   int iEventIndex, iAnnotIndex;

   for( iEventIndex = 0; iEventIndex < iNumEvents; iEventIndex++ )
   {
      for( iAnnotIndex = 0; iAnnotIndex < iNumAnnots; iAnnotIndex++ )
      {
         if( pAnnots[iAnnotIndex].iInfoType == ESR_ANNOTATION )
         {
            if( (AppEventType)pAnnots[iAnnotIndex].iAnnotation ==
                pEventTable[iEventIndex] )
               return( pEventTable[iEventIndex] );
         }
      }
   }

   return( APPEVENT_INVALID );
}

/******************************************************************************
* int AppRecoResultMain( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultMain( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
   int i=0, esrrc=0, iNumAnnots=0, iDigitIndex=0, PScore=0 ,Plen=RECOTEXT_LEN;
   EStringType StrType = 0;
   ESRAnnotationInfo *pAnnotInfo = NULL;

   AppEventType EventType;
   RecoResultData *pData;
   /* Declare array of event IDs valid for this result callback. */
   AppEventType ValidEvents[] =
   {
      APPEVENT_DIAL, APPEVENT_ADDNAME, APPEVENT_MODIFYOPTIONS,
      APPEVENT_CHECKEMAIL, APPEVENT_GOODBYE, APPEVENT_DIRECTIONS, APPEVENT_WCIS
   };
   int iNumValidEvents = sizeof(ValidEvents)/sizeof(ValidEvents[0]);

   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;


   esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
				  1,
				  &hVocabSet,
				  &VocabId,
				  pData->pEvent->szRecoText,
				  &Plen,
				  &StrType,
				  &Result,
				  &PScore);

   
   /* If the result is rejected, then it is a recognition error */
   if( Result == ESR_RECO_RESULT_REJECTED )
   {
      /* Set the event to show that a recognition error occurred.*/
      pData->pEvent->Type = APPEVENT_RECOREJECTION;
      return( 0 );
   }
  
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultText(NULL) = 0x%x in "
                  "AppRecoResultMain().\n", esrrc );
	  return 0;
   }

   /* UIPrintf( "AppRecoResultMain(): <%s>\n", pData->pEvent->szRecoText ); */

   /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);



   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultAnnotations(NULL) = 0x%x in "
                  "AppRecoResultMain().\n", esrrc );
	  return 0;
   }

   if( iNumAnnots > 0 )
   {
      /* Allocate the array to contain the annotations. */
      pAnnotInfo = (ESRAnnotationInfo *)
                     malloc( iNumAnnots * sizeof(ESRAnnotationInfo) );
      if( pAnnotInfo == NULL )
      {
         ErrPrintf( "malloc() failed in AppRecoResultMain().\n" );
         return( 0 );
      }

      /* Now get the annotation values. */
	  esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 pAnnotInfo,
								 &iNumAnnots,
								 &Result,
								 &PScore);
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
                     "AppRecoResultMain().\n", esrrc );
		 free(pAnnotInfo);
		 return 0;
      }

      /* Get the event type from the array of annotations. */
      EventType = MatchValidEvent( pAnnotInfo, iNumAnnots, ValidEvents,
                                   iNumValidEvents );

      switch( EventType )
      {
         case APPEVENT_DIAL:
            /* Store the dial command information into the event structure.
               The data to be saved are the digits and their corresponding
			   numbers. */
            pData->pEvent->Type = EventType;
            pData->pEvent->Event.Dial.iNumDigits = iNumAnnots - 1;
            iDigitIndex = 0;
            for( i = 0; i < iNumAnnots; i++ )
            {
               /* Ensure that there is a valid annotation at this spot. */
               if( pAnnotInfo[i].iInfoType == ESR_ANNOTATION )
               {
                  /* Ensure that the annotation value is for one of the digits
                     Note that values 10 and 11 are for the pound (#) and star
					 (*) keys, respectively.  */
                  if( (pAnnotInfo[i].iAnnotation >= 0) &&
                      (pAnnotInfo[i].iAnnotation <= 11) )
                  {
                     pData->pEvent->Event.Dial.aiDigits[iDigitIndex] =
                                                   pAnnotInfo[i].iAnnotation;
                     iDigitIndex++;
                  }
               }
            }
            break;

         case APPEVENT_ADDNAME:
         case APPEVENT_MODIFYOPTIONS:
         case APPEVENT_CHECKEMAIL:
         case APPEVENT_GOODBYE:
		 case APPEVENT_DIRECTIONS:
         case APPEVENT_WCIS:
            /* These events have do not require any data from the command. */
            pData->pEvent->Type = EventType;
            break;
      }

      /* Free the array of annotations. */
      free( pAnnotInfo );
   }

   return( 0 );
}

/******************************************************************************
* int AppRecoResultNames( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultNames( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
   int i=0, esrrc=0, iNumAnnots=0, Plen=RECOTEXT_LEN, PScore=0;
   EStringType StrType=0;
   ESRAnnotationInfo *pAnnotInfo=NULL;

   AppEventType EventType;
   RecoResultData *pData;
   /* Declare the array of event IDs valid for this result callback. */
   AppEventType ValidEvents[] =
   {
      APPEVENT_CALL, APPEVENT_DELETENAME
   };
   int iNumValidEvents = sizeof(ValidEvents)/sizeof(ValidEvents[0]);

   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;

   /* Get the text of the actual recognized phrase.  For this case, a local
      char array will hold the data.  This API could also be used to query
	  the size of the string so that it can be dynamically allocated. */
   
   esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
						  1,
						  &hVocabSet,
						  &VocabId,
						  pData->pEvent->szRecoText,
						  &Plen,
						  &StrType,
						  &Result,
						  &PScore);

   /* If the result is rejected, then it is a recognition error */
   if( Result == ESR_RECO_RESULT_REJECTED )
   {
      /* Set the event to show that a recognition error occurred.*/
      pData->pEvent->Type = APPEVENT_RECOREJECTION;
      return( 0 );
   }

   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultText(NULL) = 0x%x in "
                  "AppRecoResultNames().\n", esrrc );
	  return 0;
   }
   /* UIPrintf( "AppRecoResultNames(): <%s>\n", pData->pEvent->szRecoText ); */

      /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultAnnotations(NULL) = 0x%x in "
                  "AppRecoResultNames().\n", esrrc );
   }

   if( iNumAnnots > 0 )
   {
      /* Allocate the array to contain the annotations. */
      pAnnotInfo = (ESRAnnotationInfo *)
                     malloc( iNumAnnots * sizeof(ESRAnnotationInfo) );

      if( pAnnotInfo == NULL )
      {
         ErrPrintf( "malloc() failed in AppRecoResultNames().\n" );
         return( 0 );
      }

	  /* Now get the annotation values. */
	  esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 pAnnotInfo,
								 &iNumAnnots,
								 &Result,
								 &PScore);
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
                     "AppRecoResultNames().\n", esrrc );
		 free(pAnnotInfo);
		 return (0);
      }

      /* Get the event type from the array of annotations. */
      EventType = MatchValidEvent( pAnnotInfo, iNumAnnots, ValidEvents,
                                   iNumValidEvents );

      switch( EventType )
      {
         case APPEVENT_CALL:
            pData->pEvent->Type = EventType;
            for( i = 0; i < iNumAnnots; i++ )
            {
               /* Ensure that there is a valid annotation that is
                  user data from an external list entry.  */
               if( pAnnotInfo[i].iInfoType == ESR_USER_DATA )
               {
                  pData->pEvent->Event.Call.iAddrBookIndex =
                                             (int)pAnnotInfo[i].pUserData;
                  break;
               }
            }
            break;

         case APPEVENT_DELETENAME:
            pData->pEvent->Type = EventType;
            for( i = 0; i < iNumAnnots; i++ )
            {
               /* Ensure that there is a valid annotation that is
                  user data from an external list entry. */
               if( pAnnotInfo[i].iInfoType == ESR_USER_DATA )
               {
                  pData->pEvent->Event.DeleteName.iAddrBookIndex =
                                             (int)pAnnotInfo[i].pUserData;
                  break;
               }
            }
            break;
      }

      /* Free the array of annotations. */
      free( pAnnotInfo );
   }

   return( 0 );
}

/******************************************************************************
* int AppRecoResultYesNo( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultYesNo( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
   int esrrc=0, iNumAnnots=0,Plen=RECOTEXT_LEN,PScore=0;
   EStringType StrType=0;
   ESRAnnotationInfo *pAnnotInfo=NULL;

   AppEventType EventType;
   RecoResultData *pData;
   /* Declare an array of event IDs valid for this result callback. */
   AppEventType ValidEvents[] =
   {
      APPEVENT_YES, APPEVENT_NO, APPEVENT_WCIS
   };
   int iNumValidEvents = sizeof(ValidEvents)/sizeof(ValidEvents[0]);

   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;

   esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
						  1,
						  &hVocabSet,
						  &VocabId,
						  pData->pEvent->szRecoText,
						  &Plen,
						  &StrType,
						  &Result,
						  &PScore);

   /* If the result is rejected, then it is a recognition error */
   if( Result == ESR_RECO_RESULT_REJECTED )
   {
      /* Set the event to show that a recognition error occurred.*/
      pData->pEvent->Type = APPEVENT_RECOREJECTION;
      return( 0 );
   }
   
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultText(NULL) = 0x%x in "
                  "AppRecoResultYesNo().\n", esrrc );
	  return 0;
   }

   /* UIPrintf( "AppRecoResultNames(): <%s>\n", pData->pEvent->szRecoText ); */

   /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultAnnotations(NULL) = 0x%x in "
                  "AppRecoResultYesNo().\n", esrrc );
	  return (0);
   }

   if( iNumAnnots > 0 )
   {
      /* Allocate the array to contain the annotations. */
      pAnnotInfo = (ESRAnnotationInfo *)
                     malloc( iNumAnnots * sizeof(ESRAnnotationInfo) );

      if( pAnnotInfo == NULL )
      {
         ErrPrintf( "malloc() failed in AppRecoResultYesNo().\n" );
         return( 0 );
      }

	  /* Now get the annotation values. */
	  esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 pAnnotInfo,
								 &iNumAnnots,
								 &Result,
								 &PScore);
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
                     "AppRecoResultYesNo().\n", esrrc );
		 free(pAnnotInfo);
		 return 0;
      }

      /* Get the event type from the array of annotations. */
      EventType = MatchValidEvent( pAnnotInfo, iNumAnnots, ValidEvents,
                                   iNumValidEvents );

      switch( EventType )
      {
         case APPEVENT_WCIS:
         case APPEVENT_YES:
         case APPEVENT_NO:
            pData->pEvent->Type = EventType;
            break;
      }
   }

   /* Free the array of annotations. */
   if(pAnnotInfo)
	   free( pAnnotInfo );

   return( 0 );
}

/******************************************************************************
* int AppRecoResultOptions( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultOptions( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
   int esrrc=0, iNumAnnots=0, Plen=RECOTEXT_LEN, PScore=0;
   EStringType StrType=0;
   ESRAnnotationInfo *pAnnotInfo=NULL;

   AppEventType EventType;
   RecoResultData *pData;
   /* Declare an array of event IDs valid for this result callback. */
   AppEventType ValidEvents[] =
   {
      APPEVENT_SAVEADDR, APPEVENT_RESTOREADDR, APPEVENT_IMPORTNAMELIST,
      APPEVENT_WCIS
   };
   int iNumValidEvents = sizeof(ValidEvents)/sizeof(ValidEvents[0]);

   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;

   esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
						  1,
						  &hVocabSet,
						  &VocabId,
						  pData->pEvent->szRecoText,
						  &Plen,
						  &StrType,
						  &Result,
						  &PScore);

   /* If the result is rejected, then it is a recognition error */
   if( Result == ESR_RECO_RESULT_REJECTED )
   {
      /* Set the event to show that a recognition error occurred.*/
      pData->pEvent->Type = APPEVENT_RECOREJECTION;
      return( 0 );
   }
   
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultText(NULL) = 0x%x in "
                  "AppRecoResultOptions().\n", esrrc );
	  return 0;
   }

   /* UIPrintf( "AppRecoResultNames(): <%s>\n", pData->pEvent->szRecoText ); */

   /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);  
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultAnnotations(NULL) = 0x%x in "
                  "AppRecoResultOptions().\n", esrrc );
	  return 0;
   }

   if( iNumAnnots > 0 )
   {
      /* Allocate the array to contain the annotations. */
      pAnnotInfo = (ESRAnnotationInfo *)
                     malloc( iNumAnnots * sizeof(ESRAnnotationInfo) );

      if( pAnnotInfo == NULL )
      {
         ErrPrintf( "malloc() failed in AppRecoResultOptions().\n" );
         return( 0 );
      }

	       /* Now get the annotation values. */
	  esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 pAnnotInfo,
								 &iNumAnnots,
								 &Result,
								 &PScore);
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
                     "AppRecoResultOptions().\n", esrrc );
		 free(pAnnotInfo);
		 return 0;
      }

      /* Get the event type from the array of annotations. */
      EventType = MatchValidEvent( pAnnotInfo, iNumAnnots, ValidEvents,
                                   iNumValidEvents );

      switch( EventType )
      {
         case APPEVENT_SAVEADDR:
         case APPEVENT_RESTOREADDR:
         case APPEVENT_IMPORTNAMELIST:
         case APPEVENT_WCIS:
            pData->pEvent->Type = EventType;
            break;
      }

      /* Free the array of annotations. */
      free( pAnnotInfo );
   }

   return( 0 );
}


/******************************************************************************
* int AppRecoResultEmail( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultEmail( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
   int i=0, esrrc=0, iNumAnnots=0, Plen=RECOTEXT_LEN, PScore=0;
   EStringType StrType=0;
   ESRAnnotationInfo *pAnnotInfo=NULL;

   AppEventType EventType;
   RecoResultData *pData;
   /* Declare an array of event IDs valid for this result callback. */
   AppEventType ValidEvents[] =
   {
      APPEVENT_READMAIL,
      APPEVENT_SUMEMAIL,
      APPEVENT_EXITEMAIL,
      APPEVENT_WCIS
   };
   int iNumValidEvents = sizeof(ValidEvents)/sizeof(ValidEvents[0]);

   /* The user data is a pointer to the AppEvent structure that will store the
      result of the command. */
   pData = (RecoResultData *)pUserData;

   esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
						  1,
						  &hVocabSet,
						  &VocabId,
						  pData->pEvent->szRecoText,
						  &Plen,
						  &StrType,
						  &Result,
						  &PScore);


   /* If the result is rejected, then it is a recognition error */
   if( Result == ESR_RECO_RESULT_REJECTED )
   {
      /* Set the event to show that a recognition error occurred.*/
      pData->pEvent->Type = APPEVENT_RECOREJECTION;
      return( 0 );
   }
   
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultText(NULL) = 0x%x in "
                  "AppRecoResultEmail().\n", esrrc );
	  return 0;
   }

   /* UIPrintf( "AppRecoResultNames(): <%s>\n", pData->pEvent->szRecoText ); */

       /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);
   if( esrrc != ESR_RC_OK )
   {
      ErrPrintf( "esrRecoQueryResultAnnotations(NULL) = 0x%x in "
                  "AppRecoResultEmail().\n", esrrc );
	  return 0;
   }

   if( iNumAnnots > 0 )
   {
      /* Allocate the array to contain the annotations. */
      pAnnotInfo = (ESRAnnotationInfo *)
                     malloc( iNumAnnots * sizeof(ESRAnnotationInfo) );

      if( pAnnotInfo == NULL )
      {
         ErrPrintf( "malloc() failed in AppRecoResultEmail().\n" );
         return( 0 );
      }

	       /* Now get the annotation values. */
	  esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 pAnnotInfo,
								 &iNumAnnots,
								 &Result,
								 &PScore);
      if( esrrc != ESR_RC_OK )
      {
         ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
                    "AppRecoResultEmail().\n", esrrc );
		 free(pAnnotInfo);
		 return 0;
      }

      /* Get the event type from the array of annotations. */
      EventType = MatchValidEvent( pAnnotInfo, iNumAnnots, ValidEvents,
                                   iNumValidEvents );

      switch( EventType )
      {
         case APPEVENT_READMAIL:
         case APPEVENT_SUMEMAIL:
            pData->pEvent->Type = EventType;

            /* Get the annotation of the note to read. */
            for( i = 0; i < iNumAnnots; i++ )
            {
               /* Ensure that there is a valid annotation and that it is not
                  the annotation for the event, but instead the
                  annotation for the item to be acted on (in this case,
                  which email note).
               */
               if( (pAnnotInfo[i].iInfoType == ESR_ANNOTATION) &&
                   (pAnnotInfo[i].iAnnotation != EventType) )
               {
                  pData->pEvent->Event.EmailNote.iNoteIndex =
                                                   pAnnotInfo[i].iAnnotation;
                  break;
               }
            }


            break;
         case APPEVENT_EXITEMAIL:
         case APPEVENT_WCIS:
            pData->pEvent->Type = EventType;
            break;
      }

      /* Free the array of annotations. */
      free( pAnnotInfo );
   }

   return( 0 );
}

/******************************************************************************
* int AppRecoResultDirections( ESREngineHandle hEngine, ESRRecoResultFlag Result,
*                        ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                         ESRRecoPhraseHandle hPhrase, void *pUserData )
******************************************************************************/
int AppRecoResultDirections( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData )
{
	int esrrc=0, iNumAnnots=0,Plen=RECOTEXT_LEN,PScore=0;
	EStringType StrType=0;
	RecoResultData *pData;

	/* The user data is a pointer to the AppEvent structure that will store the
	result of the command. */
	pData = (RecoResultData *)pUserData;
	
    esrrc = esrRecoQueryResultText(hEngine,
		                  hPhrase,
						  1,
						  &hVocabSet,
						  &VocabId,
						  pData->pEvent->szRecoText,
						  &Plen,
						  &StrType,
						  &Result,
						  &PScore);

	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoQueryResultText = 0x%x in "
			"AppRecoResultDirections().\n", esrrc );
		return 0;
	}
	
       /* Query the number of annotations. */
   esrrc = esrRecoQueryResultAnnotations(hEngine,
	                             hPhrase,
								 1,
								 &hVocabSet,
								 &VocabId,
								 NULL,
								 &iNumAnnots,
								 &Result,
								 &PScore);
	if( esrrc != ESR_RC_OK )
	{
		ErrPrintf( "esrRecoQueryResultAnnotations(pAnnotInfo) = 0x%x in "
			"AppRecoResultDirections().\n", esrrc );
	}
	if (iNumAnnots > 0)
	{
		/* only annotation is 'what can i say', so no need to check it */
		pData->pEvent->Type = APPEVENT_WCIS;
	}
	else
	{
		/* If the result is rejected, then it is a recognition error */
		if( Result == ESR_RECO_RESULT_REJECTED )
		{
			/* Set the event to show that a recognition error occurred.*/
			pData->pEvent->Type = APPEVENT_RECOREJECTION;
			return( 0 );
		}
	}
	
	return( 0 );
}

/********************************************************************************
* void AppEsrFrameStatsCB( ESREngineHandle hEngine, 
*			ESRSpeechDetectorFrameStatsStruct *pEsrSDFrameStats,
*			void *PUserData)
********************************************************************************/
void AppEsrFrameStatsCB( ESREngineHandle hEngine, 
			ESRSpeechDetectorFrameStatsStruct* pEsrSDFrameStats,
			void *pUserData) 
{
	AOPStatus aopStatus;
	AOPRC aoprc;
	/* pass frame stats data to the aop */
	aoprc = aopProcessFrameStats( g_hAOP, pEsrSDFrameStats, &aopStatus );
	if ( aoprc != AOP_RC_OK )
	{
		ErrPrintf( "aopProcessFrameStats( g_hAOP, pEsrSDFrameStats, &aopStatus) = 0x%x in "
			"AppEsrFrameStatsCB().\n", aoprc );
	}
}


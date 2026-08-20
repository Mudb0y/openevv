/*=========================================================================*/
/*                                                                         */
/* esrcbs.h                                                                */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1	AT2T5ZZ V4.3 		                                   */
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

ESRAudioCBRc AppEsrAudioCB( ESREngineHandle hEngine, int iAudioFlag,
                            void *pUserData );
ESRErrorCBRc AppEsrErrorCB( ESREngineHandle hEngine, long lErrNum,
                            void *pUserData);
ESRErrorCBRc AppEsrRecoErrorCB( ESREngineHandle hEngine, long lErrNum,
                                void *pUserData);
void AppEsrRecoStateCB( ESREngineHandle hEngine, ESRRecoStates PrevState,
                        ESRRecoStates NewState, void *pUserData);
int AppEsrAcbfResult( ESREngineHandle hEngine, ESRBaseformResult *baseform,
                        int iLen, void *pUserData);
ESRErrorCBRc AppEsrAcbfError( ESREngineHandle hEngine, long lErrNum,
                                 void *pUserData);
void AppEsrAcbfStateChange( ESREngineHandle hEngine,
                                    ESRAcbfStates PrevState, ESRAcbfStates
                                    NewState, void *pUserData);
int AppRecoResultMain( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
int AppRecoResultNames( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
int AppRecoResultYesNo( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
int AppRecoResultOptions( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
int AppRecoResultEmail( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
int AppRecoResultDirections( ESREngineHandle hEngine, ESRRecoResultFlag Result,
                       ESRVocabSetHandle hVocabSet, ESRVocabId VocabId,
                       ESRRecoPhraseHandle hPhrase, void *pUserData );
void AppEsrFrameStatsCB( ESREngineHandle hEngine, 
			ESRSpeechDetectorFrameStatsStruct *pEsrSDFrameStats,
			void *pUserData);

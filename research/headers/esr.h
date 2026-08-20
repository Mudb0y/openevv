/*****************************************************************************/
/*                                                                           */
/* Licensed Materials - Property of IBM                                      */
/* AT7AENA, AT7PRNA V2.3  AT7A3NA V3.1  AS8P7ZZ V4.1 ASSF4ZZ V5.2,           */
/* AT2T5ZZ V4.3                                                              */
/* (C) Copyright IBM Corp. 1999,2004  All Rights Reserved.                   */
/* US Government Users Restricted Rights - Use, duplication or disclosure    */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                    */
/*                                                                           */
/* The following IBM source code is provided to assist you in your           */
/* development.  You may use this code only in accordance with the           */
/* IBM License Agreement accompanying the Licensed Materials.                */
/*                                                                           */
/* This copyright statement may not be removed.                              */
/*****************************************************************************/

#ifndef _ESR_H_
#define _ESR_H_

#include "evvrc.h"
#include "ecomdefs.h"
#include "esr_cmn.h"

VVE_DECLSPEC ESRRC esrCreate(ESREngineHandle *phEngine, ESRInitializationData *pInitializationData, ESRPCMAttrs *pPCMAttrs, ESRAttrs *pInitialAttrs);
VVE_DECLSPEC ESRRC esrDestroy(ESREngineHandle hEngine, ESRInitializationData **ppInitializationData);
VVE_DECLSPEC ESRRC esrSingleStep(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrGetAudioAdjustmentData(ESREngineHandle hEngine, ESREngineAudioAdjData *pAudioAdjustmentData, int *piNumBytes);
VVE_DECLSPEC ESRRC esrSetAudioAdjustmentData(ESREngineHandle hEngine, ESREngineAudioAdjData *pAudioAdjustmentData);
VVE_DECLSPEC ESRRC esrStartEngineTask(ESREngineHandle hEngine, OSTaskInfo *pTaskInfo, int iStackSize);
VVE_DECLSPEC ESRRC esrStopEngineTask(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrQueryPCMQFreeSpace(ESREngineHandle hEngine, unsigned long *plNumBytes);
VVE_DECLSPEC ESRRC esrEnqueuePCM(ESREngineHandle hEngine, unsigned char *pucPCM, long int liNumBytes, unsigned long *pliNumBytesUsed);
VVE_DECLSPEC ESRRC esrGetAttrs(ESREngineHandle hEngine, ESRAttrs *pESRAttrs);
VVE_DECLSPEC ESRRC esrSetAttrs(ESREngineHandle hEngine, ESRAttrs *pESRAttrs);
VVE_DECLSPEC ESRRC esrRegisterErrorCB(ESREngineHandle hEngine, ESRErrorCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrUnregisterErrorCB(ESREngineHandle hEngine, ESRErrorCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRegisterAudioCB(ESREngineHandle hEngine, ESRAudioCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrUnregisterAudioCB(ESREngineHandle hEngine, ESRAudioCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrCompareBaseforms(ESREngineHandle hEngine, ESRBaseformResult *pBaseform1, int iLen1, ESRBaseformResult *pBaseform2, int iLen2, ESRBaseformCompareInfo *pInfo);
VVE_DECLSPEC ESRRC esrQueryEngineInfo(ESREngineHandle hEngine, ESREngineInfo *pInfo);

VVE_DECLSPEC ESRRC esrRecoRegisterErrorCB(ESREngineHandle hEngine, ESRRecoErrorCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterErrorCB(ESREngineHandle hEngine, ESRRecoErrorCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoRegisterStateChangeCB(ESREngineHandle hEngine, ESRRecoStateChangeCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterStateChangeCB(ESREngineHandle hEngine, ESRRecoStateChangeCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoRegisterFrameStatsCB(ESREngineHandle hEngine, ESRRecoFrameStatsCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterFrameStatsCB(ESREngineHandle hEngine, ESRRecoFrameStatsCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoSpeechStateQueryFrame ( ESREngineHandle hEngine, int * piFrame );
VVE_DECLSPEC ESRRC esrRecoRegisterVocab(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, ESRRecoResultCB pfnRecoCallback, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterVocab(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoEnableVocab(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet, ESRVocabId VocabId);
VVE_DECLSPEC ESRRC esrRecoDisableVocab(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet, ESRVocabId VocabId);
VVE_DECLSPEC ESRRC esrRecoQueryVocabIds(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet, ESRVocabId *pVocabIds, int *piNumElements);
VVE_DECLSPEC ESRRC esrRecoQueryState(ESREngineHandle hEngine, ESRRecoStates *pState);
VVE_DECLSPEC ESRRC esrRecoInitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoUninitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoSuspend(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoResume(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoStartListening(ESREngineHandle hEngine, ESRAudioCBMode AudioCBMode);
VVE_DECLSPEC ESRRC esrRecoStopListening(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoQueryResultCount(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int *piNumAcceptedResults, int *piNumRejectedResults);
VVE_DECLSPEC ESRRC esrRecoQueryResultAnnotations(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRVocabSetHandle *phVocabSet, ESRVocabId *pVocabId, ESRAnnotationInfo *piAnnots, int *piLen, ESRRecoResultFlag *pResultFlag, int *piScore);
VVE_DECLSPEC ESRRC esrRecoQueryResultInfo(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRVocabSetHandle *phVocabSet, ESRVocabId *pVocabId, ESRResultInfo *pWordInfo, int *piArraySize, int *piMemSize, EStringType *pStrType, ESRRecoResultFlag *pResultFlag, int *piScore);
VVE_DECLSPEC ESRRC esrRecoQueryResultText(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRVocabSetHandle *phVocabSet, ESRVocabId *pVocabId, unsigned char *pszPhrase, int *piLen, EStringType *pStrType, ESRRecoResultFlag *pResultFlag, int *piScore); 
VVE_DECLSPEC ESRRC esrRecoQueryResultTranslation(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRVocabSetHandle *phVocabSet, ESRVocabId *pVocabId, unsigned char *pszTranslation, int *piLen, EStringType *pStrType, ESRRecoResultFlag *pResultFlag, int *piScore);
VVE_DECLSPEC ESRRC esrRecoQueryResultRuleParse(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRVocabSetHandle *phVocabSet, ESRVocabId *pVocabId, ESRRuleParse *pRuleParse,int *piArraySize, int *piMemSize, EStringType *pStrType, ESRRecoResultFlag *pResultFlag, int *piScore);
VVE_DECLSPEC ESRRC esrRecoQuerySpeechState(ESREngineHandle hEngine, ESRRecoSpeechStates *pState);
VVE_DECLSPEC ESRRC esrRecoRegisterSpeechStateCB(ESREngineHandle hEngine, ESRRecoSpeechStateCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterSpeechStateCB(ESREngineHandle hEngine, ESRRecoSpeechStateCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoSetPhraseData(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase);
VVE_DECLSPEC ESRRC esrRecoGetPhraseData(ESREngineHandle hEngine, ESRRecoPhraseHandle *phPhrase);
VVE_DECLSPEC ESRRC esrRecoFreePhraseData(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrRecoRegisterNBestResultCB(ESREngineHandle hEngine, ESRRecoNBestResultCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrRecoUnregisterNBestResultCB(ESREngineHandle hEngine, ESRRecoNBestResultCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrRecoFinalizeResult(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum);
VVE_DECLSPEC ESRRC esrRecoQueryResultStats(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iResultNum, ESRResultStats *pResultStats);
VVE_DECLSPEC ESRRC esrRecoSetEOUMode(ESREngineHandle hEngine, ESREOUMode Mode, unsigned long ulTimeout);

VVE_DECLSPEC ESRRC esrAcbfRegisterErrorCB(ESREngineHandle hEngine, ESRAcbfErrorCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrAcbfUnregisterErrorCB(ESREngineHandle hEngine, ESRAcbfErrorCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrAcbfRegisterResultCB(ESREngineHandle hEngine, ESRAcbfResultCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrAcbfUnregisterResultCB(ESREngineHandle hEngine, ESRAcbfResultCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrAcbfRegisterStateChangeCB(ESREngineHandle hEngine, ESRAcbfStateChangeCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrAcbfUnregisterStateChangeCB(ESREngineHandle hEngine, ESRAcbfStateChangeCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrAcbfRegisterFrameStatsCB(ESREngineHandle hEngine, ESRAcbfFrameStatsCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrAcbfUnregisterFrameStatsCB(ESREngineHandle hEngine, ESRAcbfFrameStatsCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrAcbfSpeechStateQueryFrame ( ESREngineHandle hEngine, int * piFrame );
VVE_DECLSPEC ESRRC esrAcbfQueryState(ESREngineHandle hEngine, ESRAcbfStates *pState);
VVE_DECLSPEC ESRRC esrAcbfInitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrAcbfUninitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrAcbfStartListening(ESREngineHandle hEngine, ESRAudioCBMode AudioCBMode);
VVE_DECLSPEC ESRRC esrAcbfStopListening(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrAcbfQuerySpeechState(ESREngineHandle hEngine, ESRAcbfSpeechStates *pState);
VVE_DECLSPEC ESRRC esrAcbfRegisterSpeechStateCB(ESREngineHandle hEngine, ESRAcbfSpeechStateCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrAcbfUnregisterSpeechStateCB(ESREngineHandle hEngine, ESRAcbfSpeechStateCB pFn, void **ppUserData);

VVE_DECLSPEC ESRRC esrVmgrRegisterVocabSet(ESRVocabSet *pVocabSet, ESRVocabSetHandle *phVocabSet);
VVE_DECLSPEC ESRRC esrVmgrUnregisterVocabSet(ESRVocabSetHandle hVocabSet, ESRVocabSet **ppVocabSet);
VVE_DECLSPEC ESRRC esrVmgrQueryRegisteredSets(ESRVocabSetHandle *phVocabSets, int *piNumElements);
VVE_DECLSPEC ESRRC esrVmgrAddBaseformToList(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRBaseformInfo *pInfo, ESRBaseformResult *baseform, int iLen);
VVE_DECLSPEC ESRRC esrVmgrRemoveBaseformFromList(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRBaseformResult *baseform, int iLen);
VVE_DECLSPEC ESRRC esrVmgrCommitBaseformChanges(void);
VVE_DECLSPEC ESRRC esrVmgrGetListFromSet(ESRVocabSetHandle hVocabSet, ESRExtListID listID, void *buffer, int *pLen);
VVE_DECLSPEC ESRRC esrVmgrPutListInSet(ESRVocabSetHandle hVocabSet, ESRExtListID listID, void *buffer, int length);
VVE_DECLSPEC ESRRC esrVmgrQueryListIds(ESRVocabSetHandle hVocabSet, ESRExtListID *pListIds, int *piNumElements);
VVE_DECLSPEC ESRRC esrVmgrQueryBaseformInList(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRBaseformInfo *pInfo, ESRBaseformResult *baseform, int iLen);
VVE_DECLSPEC ESRRC esrVmgrCheckBaseform(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRBaseformResult *pBaseform, int iLen, ESRBaseformInfo *pInfo);
VVE_DECLSPEC ESRRC esrVmgrGetListAttrs(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRListAttrs *pAttrs);
VVE_DECLSPEC ESRRC esrVmgrSetListAttrs(ESRVocabSetHandle hVocabSet, ESRExtListID listID, ESRListAttrs *pAttrs);
VVE_DECLSPEC ESRRC esrVmgrGetVocabNameFromId(ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, unsigned char *pszName, int *piSize, EStringType *pStrType);
VVE_DECLSPEC ESRRC esrVmgrGetVocabIdFromName(ESRVocabSetHandle hVocabSet, unsigned char *pszName, EStringType StrType, ESRVocabId *pVocabId);

VVE_DECLSPEC ESRRC esrEnrlRegisterErrorCB(ESREngineHandle hEngine, ESREnrlErrorCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrEnrlUnregisterErrorCB(ESREngineHandle hEngine, ESREnrlErrorCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrEnrlRegisterResultCB(ESREngineHandle hEngine, ESREnrlResultCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrEnrlUnregisterResultCB(ESREngineHandle hEngine, ESREnrlResultCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrEnrlRegisterStateChangeCB(ESREngineHandle hEngine, ESREnrlStateChangeCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrEnrlUnregisterStateChangeCB(ESREngineHandle hEngine, ESREnrlStateChangeCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrEnrlRegisterFrameStatsCB(ESREngineHandle hEngine, ESREnrlFrameStatsCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrEnrlUnregisterFrameStatsCB(ESREngineHandle hEngine, ESREnrlFrameStatsCB pFn, void **ppUserData);
VVE_DECLSPEC ESRRC esrEnrlSpeechStateQueryFrame ( ESREngineHandle hEngine, int * piFrame );
VVE_DECLSPEC ESRRC esrEnrlQueryState(ESREngineHandle hEngine, ESREnrlStates *pState);
VVE_DECLSPEC ESRRC esrEnrlInitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrEnrlUninitialize(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrEnrlStartListening(ESREngineHandle hEngine, ESRAudioCBMode AudioCBMode);
VVE_DECLSPEC ESRRC esrEnrlStopListening(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrEnrlQuerySpeechState(ESREngineHandle hEngine, ESREnrlSpeechStates *pState);
VVE_DECLSPEC ESRRC esrEnrlRegisterSpeechStateCB(ESREngineHandle hEngine, ESREnrlSpeechStateCB pFn, void *pUserData);
VVE_DECLSPEC ESRRC esrEnrlUnregisterSpeechStateCB(ESREngineHandle hEngine, ESREnrlSpeechStateCB pFn, void **ppUserData);

VVE_DECLSPEC ESRRC esrEnrlRegisterScript(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet);
VVE_DECLSPEC ESRRC esrEnrlUnregisterScript(ESREngineHandle hEngine, ESRVocabSetHandle hVocabSet);
VVE_DECLSPEC ESRRC esrEnrlRedoLastUtterance(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrEnrlGenerateVoiceModel(ESREngineHandle hEngine, ESRVoiceModel *pVoiceModel, int *piSize);
VVE_DECLSPEC ESRRC esrEnrlRestart(ESREngineHandle hEngine);
VVE_DECLSPEC ESRRC esrEnrlQueryCurrentPhraseIndex(ESREngineHandle hEngine, int *piIndex);
VVE_DECLSPEC ESRRC esrEnrlQueryPhraseCount(ESREngineHandle hEngine, int *piCount);
VVE_DECLSPEC ESRRC esrEnrlQueryPhrase(ESREngineHandle hEngine, int iPhraseNum, ESRVocabId *pVocabId, unsigned char *pszPhrase, int *piLen, EStringType *pStrType);

VVE_DECLSPEC ESRRC esrGetVoiceModel(ESREngineHandle hEngine, ESRVoiceModel *pVoiceModel, int *piSize);
VVE_DECLSPEC ESRRC esrSetVoiceModel(ESREngineHandle hEngine, ESRVoiceModel *pVoiceModel);

/* Deprecated defines for compatibility purposes only */
#define ESR_SETTINGS_PCMQUEUESIZE   ESR_ATTRS_PCMQUEUESIZE
#define ESR_SETTINGS_CEPQUEUESIZE   ESR_ATTRS_CEPQUEUESIZE
#define ESR_SETTINGS_RANKQUEUESIZE  ESR_ATTRS_RANKQUEUESIZE
#define ESR_SETTINGS_AUDIOADJRESET  ESR_ATTRS_AUDIOADJRESET
#define ESR_SETTINGS_LISTENINGMODE  ESR_ATTRS_LISTENINGMODE
#define ESR_SETTINGS_REJTHRESHOLD   ESR_ATTRS_REJTHRESHOLD
#define ESR_SETTINGS_SEARCHWIDTH    ESR_ATTRS_SEARCHWIDTH
#define ulValidSettings             ulValidAttrs
typedef ESRAttrsFlags ESRSettingsFlags;
typedef ESRAttrs ESRSettings;
typedef ESRPCMAttrs ESRPCMSettings;
typedef ESRRecoResultFlag ESRRecoResult;
typedef struct _ESRWordInfo
{
	const char *pszSpelling;
	ESRWordInfoType iInfoType;
	int iAnnotation;
	ESRBaseformResult *pBaseform;
	int iBaseformLen;
	void *pUserData;
} ESRWordInfo;
VVE_DECLSPEC ESRRC esrQuerySettings(ESREngineHandle hEngine, ESRSettings *pESRSettings);
VVE_DECLSPEC ESRRC esrSet(ESREngineHandle hEngine, ESRSettings *pESRSettings);
VVE_DECLSPEC ESRRC esrVmgrQueryPhraseInfo(ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, ESRRecoPhraseHandle hPhrase, ESRWordInfo *pWordInfo, int *piArraySize, int *piMemSize, EStringType *pStrType);
VVE_DECLSPEC ESRRC esrVmgrQueryPhraseText(ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, ESRRecoPhraseHandle hPhrase, char *pszPhrase, int *piLen, EStringType *pStrType);
VVE_DECLSPEC ESRRC esrVmgrQueryPhraseAnnotations(ESRVocabSetHandle hVocabSet, ESRRecoPhraseHandle hPhrase, ESRAnnotationInfo *piAnnots, int *piLen);
VVE_DECLSPEC ESRRC esrDiscardPCM(ESREngineHandle hEngine);

#endif


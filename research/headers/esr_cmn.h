/*****************************************************************************/
/*                                                                           */
/* Licensed Materials - Property of IBM                                      */
/* ASSF4ZZ V5.2, AT2T5ZZ V4.3                                                */
/* (C) Copyright IBM Corp. 2004  All Rights Reserved.                        */
/* US Government Users Restricted Rights - Use, duplication or disclosure    */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                    */
/*                                                                           */
/* The following IBM source code is provided to assist you in your           */
/* development.  You may use this code only in accordance with the           */
/* IBM License Agreement accompanying the Licensed Materials.                */
/*                                                                           */
/* This copyright statement may not be removed.                              */
/*****************************************************************************/


#ifndef _ESR_CMN_H_
#define _ESR_CMN_H_

typedef int ESRRC;

typedef unsigned char ESREngineAudioAdjData;
typedef unsigned char ESRVocabSet;

typedef int ESRVocabSetHandle;
typedef unsigned long ESRVocabId;
typedef unsigned long ESRExtListID;

typedef int * ESRRecoPhraseHandle;

typedef unsigned char ESRVoiceModel;

typedef enum
{
	ESR_ERROR_PROCESSED              = 0,
	ESR_ERROR_NOT_PROCESSED          = 1
} ESRErrorCBRc;

typedef int ESRAudioCBRc;

typedef enum {
		ESR_ACBF_SPEECHSTATE_SILENT, 
		ESR_ACBF_SPEECHSTATE_SPEAKING
} ESRAcbfSpeechStates;

typedef enum {
		ESR_RECO_SPEECHSTATE_SILENT, 
		ESR_RECO_SPEECHSTATE_SPEAKING
} ESRRecoSpeechStates;

typedef enum {
	ESR_ENRL_SPEECHSTATE_SILENT, 
	ESR_ENRL_SPEECHSTATE_SPEAKING
} ESREnrlSpeechStates;

typedef enum
{
	ESR_ACBF_STATE_NOTPRESENT        = -1,
	ESR_ACBF_STATE_DISABLED          =  0,
	ESR_ACBF_STATE_IDLE              =  1,
	ESR_ACBF_STATE_PROCESSING        =  2,
	ESR_ACBF_STATE_SUSPENDED         =  3
} ESRAcbfStates;

typedef enum
{
	ESR_RECO_STATE_NOTPRESENT        = -1,
	ESR_RECO_STATE_DISABLED          =  0,
	ESR_RECO_STATE_IDLE              =  1,
	ESR_RECO_STATE_PROCESSING        =  2,
	ESR_RECO_STATE_SUSPENDED         =  3
} ESRRecoStates;

typedef enum
{
	ESR_ENRL_STATE_NOTPRESENT        = -1,
	ESR_ENRL_STATE_DISABLED          =  0,
	ESR_ENRL_STATE_IDLE              =  1,
	ESR_ENRL_STATE_PROCESSING        =  2,
	ESR_ENRL_STATE_SUSPENDED         =  3
} ESREnrlStates;

typedef struct _ESRPCMAttrs
{
	unsigned long ulSamplingRate;
	unsigned long ulBitsPerSample;
	unsigned long ulNumChannels;
} ESRPCMAttrs;

typedef enum
{
	ESR_RECO_RESULT_ACCEPTED = 0,
	ESR_RECO_RESULT_REJECTED = 1
} ESRRecoResultFlag;

typedef enum {
	ESR_SDTRANSITION_TYPE_SILENCE_2_SPEECH	= 0,
	ESR_SDTRANSITION_TYPE_SPEECH_2_SILENCE	= 1,
	ESR_SDTRANSITION_TYPE_NONE				= 2
} ESRSpeechDetectorTransition;

typedef enum 
{
	ESR_FRAME_SPEECHSTATE_SPEECH,
	ESR_FRAME_SPEECHSTATE_SILENCE
} ESRFrameSpeechState;

typedef struct _ESRSpeechDetectorFrameStatsStruct
{
	int							iMaxInBuffer;
	int							iMinInBuffer;
	int							iEnergy;
	ESRFrameSpeechState			SpeechState;
	int							iFrame;
	ESRSpeechDetectorTransition	SDTransition;
	int							iFrameSDTransition;
	unsigned long               ulRawEnergy;
} ESRSpeechDetectorFrameStatsStruct;

typedef enum {
	ESR_DWMODE_WORDPENALTY = 0,
	ESR_DWMODE_UNIFORMLIKELYHOOD = 1,
	ESR_DWMODE_WEIGHTS = 2
} ESRDecodingWeightMode;

typedef struct _ESREngineInfo
{
	int iGCVersion;
	int iEngineVersion;
	int iFrequency;  /* 11025, 12000, 16000, 22050, 44100 */
	EVVEndianness Endianness;
	char szAMName[EVV_MAX_AM_NAMELEN + 1];
} ESREngineInfo;

#define ESR_SD_MAX_IN_BUFFER		0x00000001
#define ESR_SD_MIN_IN_BUFFER		0x00000002
#define ESR_SD_ENERGY				0x00000004
#define ESR_SD_SPEECH_STATE			0x00000010
#define ESR_SD_FRAME_NUM			0x00000020
#define ESR_SD_TRANSITION			0x00000040
#define ESR_SD_TRANSITION_FRAME_NUM	0x00000100 



typedef unsigned int ESREngineHandle ;
typedef unsigned char ESRBaseformResult;

typedef ESRErrorCBRc (*ESRErrorCB)(ESREngineHandle hEngine, long lErrNum, void *pUserData);
typedef ESRAudioCBRc (*ESRAudioCB)(ESREngineHandle hEngine, int iAudioFlag, void *pUserData);
typedef void (*ESRRecoStateChangeCB)(ESREngineHandle hEngine, ESRRecoStates PrevState, ESRRecoStates NewState, void *pUserData);
typedef void (*ESRAcbfStateChangeCB)(ESREngineHandle hEngine, ESRAcbfStates PrevState, ESRAcbfStates NewState, void *pUserData);
typedef int (*ESRRecoResultCB)(ESREngineHandle hEngine, ESRRecoResultFlag ResultFlag, ESRVocabSetHandle hVocabSet, ESRVocabId VocabId, ESRRecoPhraseHandle hPhrase, void *pUserData);
typedef int (*ESRAcbfResultCB)(ESREngineHandle hEngine, ESRBaseformResult *baseform, int iLen, void *pUserData);
typedef int (*ESRRecoNBestResultCB)(ESREngineHandle hEngine, ESRRecoPhraseHandle hPhrase, int iNumAcceptedResults, int iNumRejectedResults, void *pUserData);
typedef void (*ESRRecoSpeechStateCB)(ESREngineHandle hEngine, ESRRecoSpeechStates prevState, ESRRecoSpeechStates newState, void *pUserData);
typedef void (*ESRAcbfSpeechStateCB)(ESREngineHandle hEngine, ESRAcbfSpeechStates prevState, ESRAcbfSpeechStates newState, void *pUserData);
typedef ESRErrorCBRc (*ESRRecoErrorCB)(ESREngineHandle hEngine, long lErrNum, void *pUserData);
typedef ESRErrorCBRc (*ESRAcbfErrorCB)(ESREngineHandle hEngine, long lErrNum, void *pUserData);

typedef void (*ESRRecoFrameStatsCB)( ESREngineHandle hEngine, ESRSpeechDetectorFrameStatsStruct * pesrSDFrameStats, void * pUserData );
typedef void (*ESRAcbfFrameStatsCB)( ESREngineHandle hEngine, ESRSpeechDetectorFrameStatsStruct * pesrSDFrameStats, void * pUserData );

typedef void (*ESREnrlStateChangeCB)(ESREngineHandle hEngine, ESREnrlStates PrevState, ESREnrlStates NewState, void *pUserData);
typedef int (*ESREnrlResultCB)(ESREngineHandle hEngine, int iPhraseIndex, ESRVocabId VocabId, void *pUserData);
typedef void (*ESREnrlSpeechStateCB)(ESREngineHandle hEngine, ESREnrlSpeechStates prevState, ESREnrlSpeechStates newState, void *pUserData);
typedef ESRErrorCBRc (*ESREnrlErrorCB)(ESREngineHandle hEngine, long lErrNum, void *pUserData);
typedef void (*ESREnrlFrameStatsCB)( ESREngineHandle hEngine, ESRSpeechDetectorFrameStatsStruct * pesrSDFrameStats, void * pUserData );


typedef struct _ESRBaseformInfo
{
	char *pszSpelling;
	EStringType strType;
	void *pUserData;
} ESRBaseformInfo;

typedef enum
{
	ESR_ANNOTATION 		= 0,
	ESR_NO_ANNOTATION		= 1,
	ESR_USER_DATA			= 2
} ESRWordInfoType;

typedef struct _ESRHomonymInfo
{
	const unsigned char *pszSpelling; 
	ESRWordInfoType iInfoType;
	int iAnnotation;
	void *pUserData;
} ESRHomonymInfo;

typedef struct _ESRAnnotationInfo
{
	ESRWordInfoType iInfoType;
	int iAnnotation;
	void *pUserData;
} ESRAnnotationInfo;

typedef struct _ESRResultInfo
{
	ESRHomonymInfo *pHomonyms;
	int iNumHomonyms;
	ESRBaseformResult *pBaseform;
	int iBaseformLen;
} ESRResultInfo;

typedef struct _ESRRuleParse
{
	unsigned char *pszRuleNT;
	int      iDerivationStart;
	int      iDerivationLength;
	unsigned char *pszTag;
	unsigned char *pszPreviousNT;
	int      ipszParentRuleNT;
} ESRRuleParse;

typedef unsigned long ESRBlockFlags;

#define ESR_BLOCK_DSP                  0x00000001
#define ESR_BLOCK_SD                   0x00000002
#define ESR_BLOCK_LBL                  0x00000004
#define ESR_BLOCK_DCD                  0x00000008
#define ESR_BLOCK_ABS                  0x00000010
#define ESR_BLOCK_MN                   0x00000020
#define ESR_BLOCK_PQ                   0x00000040
#define ESR_BLOCK_CQ                   0x00000080
#define ESR_BLOCK_RQ                   0x00000100
#define ESR_BLOCK_TR                   0x00000200
#define ESR_BLOCK_PCH                  0x00000400
#define ESR_BLOCK_LBE                  0x00000800
#define ESR_BLOCK_ATA                  0x00001000
#define ESR_BLOCK_PS                   0x00002000

#define ESR_BLOCK_ALL                  0x00003FFF

typedef struct _ESRInitializationData
{
	ESRBlockFlags ulValidBlocks;

	void *pDspBlock;
	void *pSdBlock;
	void *pLblBlock;
	void *pDcdBlock;
	void *pAbsBlock;
	void *pMnBlock;
	void *pPqBlock;
	void *pCqBlock;
	void *pRqBlock;
	void *pTrBlock;
	void *pPchBlock;
	void *pLbeBlock;
	void *pAtaBlock;
	void *pPsBlock;

} ESRInitializationData;

typedef enum
{
	ESR_RECO_LMODE_PUSHTOTALK     = 0x0008,
	ESR_RECO_LMODE_ALWAYSLISTEN   = 0x0009,
	ESR_RECO_LMODE_PUSHTOACTIVATE = 0x000A,
	ESR_ACBF_LMODE_PUSHTOTALK     = 0x0080,
	ESR_ACBF_LMODE_ALWAYSLISTEN   = 0x0090,
	ESR_ACBF_LMODE_PUSHTOACTIVATE = 0x00A0,
	ESR_ENRL_LMODE_PUSHTOTALK     = 0x0800,
	ESR_ENRL_LMODE_ALWAYSLISTEN   = 0x0900,
	ESR_ENRL_LMODE_PUSHTOACTIVATE = 0x0A00,
} ESRListeningMode;

#define ESR_LMODE_PUSHTOTALK     ((ESRListeningMode)(ESR_RECO_LMODE_PUSHTOTALK | ESR_ACBF_LMODE_PUSHTOTALK | ESR_ENRL_LMODE_PUSHTOTALK))
#define ESR_LMODE_ALWAYSLISTEN   ((ESRListeningMode)(ESR_RECO_LMODE_ALWAYSLISTEN | ESR_ACBF_LMODE_PUSHTOTALK | ESR_ENRL_LMODE_PUSHTOTALK))
#define ESR_LMODE_PUSHTOACTIVATE ((ESRListeningMode)(ESR_RECO_LMODE_PUSHTOACTIVATE | ESR_ACBF_LMODE_PUSHTOTALK | ESR_ENRL_LMODE_PUSHTOTALK))

typedef enum
{
	ESR_AUDIO_CB_ACTIVE = 0,
	ESR_AUDIO_CB_INACTIVE = 1
} ESRAudioCBMode;

typedef enum
{
	ESR_AUDIO_START	 =  0,
	ESR_AUDIO_STOP	 =  1
} ESRAudioCBState;

typedef enum {
	ESR_AAMODE_NEVER = 0,
	ESR_AAMODE_BLOCK_NEXT = 1,
	ESR_AAMODE_BLOCK_ALWAYS = 2
} ESRAudioAdjMode;

typedef enum {
	ESR_EOUMODE_MODE1 = 0,
	ESR_EOUMODE_MODE2 = 1,
	ESR_EOUMODE_MODE3 = 2
} ESREOUMode;

typedef enum {
	ESR_RTADAPTMODE_NONE = 0,
	ESR_RTADAPTMODE_UNSUPERVISED = 1
} ESRRuntimeAdaptationMode;

#define ESR_AABUFSIZE_AUTOMATIC -1

typedef unsigned long ESRAttrsFlags;

#define ESR_ATTRS_PCMQUEUESIZE                  0x00000001
#define ESR_ATTRS_CEPQUEUESIZE                  0x00000002
#define ESR_ATTRS_RANKQUEUESIZE                 0x00000004
#define ESR_ATTRS_AUDIOADJRESET                 0x00000010
#define ESR_ATTRS_LISTENINGMODE                 0x00000020
#define ESR_ATTRS_REJTHRESHOLD                  0x00000040
#define ESR_ATTRS_SEARCHWIDTH                   0x00000080
#define ESR_ATTRS_MAXNBESTRESULTS               0x00000100
#define ESR_ATTRS_MINSPEECHLENGTH               0x00000200
#define ESR_ATTRS_MINSILENCELENGTH              0x00000400
#define ESR_ATTRS_MINEOUSILENCELENGTH           0x00000800
#define ESR_ATTRS_AUDIOADJBUFFERSIZE            0x00001000
#define ESR_ATTRS_AUDIOADJMODE                  0x00002000
#define ESR_ATTRS_CONFUSABILITYTHRESHOLD        0x00004000
#define ESR_ATTRS_FIRSTFRAMEREPEAT              0x00008000
#define ESR_ATTRS_RUNTIMEADAPTATIONMODE         0x00010000
#define ESR_ATTRS_MAXACBFBASEFORMS              0x00020000
#define ESR_ATTRS_DECODINGWEIGHTMODE            0x00040000

#define ESR_ATTRS_ALL                           0x0007FFFF


typedef struct _ESRAttrs
{
	ESRAttrsFlags ulValidAttrs;

	unsigned long ulPCMQueueSize;
	unsigned long ulCepQueueSize;
	unsigned long ulRankQueueSize;
	unsigned long ulAudioAdjResetPerUtt;
	ESRListeningMode ulListeningMode;
	unsigned long ulRejThreshold;
	unsigned long ulSearchWidth;
	unsigned long ulMaxNBestResults; 
	unsigned long ulMinSpeechLength;
	unsigned long ulMinSilenceLength;
	unsigned long ulMinEOUSilenceLength;
	unsigned long ulAudioAdjBufferSize;
	ESRAudioAdjMode ulAudioAdjMode;
	unsigned long ulConfusabilityThreshold;
	unsigned long ulFirstFrameRepeat;
	ESRRuntimeAdaptationMode ulRuntimeAdaptationMode;
	unsigned long ulMaxAcbfBaseforms;
	ESRDecodingWeightMode ulDecodingWeightMode;
} ESRAttrs;

#define ESR_LISTATTRS_BASEFORMSUSED                  0x00000001


typedef unsigned long ESRListAttrFlags;

typedef struct _ESRListAttrs
{
	ESRListAttrFlags ulValidAttrs;

	unsigned long ulBaseformsUsed;
} ESRListAttrs;

typedef struct _ESRResultStats
{
	unsigned int iDuration;
	unsigned int iRejectionScore;
	unsigned int iScore;
	unsigned int iReserved1;
} ESRResultStats;

typedef struct _ESRBaseformCompareInfo
{
	unsigned long ulIsConfusable;
	unsigned long ulConfusability;
	unsigned long ulReserved1;
} ESRBaseformCompareInfo;

#define ESR_LISTATTRS_BASEFORMSUSED_ALL              ((unsigned long)-1)

#define ESR_BASE_OFFSET                  0x60000

#define ESR_RC_OK                                  0

#define ESR_RC_AUDIO_CB_STILL_REG                               (ESR_BASE_OFFSET + 0x8000)
#define ESR_RC_ERROR_CB_STILL_REG                               (ESR_BASE_OFFSET + 0x8001)
#define ESR_RC_IDLE                                             (ESR_BASE_OFFSET + 0x8002)
#define ESR_RC_INVALID_AUDADJ_DATA                              (ESR_BASE_OFFSET + 0x8003)
#define ESR_RC_INVALID_ENG_HANDLE                               (ESR_BASE_OFFSET + 0x8004)
#define ESR_RC_INVALID_ENV_DATA                                 (ESR_BASE_OFFSET + 0x8005)
#define ESR_RC_INVALID_EXT_LISTID                               (ESR_BASE_OFFSET + 0x8006)
#define ESR_RC_INVALID_EXTERNAL_LIST                            (ESR_BASE_OFFSET + 0x8007)
#define ESR_RC_INVALID_PCM_SIZE                                 (ESR_BASE_OFFSET + 0x8008)
#define ESR_RC_INVALID_PHRASE                                   (ESR_BASE_OFFSET + 0x8009)
#define ESR_RC_INVALID_VOCABSET                                 (ESR_BASE_OFFSET + 0x800A)
#define ESR_RC_LIST_BUSY                                        (ESR_BASE_OFFSET + 0x800B)
#define ESR_RC_NBEST_CB_STILL_REG                               (ESR_BASE_OFFSET + 0x800C)
#define ESR_RC_NO_NBEST_CALLBACK                                (ESR_BASE_OFFSET + 0x800D)
#define ESR_RC_PCM_PUT_OVERRUN                                  (ESR_BASE_OFFSET + 0x800E)
#define ESR_RC_PHRASEDATA_NOT_FREED                             (ESR_BASE_OFFSET + 0x800F)
#define ESR_RC_PHRASEDATA_NOT_SAVED                             (ESR_BASE_OFFSET + 0x8010)
#define ESR_RC_RECO_NOT_SUSPENDED                               (ESR_BASE_OFFSET + 0x8011)
#define ESR_RC_RECO_SUSPENDED                                   (ESR_BASE_OFFSET + 0x8012)
#define ESR_RC_RESULT_CB_STILL_REG                              (ESR_BASE_OFFSET + 0x8013)
#define ESR_RC_SETS_STILL_REG                                   (ESR_BASE_OFFSET + 0x8014)
#define ESR_RC_SPEECH_CB_STILL_REG                              (ESR_BASE_OFFSET + 0x8015)
#define ESR_RC_STATE_CB_STILL_REG                               (ESR_BASE_OFFSET + 0x8016)
#define ESR_RC_TOO_MANY_ENGINES                                 (ESR_BASE_OFFSET + 0x8017)
#define ESR_RC_TOO_MANY_WORDS                                   (ESR_BASE_OFFSET + 0x8018)
#define ESR_RC_UNABLE_TO_FIND_MATCH                             (ESR_BASE_OFFSET + 0x8019)
#define ESR_RC_VOCAB_NOT_REGISTERED                             (ESR_BASE_OFFSET + 0x801A)
#define ESR_RC_VOCABS_STILL_REG                                 (ESR_BASE_OFFSET + 0x801B)

#define ESR_RC_CANNOT_SET_PCMQSIZE                              (ESR_BASE_OFFSET + 0x8020)
#define ESR_RC_CANNOT_SET_CEPQSIZE                              (ESR_BASE_OFFSET + 0x8021)
#define ESR_RC_CANNOT_SET_RNKQSIZE                              (ESR_BASE_OFFSET + 0x8022)
#define ESR_RC_CANNOT_SET_SIGRESET                              (ESR_BASE_OFFSET + 0x8023)
#define ESR_RC_CANNOT_SET_AUDADJ                                (ESR_BASE_OFFSET + 0x8024)
#define ESR_RC_CANNOT_SET_LMODE                                 (ESR_BASE_OFFSET + 0x8025)
#define ESR_RC_CANNOT_SET_REJTHRES                              (ESR_BASE_OFFSET + 0x8026)
#define ESR_RC_INVALID_REJTHRES                                 (ESR_BASE_OFFSET + 0x8027)
#define ESR_RC_INVALID_PCMQSIZE                                 (ESR_BASE_OFFSET + 0x8028)
#define ESR_RC_INVALID_CEPQSIZE                                 (ESR_BASE_OFFSET + 0x8029)
#define ESR_RC_INVALID_RANKQSIZE                                (ESR_BASE_OFFSET + 0x802A)
#define ESR_RC_INVALID_SIGRESET                                 (ESR_BASE_OFFSET + 0x802B)
#define ESR_RC_INVALID_AUDADJRESET                              (ESR_BASE_OFFSET + 0x802C)
#define ESR_RC_INVALID_LMODE                                    (ESR_BASE_OFFSET + 0x802D)
#define ESR_RC_ALREADY_LISTENING                                (ESR_BASE_OFFSET + 0x802E)
#define ESR_RC_NOT_LISTENING                                    (ESR_BASE_OFFSET + 0x802F)
#define ESR_RC_INVALID_SERVICE                                  (ESR_BASE_OFFSET + 0x8030)
#define ESR_RC_INVALID_CB_TYPE                                  (ESR_BASE_OFFSET + 0x8031)
#define ESR_RC_NO_TRANSLATION_DATA                              (ESR_BASE_OFFSET + 0x8032)
#define ESR_RC_NO_RULEPARSE_DATA                                (ESR_BASE_OFFSET + 0x8033)
#define ESR_RC_FRAMESTATS_CB_STILL_REG                          (ESR_BASE_OFFSET + 0x8034)
#define ESR_RC_INVALID_VOCAB_NAME                               (ESR_BASE_OFFSET + 0x8035)
#define ESR_RC_NO_MATCHING_GRAMMAR                              (ESR_BASE_OFFSET + 0x8036)
#define ESR_RC_DUPLICATE_VOCAB_NAME                             (ESR_BASE_OFFSET + 0x8037)
#define ESR_RC_PCM_Q_OVERRUN                                    (ESR_BASE_OFFSET + 0x8038)
#define ESR_RC_CEP_Q_OVERRUN                                    (ESR_BASE_OFFSET + 0x8039)
#define ESR_RC_RANK_Q_OVERRUN                                   (ESR_BASE_OFFSET + 0x803A)
#define ESR_RC_EDECO_INIT_FAILED                                (ESR_BASE_OFFSET + 0x803B)
#define ESR_RC_LEAFPROB_ACCESS                                  (ESR_BASE_OFFSET + 0x803C)
#define ESR_RC_NO_VOCABS_ENABLED                                (ESR_BASE_OFFSET + 0x803D)
#define ESR_RC_NO_VOCABS_DEFINED                                (ESR_BASE_OFFSET + 0x803E)
#define ESR_RC_SIGPROC_ERROR                                    (ESR_BASE_OFFSET + 0x803F)
#define ESR_RC_INVALID_VOCAB_FORMAT                             (ESR_BASE_OFFSET + 0x8040)
#define ESR_RC_ALREADY_FOCUSED                                  (ESR_BASE_OFFSET + 0x8041)
#define ESR_RC_NOT_FOCUSED                                      (ESR_BASE_OFFSET + 0x8042)
#define ESR_RC_SCRIPT_NOT_REGISTERED                            (ESR_BASE_OFFSET + 0x8043)
#define ESR_RC_VOICEMODEL_NOT_SET                               (ESR_BASE_OFFSET + 0x8044)
#define ESR_RC_AUDIO_ERROR                                      (ESR_BASE_OFFSET + 0x8045)
#define ESR_RC_INVALID_EOUMODE                                  (ESR_BASE_OFFSET + 0x8046)
#define ESR_RC_INVALID_TIMEOUT_VALUE                            (ESR_BASE_OFFSET + 0x8047)

#define ESR_RC_BAD_ENG_MATCH                                    (ESR_BASE_OFFSET + 0xC000)
#define ESR_RC_INVALID_CONTEXT                                  (ESR_BASE_OFFSET + 0xC001)

#define ESR_RC_ALREADY_INITIALIZED                              (ESR_BASE_OFFSET + EVV_RC_ALREADY_INITIALIZED)
#define ESR_RC_ALREADY_REGISTERED                               (ESR_BASE_OFFSET + EVV_RC_ALREADY_REGISTERED)
#define ESR_RC_BASEFORM_NOT_FOUND                               (ESR_BASE_OFFSET + EVV_RC_BASEFORM_NOT_FOUND)
#define ESR_RC_BASEFORM_TOO_LONG                                (ESR_BASE_OFFSET + EVV_RC_BASEFORM_TOO_LONG)
#define ESR_RC_BUFFER_TOO_SMALL                                 (ESR_BASE_OFFSET + EVV_RC_BUFFER_TOO_SMALL)
#define ESR_RC_DUPLICATE_BASEFORM                               (ESR_BASE_OFFSET + EVV_RC_DUPLICATE_BASEFORM)
#define ESR_RC_ENGINE_NOT_STARTED                               (ESR_BASE_OFFSET + EVV_RC_ENGINE_NOT_STARTED)
#define ESR_RC_ENGINE_STILL_ACTIVE                              (ESR_BASE_OFFSET + EVV_RC_ENGINE_STILL_ACTIVE)
#define ESR_RC_FUNCTION_UNAVAILABLE                             (ESR_BASE_OFFSET + EVV_RC_FUNCTION_UNAVAILABLE)
#define ESR_RC_INVALID_BASEFORM                                 (ESR_BASE_OFFSET + EVV_RC_INVALID_BASEFORM)
#define ESR_RC_INVALID_FLAG                                     (ESR_BASE_OFFSET + EVV_RC_INVALID_FLAG)
#define ESR_RC_INVALID_FORMAT                                   (ESR_BASE_OFFSET + EVV_RC_INVALID_FORMAT)
#define ESR_RC_INVALID_HANDLE                                   (ESR_BASE_OFFSET + EVV_RC_INVALID_HANDLE)
#define ESR_RC_INVALID_IMAGE                                    (ESR_BASE_OFFSET + EVV_RC_INVALID_IMAGE)
#define ESR_RC_INVALID_SAMP_RATE                                (ESR_BASE_OFFSET + EVV_RC_INVALID_SAMP_RATE)
#define ESR_RC_INVALID_SAMPLESIZE                               (ESR_BASE_OFFSET + EVV_RC_INVALID_SAMPLESIZE)
#define ESR_RC_INVALID_STATE                                    (ESR_BASE_OFFSET + EVV_RC_INVALID_STATE)
#define ESR_RC_INVALID_STRING_TYPE                              (ESR_BASE_OFFSET + EVV_RC_INVALID_STRING_TYPE)
#define ESR_RC_INVALID_VALUE                                    (ESR_BASE_OFFSET + EVV_RC_INVALID_VALUE)
#define ESR_RC_INVALID_VOCABID                                  (ESR_BASE_OFFSET + EVV_RC_INVALID_VOCABID)
#define ESR_RC_MEM_ALLOC_ERROR                                  (ESR_BASE_OFFSET + EVV_RC_MEM_ALLOC_ERROR)
#define ESR_RC_NOT_INITIALIZED                                  (ESR_BASE_OFFSET + EVV_RC_NOT_INITIALIZED)
#define ESR_RC_NOT_REGISTERED                                   (ESR_BASE_OFFSET + EVV_RC_NOT_REGISTERED)
#define ESR_RC_RESULT_OUT_OF_RANGE                              (ESR_BASE_OFFSET + EVV_RC_RESULT_OUT_OF_RANGE)
#define ESR_RC_SERVICE_INITIALIZED                              (ESR_BASE_OFFSET + EVV_RC_SERVICE_INITIALIZED)
#define ESR_RC_INVALID_NUMCHANNELS                              (ESR_BASE_OFFSET + EVV_RC_INVALID_NUM_CHANNELS)
#define ESR_RC_SYSTEM_ERROR                                     (ESR_BASE_OFFSET + EVV_RC_SYSTEM_ERROR)

#define ESR_RC_INTERNAL_ERROR                                   (ESR_BASE_OFFSET + EVV_RC_INTERNAL_ERROR)
#define ESR_RC_INVALID_CALLBACK                                 (ESR_BASE_OFFSET + EVV_RC_INVALID_CALLBACK)
#define ESR_RC_INVALID_DATA                                     (ESR_BASE_OFFSET + EVV_RC_INVALID_DATA)
#define ESR_RC_UNABLE_TO_STARTTASK                              (ESR_BASE_OFFSET + EVV_RC_UNABLE_TO_STARTTASK)
#define ESR_RC_UNSUPPORTED_LANGUAGE                             (ESR_BASE_OFFSET + EVV_RC_UNSUPPORTED_LANGUAGE)

#endif


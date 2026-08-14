/*=========================================================================*/
/*                                                                         */
/* eci.h                                                                   */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1 V2.3                                                       */
/* (C) Copyright IBM Corp. 1999, 2003  All Rights Reserved.                */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement for the IBM Embedded ViaVoice, Multiplatform      */
/* Edition.                                                                */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*                                                                         */
/*=========================================================================*/



#ifndef __ECI_H
#define __ECI_H

#ifndef __ECI_DEFINE_BOOLEAN
#define __ECI_DEFINE_BOOLEAN
typedef int Boolean;
#endif

#define ECITrue		1
#define ECIFalse	0


#ifndef ECI_TCHAR_DEFINED
typedef char ECI_TCHAR;
#define ECI_TCHAR_DEFINED
#endif

#ifndef ECIFNDECLARE
        typedef signed long ECIint32;
#ifdef _MSC_VER
        #ifdef _WIN32_WCE
                typedef char ECIsystemChar;
                #define ECIFNDECLARE __stdcall
        #elif defined _WIN32
                #include <tchar.h>
                typedef ECI_TCHAR ECIsystemChar;
                #define ECIFNDECLARE __stdcall
        #endif

#elif defined __TURBOC__
        #ifdef __WIN32__
                #include <tchar.h>
                #define ECIFNDECLARE __stdcall
                typedef ECI_TCHAR ECIsystemChar;
        #endif
#else
        #define ECIFNDECLARE
        typedef char ECIsystemChar;
#endif
#endif

#ifndef NULL_ECI_HAND
 #define NULL_ECI_HAND  0
#endif

#define ECI_PRESET_VOICES  8
#define ECI_USER_DEFINED_VOICES  8

#define ECI_VOICE_NAME_LENGTH  30

#define ECI_NOERROR             0x00000000
#define ECI_SYSTEMERROR         0x00000001
#define ECI_MEMORYERROR         0x00000002
#define ECI_MODULELOADERROR     0x00000004
#define ECI_DELTAERROR          0x00000008
#define ECI_SYNTHERROR          0x00000010
#define ECI_DEVICEERROR         0x00000020
#define ECI_DICTERROR           0x00000040
#define ECI_PARAMETERERROR      0x00000080
#define ECI_SYNTHESIZINGERROR   0x00000100
#define ECI_DEVICEBUSY          0x00000200
#define ECI_SYNTHESISPAUSED     0x00000400
#define ECI_REENTRANTCALL       0x00000800
#define ECI_ROMANIZERERROR      0x00001000
#define ECI_SYNTHESIZING        0x00002000

#define eciPhonemeLength (4)

#ifdef __cplusplus
extern "C" {
#endif

typedef void* ECIHand;

typedef const void* ECIInputText;

typedef enum {
    eciSynthMode,
    eciInputType,
    eciTextMode,
    eciDictionary,
    eciSampleRate = 5,
    eciWantPhonemeIndices = 7,
    eciRealWorldUnits,
    eciLanguageDialect,
    eciNumberMode,
    eciWantWordIndex = 12,
    eciNumDeviceBlocks,
    eciSizeDeviceBlocks,
    eciNumPrerollDeviceBlocks,
    eciSizePrerollDeviceBlocks,
    eciNumParams
} ECIParam;

typedef enum {
    eciGender,
    eciHeadSize,
    eciPitchBaseline,
    eciPitchFluctuation,
    eciRoughness,
    eciBreathiness,
    eciSpeed,
    eciVolume,
    eciNumVoiceParams
} ECIVoiceParam;

typedef enum
{
  DictNoError,
  DictFileNotFound,
  DictOutOfMemory,
  DictInternalError,
  DictNoEntry,
  DictErrLookUpKey,
  DictAccessError,
  DictInvalidVolume
} ECIDictError;

typedef enum {
	VoiceNoError,
	VoiceSystemError,
	VoiceNotRegisteredError,
	VoiceInvalidFileFormatError
} ECIVoiceError;

typedef void* ECIDictHand;
#define NULL_DICT_HAND 0

typedef enum
{
    eciMainDict    = 0,
    eciRootDict    = 1,
    eciAbbvDict    = 2,
    eciMainDictExt = 3
} ECIDictVolume;

typedef enum
{
    NODEFINEDCODESET                = 0x00000000,
    eciGeneralAmericanEnglish       = 0x00010000,
    eciBritishEnglish               = 0x00010001,
    eciCastilianSpanish             = 0x00020000,
    eciMexicanSpanish               = 0x00020001,
    eciStandardFrench               = 0x00030000,
    eciCanadianFrench               = 0x00030001,
    eciStandardGerman               = 0x00040000,
    eciStandardItalian              = 0x00050000,
    eciMandarinChinese              = 0x00060000,
    eciMandarinChineseGB            = eciMandarinChinese,
    eciMandarinChinesePinYin        = 0x00060100,
    eciMandarinChineseUCS           = 0x00060800,
    eciTaiwaneseMandarin            = 0x00060001,
    eciTaiwaneseMandarinBig5        = eciTaiwaneseMandarin,
    eciTaiwaneseMandarinZhuYin      = 0x00060101,
    eciTaiwaneseMandarinPinYin      = 0x00060201,		
    eciTaiwaneseMandarinUCS         = 0x00060801,
    eciBrazilianPortuguese          = 0x00070000,
    eciStandardJapanese             = 0x00080000,
    eciStandardJapaneseSJIS         = eciStandardJapanese,
    eciStandardJapaneseUCS          = 0x00080800,
    eciStandardFinnish              = 0x00090000,
    eciStandardKorean               = 0x000A0000,
    eciStandardKoreanUHC            = eciStandardKorean,
    eciStandardKoreanUCS            = 0x000A0800,
    eciStandardCantonese            = 0x000B0000,
    eciStandardCantoneseGB          = eciStandardCantonese,
    eciStandardCantoneseUCS         = 0x000B0800,
    eciHongKongCantonese            = 0x000B0001,
    eciHongKongCantoneseBig5        = eciHongKongCantonese,
    eciHongKongCantoneseUCS         = 0x000B0801,
    eciStandardDutch                = 0x000C0000,
    eciStandardNorwegian            = 0x000D0000,
    eciStandardSwedish              = 0x000E0000,
    eciStandardDanish               = 0x000F0000,
    eciStandardReserved             = 0x00100000,
    eciStandardThai                 = 0x00110000,
    eciStandardThaiTIS              = eciStandardThai

}  ECILanguageDialect;

typedef enum
{
    eciUndefinedPOS = 0,
    eciFutsuuMeishi = 1,
    eciKoyuuMeishi,
    eciSahenMeishi,
    eciMingCi
} ECIPartOfSpeech;


#if defined(WIN32)
#pragma pack(push, 1)
#elif defined(UNDER_CE) && (defined(MIPS) || defined(SH3))
#pragma pack(push, 4)
#endif
typedef struct {
   union {
       unsigned char  sz[eciPhonemeLength+1];
       unsigned short wsz[eciPhonemeLength+1];
   } phoneme;
    ECILanguageDialect eciLanguageDialect;
    unsigned char mouthHeight;
    unsigned char mouthWidth;
    unsigned char mouthUpturn;
    unsigned char jawOpen;
    unsigned char teethUpperVisible;
    unsigned char teethLowerVisible;
    unsigned char tonguePosn;
    unsigned char lipTension;
} ECIMouthData;

typedef struct ECIVoiceAttrib {
   int eciSampleRate;   
   ECILanguageDialect languageID;
} ECIVoiceAttrib;

#if defined(WIN32) || ( defined(UNDER_CE) && (defined(MIPS) || defined(SH3)) )
#pragma pack(pop)
#endif

typedef enum {
       eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply, eciPhonemeIndexReply, eciWordIndexReply, eciStringIndexReply,
		 eciAudioIndexReply, eciReserved01
} ECIMessage;

typedef enum {
    eciDataNotProcessed, eciDataProcessed, eciDataAbort
} ECICallbackReturn;

typedef ECICallbackReturn (* ECICallback)(ECIHand hEngine, ECIMessage Msg, long lParam, void *pData);

#if defined(_WIN32) || defined(_Windows)
typedef enum {
    eciGeneralDB,
    eciAboutDB,
    eciVoicesDB,
    eciReadingDB,
    eciMainDictionaryDB,
    eciRootDictionaryDB,
    eciNumDialogBoxes
} ECIDialogBox;
#endif


#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef Boolean (ECIFNDECLARE * PGETFILTEROBJECT) (unsigned long idInterface, void **ppUnknown);
typedef void *ECIFilterHand;

#define NULL_FILTER_HAND 0

typedef struct ECIFilterAttrib
{
	char eciFilterName[80];
	ECIint32 language;
}ECIFilterAttrib;

typedef enum ECIFilterError {
  FilterNoError,					
  FilterFileNotFound,        	
  FilterOutOfMemory,          
  FilterInternalError,			
  FilterAccessError,
  FilterNotRegisteredError,
  FilterSystemError
}ECIFilterError;  

ECIFilterError ECIFNDECLARE eciRegisterFilter(ECIHand eciHand,unsigned int filternumber, PGETFILTEROBJECT *filterentrypoint, ECIFilterAttrib *fAttrib, Boolean autoload);
ECIFilterError ECIFNDECLARE eciUnregisterFilter(ECIHand eciHand,unsigned int filternumber, ECIFilterAttrib *fAttrib);
ECIFilterError ECIFNDECLARE eciDeactivateFilter(ECIHand eciHandle, ECIFilterHand pFilter);
ECIFilterHand  ECIFNDECLARE eciNewFilter(ECIHand eciHandle, unsigned int filterNum, Boolean bGlobal);
ECIFilterError ECIFNDECLARE eciActivateFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand);
ECIFilterHand  ECIFNDECLARE eciDeleteFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand);
ECIFilterError ECIFNDECLARE eciUpdateFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand,
                                            ECIInputText key, ECIInputText translation);

	
ECIHand ECIFNDECLARE eciNew(void);
ECIHand ECIFNDECLARE eciNewEx(ECILanguageDialect Value);
ECIHand ECIFNDECLARE eciDelete(ECIHand hEngine);
Boolean ECIFNDECLARE eciReset(ECIHand hEngine);
void ECIFNDECLARE eciVersion(char *pBuffer);
Boolean ECIFNDECLARE eciTestPhrase(ECIHand hEngine);
Boolean ECIFNDECLARE eciSpeakText(ECIInputText pText, Boolean bAnnotationsInTextPhrase);
Boolean ECIFNDECLARE eciSpeakTextEx(ECIInputText pText, Boolean bAnnotationsInTextPhrase, ECILanguageDialect Value);
int ECIFNDECLARE eciGetParam(ECIHand hEngine, ECIParam Param);
int ECIFNDECLARE eciSetParam(ECIHand hEngine, ECIParam Param, int iValue);
int ECIFNDECLARE eciGetDefaultParam(ECIParam parameter);
int ECIFNDECLARE eciSetDefaultParam(ECIParam parameter, int value);
int ECIFNDECLARE eciGetAvailableLanguages(ECILanguageDialect *paLangs, int *piNumLangs);
Boolean ECIFNDECLARE eciCopyVoice(ECIHand hEngine, int iVoiceFrom, int iVoiceTo);
Boolean ECIFNDECLARE eciGetVoiceName(ECIHand hEngine, int iVoice, void *pBuffer);
Boolean ECIFNDECLARE eciSetVoiceName(ECIHand hEngine, int iVoice, const void *pBuffer);
int ECIFNDECLARE eciGetVoiceParam(ECIHand hEngine, int iVoice, ECIVoiceParam Param);
int ECIFNDECLARE eciSetVoiceParam(ECIHand hEngine, int iVoice,
                                  ECIVoiceParam Param, int iValue);
Boolean ECIFNDECLARE eciAddText(ECIHand hEngine, ECIInputText pText);
Boolean ECIFNDECLARE eciInsertIndex(ECIHand hEngine, int iIndex);
Boolean ECIFNDECLARE eciSynthesize(ECIHand hEngine);
Boolean ECIFNDECLARE eciSynthesizeFile(ECIHand hEngine, const void *pFilename);
Boolean ECIFNDECLARE eciClearInput(ECIHand hEngine);
Boolean ECIFNDECLARE eciGeneratePhonemes(ECIHand hEngine, int iSize, void *pBuffer);
int ECIFNDECLARE eciGetIndex(ECIHand hEngine);
Boolean ECIFNDECLARE eciStop(ECIHand hEngine);
Boolean ECIFNDECLARE eciSpeaking(ECIHand hEngine);
Boolean ECIFNDECLARE eciSynchronize(ECIHand hEngine);
Boolean ECIFNDECLARE eciSetOutputBuffer(ECIHand hEngine, int iSize, short *psBuffer);
Boolean ECIFNDECLARE eciSetOutputFilename(ECIHand hEngine, const void *pFilename);
Boolean ECIFNDECLARE eciSetOutputDevice(ECIHand hEngine, int iDevNum);
Boolean ECIFNDECLARE eciPause(ECIHand hEngine, Boolean On);
void ECIFNDECLARE eciRegisterCallback(ECIHand hEngine, ECICallback Callback, void *pData);
ECIDictHand ECIFNDECLARE eciNewDict(ECIHand hEngine);
ECIDictHand ECIFNDECLARE eciGetDict(ECIHand hEngine);
ECIDictError ECIFNDECLARE eciSetDict(ECIHand hEngine, ECIDictHand hDict);
ECIDictHand ECIFNDECLARE eciDeleteDict(ECIHand hEngine, ECIDictHand hDict);
ECIDictError ECIFNDECLARE eciLoadDict(ECIHand hEngine, ECIDictHand hDict, ECIDictVolume DictVol, ECIInputText pFilename);
ECIDictError ECIFNDECLARE eciSaveDict(ECIHand hEngine, ECIDictHand hDict, ECIDictVolume DictVol, ECIInputText pFilename);
ECIDictError ECIFNDECLARE eciUpdateDict(ECIHand hEngine, ECIDictHand hDict,
                                        ECIDictVolume DictVol, ECIInputText pKey, ECIInputText pTranslationValue);
ECIDictError ECIFNDECLARE eciDictFindFirst(ECIHand hEngine,
                                           ECIDictHand hDict, ECIDictVolume DictVol,
                                           ECIInputText *ppKey, ECIInputText *ppTranslationValue);
ECIDictError ECIFNDECLARE eciDictFindNext(ECIHand hEngine,
                                          ECIDictHand hDict, ECIDictVolume DictVol,
                                          ECIInputText *ppKey, ECIInputText *ppTranslationValue);

ECIInputText ECIFNDECLARE eciDictLookup(ECIHand hEngine,
                                        ECIDictHand hDict, ECIDictVolume DictVol,
                                        ECIInputText pKey);
ECIDictError ECIFNDECLARE eciUpdateDictA(ECIHand hEngine,
                                         ECIDictHand hDict, ECIDictVolume DictVol,
                                         ECIInputText pKey, ECIInputText pTranslationValue, ECIPartOfSpeech PartOfSpeech);
ECIDictError ECIFNDECLARE eciDictFindFirstA(ECIHand hEngine,
                                            ECIDictHand hDict, ECIDictVolume DictVol,
                                            ECIInputText *ppKey, ECIInputText *ppTranslationValue, ECIPartOfSpeech *pPartOfSpeech);
ECIDictError ECIFNDECLARE eciDictFindNextA(ECIHand hEngine,
                                           ECIDictHand hDict, ECIDictVolume DictVol,
                                           ECIInputText *ppKey, ECIInputText *ppTranslationValue, ECIPartOfSpeech *pPartOfSpeech);
ECIDictError ECIFNDECLARE eciDictLookupA(ECIHand hEngine,
                                         ECIDictHand hDict, ECIDictVolume DictVol,
                                         ECIInputText pKey, ECIInputText *ppTranslationValue, ECIPartOfSpeech *pPartOfSpeech);

#define eciDictFindFirst(eciHandle, dictHandle, dictVolume, ppkey, pptranslation) \
        eciDictFindFirst((eciHandle), (dictHandle), (dictVolume), (ECIInputText *) (ppkey), (ECIInputText *) (pptranslation))
#define eciDictFindNext(eciHandle, dictHandle, dictVolume, ppkey, pptranslation) \
        eciDictFindNext((eciHandle), (dictHandle), (dictVolume), (ECIInputText *) (ppkey), (ECIInputText *) (pptranslation))
        
ECIVoiceError ECIFNDECLARE eciRegisterVoice(ECIHand eciHand, int voiceNumber, void *vData, ECIVoiceAttrib *vAttrib);
ECIVoiceError ECIFNDECLARE eciUnregisterVoice(ECIHand eciHand, int voiceNumber, ECIVoiceAttrib *vAttrib, void **vData);
#ifdef __cplusplus
}
#endif

#endif

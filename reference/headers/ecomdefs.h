/*=========================================================================*/
/*                                                                         */
/* ecomdefs.h                                                              */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1, AT2T5ZZ v4.3                                              */
/* (C) Copyright IBM Corp. 1999, 2004  All Rights Reserved.                */
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

#ifndef _ECOMDEFS_H_
#define _ECOMDEFS_H_

typedef enum
{
   ESTRING_ASCII =0,
   ESTRING_UNICODE =1,
   ESTRING_INVALID=-1,
} EStringType;


typedef struct _OSTaskInfo
{
   unsigned long ulValidData;
   unsigned long ulTaskId;
} OSTaskInfo;

typedef enum
{
        EVV_LANG_EN=0x0001, EVV_LANG_DE=0x0003, EVV_LANG_FR=0x0004,
        EVV_LANG_IT=0x0005, EVV_LANG_ES=0x0006, EVV_LANG_JA=0x0007,
        EVV_LANG_ZH=0x0008, EVV_LANG_KO=0x0009, EVV_LANG_PT=0x000a
} EVVLang;

typedef enum
{
        EVV_COUNTRY_US=0x0001, EVV_COUNTRY_GB=0x0002, EVV_COUNTRY_DE=0x0003,
        EVV_COUNTRY_FR=0x0004, EVV_COUNTRY_IT=0x0005, EVV_COUNTRY_ES=0x0006,
        EVV_COUNTRY_JP=0x0007, EVV_COUNTRY_CN=0x0008, EVV_COUNTRY_TW=0x0009,
        EVV_COUNTRY_KR=0x000a, EVV_COUNTRY_BR=0x000b, EVV_COUNTRY_CA=0x000c
} EVVCountry;

#define EVVMAKELOCALE(_lang, _country)  ((_lang<<16)|_country)

typedef enum
{
        EVV_EN_US = EVVMAKELOCALE(EVV_LANG_EN, EVV_COUNTRY_US),
        EVV_EN_GB = EVVMAKELOCALE(EVV_LANG_EN, EVV_COUNTRY_GB),
        EVV_DE_DE = EVVMAKELOCALE(EVV_LANG_DE, EVV_COUNTRY_DE),
        EVV_FR_FR = EVVMAKELOCALE(EVV_LANG_FR, EVV_COUNTRY_FR),
        EVV_IT_IT = EVVMAKELOCALE(EVV_LANG_IT, EVV_COUNTRY_IT),
        EVV_ES_ES = EVVMAKELOCALE(EVV_LANG_ES, EVV_COUNTRY_ES),
        EVV_JA_JP = EVVMAKELOCALE(EVV_LANG_JA, EVV_COUNTRY_JP),
        EVV_ZH_CN = EVVMAKELOCALE(EVV_LANG_ZH, EVV_COUNTRY_CN),
        EVV_ZH_TW = EVVMAKELOCALE(EVV_LANG_ZH, EVV_COUNTRY_TW),
        EVV_KO_KR = EVVMAKELOCALE(EVV_LANG_KO, EVV_COUNTRY_KR),
        EVV_PT_BR = EVVMAKELOCALE(EVV_LANG_PT, EVV_COUNTRY_BR),
        EVV_FR_CA = EVVMAKELOCALE(EVV_LANG_FR, EVV_COUNTRY_CA),
        EVV_ES_US = EVVMAKELOCALE(EVV_LANG_ES, EVV_COUNTRY_US)
} EVVLocale;





/*Precompiled Vocabulary Set Support*/
typedef enum 
{
	EVV_ENDIANNESS_LE = 0,
	EVV_ENDIANNESS_BE = 1
} EVVEndianness;

#define EVV_MAX_AM_NAMELEN 8


#if defined(__cplusplus)
#define VVE_DECLSPEC extern "C"
#else
#define VVE_DECLSPEC extern
#endif

#endif

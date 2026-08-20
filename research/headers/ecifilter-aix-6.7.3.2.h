/*=========================================================================*/
/*                                                                         */
/* mailfilter.h                                                            */
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

#ifndef __ECIFILTER_H
#define __ECIFILTER_H

#include "eci.h" 

#define eciDeactivateFilter   _eciDeactivateFilter
#define eciNewFilter          _eciNewFilter
#define eciSetFilter          _eciSetFilter
#define eciDeleteFilter       _eciDeleteFilter
#define eciUpdateFilter       _eciUpdateFilter
#define eciGetFilteredText    _eciGetFilteredText

#ifdef __cplusplus
extern "C" {
#endif 

typedef void *ECIFilterHand;

#define NULL_FILTER_HAND 0

typedef enum ECIFilterError {
  FilterNoError,					
  FilterFileNotFound,        	
  FilterOutOfMemory,          
  FilterInternalError,			
  FilterAccessError				
}; 

enum ECIFilterError ECIFNDECLARE eciDeactivateFilter(ECIHand eciHandle, ECIFilterHand pFilter);
#ifdef __cplusplus
ECIFilterHand  ECIFNDECLARE eciNewFilter(ECIHand eciHandle, unsigned int filterNum = 0, Boolean bGlobal = false);
#else
ECIFilterHand  ECIFNDECLARE eciNewFilter(ECIHand eciHandle, unsigned int filterNum, Boolean bGlobal);
#endif
enum ECIFilterError ECIFNDECLARE eciActivateFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand);
ECIFilterHand  ECIFNDECLARE eciDeleteFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand);
enum ECIFilterError ECIFNDECLARE eciUpdateFilter(ECIHand eciHandle, ECIFilterHand whichFilterHand,
																 ECIInputText key, ECIInputText translation);
enum ECIFilterError ECIFNDECLARE eciGetFilteredText(ECIHand eciHandle, ECIFilterHand whichFilterHand,
																	 ECIInputText input, ECIInputText *filteredText);
#ifdef __cplusplus
}
#endif
#endif // #ifndef __ECIFILTER_H

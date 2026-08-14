/*=========================================================================*/
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192  V2.1  AT2T5ZZ v4.3 		                                   */
/* (C) Copyright IBM Corp.  1999, 2004  All Rights Reserved.               */
/*                                                                         */
/* The following IBM source code is provided to assist you in your         */
/* development.  You may use this code only in accordance with the         */
/* IBM License Agreement accompanying the Licensed Materials.              */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*=========================================================================*/



#ifndef INCL_EDU_H
#define INCL_EDU_H

#include "evvrc.h"

typedef unsigned char EDUCollection;
typedef unsigned int EDURC;


#ifdef __cplusplus
extern "C" {
#endif




EDURC eduQueryCollection( EDUCollection *pEduCollection,
                        unsigned char *pucEduDataType,
                        unsigned long *pulEduCollectionBlockCount );

EDURC eduGetBlock( EDUCollection *pEduCollection,
                 unsigned long ulEduBlockIndex,
                 unsigned long * pulSize,
                 void **ppBlock );

EDURC eduGetBlockByID( EDUCollection *pEduCollection,
                     unsigned long ulEduBlockId,
                     unsigned long * pulSize,
                     void **ppBlock );

EDURC eduLoadCollection( const char *pszFileName,
                       EDUCollection *pEduCollection,
                       unsigned long *pulBufSize );


#ifdef __cplusplus
}
#endif


#define EDU_BASE_OFFSET					 0x40000

#define EDU_RC_OK						 0


#define EDU_RC_ID_NOT_FOUND              EDU_BASE_OFFSET + 0x8000
#define EDU_RC_SHARES_EXIST              EDU_BASE_OFFSET + 0x8001


#define EDU_RC_INVALID_DATA              EDU_BASE_OFFSET + EVV_RC_INVALID_DATA
#define EDU_RC_INVALID_TYPE              EDU_BASE_OFFSET + EVV_RC_INVALID_TYPE
#define EDU_RC_INVALID_HANDLE            EDU_BASE_OFFSET + EVV_RC_INVALID_HANDLE
#define EDU_RC_INVALID_BUFFER_SIZE       EDU_BASE_OFFSET + EVV_RC_INVALID_BUFFER_SIZE
#define EDU_RC_INVALID_INST				 EDU_BASE_OFFSET + EVV_RC_INVALID_INST
#define EDU_RC_INVALID_STRING_TYPE		 EDU_BASE_OFFSET + EVV_RC_INVALID_STRING_TYPE
#define EDU_RC_INVALID_VALUE			 EDU_BASE_OFFSET + EVV_RC_INVALID_VALUE
#define EDU_RC_INDEX_OUT_OF_BOUNDS       EDU_BASE_OFFSET + EVV_RC_INDEX_OUT_OF_BOUNDS
#define EDU_RC_NO_FILE_SUPPORT           EDU_BASE_OFFSET + EVV_RC_NO_FILE_SUPPORT
#define EDU_RC_FILE_NOT_FOUND            EDU_BASE_OFFSET + EVV_RC_FILE_NOT_FOUND 
#define EDU_RC_FILE_OPEN_ERROR           EDU_BASE_OFFSET + EVV_RC_FILE_OPEN_ERROR 
#define EDU_RC_FILE_READ_ERROR           EDU_BASE_OFFSET + EVV_RC_FILE_READ_ERROR
#define EDU_RC_FILE_DATA_INVALID         EDU_BASE_OFFSET + EVV_RC_FILE_INVALID_DATA
#define EDU_RC_INTERNAL_ERROR			 EDU_BASE_OFFSET + EVV_RC_INTERNAL_ERROR
#define EDU_RC_SYSTEM_ERROR				 EDU_BASE_OFFSET + EVV_RC_SYSTEM_ERROR
#define EDU_RC_MEM_ALLOC_ERROR			 EDU_BASE_OFFSET + EVV_RC_MEM_ALLOC_ERROR
#define EDU_RC_RESOURCES_REMAIN		     EDU_BASE_OFFSET + EVV_RC_RESOURCES_REMAIN
#define EDU_RC_RESOURCE_UNAVAILABLE		 EDU_BASE_OFFSET + EVV_RC_RESOURCE_UNAVAILABLE
#define EDU_RC_UNSUPPORTED_VERSION		 EDU_BASE_OFFSET + EVV_RC_UNSUPPORTED_VERSION
#define EDU_RC_FUNCTION_UNAVAILABLE		 EDU_BASE_OFFSET + EVV_RC_FUNCTION_UNAVAILABLE



#endif

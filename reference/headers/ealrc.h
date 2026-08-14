/*=========================================================================*/
/*                                                                         */
/* ealrc.h                                                                 */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1, AT2T5ZZ v4.3 			                           */
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

#ifndef __EALRC_H__
#define __EALRC_H__

#define EAL_BASE_OFFSET 0x30000
#include "evvrc.h"

#define EAL_RC_OK                            0
#define EAL_RC_INVALID_PARAM                 ( EAL_BASE_OFFSET +  EVV_RC_INVALID_PARAM)
#define EAL_RC_NO_DEVICES                    ( EAL_BASE_OFFSET +  EVV_RC_NO_DEVICES )
#define EAL_RC_INVALID_FMT_COMBO             ( EAL_BASE_OFFSET +  EVV_RC_INVALID_FMT_COMBO )
#define EAL_RC_DEVICE_ACTIVE                 ( EAL_BASE_OFFSET +  EVV_RC_DEVICE_ACTIVE )
#define EAL_RC_NOT_OPEN                      ( EAL_BASE_OFFSET +  EVV_RC_NOT_OPEN )
#define EAL_RC_INVALID_REQUEST               ( EAL_BASE_OFFSET +  EVV_RC_INVALID_REQUEST )
#define EAL_RC_RESOURCES_REMAIN              ( EAL_BASE_OFFSET +  EVV_RC_RESOURCES_REMAIN )
#define EAL_RC_DEVICE_IN_USE                 ( EAL_BASE_OFFSET +  EVV_RC_DEVICE_IN_USE )
#define EAL_RC_INTERNAL_ERROR                ( EAL_BASE_OFFSET +  EVV_RC_INTERNAL_ERROR)
#define EAL_RC_INVALID_HANDLE                ( EAL_BASE_OFFSET +  EVV_RC_INVALID_HANDLE )
#define EAL_RC_UNABLE_TO_STARTTASK           ( EAL_BASE_OFFSET +  EVV_RC_UNABLE_TO_STARTTASK)
#define EAL_RC_ALREADY_REGED                 ( EAL_BASE_OFFSET +  EVV_RC_ALREADY_REGISTERED)
#define EAL_RC_DEVICE_NOT_OPEN               ( EAL_BASE_OFFSET +  EVV_RC_DEVICE_NOT_OPEN )
#define EAL_RC_MEM_ALLOC_ERROR               ( EAL_BASE_OFFSET +  EVV_RC_MEM_ALLOC_ERROR )
#define EAL_RC_TOO_HIGH                      ( EAL_BASE_OFFSET +  EVV_RC_TOO_HIGH )
#define EAL_RC_TOO_LOW                       ( EAL_BASE_OFFSET +  EVV_RC_TOO_LOW )
#define EAL_RC_SYS_OPEN_FAILED               ( EAL_BASE_OFFSET +  EVV_RC_SYS_OPEN_FAILED)
#define EAL_RC_SYS_CLOSE_FAILED              ( EAL_BASE_OFFSET +  EVV_RC_SYS_CLOSE_FAILED)
#define EAL_RC_SYS_START_FAILED              ( EAL_BASE_OFFSET +  EVV_RC_SYS_START_FAILED)
#define EAL_RC_SYS_XFER_FAILED               ( EAL_BASE_OFFSET +  EVV_RC_SYS_XFER_FAILED )
#define EAL_RC_SYS_RESET_FAILED              ( EAL_BASE_OFFSET +  EVV_RC_SYS_RESET_FAILED)
#define EAL_RC_SYS_STOP_FAILED               ( EAL_BASE_OFFSET +  EVV_RC_SYS_STOP_FAILED )
#define EAL_RC_SYSTEM_ERROR                  ( EAL_BASE_OFFSET +  EVV_RC_SYSTEM_ERROR )
#define EAL_RC_DEVICES_STILL_OPEN            ( EAL_BASE_OFFSET +  EVV_RC_DEVICES_STILL_OPEN)
#define EAL_RC_INVALID_TYPE                  ( EAL_BASE_OFFSET +  EVV_RC_INVALID_TYPE)
#define EAL_RC_LIBRARY_LOAD_ERROR            ( EAL_BASE_OFFSET +  EVV_RC_LIBRARY_LOAD_ERROR )
#define EAL_RC_INVALID_LIBRARY               ( EAL_BASE_OFFSET +  EVV_RC_INVALID_LIBRARY)

/* EAL specific codes from dil. */
#define EAL_RC_INVALID_DEV_ID                ( EAL_BASE_OFFSET +  0x8001)
#define EAL_RC_SYS_UNDERRUN                  ( EAL_BASE_OFFSET +  0x8002)
#define EAL_RC_SYS_OVERRUN                   ( EAL_BASE_OFFSET +  0x8003)
#define EAL_RC_INVALID_CHANNEL               ( EAL_BASE_OFFSET +  0x8004)

#define EAL_RC_INVALID_MAP_VER               ( EAL_BASE_OFFSET +  0x8011)
#define EAL_RC_MAX_NUM_OF_DEVICES_EXCEEDED   ( EAL_BASE_OFFSET +  0x8012)
#define EAL_RC_NO_STEP                       ( EAL_BASE_OFFSET +  0x8013)
#define EAL_RC_NOT_REGED                     ( EAL_BASE_OFFSET +  0x8014) 
#define EAL_RC_MAP_ALREADY_REGED             ( EAL_BASE_OFFSET +  0x8015)
#define EAL_RC_CTRL_ERROR                    ( EAL_BASE_OFFSET +  0xc000)
#define EAL_RC_FLTR_ERROR                    ( EAL_BASE_OFFSET +  0xc001)

/* these two are used internaly to the eal, and should never be returned to the user*/
#define EAL_RC_ALL_BUFFERS_QUEUED            ( EAL_BASE_OFFSET +  0x800e) 
#define EAL_RC_NO_BUFFERS                    ( EAL_BASE_OFFSET +  0x800f) 

/* these two are never generated.  They are left in to prevent build breaks*/
#define EAL_RC_INVALID_CB_TYPE          ( EAL_BASE_OFFSET +  0x8010) 
#define EAL_RC_FC_STILL_REGED           ( EAL_BASE_OFFSET +  0x8000 )

/* compatability defines. */
#define EAL_RC_ALREADY_REGISTERED            EAL_RC_ALREADY_REGED
#define EAL_RC_TASK_CREATION                 EAL_RC_UNABLE_TO_STARTTASK           
#define EAL_RC_INVALID_DEV_HANDLE            EAL_RC_INVALID_HANDLE
#define EAL_RC_INVALID_DEV_TYPE              EAL_RC_INVALID_TYPE
#define EAL_RC_DYN_LIB_NOT_FOUND             EAL_RC_INVALID_LIBRARY
#define EAL_RC_DYN_LIB_LOAD_FAILED           EAL_RC_LIBRARY_LOAD_ERROR

#define DIL2EAL(x) (EALRC)((x) & (~0x00080000))
#endif /* __EALRC_H__ */


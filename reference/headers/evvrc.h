/*=========================================================================*/
/*                                                                         */
/* evvrc.h                                                                 */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* ASSF3ZZ V4.2 ASSF4ZZ V5.2 AT2T5ZZ v4.3  				   */
/* (C) Copyright IBM Corp. 2003, 2004		    All Rights Reserved.   */
/* US Government Users Restricted Rights - Use, duplication or disclosure  */
/* restricted by GSA ADP Schedule Contract with IBM Corp.                  */
/*                                                                         */
/* The following IBM source code is provided to assist you in your	   */
/* development.  You may use this code only in accordance with the	   */
/* IBM License Agreement accompanying the Licensed Materials.              */
/*                                                                         */
/* This copyright statement may not be removed.                            */
/*                                                                         */
/*=========================================================================*/

#ifndef __INCL_EVVRC_H_
#define __INCL_EVVRC_H_

#define EVV_RC_OK                                                              0			

#define EVV_RC_ALREADY_INITIALIZED                                        0x0010
#define EVV_RC_ALREADY_REGISTERED                                         0x0011
#define EVV_RC_AUDIO_IN_USE                                               0x0012
#define EVV_RC_BASEFORM_NOT_FOUND                                         0x0013
#define EVV_RC_BASEFORM_TOO_LONG                                          0x0014
#define EVV_RC_BUFFER_TOO_SMALL                                           0x0015
#define EVV_RC_CB_NOT_REGISTERED                                          0x0016
#define EVV_RC_CB_REGISTERED                                              0x0017
#define EVV_RC_CONFIG_ERROR                                               0X0018
#define EVV_RC_CONNECT_ERROR                                              0x0019
#define EVV_RC_DEVICE_ACTIVE                                              0x001A
#define EVV_RC_DEVICE_IN_USE                                              0x001B
#define EVV_RC_DEVICE_NOT_OPEN                                            0x001C
#define EVV_RC_DEVICES_STILL_OPEN                                         0x001D
#define EVV_RC_DISCONNECT_ERROR                                           0x001E
#define EVV_RC_DUPLICATE_BASEFORM                                         0x001F
#define EVV_RC_EAL_ERROR                                                  0x0020
#define EVV_RC_ENGINE_NOT_STARTED                                         0x0021
#define EVV_RC_ENGINE_STILL_ACTIVE                                        0x0022
#define EVV_RC_FILE_INVALID_DATA                                          0x0023
#define EVV_RC_FILE_NOT_FOUND                                             0x0024
#define EVV_RC_FILE_OPEN_ERROR                                            0x0025
#define EVV_RC_FILE_READ_ERROR                                            0x0026
#define EVV_RC_FUNCTION_UNAVAILABLE                                       0x0027
#define EVV_RC_INDEX_OUT_OF_BOUNDS                                        0x0028
#define EVV_RC_INVALID_BASEFORM                                           0x0029
#define EVV_RC_INVALID_BUFFER_SIZE                                        0x002A
#define EVV_RC_INVALID_CONFIG_VER                                         0x002B
#define EVV_RC_INVALID_FLAG                                               0x002C
#define EVV_RC_INVALID_FMT_COMBO                                          0x002D
#define EVV_RC_INVALID_FORMAT                                             0x002E
#define EVV_RC_INVALID_HANDLE                                             0x002F
#define EVV_RC_INVALID_IMAGE                                              0x0030
#define EVV_RC_INVALID_LANGUAGE                                           0x0031
#define EVV_RC_INVALID_KEY                                                0x0032
#define EVV_RC_INVALID_NUM_CHANNELS                                       0x0033
#define EVV_RC_INVALID_PARAM                                              0x0034
#define EVV_RC_INVALID_REQUEST                                            0x0035
#define EVV_RC_INVALID_SAMP_RATE                                          0x0036
#define EVV_RC_INVALID_SAMPLESIZE                                         0x0037
#define EVV_RC_INVALID_SET                                                0x0038
#define EVV_RC_INVALID_STATE                                              0x0039
#define EVV_RC_INVALID_STRING_TYPE                                        0x003A
#define EVV_RC_INVALID_TYPE                                               0x003B
#define EVV_RC_INVALID_VALUE                                              0x003C
#define EVV_RC_INVALID_VOCABID                                            0x003D
#define EVV_RC_LOCK_FAIL                                                  0x003E
#define EVV_RC_MEM_ALLOC_ERROR                                            0x003F
#define EVV_RC_MEM_FREE_ERROR                                             0x0040
#define EVV_RC_MEM_SET_ERROR                                              0x0041
#define EVV_RC_MUTEX_CREATE_ERROR                                         0x0042
#define EVV_RC_MUTEX_DELETE_ERROR                                         0x0043
#define EVV_RC_MUTEX_SIGNAL_ERROR                                         0x0044
#define EVV_RC_MUTEX_TEST_ERROR                                           0x0045
#define EVV_RC_NAMED_BINARY_SEMAPHORE_CREATE_ERROR                        0x0046
#define EVV_RC_NAMED_BINARY_SEMAPHORE_DELETE_ERROR                        0x0047
#define EVV_RC_NAMED_BINARY_SEMAPHORE_SIGNAL_ERROR                        0x0048
#define EVV_RC_NAMED_BINARY_SEMAPHORE_TEST_ERROR                          0x0049
#define EVV_RC_NAMED_MUTEX_CREATE_ERROR                                   0x004A
#define EVV_RC_NAMED_MUTEX_DELETE_ERROR                                   0x004B
#define EVV_RC_NAMED_MUTEX_LOCK_ERROR                                     0x004C
#define EVV_RC_NAMED_MUTEX_UNLOCK_ERROR                                   0x004D
#define EVV_RC_NO_DEVICES                                                 0x004E
#define EVV_RC_NO_FILE_SUPPORT                                            0x004F
#define EVV_RC_NOT_INITIALIZED                                            0x0050
#define EVV_RC_NOT_OPEN                                                   0x0051
#define EVV_RC_NOT_REGISTERED                                             0x0052
#define EVV_RC_RECEIVE_ERROR                                              0x0053
#define EVV_RC_RESULT_OUT_OF_RANGE                                        0x0054
#define EVV_RC_SEND_ERROR                                                 0x0055
#define EVV_RC_SERVICE_INITIALIZED                                        0x0056
#define EVV_RC_SYS_CLOSE_FAILED                                           0x0057
#define EVV_RC_SYS_OPEN_FAILED                                            0x0058
#define EVV_RC_SYS_RESET_FAILED                                           0x0059
#define EVV_RC_SYS_START_FAILED                                           0x005A
#define EVV_RC_SYS_STOP_FAILED                                            0x005B
#define EVV_RC_SYS_XFER_FAILED                                            0x005C
#define EVV_RC_TIMEOUT                                                    0x005D
#define EVV_RC_TOO_HIGH                                                   0x005E
#define EVV_RC_TOO_LOW                                                    0x005F

/* unrecoverable errors */
#define EVV_RC_INSTALLATION_ERROR                                         0x4000
#define EVV_RC_INTERNAL_ERROR                                             0x4001
#define EVV_RC_INVALID_CALLBACK                                           0x4002
#define EVV_RC_INVALID_DATA                                               0x4003
#define EVV_RC_INVALID_DATA_FILE                                          0x4004
#define EVV_RC_INVALID_INST                                               0x4005
#define EVV_RC_LANGUAGE_MISMATCH                                          0x4006
#define EVV_RC_RESOURCES_REMAIN                                           0x4007
#define EVV_RC_RESOURCE_UNAVAILABLE                                       0x4008
#define EVV_RC_SYSTEM_ERROR                                               0x4009
#define EVV_RC_TOO_MANY_HANDLES                                           0x400A
#define EVV_RC_UNABLE_TO_STARTTASK                                        0x400B
#define EVV_RC_UNSUPPORTED_LANGUAGE                                       0x400C
#define EVV_RC_UNSUPPORTED_VERSION                                        0x400D
#define EVV_RC_LIBRARY_LOAD_ERROR                                         0x400E
#define EVV_RC_INVALID_LIBRARY                                            0x400F

#endif

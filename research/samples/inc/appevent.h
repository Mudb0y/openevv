/*=========================================================================*/
/*                                                                         */
/* appevent.h                                                              */
/*                                                                         */
/* Licensed Materials - Property of IBM                                    */
/* 11K6192 V2.1 AT2T5ZZ V4.3 		                                   */
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

/* Protect against mulitple includes of this file. */
#if !defined(_APPEVENT_H_)
#define _APPEVENT_H_


#include <os.h>

/* Length of the string used to contain the recognized phrase text. */
#define RECOTEXT_LEN   128

typedef enum
{
   APPEVENT_INVALID        = 99999,

   APPEVENT_RECOERROR      = 00000,

   APPEVENT_CMDTIMEOUT     = 00001,
   APPEVENT_CMDERROR       = 00002,
   APPEVENT_RECOREJECTION  = 00003,

   APPEVENT_DIAL           = 50000,
   APPEVENT_ADDNAME        = 50001,
   APPEVENT_MODIFYOPTIONS  = 50002,
   APPEVENT_CHECKEMAIL     = 50003,
   APPEVENT_GOODBYE        = 50004,
   APPEVENT_DIRECTIONS     = 50005,
   APPEVENT_CALL           = 50100,
   APPEVENT_DELETENAME     = 50101,

   APPEVENT_YES            = 60000,
   APPEVENT_NO             = 60001,

   APPEVENT_SAVEADDR       = 80000,
   APPEVENT_RESTOREADDR    = 80001,
   APPEVENT_IMPORTNAMELIST = 80004,

   APPEVENT_READMAIL       = 90000,
   APPEVENT_SUMEMAIL       = 90001,
   APPEVENT_EXITEMAIL      = 90002,

   APPEVENT_WCIS           = 70000
} AppEventType;

typedef struct _AppEventDail
{
   int iNumDigits;
   int aiDigits[11];
} AppEventDial;

typedef struct _AppEventRecoError
{
   long lErr;
} AppEventRecoError;

typedef struct _AppEventCall
{
   int iAddrBookIndex;
} AppEventCall;

typedef struct _AppEventDeleteName
{
   int iAddrBookIndex;
} AppEventDeleteName;

typedef struct _AppEventEmailNote
{
   int iNoteIndex;
} AppEventEmailNote;

typedef struct _AppEvent
{
	AppEventType Type;
   char szRecoText[RECOTEXT_LEN+1];

	union
	{
      AppEventRecoError Error;
		AppEventDial Dial;
      AppEventCall Call;
      AppEventDeleteName DeleteName;
      AppEventEmailNote EmailNote;
	} Event;
} AppEvent;

#endif   /* _APPEVENT_H_ */
